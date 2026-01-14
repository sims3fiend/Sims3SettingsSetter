#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class UncompressedSimTexturesPatch : public OptimizationPatch {
private:
    // This is Sims3::ModelBuilder::CreateOverlayTexture_Game, which creates a sim's texture during gameplay.
    static inline const AddressInfo createOverlayTexture_GameFunctionAddressInfo = {
        .name = "UncompressedSimTexturesPatch::createOverlayTexture_GameFunction",
        .addresses = {
            {GameVersion::Retail, 0x005d0420},
            {GameVersion::Steam,  0x005cfa90},
            {GameVersion::EA,     0x005d0c90},
        },
        .pattern = "83 EC 08 53 55 8B 6C 24 20 56 8B D9 8D 44 24 24 C1 E5 10 0B 6C 24 20 50 8D 4C 24 10 8D B3 8C 00 00 00 51 8B CE 89 6C 24 2C",
    };
    // Also of interest is Sims3::ModelBuilder::CreateOverlayTexture_CAS, which creates a sim's texture in CAS.
    // The two functions are very similar,
    // the main difference being that the CAS version always uses the D3DFMT_A8R8G8B8 surface format.

    static constexpr uintptr_t offsetOfMoveSurfaceFormatIntoECX = 205;
    // I don't know if this is what this boolean actually denotes,
    // but the CAS version of the function has it as 1, whereas the Game version has it as 0.
    static constexpr uintptr_t offsetOfPushIsUncompressedBoolean = 261;

    static constexpr uint8_t kSurfaceFormat_A8R8G8B8 = 0x3D;

    std::vector<PatchHelper::PatchLocation> patchedLocations;
public:
    UncompressedSimTexturesPatch() : OptimizationPatch("UncompressedSimTexturesPatch", nullptr) {
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[UncompressedSimTexturesPatch] Installing...");

        auto createOverlayTexture_GameFunctionAddress = createOverlayTexture_GameFunctionAddressInfo.Resolve();

        if (!createOverlayTexture_GameFunctionAddress) {
            return Fail("Could not resolve createOverlayTexture_GameFunction address");
        }

        uintptr_t base = *createOverlayTexture_GameFunctionAddress;

        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();

        uint8_t moveSurfaceFormatIntoECX[6] = {
            /* An inert DS prefix is used to pad the instruction to six bytes. */
            0x3E, 0xB9, kSurfaceFormat_A8R8G8B8, 0x00, 0x00, 0x00, // ds: mov ecx, kSurfaceFormat_A8R8G8B8
        };

        successful &= PatchHelper::WriteProtectedMemory(reinterpret_cast<uint8_t*>(base + offsetOfMoveSurfaceFormatIntoECX),
                                                        moveSurfaceFormatIntoECX, 6, &tx.locations);
        successful &= PatchHelper::WriteByte(base + offsetOfPushIsUncompressedBoolean + 1, true, &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

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
                   "For example, an uncompressed 4096x4096 texture occupies 64MB of VRAM, whereas a compressed 4096x4096 texture would occupy only 16MB of VRAM.",
    .category = "Graphics",
    .experimental = false,
    .supportedVersions = VERSION_ALL,
    .technicalDetails = {
        "Typically, sims' textures use the D3DFMT_DXT5 surface-format during gameplay. This patch forces them to use D3DFMT_A8R8G8B8, like in CAS.",
        "The reason why DXVK is borderline-required to use this patch is because native D3D9 stores textures in both VRAM and virtual memory,\n"
        "which means the larger uncompressed textures may quickly fill the game's 4GB address-space.\n"
        "This is not an issue with DXVK as it stores only a small portion of textures in virtual memory.",
        "This patch was authored by \"Just Harry\", with significant reverse-engineering assistance from sims3fiend.",
    }
})

