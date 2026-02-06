#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

#pragma comment(lib, "ntdll.lib")

extern "C" __declspec(dllimport) NTSTATUS __stdcall NtDelayExecution(BOOLEAN Alertable, LARGE_INTEGER* Interval);

class SmoothPatchPrecise : public OptimizationPatch {
  private:
    // This is Sims3::Scripting::ScriptHostBase::IdleSimulationCycle called from Sims3::MonoEngine::MonoScriptHost::Simulate, I think
    static inline const AddressInfo simulationFrameRateLimiterFunctionAddressInfo = {.name = "SmoothPatchPrecise::simulationFrameTimingSetup",
        .addresses =
            {
                {GameVersion::Retail, 0x00769600},
                {GameVersion::Steam, 0x007694b0},
                {GameVersion::EA, 0x0076a430},
            },
        .pattern = "C7 86 58 0A 00 00 40 8A F7 01 89 8E 5C 0A 00 00 8B 86 58 0A 00 00 8B B6 5C 0A 00 00 3B F1 7C 28 7F 04 3B C1 76 22 51 68 40 42 0F 00 56 50",
        .patternOffset = -218};

    // This is the global (gSimulationThreadId) set by Sims3::Utility::SetSimulationThreadId (called from Sims3::Scripting::ScriptService::Init)
    static inline const AddressInfo simulatorThreadIDAddressInfo = {
        .name = "SmoothPatchPrecise::simulatorThreadID",
        .addresses =
            {
                {GameVersion::Retail, 0x011d9a8c},
                {GameVersion::Steam, 0x011d8a8c},
                {GameVersion::EA, 0x01232b3c},
            },
        // Pattern: 8B 06 8B 50 10 83 C4 04 8B CE FF D2 84 C0 74 DC 6A 00 6A 00 6A 00 6A 00
        // Immediately preceding this code is a call to a function,
        // the address written to in the body of that function is the address of the simulator's thread-ID.
    };

