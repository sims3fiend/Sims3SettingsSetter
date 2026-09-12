#include "../patch_system.h"
#include "../patch_helpers.h"
#include <windows.h>
#include <atomic>
#include <immintrin.h>
#include <algorithm>
#include <string>

// Configuration constants
// 50us is roughly 150,000 cycles. 300us is ~1,000,000 cycles
// A context switch sleep costs ~3-15ms of LATENCY (opportunity cost), but only ~5k cycles of CPU work
// We trade CPU work to save Latency, essentially
static const DWORD MIN_SPIN_US = 50;
static const DWORD MAX_SPIN_US = 500;   // Capped to prevents syscall spam worst-cases, 500 ideal I thinkie, may A/B 750~ later idk. Might make this configurable in the future but probably too complex for end users x
static const DWORD ADAPT_INTERVAL = 64; // Adapt faster

class AdaptiveWaitPatch : public OptimizationPatch {
  private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    static AdaptiveWaitPatch* instance;

    static std::atomic<DWORD> g_spinBudgetUs;
    static std::atomic<DWORD> g_spinSuccesses;
    static std::atomic<DWORD> g_spinFailures;
    static std::atomic<DWORD> g_totalCalls;
    static std::atomic<float> g_lastSuccessRate;
    static std::atomic<DWORD> g_stableIntervals;

    // Runtime gate. The detour stays installed for the life of the process (see Uninstall);
    // this atomic is what actually turns the optimisation on and off.
    static std::atomic<bool> g_active;
    // Every call that reached the hook, eligible or not. g_totalCalls only counts calls that
    // actually spun, so without this there is no way to tell "installed and busy" from
    // "installed and never engaged".
    static std::atomic<DWORD> g_interceptedCalls;
    // Cumulative spin outcomes. The adaptive loop's g_spinSuccesses/g_spinFailures are
    // exchange(0)'d every ADAPT_INTERVAL calls, so they steer the budget but can never answer
    // "did the spinning pay off?". These three are never reset except by Install(), and they
    // separate the outcome that justifies this patch from the two that don't:
    //   g_spinWins      - signaled after at least one pause: a real kernel block avoided.
    //   g_spinFreebies  - signaled on the very first 0ms poll, before any pausing. The plain
    //                     blocking call would have returned just as fast; this is a wash.
    //   g_spinLosses    - budget exhausted: we spun and then blocked anyway. Pure cost.
    static std::atomic<DWORD> g_spinWins;
    static std::atomic<DWORD> g_spinFreebies;
    static std::atomic<DWORD> g_spinLosses;
    static bool s_detoursInstalled;

    // Function pointers for the inline detour
    typedef DWORD(WINAPI* WaitForSingleObject_t)(HANDLE, DWORD);
    static WaitForSingleObject_t Original_WaitForSingleObject;

    typedef DWORD(WINAPI* WaitForSingleObjectEx_t)(HANDLE, DWORD, BOOL);
    static WaitForSingleObjectEx_t Original_WaitForSingleObjectEx;

// Pseudo-handle for GetCurrentThread() - safe cast for all architectures
#define CURRENT_THREAD_PSEUDO_HANDLE ((HANDLE)(LONG_PTR) - 2)

