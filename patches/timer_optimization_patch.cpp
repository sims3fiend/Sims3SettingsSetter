#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <winternl.h>
//Might break into generic hook patches + steam specific since the some stuff is all steam specific, RIP EA once again
//This is still a WIP however I don't know if it's worth continuing or even keeping tbh
#pragma comment(lib, "winmm.lib")

typedef NTSTATUS(WINAPI* NtSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
typedef NTSTATUS(WINAPI* NtQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

class TimerOptimizationPatch : public OptimizationPatch {
private:
    typedef void (WINAPI* InitializeCriticalSectionFn)(LPCRITICAL_SECTION lpCriticalSection);
    typedef BOOL (WINAPI* InitializeCriticalSectionAndSpinCountFn)(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount);
    typedef DWORD (WINAPI* SetCriticalSectionSpinCountFn)(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount);

    InitializeCriticalSectionFn originalInitializeCriticalSection;
    InitializeCriticalSectionAndSpinCountFn originalInitializeCriticalSectionAndSpinCount;
    SetCriticalSectionSpinCountFn originalSetCriticalSectionSpinCount;

    static const DWORD DEFAULT_SPIN_COUNT = 4000;       // Standard "good enough" spin count
    static const UINT TARGET_TIMER_RES = 1;

    // Configuration structure for tiered critical section optimization
    struct CriticalSectionConfig {
        uintptr_t address;
        DWORD spinCount;
        const char* debugName;
    };

    // Tiered critical section targets
    static const std::vector<CriticalSectionConfig> CS_TARGETS;

    ULONG originalTimerResolution = 0;
    std::vector<DetourHelper::Hook> hooks;

    static TimerOptimizationPatch* instance;

    static void WINAPI HookedInitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
        if (!instance) {
            InitializeCriticalSection(lpCriticalSection);
            return;
        }

        if (instance->originalInitializeCriticalSectionAndSpinCount) {
            // Redirect to the version with spin count
            instance->originalInitializeCriticalSectionAndSpinCount(lpCriticalSection, DEFAULT_SPIN_COUNT);
        } else {
            // Fallback
            if (instance->originalInitializeCriticalSection) {
                instance->originalInitializeCriticalSection(lpCriticalSection);
            }
            // Try to set it afterwards if we have the setter
            if (instance->originalSetCriticalSectionSpinCount) {
                instance->originalSetCriticalSectionSpinCount(lpCriticalSection, DEFAULT_SPIN_COUNT);
            }
        }
    }

    static BOOL WINAPI HookedInitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount) {
        // Upgrade pathetic spin counts (like 0 or 10) to something useful
        if (dwSpinCount < DEFAULT_SPIN_COUNT) {
            dwSpinCount = DEFAULT_SPIN_COUNT;
        }

        if (instance && instance->originalInitializeCriticalSectionAndSpinCount) {
            return instance->originalInitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
        }
        return ::InitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);
    }

    static DWORD WINAPI HookedSetCriticalSectionSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount) {
        if (dwSpinCount < DEFAULT_SPIN_COUNT) {
            dwSpinCount = DEFAULT_SPIN_COUNT;
        }

        if (instance && instance->originalSetCriticalSectionSpinCount) {
            return instance->originalSetCriticalSectionSpinCount(lpCriticalSection, dwSpinCount);
        }
        return ::SetCriticalSectionSpinCount(lpCriticalSection, dwSpinCount);
    }

    //helpers
    bool SetHighResolutionTimer() {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) {
            LOG_ERROR("[TimerOpt] Failed to get ntdll handle");
            return false;
        }

        auto NtSetTimerResolution = (NtSetTimerResolution_t)GetProcAddress(ntdll, "NtSetTimerResolution");
        auto NtQueryTimerResolution = (NtQueryTimerResolution_t)GetProcAddress(ntdll, "NtQueryTimerResolution");

        if (!NtSetTimerResolution || !NtQueryTimerResolution) {
            LOG_ERROR("[TimerOpt] Failed to get timer functions");
            return false;
        }

        ULONG minRes, maxRes, currentRes;
        if (NtQueryTimerResolution(&minRes, &maxRes, &currentRes) == 0) {
            originalTimerResolution = currentRes;

            ULONG newRes = 0;
            NTSTATUS status = NtSetTimerResolution(10000, TRUE, &newRes);

            if (status == 0) {
                LOG_INFO("[TimerOpt] Timer resolution: " +
                         std::to_string(newRes / 10000.0) + "ms");
            }
        }

        timeBeginPeriod(TARGET_TIMER_RES);
        return true;
    }

    void RestoreTimerResolution() {
        timeEndPeriod(TARGET_TIMER_RES);

        if (originalTimerResolution != 0) {
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (!ntdll) return;

            auto NtSetTimerResolution = (NtSetTimerResolution_t)GetProcAddress(ntdll, "NtSetTimerResolution");
            if (NtSetTimerResolution) {
                ULONG currentRes = 0;
                NtSetTimerResolution(originalTimerResolution, TRUE, &currentRes);
                LOG_INFO("[TimerOpt] Timer resolution restored");
            }
        }
    }

    bool PatchSpecificCriticalSections() {
        int successCount = 0;

        LOG_INFO("[TimerOpt] Applying tiered Critical Section patching...");

        for (const auto& config : CS_TARGETS) {
            LPCRITICAL_SECTION critSec = (LPCRITICAL_SECTION)config.address;

            // 1. Memory Safety Check
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(critSec, &mbi, sizeof(mbi)) == 0 ||
                mbi.State != MEM_COMMIT ||
                (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) == 0) {

                LOG_WARNING("[TimerOpt] Skipped " + std::string(config.debugName) + " (Invalid Memory)");
                continue;
            }

            // 2. Apply the tiered spin count
            DWORD prevCount = 0;
            if (originalSetCriticalSectionSpinCount) {
                prevCount = originalSetCriticalSectionSpinCount(critSec, config.spinCount);
            } else {
                prevCount = ::SetCriticalSectionSpinCount(critSec, config.spinCount);
            }

            // 3. Log the upgrade
            // Only log if we actually changed it
            if (prevCount != config.spinCount) {
                char addressBuffer[32];
                sprintf_s(addressBuffer, "0x%08X", (unsigned int)config.address);

                LOG_INFO("[TimerOpt] Tuned " + std::string(config.debugName) +
                         " (" + std::string(addressBuffer) + ")" +
                         " Spin: " + std::to_string(prevCount) + " -> " + std::to_string(config.spinCount));
                successCount++;
            }
        }

        LOG_INFO("[TimerOpt] Successfully tuned " + std::to_string(successCount) + "/" + std::to_string(CS_TARGETS.size()) + " critical sections");
        return true;
    }

