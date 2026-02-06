#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <winternl.h>
#pragma comment(lib, "winmm.lib")

typedef NTSTATUS(WINAPI* NtSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
typedef NTSTATUS(WINAPI* NtQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

class TimerOptimizationPatch : public OptimizationPatch {
private:
    // Address definitions for critical section targets, TODO better ways to double check these
    static inline const AddressInfo monoMethodCacheTimer = {
        .name = "TimerOpt::monoMethodCacheTimer", // marshal_mutex, global lock for hash tables, need to double check that this is sane but should be fine x
        .addresses = {
            {GameVersion::Retail, 0x011f53e4},
            {GameVersion::Steam,  0x011f43e4},
            {GameVersion::EA,     0x0124e474},
        },
    };

    static inline const AddressInfo browserTimer = {
        .name = "TimerOpt::browserTimer",
        .addresses = {
            {GameVersion::Retail, 0x011eb210},
            {GameVersion::Steam,  0x011ea210},
            {GameVersion::EA,     0x01244260},
        },
    };

    static inline const AddressInfo unknownTimer = {
        .name = "TimerOpt::unknownTimer",
        .addresses = {
            {GameVersion::Retail, 0x011f53a8},
            {GameVersion::Steam,  0x011f43a8},
            {GameVersion::EA,     0x0124e438},
        },
    };

    typedef void (WINAPI* InitializeCriticalSectionFn)(LPCRITICAL_SECTION lpCriticalSection);
    typedef BOOL (WINAPI* InitializeCriticalSectionAndSpinCountFn)(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount);
    typedef DWORD (WINAPI* SetCriticalSectionSpinCountFn)(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount);

    InitializeCriticalSectionFn originalInitializeCriticalSection;
    InitializeCriticalSectionAndSpinCountFn originalInitializeCriticalSectionAndSpinCount;
    SetCriticalSectionSpinCountFn originalSetCriticalSectionSpinCount;

    static const DWORD DEFAULT_SPIN_COUNT = 4000;
    static const UINT TARGET_TIMER_RES = 1;

    // Runtime configuration structure for critical section optimization
    struct CriticalSectionConfig {
        uintptr_t address;
        DWORD spinCount;
        const char* debugName;
    };

    std::vector<CriticalSectionConfig> csTargets;  // Built at runtime from AddressInfo

    ULONG originalTimerResolution = 0;
    std::vector<DetourHelper::Hook> hooks;

    static TimerOptimizationPatch* instance;

    static void WINAPI HookedInitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
        if (!instance) {
            InitializeCriticalSection(lpCriticalSection);
            return;
        }

        if (instance->originalInitializeCriticalSectionAndSpinCount) {
            instance->originalInitializeCriticalSectionAndSpinCount(lpCriticalSection, DEFAULT_SPIN_COUNT);
        } else {
            if (instance->originalInitializeCriticalSection) {
                instance->originalInitializeCriticalSection(lpCriticalSection);
            }
            if (instance->originalSetCriticalSectionSpinCount) {
                instance->originalSetCriticalSectionSpinCount(lpCriticalSection, DEFAULT_SPIN_COUNT);
            }
        }
    }

    static BOOL WINAPI HookedInitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION lpCriticalSection, DWORD dwSpinCount) {
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
                LOG_INFO(std::format("[TimerOpt] Timer resolution: {}ms", newRes / 10000.0));
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

    bool BuildCSTargets() {
        csTargets.clear();

        // Build CS targets from AddressInfo, resolving for current version
        auto monoAddr = monoMethodCacheTimer.Resolve();
        auto browserAddr = browserTimer.Resolve();
        auto unkAddr = unknownTimer.Resolve();

        if (monoAddr) {
            csTargets.push_back({*monoAddr, 24000, "Mono_MethodCache"});
        }
        if (browserAddr) {
            csTargets.push_back({*browserAddr, 20000, "Browser"});
        }
        if (unkAddr) {
            csTargets.push_back({*unkAddr, 8000, "Unk_LookAtLater"});
        }

        return !csTargets.empty();
    }

    bool PatchSpecificCriticalSections() {
        int successCount = 0;

        LOG_INFO("[TimerOpt] Applying tiered Critical Section patching...");

        for (const auto& config : csTargets) {
            LPCRITICAL_SECTION critSec = (LPCRITICAL_SECTION)config.address;

            MEMORY_BASIC_INFORMATION mbi;
            if (!PatchHelper::IsMemoryWritable(critSec, &mbi)) {
                LOG_WARNING(std::format("[TimerOpt] Skipped {} (Invalid Memory at {:#010x}; State: {:#x}; Protect: {:#x})",
                          config.debugName, config.address, mbi.State, mbi.Protect));
                continue;
            }

            DWORD prevCount = 0;
            if (originalSetCriticalSectionSpinCount) {
                prevCount = originalSetCriticalSectionSpinCount(critSec, config.spinCount);
            } else {
                prevCount = ::SetCriticalSectionSpinCount(critSec, config.spinCount);
            }

            if (prevCount != config.spinCount) {
                LOG_INFO(std::format("[TimerOpt] Tuned {} ({:#010x}) Spin: {} -> {}",
                         config.debugName, config.address, prevCount, config.spinCount));
                successCount++;
            }
        }

        LOG_INFO(std::format("[TimerOpt] Successfully tuned {}/{} critical sections", successCount, csTargets.size()));
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

        // Build CS targets for current version
        if (!BuildCSTargets()) {
            LOG_WARNING("[TimerOpt] No critical section targets available for this version");
        }

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
        if (!csTargets.empty() && !PatchSpecificCriticalSections()) {
            DetourHelper::RemoveHooks(hooks);
            RestoreTimerResolution();
            instance = nullptr;
            return false;
        }

        isEnabled = true;
        LOG_INFO("[TimerOpt] Timer optimization patch installed successfully");
        LOG_INFO("[TimerOpt] + Timer resolution: 1ms");
        LOG_INFO(std::format("[TimerOpt] + Critical sections: Default spin count {} (new), tiered spin counts for {} hot sections",
                 DEFAULT_SPIN_COUNT, csTargets.size()));
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

REGISTER_PATCH(TimerOptimizationPatch, {
    .displayName = "Timer & Threading Optimizations",
    .description = "Fixes several timing and threading inefficiencies to reduce CPU usage, stutter, etc. for smoother gameplay.",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = VERSION_ALL,
    .technicalDetails = {
        "Sets system timer to 1ms via NtSetTimerResolution + timeBeginPeriod",
        "Hooks InitializeCriticalSection(AndSpinCount) to force 4000 spin count for new crit sections, reducing context switches",
        "Tiered critical section optimization for static critical sections (more to come, currently 3/47)",
        "Once enabled, static address patches will remain active even when disabled",
        "Should make performance good, idfk lmao"
    }
})
