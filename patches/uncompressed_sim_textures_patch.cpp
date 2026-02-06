#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../settings.h"
#include "../config/config_value_manager.h"

class UncompressedSimTexturesPatch : public OptimizationPatch {
private:
    // This is a branch within Sims3::CAS::ModelBuilder::SetBuildOptions
    // that sets an uncompressed surface-format for low-LOD sims.
    static inline const AddressInfo setBuildOptionsUncompressedBranchAddressInfo = {
        .name = "UncompressedSimTexturesPatch::setBuildOptionsUncompressedBranch",
        .addresses = {
            {GameVersion::Retail, 0x005c9099},
            {GameVersion::Steam,  0x005c8649},
            {GameVersion::EA,     0x005c99e9},
        },
        .pattern = "83 B9 A0 01 00 00 02 8A 44 24 0C 88 81 A4 01 00 00 73 21 8A 54 24 08 8A 44 24 10 F6 DA 88 81 A5 01 00 00 1B D2 83 E2 CD",
    };

    static constexpr uintptr_t offsetOfLODLevelThreshold = 6;
    static constexpr uintptr_t offsetOfConditionalSurfaceFormatDeltaMask = 39;

    bool onlyForLOD0;
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    bool WillHaveAnyEffect() const {
        if (!onlyForLOD0) {
            return true;
        }

        const auto& config = ConfigValueManager::Get().GetConfigValues();
        const auto minSimLODEntry = config.find(L"MinSimLOD");

        if (minSimLODEntry == config.end()) {
            return true;
        }

        const std::wstring& minSimLOD = minSimLODEntry->second.currentValue;

        return *minSimLOD.data() == '0';
    }
public:
    UncompressedSimTexturesPatch() : OptimizationPatch("UncompressedSimTexturesPatch", nullptr) {
        RegisterBoolSetting(&onlyForLOD0, "onlyForLOD0", true,
                            "When enabled, uncompressed textures will take effect only for sims using the lowest (most detailed) LOD (which is LOD0), "
                            "which are sims close to the camera when the \"Sim Detail\" setting is \"Very High\".\n"
                            "This will reduce VRAM usage somewhat.\n"
                            "When disabled, uncompressed textures will take effect for LOD1 sims also, regardless of the \"Sim Detail\" setting.");
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[UncompressedSimTexturesPatch] Installing...");
        LOG_INFO(std::format("[UncompressedSimTexturesPatch] onlyForLOD0: {}", onlyForLOD0));

        auto setBuildOptionsUncompressedBranchAddress = setBuildOptionsUncompressedBranchAddressInfo.Resolve();

        if (!setBuildOptionsUncompressedBranchAddress) {
            return Fail("Could not resolve setBuildOptionsUncompressedBranch address");
        }

        uintptr_t base = *setBuildOptionsUncompressedBranchAddress;

        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();

        if (onlyForLOD0) {
            // Change a `cmp dword ptr [ecx + 0x1a0], 2` to `cmp dword ptr [ecx + 0x1a0], 1`.
            // Such that the subsequent check becomes `if (lodLevel < 1) {/* Use uncompressed textures. */}`
            successful &= PatchHelper::WriteByte(base + offsetOfLODLevelThreshold, 1, &tx.locations);
        }

        // Turn an `and edx, -51` into `and edx, 0` so that kSurfaceFormat_A8R8G8B8
        // is used regardless of what the function's third parameter was supplied as.
        successful &= PatchHelper::WriteByte(base + offsetOfConditionalSurfaceFormatDeltaMask, 0, &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

        if (!WillHaveAnyEffect()) {
            lastError = "This patch will have no effect because \"onlyForLOD0\" is checked, but \"MinSimLOD\" is not 0.\n"
                        "Either set the game's \"Sim Detail\" setting to \"Very High\", or uncheck \"onlyForLOD0\", for this patch to take effect.";
        }

        isEnabled = true;
        LOG_INFO("[UncompressedSimTexturesPatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[UncompressedSimTexturesPatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            isEnabled = true;
            return Fail("Failed to restore original code");
        }

        isEnabled = false;
        LOG_INFO("[UncompressedSimTexturesPatch] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(UncompressedSimTexturesPatch, {
    .displayName = "Uncompressed Sim Textures",
    .description = "Forces textures for sims to be uncompressed during gameplay, like they are in CAS.\n"
                   "It is not recommended to use this patch unless you are also using DXVK, as otherwise the game may run out of memory or experience Error 12.\n"
                   "This improves the graphical fidelity of sims by avoiding lossy compression and by preventing compression artefacts.\n"
                   "However, this also makes sims' textures 4 times larger in memory, so be mindful of VRAM usage.\n"
                   "Specifically, when the \"Sim Detail\" setting is \"Very High\", sim textures are 2048x2048 in size: an uncompressed 2048x2048 texture occupies 16MB of VRAM, whereas a compressed 2048x2048 texture would occupy only 4MB of VRAM.\n"
                   "When using an HQ mod (a \"GraphicsRules.sgr\" edit), sim textures may be 4096x4096 in size: an uncompressed 4096x4096 texture occupies 64MB of VRAM, whereas a compressed 4096x4096 texture occupies 16MB of VRAM.\n\n\n"
                   "After enabling/disabling this patch, a sim's textures will not change until: their outfit is changed; or they are edited in CAS; or the game's \"simCompositorCache.package\" cache is cleared (or the \"<World>_sims.package\" world-cache for custom worlds).\n\n\n"
                   "If you experience crashes when this patch is enabled, try reducing the value set for the \"d3d9.textureMemory\" setting in the \"dxvk.conf\" file.",
    .category = "Graphics",
    .experimental = false,
    .supportedVersions = VERSION_ALL,
    .technicalDetails = {
        "Typically, sims' textures use the D3DFMT_DXT5 surface-format during gameplay. This patch allows them to use D3DFMT_A8R8G8B8, like in CAS.",
        "The reason why DXVK is borderline-required to use this patch is because native D3D9 stores textures in both VRAM and virtual memory,\n"
        "which means the larger uncompressed textures may quickly fill the game's 4GB address-space.\n"
        "This is not an issue with DXVK as it stores only a small portion of textures in virtual memory.",
    }
})