    // Shared smart wait logic
    static void UpdateBudget() {
        DWORD calls = g_totalCalls.fetch_add(1, std::memory_order_relaxed);
        if ((calls % ADAPT_INTERVAL) != 0) return;

        DWORD successes = g_spinSuccesses.exchange(0, std::memory_order_relaxed);
        DWORD failures = g_spinFailures.exchange(0, std::memory_order_relaxed);
        DWORD total = successes + failures;
        if (total == 0) return;

        DWORD currentBudget = g_spinBudgetUs.load(std::memory_order_relaxed);
        float successRate = (float)successes / total;

        // Load previous rate (use union for atomic float)
        float prevRate = g_lastSuccessRate.load(std::memory_order_relaxed);
        g_lastSuccessRate.store(successRate, std::memory_order_relaxed);
        float rateDelta = successRate - prevRate;

        DWORD newBudget = currentBudget;

        // stage 1 emergency backoff
        // Loading screen / heavy I/O - cut losses immediately
        if (successRate < 0.30f) {
            newBudget = MIN_SPIN_US;
            g_stableIntervals.store(0, std::memory_order_relaxed);
        }
        // Stage 2 - Stable performance - probe for efficiency
        else if (std::abs(rateDelta) < 0.03f && currentBudget > MIN_SPIN_US) {
            DWORD stable = g_stableIntervals.fetch_add(1, std::memory_order_relaxed);

            if (stable >= 15) {
                // Probe: Can we get same success rate with less CPU?
                newBudget = (DWORD)(currentBudget * 0.90f);
                if (newBudget < MIN_SPIN_US) newBudget = MIN_SPIN_US;
                g_stableIntervals.store(0, std::memory_order_relaxed);
            }
            // else: accumulating stability, no change
        }
        // Stage 3, Rate changed - adapt
        else {
            g_stableIntervals.store(0, std::memory_order_relaxed);

            // Growth (workload got easier OR probe hurt us)
            if (successRate > 0.65f) {
                newBudget = (currentBudget + 50 < MAX_SPIN_US) ? currentBudget + 50 : MAX_SPIN_US;
            } else if (successRate > 0.50f) {
                newBudget = (currentBudget + 25 < MAX_SPIN_US) ? currentBudget + 25 : MAX_SPIN_US;
            } else if (successRate > 0.35f) {
                newBudget = (currentBudget + 10 < MAX_SPIN_US) ? currentBudget + 10 : MAX_SPIN_US;
            }
            // 30-35%: dead zone, no change (avoid oscillation)
        }

        g_spinBudgetUs.store(newBudget, std::memory_order_relaxed);
    }

    // Shared smart wait logic
    // Returns true if the wait was handled (successfully or not), false if we should fallback to the original function
    static bool TrySmartWait(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable, bool isEx, DWORD& outResult) {

        // Disabled -> pass straight through to the real API. This detour is process-wide, so
        // this line runs on every thread in the process and has to stay cheap.
        if (!g_active.load(std::memory_order_relaxed)) return false;

        g_interceptedCalls.fetch_add(1, std::memory_order_relaxed);

        // 00e661ae: Mono domain unload dead code path, don't think this is called but we check just in case, ya never kno...
        // Prevents syscall on invalid handle (fallback to WAIT_FAILED)
        if (hHandle == NULL || hHandle == INVALID_HANDLE_VALUE) {
            SetLastError(ERROR_INVALID_HANDLE);
            outResult = WAIT_FAILED;
            return true;
        }

        // 00ebb2c4: 00ebb2b0+0x14 APC pump pattern
        // GetCurrentThread() returns pseudo-handle -2
        // Only optimize when bAlertable=FALSE to preserve APC delivery
        if (dwMilliseconds == 0 && hHandle == CURRENT_THREAD_PSEUDO_HANDLE) {
            if (!bAlertable) {
                // Running thread never signals - guaranteed WAIT_TIMEOUT
                outResult = WAIT_TIMEOUT;
                return true;
            }
            // Fall through for alertable case as kernel must process APC queue
        }

        // avoid:
        // Alertable waits
        if (isEx && bAlertable) return false;

        // Poll (0ms), spinning adds overhead
        if (dwMilliseconds == 0) return false;

        // Finite waits are likely intentional delays or timeout logic
        if (dwMilliseconds != INFINITE) return false;

        // HYBRID SPIN (INFINITE waits only)

        DWORD budget = g_spinBudgetUs.load(std::memory_order_relaxed);
        if (budget < MIN_SPIN_US) return false;

        // Cache QPC frequency once per process (invariant)
        static LARGE_INTEGER freq = {};
        if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);

        LARGE_INTEGER start, now;
        QueryPerformanceCounter(&start);
        LONGLONG budgetTicks = (budget * freq.QuadPart) / 1000000LL;

        DWORD pauseCycles = 0;
        DWORD loopCount = 0;

