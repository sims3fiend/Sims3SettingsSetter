#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

// Lot visibility camera patch - prevents distance-based lot loading
class LotVisibilityCameraPatch : public OptimizationPatch {
  private:
    static inline const AddressInfo visibilityCondition = {
        .name = "LotVisibilityCamera::visibilityCondition",
        .addresses =
            {
                {GameVersion::Retail, 0x00c62ae5},
                {GameVersion::Steam, 0x00c63015},
                {GameVersion::EA, 0x00c623a5},
            },
        .pattern = "74 ?? F3 0F 10 44 24 08 F3 0F 5C 87 E0 00 00 00 F3 0F 11 44 24 08 D9 44 24 08 5F 5E 8B E5 5D C2 0C 00",
        .expectedBytes = {0x74}, // JZ instruction
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    LotVisibilityCameraPatch() : OptimizationPatch("LotVisibilityCamera", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotVisibilityCameraPatch] Installing...");

        auto addr = visibilityCondition.Resolve();
        if (!addr) { return Fail("Could not resolve visibility condition address"); }

        // Change JZ to JMP (0x74 -> 0xEB) - prevents distance-based lot loading
        BYTE expectedOld = 0x74;
        if (!PatchHelper::WriteByte(*addr, 0xEB, &patchedLocations, &expectedOld)) { return Fail(std::format("Failed to patch visibility condition at {:#010x}", *addr)); }
        LOG_INFO("[LotVisibilityCameraPatch] + Visibility condition patched (JZ -> JMP)");

        isEnabled = true;
        LOG_INFO("[LotVisibilityCameraPatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotVisibilityCameraPatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original values"); }

        isEnabled = false;
        LOG_INFO("[LotVisibilityCameraPatch] Successfully uninstalled");
        return true;
    }
};

// Auto-register the patch
REGISTER_PATCH(LotVisibilityCameraPatch, {.displayName = "Lot Visibility Camera Override",
                                             .description = "Disables view-based lot loading by patching the camera visibility check.",
                                             .category = "Performance",
                                             .experimental = false,
                                             .supportedVersions = VERSION_ALL,
                                             .technicalDetails = {"Modifies a conditional jump (JZ -> JMP)", "Prevents lot loading/unloading based on camera view"}})
