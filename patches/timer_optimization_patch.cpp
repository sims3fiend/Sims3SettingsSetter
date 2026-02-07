#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <winternl.h>
#pragma comment(lib, "winmm.lib")

typedef NTSTATUS(WINAPI* NtSetTimerResolution_t)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
typedef NTSTATUS(WINAPI* NtQueryTimerResolution_t)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);

class TimerOptimizationPatch : public OptimizationPatch {
  private:
    static const UINT TARGET_TIMER_RES = 1;

    ULONG originalTimerResolution = 0;

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

            if (status == 0) { LOG_INFO(std::format("[TimerOpt] Timer resolution: {}ms", newRes / 10000.0)); }
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

  public:
    TimerOptimizationPatch() : OptimizationPatch("TimerOptimization", nullptr), originalTimerResolution(0) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[TimerOpt] Installing timer optimization patch...");

        if (!SetHighResolutionTimer()) { return Fail("Failed to set high resolution timer"); }

        isEnabled = true;
        LOG_INFO("[TimerOpt] Timer optimization patch installed successfully");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[TimerOpt] Removing timer optimization patch...");

        RestoreTimerResolution();

        isEnabled = false;
        LOG_INFO("[TimerOpt] Timer optimization patch removed successfully");
        return true;
    }
};

REGISTER_PATCH(TimerOptimizationPatch,
    {.displayName = "Timer Optimization",
        .description = "Sets system timer resolution to 1ms for smoother frame pacing and reduced stutter.",
        .category = "Performance",
        .experimental = false,
        .supportedVersions = VERSION_ALL,
        .technicalDetails = {"Sets system timer to 1ms via NtSetTimerResolution + timeBeginPeriod", "Reduces Sleep() granularity from ~15.6ms to ~1ms", "Properly restores original resolution on disable"}})