        while (true) {
            // A). Kernel object state check (syscall required - no user-mode visibility)
            // CRITICAL: Force bAlertable=FALSE to prevent APC side-effects during spin
            DWORD result;
            if (isEx) {
                result = Original_WaitForSingleObjectEx(hHandle, 0, FALSE);
            } else {
                result = Original_WaitForSingleObject(hHandle, 0);
            }

            // B) Success - object signaled
            if (result != WAIT_TIMEOUT) {
                g_spinSuccesses.fetch_add(1, std::memory_order_relaxed);
                // loopCount is still 0 only on the very first poll, i.e. the object was already
                // signaled when we were called and no pausing happened. Separate those out.
                if (loopCount == 0) {
                    g_spinFreebies.fetch_add(1, std::memory_order_relaxed);
                } else {
                    g_spinWins.fetch_add(1, std::memory_order_relaxed);
                }
                outResult = result;
                UpdateBudget();
                return true;
            }

            // C) Budget check (batched every 8 iterations to reduce QPC overhead)
            if ((loopCount & 7) == 0) {
                QueryPerformanceCounter(&now);
                if ((now.QuadPart - start.QuadPart) >= budgetTicks) {
                    break; // Exhausted - fallback to blocking kernel wait
                }
            }
            loopCount++;

            // D) User-mode backoff (exponential to reduce syscall density)
            // Grows: 1, 2, 4, 8, 16... capped at 4096 pause instructions
            DWORD loops = (1 << pauseCycles);
            if (loops > 4096) loops = 4096;

            for (DWORD i = 0; i < loops; i++) {
                _mm_pause(); // x86 PAUSE - hint to CPU for spin-wait loop
            }

            if (pauseCycles < 12) pauseCycles++; // Cap at 2^12 = 4096
        }

        // Spin budget exhausted, fallback to true kernel blocking
        g_spinFailures.fetch_add(1, std::memory_order_relaxed);
        g_spinLosses.fetch_add(1, std::memory_order_relaxed);
        UpdateBudget();
        return false;
    }

    static DWORD WINAPI Hooked_WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
        DWORD result;
        if (TrySmartWait(hHandle, dwMilliseconds, FALSE, false, result)) { return result; }
        // NOTE: no "return WaitForSingleObject(...)" fallback any more; with an inline detour
        // on the export that call would re-enter this hook and recurse forever. Install() fails
        // if the trampoline was not resolved, so it is non-null by the time we get here.
        return Original_WaitForSingleObject(hHandle, dwMilliseconds);
    }

    static DWORD WINAPI Hooked_WaitForSingleObjectEx(HANDLE hHandle, DWORD dwMilliseconds, BOOL bAlertable) {
        DWORD result;
        if (TrySmartWait(hHandle, dwMilliseconds, bAlertable, true, result)) { return result; }
        // Same reasoning as above; the direct-API fallback would recurse through our own detour.
        return Original_WaitForSingleObjectEx(hHandle, dwMilliseconds, bAlertable);
    }

  public:
    AdaptiveWaitPatch();
    ~AdaptiveWaitPatch() override;

    bool Install() override {
        if (isEnabled) return true;

        g_spinBudgetUs.store(100); // Start smol
        g_spinSuccesses.store(0);
        g_spinFailures.store(0);
        g_totalCalls.store(0);
        g_interceptedCalls.store(0);
        g_spinWins.store(0);
        g_spinFreebies.store(0);
        g_spinLosses.store(0);
        g_lastSuccessRate.store(0.5f);
        g_stableIntervals.store(0);

        HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
        if (!hKernel32) return Fail("Failed to get kernel32 module handle");

        // Inline detour on the kernel32 export, not an IAT hook on the main module.
        //
        // Why: On EA App builds (1.69) TS3.exe is packed and import-stripped, so there is
        // no kernel32 descriptor to walk and IAT hooking cannot work there at all.
        // Detouring the export needs no import metadata and behaves the same on Steam 1.67
        // and EA App 1.69. This also resulted in a non-descriptive error when trying to
        // enable the patch.
        //
        // Bonus maybe? this now also catches waits issued from inside other modules (the Mono
        // runtime, MSVCR80, d3d9), which an IAT hook on the main module structurally could
        // never see as each module has its own IAT. On 1.67 only 4 of 13 static
        // WaitForSingleObject call sites in the main exe pass INFINITE, and all 5
        // WaitForSingleObjectEx sites pass bAlertable=TRUE and were being declined outright,
        // so the old reach was considerably narrower than it looked
        if (!s_detoursInstalled) {
            Original_WaitForSingleObject = (WaitForSingleObject_t)GetProcAddress(hKernel32, "WaitForSingleObject");
            Original_WaitForSingleObjectEx = (WaitForSingleObjectEx_t)GetProcAddress(hKernel32, "WaitForSingleObjectEx");

            if (!Original_WaitForSingleObject || !Original_WaitForSingleObjectEx) { return Fail("Failed to resolve WaitForSingleObject/Ex in kernel32"); }

            std::vector<DetourHelper::Hook> hooks = {{reinterpret_cast<void**>(&Original_WaitForSingleObject), reinterpret_cast<void*>(Hooked_WaitForSingleObject)},
                {reinterpret_cast<void**>(&Original_WaitForSingleObjectEx), reinterpret_cast<void*>(Hooked_WaitForSingleObjectEx)}};

            if (!DetourHelper::InstallHooks(hooks)) { return Fail("Failed to install WaitForSingleObject/Ex detours"); }

            s_detoursInstalled = true;
            LOG_INFO("[AdaptiveWait] Detoured kernel32!WaitForSingleObject/Ex (process-wide)");
        }

        g_active.store(true, std::memory_order_release);
        isEnabled = true;
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        // Deliberately does not detach. WaitForSingleObject is entered by every thread in the
        // process, and DetourHelper only calls DetourUpdateThread(GetCurrentThread()), so a
        // detach here could rewrite the prologue out from under a thread already inside it.
        // Clearing the gate makes the hook a pass-through on its very next entry, which is the
        // safe way to switch this off. (The previous Uninstall did neither; it flipped
        // isEnabled and left the hook fully live and still spinning.)

        // Report the outcome split, not just the engagement count. "Engaged" includes spins that
        // burned their whole budget and blocked anyway, so engagement alone cannot say whether the
        // patch helps. The final spin budget is a free second opinion: UpdateBudget() slams it to
        // MIN_SPIN_US below a 30% success rate and grows it toward MAX_SPIN_US above 65%, so a
        // budget still sitting at the floor means the spinning was mostly not paying off.
        LOG_INFO("[AdaptiveWait] Disabled - intercepted " + std::to_string(g_interceptedCalls.load(std::memory_order_relaxed)) + " wait(s), engaged on " + std::to_string(g_totalCalls.load(std::memory_order_relaxed)) +
                 " (blocks avoided: " + std::to_string(g_spinWins.load(std::memory_order_relaxed)) + ", already signaled: " + std::to_string(g_spinFreebies.load(std::memory_order_relaxed)) +
                 ", spun then blocked: " + std::to_string(g_spinLosses.load(std::memory_order_relaxed)) + "), final spin budget " + std::to_string(g_spinBudgetUs.load(std::memory_order_relaxed)) + "us");

        isEnabled = false;
        return true;
    }
};