    static inline const AddressInfo _alldivAddressInfo = {
        .name = "SmoothPatchPrecise::_alldiv",
        .addresses =
            {
                {GameVersion::Retail, 0x00f7f8e0},
                {GameVersion::Steam, 0x00f7ef00},
                {GameVersion::EA, 0x00fc47d0},
            },
        // Immediately after the pattern of `simulationFrameTimingSetup` is a call to _alldiv.
        // (Ghidra will have recognised it anyway: you can simply search the symbols for "_alldiv").
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;
    float frameRateLimitSettingStorage;
    float frameRateLimit;
    float frameTimeAsNanoseconds;

    static constexpr uintptr_t offsetOfFrameTime = 30;
    static constexpr uintptr_t offsetOfNegativeDelayLo0 = 176;
    static constexpr uintptr_t offsetOfNegativeDelayLo1 = 188;
    static constexpr uintptr_t offsetOfDelayLo0 = 212;
    static constexpr uintptr_t offsetOfDelayLo1 = 224;
    static constexpr uintptr_t offsetOfSleepCall = 256;

    static constexpr uint32_t oneSecondAsNanoseconds = 1'000'000'000;

    bool CanPatchSafely() {
        // As we're patching a (transitive) call to a sleep function,
        // it is very likely that we'll patch the function whilst the
        // simulator thread is sleeping: in such a scenario the thread will
        // wake up and return into the middle of an instruction, and then crash the game.
        // To avoid this happening,
        // we'll patch the function only if the simulator thread is not running.

        auto simulatorThreadIDAddress = simulatorThreadIDAddressInfo.Resolve();

        if (!simulatorThreadIDAddress) { return Fail("Could not resolve simulatorThreadID address"); }

        uint32_t simulatorThreadID = *reinterpret_cast<const uint32_t*>(*simulatorThreadIDAddress);

        if (simulatorThreadID == 0) { return true; }

        HANDLE simulatorThread = OpenThread(THREAD_QUERY_INFORMATION, false, simulatorThreadID);

        if (simulatorThread == nullptr) { return true; }

        CloseHandle(simulatorThread);

        return Fail("The game must be restarted for this patch to be installed/uninstalled.");
    }

  public:
    SmoothPatchPrecise() : OptimizationPatch("SmoothPatchPrecise", nullptr) {
        // The game's code/debug-printing refers to this as the simulation frame-rate,
        // but users are used to it being called the tick-rate, so we'll call it that in the UI.
        RegisterFloatSetting(&frameRateLimitSettingStorage, "tickRateLimit", SettingUIType::InputBox,
            60.0f,         // Most people will be using a 60 Hz display, so 60 is a reasonable default.
            0.25f,         // (oneSecondAsNanoseconds / 0.25) is close to overflowing a 32-bit value.
            1000000001.0f, // (oneSecondAsNanoseconds / (oneSecondAsNanoseconds + 1)) will truncate to 0.
            "Tick-rate limit",
            {
                {"Unlimited", 1000000001.0f},
                {"30 TPS", 30.0f},
                {"60 TPS", 60.0f},
                {"75 TPS", 75.0f},
                {"120 TPS", 120.0f},
                {"144 TPS", 144.0f},
                {"160 TPS", 160.0f},
                {"165 TPS", 165.0f},
                {"200 TPS", 200.0f},
                {"240 TPS", 240.0f},
                {"360 TPS", 360.0f},
                {"480 TPS", 480.0f},
                {"500 TPS", 500.0f},
                {"1,000 TPS", 1000.0f},
            });
    }

    bool Install() override {
        if (isEnabled) return true;
        isEnabled = true;

        frameRateLimit = frameRateLimitSettingStorage;
        frameTimeAsNanoseconds = static_cast<float>(oneSecondAsNanoseconds) / frameRateLimit;

        uint32_t frameDelay = static_cast<uint32_t>(frameTimeAsNanoseconds);
#pragma warning(disable : 4146, justification : "Two's complement is not an error. Good grief.")
        uint32_t frameDelayNegated = -frameDelay;

        auto simulationFrameRateLimiterFunctionAddress = simulationFrameRateLimiterFunctionAddressInfo.Resolve();

        if (!simulationFrameRateLimiterFunctionAddress) { return Fail("Could not resolve simulationFrameRateLimiterFunction address"); }

        uintptr_t base = *simulationFrameRateLimiterFunctionAddress;

        if (patchedLocations.size() != 0) {
            // If we've already patched this function,
            // then we're reinstalling the patch because the user changed a setting.
            // We can do this safely as we're just changing immediate values.

            lastError.clear();
            LOG_INFO("[SmoothPatchPrecise] Reinstalling...");
            LOG_INFO(std::format("[SmoothPatchPrecise] frameRateLimit: {}; frameTimeAsNanoseconds: {:.2f}", frameRateLimit, frameTimeAsNanoseconds));

            bool successful = true;
            auto tx = PatchHelper::BeginTransaction();

            successful &= PatchHelper::WriteDWORD(base + offsetOfFrameTime, (uintptr_t)&frameTimeAsNanoseconds, &tx.locations);
            successful &= PatchHelper::WriteDWORD(base + offsetOfNegativeDelayLo0, frameDelayNegated, &tx.locations);
            successful &= PatchHelper::WriteDWORD(base + offsetOfNegativeDelayLo1, frameDelayNegated, &tx.locations);
            successful &= PatchHelper::WriteDWORD(base + offsetOfDelayLo0, frameDelay, &tx.locations);
            successful &= PatchHelper::WriteDWORD(base + offsetOfDelayLo1, frameDelay, &tx.locations);

            if (!successful || !PatchHelper::CommitTransaction(tx)) {
                PatchHelper::RollbackTransaction(tx);
                return Fail("Failed to reinstall");
            }

            LOG_INFO("[SmoothPatchPrecise] Successfully reinstalled");
            return true;
        }

        if (!CanPatchSafely()) return false;

        lastError.clear();
        LOG_INFO("[SmoothPatchPrecise] Installing...");
        LOG_INFO(std::format("[SmoothPatchPrecise] frameRateLimit: {}; frameTimeAsNanoseconds: {:.2f}", frameRateLimit, frameTimeAsNanoseconds));

        auto _alldivAddress = _alldivAddressInfo.Resolve();

        if (!_alldivAddress) { return Fail("Could not resolve _alldiv address"); }

        uintptr_t _alldiv = *_alldivAddress;

        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();

        successful &= PatchHelper::WriteDWORD(base + offsetOfFrameTime, (uintptr_t)&frameTimeAsNanoseconds, &tx.locations);
        successful &= PatchHelper::WriteDWORD(base + offsetOfNegativeDelayLo0, frameDelayNegated, &tx.locations);
        successful &= PatchHelper::WriteDWORD(base + offsetOfNegativeDelayLo1, frameDelayNegated, &tx.locations);
        successful &= PatchHelper::WriteDWORD(base + offsetOfDelayLo0, frameDelay, &tx.locations);
        successful &= PatchHelper::WriteDWORD(base + offsetOfDelayLo1, frameDelay, &tx.locations);

        uint8_t preciseSleepCall[34] = {
            /* We could use the more compact `0x6A imm8` encoding of push here,
               but we don't so that we can pad this block with one nop instead of two. */
            0x68,
            0xFF,
            0xFF,
            0xFF,
            0xFF, // push 0xFFFFFFFF
            0x68,
            0x9C,
            0xFF,
            0xFF,
            0xFF, // push -100
            0x56, // push esi
            0x50, // push eax
            /* We divide by -100 to convert the nanosecond-based value in eax:esi
               to a hectonanosecond-based value. */
            /* We divide by -100 instead of 100 so that the result is negative,
               which we want because NtDelayExecution uses a negative interval
               for sleeping by a relative duration. */
            //12:
            0xE8,
            0x77,
            0x77,
            0x77,
            0x77, // call _alldiv
            /* The duration to sleep for is currently held across eax:edx,
               so we store those on the stack for the `Interval` argument of NtDelayExecution. */
            0x52, // push edx
            0x50, // push eax
            0x54, // push esp
            /* A non-alertable sleep, in hopes of consistent frame-pacing. */
            0x6A,
            0x00, // push 0
                  //22:
            0xE8,
            0x77,
            0x77,
            0x77,
            0x77, // call NtDelayExecution
            0x83,
            0304,
            0x08, // add esp, 8
            0x0F,
            0x1F,
            0x40,
            0x00, // nop
        };

        uintptr_t sleepCallBase = base + offsetOfSleepCall;
        uintptr_t ntDelayExecution = reinterpret_cast<uintptr_t>(&NtDelayExecution);

        int32_t _alldivDisplacement = PatchHelper::CalculateRelativeOffset(sleepCallBase + 12, _alldiv);
        int32_t delayDisplacement = PatchHelper::CalculateRelativeOffset(sleepCallBase + 22, ntDelayExecution);

        std::memcpy(preciseSleepCall + 13, &_alldivDisplacement, 4);
        std::memcpy(preciseSleepCall + 23, &delayDisplacement, 4);

        successful &= PatchHelper::WriteProtectedMemory(reinterpret_cast<uint8_t*>(sleepCallBase), preciseSleepCall, 34, &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            isEnabled = false;
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

        LOG_INFO("[SmoothPatchPrecise] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        isEnabled = false;
        if (!CanPatchSafely()) return false;

        lastError.clear();
        LOG_INFO("[SmoothPatchPrecise] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            isEnabled = true;
            return Fail("Failed to restore original code");
        }

        LOG_INFO("[SmoothPatchPrecise] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(SmoothPatchPrecise, {.displayName = "Smooth Patch (Precise Flavour)",
                                       .description = "Adjusts the game's simulation tick-rate limiter directly. For smoother gameplay.\nMuch like LazyDuchess's original Smooth Patch.",
                                       .category = "Performance",
                                       .experimental = false,
                                       .supportedVersions = VERSION_ALL,
                                       .technicalDetails = {
                                           "Credit goes to LazyDuchess for the original Smooth Patch.",
                                           "This patch was authored by \"Just Harry\".",
                                           "Unlike the original Smooth Patch, this patch does not alter sleep-durations unrelated to the game's simulator.",
                                           "Additionally, this patch can sleep for sub-millisecond durations, so there is a difference, for example, between 750 TPS and 1,000 TPS.",
                                       }})
