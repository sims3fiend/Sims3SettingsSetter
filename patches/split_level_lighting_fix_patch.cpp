#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <vector>

class SplitLevelLightingFixPatch : public OptimizationPatch {
  private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    // Forces Sims3::Renderer::BaseLight::GetLotID() to return 0. Two static-lighting bake callers route through the getter:
    // LightCellInfo::GatherLightsNearPoint (outdoor LightingRoom light gather) and TerrainLightmapBuilder::ProcessLight (static terrain lightmap bake)
    // Both treat lotID == 0 as "world light", so zeroing the getter lets lot-bound lights contribute across lot/level boundaries
    // Dynamic per-rig Sim/object lighting reads BaseLight::mRoomID directly and is unaffected
    static inline const AddressInfo getLotIdFunc = {
        .name = "BaseLight::GetLotID",
        .pattern = "8B 81 C0 00 00 00 8B 91 C4 00 00 00 C3",
        .patternOffset = 0,
        .expectedBytes = {0x8B, 0x81, 0xC0, 0x00, 0x00, 0x00, 0x8B, 0x91, 0xC4, 0x00, 0x00, 0x00, 0xC3},
    };

  public:
    SplitLevelLightingFixPatch() : OptimizationPatch("SplitLevelLightingFix", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[SplitLevelLightingFix] Installing...");

        auto addr = getLotIdFunc.Resolve();
        if (!addr) return Fail("Could not resolve BaseLight::GetLotID");
        LOG_INFO(std::format("[SplitLevelLightingFix] BaseLight::GetLotID at {:#010x}", *addr));

        // Overwrite the 13-byte body with: xor eax,eax; xor edx,edx; ret; nop*8.
        std::vector<BYTE> newBytes = {0x33, 0xC0, 0x33, 0xD2, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        std::vector<BYTE> oldBytes(getLotIdFunc.expectedBytes.begin(), getLotIdFunc.expectedBytes.end());

        auto tx = PatchHelper::BeginTransaction();
        bool ok = PatchHelper::WriteBytes(*addr, newBytes, &tx.locations, &oldBytes);

        if (!ok || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to patch BaseLight::GetLotID");
        }
        patchedLocations = tx.locations;

        isEnabled = true;
        LOG_INFO("[SplitLevelLightingFix] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[SplitLevelLightingFix] Uninstalling...");
        if (!PatchHelper::RestoreAll(patchedLocations)) return Fail("Failed to restore original bytes");

        isEnabled = false;
        LOG_INFO("[SplitLevelLightingFix] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(
    SplitLevelLightingFixPatch, {.displayName = "Split-Level Lighting Fix",
                                    .description = "Makes lighting not stop at a single level.",
                                    .category = "Graphics",
                                    .experimental = true,
                                    .supportedVersions = VERSION_ALL,
                                    .technicalDetails = {"Replaces the 13-byte body of BaseLight::GetLotID so lot-bound lights pass the 'world light' filter in\noutdoor lighting rooms and terrain lightmaps",
                                        "Impacts LightCellInfo::GatherLightsNearPoint (outdoor-room gather) and TerrainLightmapBuilder::ProcessLight (terrain bake)\nboth treat lotID == 0 as 'world light'",
                                        "Reload the lot or restart the game after toggling to see the change", "May cause some light near walls/floors to bleed through", "All credits to Arro on Discord / Tumblr for the patch!"}})