public:
    TimerOptimizationPatch() : OptimizationPatch("TimerOptimization", nullptr),
        originalInitializeCriticalSection(nullptr),
        originalInitializeCriticalSectionAndSpinCount(nullptr),
        originalSetCriticalSectionSpinCount(nullptr),
        originalTimerResolution(0) {
    }

    ~TimerOptimizationPatch() {
        Uninstall();
        if (instance == this) {
            instance = nullptr;
        }
    }

    bool Install() override {
        if (isEnabled) return true;

        instance = this;

        lastError.clear();
        LOG_INFO("[TimerOpt] Installing timer optimization patch...");

        HMODULE hKernel = GetModuleHandle(L"kernel32.dll");
        if (!hKernel) return Fail("Failed to get kernel32.dll handle");

        originalInitializeCriticalSection =
            (InitializeCriticalSectionFn)GetProcAddress(hKernel, "InitializeCriticalSection");
        originalInitializeCriticalSectionAndSpinCount =
            (InitializeCriticalSectionAndSpinCountFn)GetProcAddress(hKernel, "InitializeCriticalSectionAndSpinCount");
        originalSetCriticalSectionSpinCount =
            (SetCriticalSectionSpinCountFn)GetProcAddress(hKernel, "SetCriticalSectionSpinCount");

        if (!originalInitializeCriticalSection || !originalInitializeCriticalSectionAndSpinCount || !originalSetCriticalSectionSpinCount) {
            instance = nullptr;
            return Fail("Cannot install timer optimization - missing CriticalSection functions");
        }

        if (!SetHighResolutionTimer()) {
            instance = nullptr;
            return Fail("Failed to set high resolution timer");
        }

        hooks = {
            {(void**)&originalInitializeCriticalSection, (void*)HookedInitializeCriticalSection},
            {(void**)&originalInitializeCriticalSectionAndSpinCount, (void*)HookedInitializeCriticalSectionAndSpinCount},
            {(void**)&originalSetCriticalSectionSpinCount, (void*)HookedSetCriticalSectionSpinCount}
        };

        if (!DetourHelper::InstallHooks(hooks)) {
            RestoreTimerResolution();
            instance = nullptr;
            return Fail("Failed to install CriticalSection hooks");
        }

        // Patch the specific critical sections that are already initialized by the game
        if (!PatchSpecificCriticalSections()) {
            DetourHelper::RemoveHooks(hooks);
            RestoreTimerResolution();
            instance = nullptr;
            return false;
        }

        isEnabled = true;
        LOG_INFO("[TimerOpt] Timer optimization patch installed successfully");
        LOG_INFO("[TimerOpt] + Timer resolution: 1ms");
        LOG_INFO("[TimerOpt] + Critical sections: Default spin count " + std::to_string(DEFAULT_SPIN_COUNT) + " (new), tiered spin counts for " + std::to_string(CS_TARGETS.size()) + " hot sections");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[TimerOpt] Removing timer optimization patch...");

        if (!DetourHelper::RemoveHooks(hooks)) {
            return Fail("Failed to remove CriticalSection hooks");
        }

        RestoreTimerResolution();

        if (instance == this) {
            instance = nullptr;
        }

        isEnabled = false;
        LOG_INFO("[TimerOpt] Timer optimization patch removed successfully");
        return true;
    }
};

TimerOptimizationPatch* TimerOptimizationPatch::instance = nullptr;

// Define the tiered critical section targets
const std::vector<TimerOptimizationPatch::CriticalSectionConfig> TimerOptimizationPatch::CS_TARGETS = {
    // CRITICAL TIER: (Short hold, high frequency)
    {0x011f43e4, 24000, "Mono_MethodCache"},

    // HIGH TIER: Many references, rarely contended
    {0x011ea210, 20000, "Browser"}, //idk if this ever gets used but would be kind of... scary if it was

    // MEDIUM TIER: The original optimization target, I forgor
    {0x011f43a8, 8000,  "Unk_LookAtLater"},
};

REGISTER_PATCH(TimerOptimizationPatch, {
    .displayName = "Timer & Threading Optimizations (Updated!)",
    .description = "Fixes several timing and threading inefficiencies to reduce CPU usage, stutter, etc. for smoother gameplay.",
    .category = "Performance",
    .experimental = false,
    .targetVersion = GameVersion::Steam,
    .technicalDetails = {
        "Sets system timer to 1ms via NtSetTimerResolution + timeBeginPeriod",
        "Hooks InitializeCriticalSection(AndSpinCount) to force 4000 spin count for new crit sections, reducing context switches",
        "Tiered critical section optimization for static critical sections (more to come, currently 3/47)", //me when I lie
        "Once enabled, static address patches will remain active even when disabled",
        "Should make performance good, idfk lmao"
    }
})
