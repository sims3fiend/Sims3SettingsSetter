#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class UncompressedLotTexturesPatch : public OptimizationPatch {
  private:
    // This is in CompositedTextureMediator::CreateCompositionDestTexture.
    // This is the format calculation that picks DXT1 (enum 2) or DXT5 (enum 10) based on whether the material has an alpha channel.
    // Replace with a constant enum 0x3E (D3DFMT_A8R8G8B8) to force uncompressed textures.
    // This ironically matches the Xbox 360 build, which seems have never had DXT compression for compositor textures, lol lmao
    static inline const AddressInfo formatCalculationAddressInfo = {
        .name = "UncompressedLotTexturesPatch::formatCalculation",
        .addresses =
            {
                {GameVersion::Steam, 0x006cc1ca},
            },
        .pattern = "0F B6 85 84 00 00 00 83 E0 01 03 C0 68 ?? ?? ?? ?? 03 C0 6A 02 03 C0 83 C8 02",
        .expectedBytes = {0x0F, 0xB6, 0x85, 0x84, 0x00, 0x00, 0x00},
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    UncompressedLotTexturesPatch() : OptimizationPatch("UncompressedLotTexturesPatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[UncompressedLotTexturesPatch] Installing...");

        auto addr = formatCalculationAddressInfo.Resolve();
        if (!addr) { return Fail("Could not resolve formatCalculation address"); }

        uintptr_t base = *addr;
        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();

        // Replace MOVZX EAX, byte ptr [EBP+0x84] (7 bytes) with MOV EAX, 0x3E (5 bytes) + 2 NOPs
        successful &= PatchHelper::WriteBytes(base, {0xB8, 0x3E, 0x00, 0x00, 0x00, 0x90, 0x90}, &tx.locations);

        // NOP out AND EAX, 0x1 (3 bytes)
        successful &= PatchHelper::WriteNOP(base + 7, 3, &tx.locations);

        // NOP out first ADD EAX, EAX (2 bytes)
        successful &= PatchHelper::WriteNOP(base + 10, 2, &tx.locations);

        // offset 12-16: PUSH <allocator name>, keep :)

        // NOP out second ADD EAX, EAX (2 bytes)
        successful &= PatchHelper::WriteNOP(base + 17, 2, &tx.locations);

        // offset 19-20: PUSH 0x2, keep

        // NOP out third ADD EAX, EAX (2 bytes)
        successful &= PatchHelper::WriteNOP(base + 21, 2, &tx.locations);

        // NOP out OR EAX, 0x2 (3 bytes)
        successful &= PatchHelper::WriteNOP(base + 23, 3, &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

        isEnabled = true;
        LOG_INFO(std::format("[UncompressedLotTexturesPatch] Installed at {:#010x}", base));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();
        LOG_INFO("[UncompressedLotTexturesPatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            isEnabled = true;
            return Fail("Failed to restore original code");
        }

        isEnabled = false;
        LOG_INFO("[UncompressedLotTexturesPatch] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(UncompressedLotTexturesPatch, {.displayName = "Uncompressed Compositor Textures",
                                                 .description = "Forces material compositor textures to use uncompressed A8R8G8B8 instead of DXT1/DXT5.\n"
                                                                "It's not recommended to use this patch unless you are also using DXVK, as otherwise the game may run out of memory.\n"
                                                                "After enabling/disabling this patch, textures will not change until: a lot is reloaded; or the game's "
                                                                "\"compositorCache.package\" cache is cleared (and the \"<World>_objects.package\" world-cache for custom worlds).",
                                                 .category = "Graphics",
                                                 .experimental = true,
                                                 .supportedVersions = VERSION_ALL,
                                                 .technicalDetails = {"Patches CompositedTextureMediator::CreateCompositionDestTexture to force surface format enum 0x3E (D3DFMT_A8R8G8B8) "
                                                                      "instead of computing enum 2 (DXT1) or 10 (DXT5) based on the material's alpha channel flag."}})
