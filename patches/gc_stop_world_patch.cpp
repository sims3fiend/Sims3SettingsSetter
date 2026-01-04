#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../addresses.h"
#include "../logger.h"

class GCStopWorldPatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    GCStopWorldPatch() : OptimizationPatch("GCStopWorld", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[GCStopWorld] Installing...");

        uintptr_t threadLoopCheck = gameAddresses->gcThreadLoopCheck;

        // Validate original bytes at target location
        // Original: 3D 00 01 00 00  ; CMP EAX, 0x100
        //           7C 05           ; JL 00e51201
        std::vector<BYTE> expectedBytes = { 0x3D, 0x00, 0x01, 0x00, 0x00, 0x7C, 0x05 };
        if (!PatchHelper::ValidateBytes((LPVOID)threadLoopCheck, expectedBytes.data(), expectedBytes.size())) {
            return Fail("Unexpected bytes at target address - game version mismatch?");
        }

        // Replace with:
        // 85 C0        ; TEST EAX, EAX
        // 74 7D        ; JZ 0x00E51276 (skip thread loop if count is 0)
        // 0F 1F 00     ; NOP padding
        std::vector<BYTE> newBytes = { 0x85, 0xC0, 0x74, 0x7D, 0x0F, 0x1F, 0x00 };

        if (!PatchHelper::WriteBytes(threadLoopCheck, newBytes, &patchedLocations, &expectedBytes)) {
            return Fail(std::format("Failed to patch thread loop check at {:#010x}", threadLoopCheck));
        }

        isEnabled = true;
        LOG_INFO("[GCStopWorld] Successfully installed");
        LOG_INFO("[GCStopWorld] Optimized GC_stop_world() to skip thread iteration when count is 0");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[GCStopWorld] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        LOG_INFO("[GCStopWorld] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(GCStopWorldPatch, {
    .displayName = "GC_stop_world() Optimization",
    .description = "Optimizes GC_stop_world() by skipping unnecessary thread iteration when count is 0",
    .category = "Performance",
    .experimental = true,
    .technicalDetails = {
        "Patches GC_stop_world() at 0x00E511F5",
        "Replaces 'CMP EAX, 0x100' check with 'TEST EAX, EAX'",
        "Adds early exit (JZ) when thread count is 0",
        "Skips EnterCriticalSection/thread iteration overhead for empty slot 0",
        "Minor speedup/overhead reduction, should have no downsides since in testing 0x011f4124 is always 0"
    }
})
