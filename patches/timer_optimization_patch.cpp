#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <winternl.h>

#pragma comment(lib, "winmm.lib")

typedef NTSTATUS(WINAPI* NtSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
typedef NTSTATUS(WINAPI* NtQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

// Static variables for the naked asm spin loop stub
static DWORD spinCounter = 0;
static FARPROC originalGetTickCountAddr = nullptr;
static FARPROC sleepFuncAddr = nullptr;

static void __declspec(naked) SpinLoopStub() {
    // Might use Xbyak or asmjit in the future, but this is okay. technically could save a call if we inlined GetTickCount but not worth it
    __asm {
        // Call original GetTickCount
        call dword ptr [originalGetTickCountAddr]

        // Save return value
        push eax

        // Increment spin counter (thread-safe)
        lock inc dword ptr [spinCounter]

        // Check if we've spun 50 times
        cmp dword ptr [spinCounter], 50
        jb skip_sleep

        // Reset counter
        mov dword ptr [spinCounter], 0

        // Sleep(1) - __stdcall so it cleans up its own stack
        push 1
        call dword ptr [sleepFuncAddr]

    skip_sleep:
        // Restore return value
        pop eax

        // Jump back to original code after the call
        push 0x00401639
        ret
    }
}

class TimerOptimizationPatch : public OptimizationPatch {
private:
    typedef void (WINAPI* InitializeCriticalSectionFn)(LPCRITICAL_SECTION lpCriticalSection);

    InitializeCriticalSectionFn originalInitializeCriticalSection;

    static const DWORD SPIN_COUNT = 8000;
    static const UINT TARGET_TIMER_RES = 1;
    static const uintptr_t SPIN_LOOP_ADDRESS = 0x00401634;

    ULONG originalTimerResolution = 0;
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    std::vector<DetourHelper::Hook> hooks;

    static TimerOptimizationPatch* instance;

    static void WINAPI HookedInitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection) {
        if (!instance || !instance->originalInitializeCriticalSection) {
            InitializeCriticalSection(lpCriticalSection);
            return;
        }

        instance->originalInitializeCriticalSection(lpCriticalSection);

        if (!SetCriticalSectionSpinCount(lpCriticalSection, SPIN_COUNT)) {
            LOG_DEBUG("[TimerOpt] Failed to set spin count for critical section");
        }
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

    bool ApplySpinLoopPatch() {
        BYTE* target = (BYTE*)SPIN_LOOP_ADDRESS;

        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(target, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT) {
            LOG_WARNING("[TimerOpt] Target address not accessible - skipping spin loop patch");
            return true;
        }

        if (target[0] != 0xE8) {
            LOG_WARNING("[TimerOpt] Target is not a CALL instruction (found 0x" +
                       std::to_string(target[0]) + ") - skipping spin loop patch");
            return true;
        }

        // Extract the original GetTickCount address from the CALL instruction
        DWORD origDisp = *(DWORD*)(target + 1);
        originalGetTickCountAddr = (FARPROC)((DWORD)(target + 5) + origDisp);

        // Get Sleep function address
        sleepFuncAddr = GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep");
        if (!sleepFuncAddr) {
            return Fail("Failed to get Sleep address");
        }

        // Initialize spin counter
        spinCounter = 0;

        // Patch the call to jump to our naked asm stub
        if (!PatchHelper::WriteRelativeJump((uintptr_t)target, (uintptr_t)SpinLoopStub, &patchedLocations)) {
            return Fail("Failed to write jump to stub");
        }

        FlushInstructionCache(GetCurrentProcess(), target, 5);
        LOG_INFO("[TimerOpt] CPU spin loop patch applied");
        return true;
    }

    void RemoveSpinLoopPatch() {
        // Nothing to clean up - naked asm stub is part of the code segment now lol
    }

public:
    TimerOptimizationPatch() : OptimizationPatch("TimerOptimization", nullptr),
        originalInitializeCriticalSection(nullptr),
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

        originalInitializeCriticalSection =
            (InitializeCriticalSectionFn)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "InitializeCriticalSection");

        if (!originalInitializeCriticalSection) {
            instance = nullptr;
            return Fail("Cannot install timer optimization - missing InitializeCriticalSection function");
        }

        if (!SetHighResolutionTimer()) {
            instance = nullptr;
            return Fail("Failed to set high resolution timer");
        }

        hooks = {
            {(void**)&originalInitializeCriticalSection, (void*)HookedInitializeCriticalSection}
        };

        if (!DetourHelper::InstallHooks(hooks)) {
            RestoreTimerResolution();
            instance = nullptr;
            return Fail("Failed to install InitializeCriticalSection hook");
        }

        if (!ApplySpinLoopPatch()) {
            DetourHelper::RemoveHooks(hooks);
            RestoreTimerResolution();
            instance = nullptr;
            return false;
        }

        isEnabled = true;
        LOG_INFO("[TimerOpt] Timer optimization patch installed successfully");
        LOG_INFO("[TimerOpt] + Timer resolution: 1ms");
        LOG_INFO("[TimerOpt] + Critical sections: Spin count " + std::to_string(SPIN_COUNT));
        LOG_INFO("[TimerOpt] + CPU spin loop: Patched");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[TimerOpt] Removing timer optimization patch...");

        RemoveSpinLoopPatch();

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            LOG_ERROR("[TimerOpt] Failed to restore spin loop patches");
        }

        if (!DetourHelper::RemoveHooks(hooks)) {
            return Fail("Failed to remove InitializeCriticalSection hook");
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
    .targetVersion = GameVersion::Steam,
    .technicalDetails = {
        "Sets system timer to 1ms via NtSetTimerResolution + timeBeginPeriod for better frame pacing",
        "Hooks InitializeCriticalSection to add 8000 spin count, reducing context switches on locks/contention",
        "Patches busy-wait loop at 0x00401634 to inject Sleep(1) every 50 iterations",
        "Basically transforms a busy-wait into a hybrid spin-wait/sleep wait, should decently reduce idle CPU usage",
    }
})
