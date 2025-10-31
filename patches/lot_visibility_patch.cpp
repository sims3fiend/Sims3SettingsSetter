#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

// Optimized lot streaming settings patch - improves performance and reduces stuttering
class LotVisibilityPatch : public OptimizationPatch {
private:
    static const uintptr_t VISIBILITY_CONDITION_ADDRESS = 0x00c63015;
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    LotVisibilityPatch() : OptimizationPatch("LotVisibility", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotVisibilityPatch] Installing...");

        // Validate the visibility condition address is accessible before patching
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)VISIBILITY_CONDITION_ADDRESS, &mbi, sizeof(mbi)) == 0) {
            return Fail("Target address 0x" + std::to_string(VISIBILITY_CONDITION_ADDRESS) + " is not accessible");
        }

        if (mbi.State != MEM_COMMIT) {
            return Fail("Target address 0x" + std::to_string(VISIBILITY_CONDITION_ADDRESS) + " is not committed memory");
        }

        // 1. Change JZ to JMP (0x74 -> 0xEB) - prevents distance-based lot loading
        BYTE expectedOld = 0x74;
        if (!PatchHelper::WriteByte(VISIBILITY_CONDITION_ADDRESS, 0xEB, &patchedLocations, &expectedOld)) {
            return Fail("Failed to patch visibility condition at 0x" + std::to_string(VISIBILITY_CONDITION_ADDRESS));
        }
        LOG_INFO("[LotVisibilityPatch] + Visibility condition patched (JZ -> JMP)");

        // 2. Enable "Throttle Lot LoD Transitions" setting
        if (!LiveSetting::Exists(L"Throttle Lot LoD Transitions")) {
            LOG_WARNING("[LotVisibilityPatch] 'Throttle Lot LoD Transitions' setting not found - may not be initialized yet");
        } else {
            if (!LiveSetting::Patch(L"Throttle Lot LoD Transitions", true, &patchedLocations)) {
                return Fail("Failed to enable 'Throttle Lot LoD Transitions'");
            }
            LOG_INFO("[LotVisibilityPatch] + Throttle Lot LoD Transitions enabled");
        }

        // 3. Set "Camera speed threshold" to 5.0 for better streaming behavior
        if (!LiveSetting::Exists(L"Camera speed threshold")) {
            LOG_WARNING("[LotVisibilityPatch] 'Camera speed threshold' setting not found - may not be initialized yet");
        } else {
            if (!LiveSetting::Patch(L"Camera speed threshold", 5.0f, &patchedLocations)) {
                return Fail("Failed to set 'Camera speed threshold'");
            }
            LOG_INFO("[LotVisibilityPatch] + Camera speed threshold set to 5.0");
        }

        isEnabled = true;
        LOG_INFO("[LotVisibilityPatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotVisibilityPatch] Uninstalling...");

        // RestoreAll automatically restores all tracked patches
        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original values");
        }

        isEnabled = false;
        LOG_INFO("[LotVisibilityPatch] Successfully uninstalled");
        return true;
    }
};

// Auto-register the patch
REGISTER_PATCH(LotVisibilityPatch, {
    .displayName = "Optimized Lot Streaming",
    .description = "Optimizes lot streaming behavior by disabling view-based loading, enabling throttle LoD transitions, and adjusting camera speed threshold debug vars",
    .category = "Performance",
    .experimental = false,
    .targetVersion = GameVersion::Steam,
    .technicalDetails = {
        "Modifies a conditional jump at 0x00c63015 (JZ -> JMP)",
        "Enables 'Throttle Lot LoD Transitions' setting",
        "Sets 'Camera speed threshold' to 5.0 (lower = more time between stopping camera movement and lot loading)",
        "Dramatically reduces stutter when playing with high lot counts"
    }
})
