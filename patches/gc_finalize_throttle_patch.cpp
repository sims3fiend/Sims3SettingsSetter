#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class GCFinalizeThrottlePatch : public OptimizationPatch {
private:
    // In Sims3::MonoEngine::MonoScriptHost::Simulate frame 
    // threshold check: CMP EAX, 0xC8 (200 frames)
    // After 200 consecutive frames where finalizers exceed limit, triggers blocking loop
    // Pattern: ADD EAX, EBP; CMP EAX, 0xC8; MOV [sFramesSinceFinalizeFinished], EAX; JNZ
    static inline const AddressInfo frameThresholdCheck = {
        .name = "GCFinalize::frameThresholdCheck",
        .pattern = "03 C5 3D C8 00 00 00 A3 ?? ?? ?? ?? 75",
        .patternOffset = 2,  // Skip "ADD EAX, EBP" to land on CMP
        .expectedBytes = {0x3D, 0xC8, 0x00, 0x00, 0x00},  // CMP EAX, 0xC8
    };

    // The blocking finalizer loop: JNZ back to CALL mono_gc_invoke_finalizers
    // This loops until ALL finalizers are done, gross
    // Pattern CALL mono_gc_invoke_finalizers; TEST EAX, EAX; JNZ <back>
    static inline const AddressInfo blockingLoopJump = {
        .name = "GCFinalize::blockingLoopJump",
        .pattern = "E8 ?? ?? ?? ?? 85 C0 75 F7 89 1D",
        .patternOffset = 7,  // Skip CALL and TEST to land on JNZ
        .expectedBytes = {0x75, 0xF7},  // JNZ rel8 (-9)
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    GCFinalizeThrottlePatch() : OptimizationPatch("GCFinalizeThrottle", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[GCFinalizeThrottle] Installing...");

        auto tx = PatchHelper::BeginTransaction();
        bool success = true;

        // Increase frame threshold from 200 (0xC8) to 32767 (0x7FFF)
        // This means it takes ~9 minutes at 60fps of sustained pressure to trigger the blocking loop instead of ~3.3 seconds
        auto thresholdAddr = frameThresholdCheck.Resolve();
        if (thresholdAddr) {
            // CMP EAX, 0xC8 (5 bytes: 3D C8 00 00 00)
            // -> CMP EAX, 0x7FFF (5 bytes: 3D FF 7F 00 00)
            std::vector<BYTE> newThreshold = {0x3D, 0xFF, 0x7F, 0x00, 0x00};
            if (!PatchHelper::WriteBytes(*thresholdAddr, newThreshold, &tx.locations)) {
                LOG_WARNING(std::format("[GCFinalizeThrottle] Failed to patch frame threshold at {:#010x}", *thresholdAddr));
                success = false;
            } else {
                LOG_INFO(std::format("[GCFinalizeThrottle] Patched frame threshold at {:#010x} (200 -> 32767)", *thresholdAddr));
            }
        } else {
            LOG_WARNING("[GCFinalizeThrottle] Could not resolve frame threshold address");
            success = false;
        }

        // NOP the blocking loop jump so it only executes once instead of looping until all finalizers done, just do one batch per frame, should be fine right???
        // JNZ rel8 (75 F7) -> NOP NOP (90 90)
        auto loopAddr = blockingLoopJump.Resolve();
        if (loopAddr) {
            std::vector<BYTE> nops = {0x90, 0x90};
            if (!PatchHelper::WriteBytes(*loopAddr, nops, &tx.locations)) {
                LOG_WARNING(std::format("[GCFinalizeThrottle] Failed to patch blocking loop at {:#010x}", *loopAddr));
                success = false;
            } else {
                LOG_INFO(std::format("[GCFinalizeThrottle] Patched blocking loop at {:#010x} (JNZ -> NOP)", *loopAddr));
            }
        } else {
            LOG_WARNING("[GCFinalizeThrottle] Could not resolve blocking loop address");
            success = false;
        }

        if (!success) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to apply one or more patches");
        }

        if (!PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to commit transaction");
        }

        patchedLocations = tx.locations;
        isEnabled = true;
        LOG_INFO("[GCFinalizeThrottle] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[GCFinalizeThrottle] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        LOG_INFO("[GCFinalizeThrottle] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(GCFinalizeThrottlePatch, {
    .displayName = "GC Finalizer Throttle",
    .description = "Prevents GC finalizer loop from blocking the simulation thread, reducing large stutters",
    .category = "Performance",
    .experimental = true,
    .supportedVersions = VERSION_ALL,
    .technicalDetails = {
        "Increases frame threshold before blocking GC from 200 to 32767 frames (~9 min at 60fps)",
        "Caps the blocking finalizer loop to 1 iteration instead of infinite",
        "Prevents the 'Forcing all finalizers to finish' stall in MonoScriptHost::Simulate",
        "May slightly increase memory usage on very long play sessions, should reduce simulation freees",
    }
})
