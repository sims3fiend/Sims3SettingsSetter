#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

// Lot visibility camera patch - prevents distance-based lot loading
// Steam only - modifies game memory
class LotVisibilityCameraPatch : public OptimizationPatch {
private:
    static const uintptr_t VISIBILITY_CONDITION_ADDRESS = 0x00c63015;
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    LotVisibilityCameraPatch() : OptimizationPatch("LotVisibilityCamera", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotVisibilityCameraPatch] Installing...");

        // Validate the visibility condition address is accessible before patching
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)VISIBILITY_CONDITION_ADDRESS, &mbi, sizeof(mbi)) == 0) {
            return Fail("Target address 0x" + std::to_string(VISIBILITY_CONDITION_ADDRESS) + " is not accessible");
        }

        if (mbi.State != MEM_COMMIT) {
            return Fail("Target address 0x" + std::to_string(VISIBILITY_CONDITION_ADDRESS) + " is not committed memory");
        }

        // Change JZ to JMP (0x74 -> 0xEB) - prevents distance-based lot loading
        BYTE expectedOld = 0x74;
        if (!PatchHelper::WriteByte(VISIBILITY_CONDITION_ADDRESS, 0xEB, &patchedLocations, &expectedOld)) {
            return Fail("Failed to patch visibility condition at 0x" + std::to_string(VISIBILITY_CONDITION_ADDRESS));
        }
        LOG_INFO("[LotVisibilityCameraPatch] + Visibility condition patched (JZ -> JMP)");

        isEnabled = true;
        LOG_INFO("[LotVisibilityCameraPatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotVisibilityCameraPatch] Uninstalling...");

        // RestoreAll automatically restores all tracked patches
        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original values");
        }

        isEnabled = false;
        LOG_INFO("[LotVisibilityCameraPatch] Successfully uninstalled");
        return true;
    }
};

// Auto-register the patch
REGISTER_PATCH(LotVisibilityCameraPatch, {
    .displayName = "Lot Visibility Camera Override",
    .description = "Disables view-based lot loading by patching the camera visibility check.",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = 1 << GameVersion::Steam_1_67_2_024037,
    .technicalDetails = {
        "Modifies a conditional jump at 0x00c63015 (JZ -> JMP)",
        "Prevents lot loading/unloading based on camera view"
    }
})
