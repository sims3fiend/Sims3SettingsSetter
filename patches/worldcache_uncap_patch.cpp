#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class WorldCacheSizePatch : public OptimizationPatch {
  private:
    static inline const AddressInfo worldCacheSizeCheck = {
        .name = "WorldCache::sizeCheck",
        .addresses =
            {
                {GameVersion::Retail, 0x005bd114},
                //{GameVersion::Steam,  0x005bc8b4},
                {GameVersion::EA, 0x005bd704},
            },
        .pattern = "0F 82 ?? ?? ?? ?? 8B 0F E8",
        .expectedBytes = {0x0F, 0x82}, // JC instruction prefix
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    WorldCacheSizePatch() : OptimizationPatch("WorldCacheSizePatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();

        LOG_INFO("[WorldCacheSizePatch] Installing...");

        auto addr = worldCacheSizeCheck.Resolve();
        if (!addr) { return Fail("Could not resolve WorldCache size check address"); }

        // JC -> JMP (force jump to success path) + nop
        // This bypasses the cache size check xoxo. I had another version of this but lost the file tehehe~ Hope this works!

        std::vector<BYTE> newBytes = {0xE9, 0x90, 0x01, 0x00, 0x00, 0x90};

        std::vector<BYTE> expectedOld = {
            0x0F, 0x82, 0x8F, 0x01, 0x00, 0x00 // JC instruction
        };

        if (!PatchHelper::WriteBytes(*addr, newBytes, &patchedLocations, &expectedOld)) { return Fail(std::format("Failed to patch WorldCache size check at {:#010x}", *addr)); }

        isEnabled = true;
        LOG_INFO("[WorldCacheSizePatch] Successfully installed - WorldCache size limit removed");
        //and this, is to go to go even further beyond!!! AAAAAAAAAA <game crashes>
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();

        LOG_INFO("[WorldCacheSizePatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original bytes"); }

        isEnabled = false;
        LOG_INFO("[WorldCacheSizePatch] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(WorldCacheSizePatch, {.displayName = "WorldCache Size Uncap",
                                        .description = "Removes the 512MB limit on WorldCache files, allowing larger caches",
                                        .category = "Performance",
                                        .experimental = true,
                                        .supportedVersions = VERSION_ALL,
                                        .technicalDetails = {
                                            "Patches the cache size check (JC -> JMP)", "Allows WorldCache files (Documents\\EA\\The Sims 3\\WorldCaches) to grow beyond 512mb",
                                            "This hopefully removes some of the lag assosiated with constantly adding and removing object from the caches",
                                            "This removes the limit entirely from the entirely, which may break past 2gb. If this happens please let me know!",
                                            "No effect on stock EA worlds which don't use WorldCache, unless..." //yohoho
                                        }})