AdaptiveWaitPatch::AdaptiveWaitPatch() : OptimizationPatch("AdaptiveWait", nullptr) {
    instance = this;
}

AdaptiveWaitPatch::~AdaptiveWaitPatch() {
    if (instance == this) { instance = nullptr; }
}

std::atomic<DWORD> AdaptiveWaitPatch::g_spinBudgetUs{0};
std::atomic<DWORD> AdaptiveWaitPatch::g_spinSuccesses{0};
std::atomic<DWORD> AdaptiveWaitPatch::g_spinFailures{0};
std::atomic<DWORD> AdaptiveWaitPatch::g_totalCalls{0};
std::atomic<float> AdaptiveWaitPatch::g_lastSuccessRate{0.5f};
std::atomic<DWORD> AdaptiveWaitPatch::g_stableIntervals{0};
std::atomic<bool> AdaptiveWaitPatch::g_active{false};
std::atomic<DWORD> AdaptiveWaitPatch::g_interceptedCalls{0};
std::atomic<DWORD> AdaptiveWaitPatch::g_spinWins{0};
std::atomic<DWORD> AdaptiveWaitPatch::g_spinFreebies{0};
std::atomic<DWORD> AdaptiveWaitPatch::g_spinLosses{0};
bool AdaptiveWaitPatch::s_detoursInstalled = false;
AdaptiveWaitPatch::WaitForSingleObject_t AdaptiveWaitPatch::Original_WaitForSingleObject = nullptr;
AdaptiveWaitPatch::WaitForSingleObjectEx_t AdaptiveWaitPatch::Original_WaitForSingleObjectEx = nullptr;
AdaptiveWaitPatch* AdaptiveWaitPatch::instance = nullptr;

REGISTER_PATCH(AdaptiveWaitPatch, {.displayName = "Adaptive Thread Waiting",
                                      .description = "Replaces standard thread sleeping with a hybrid spin-wait to reduce stutter on short locks.",
                                      .category = "Performance",
                                      .experimental = true,
                                      .technicalDetails = {"Detours kernel32!WaitForSingleObject/Ex process-wide", "Spin budget adapts between 50-500us based on success rate",
                                          "Spends extra CPU time spinning to avoid 3-15ms thread sleep latency", "Lower frame time variance at the cost of slightly higher CPU usage"}})
