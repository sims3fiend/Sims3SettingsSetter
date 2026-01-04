#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../addresses.h"
#include "../logger.h"

class WorldCacheSizePatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    WorldCacheSizePatch() : OptimizationPatch("WorldCacheSizePatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();

        LOG_INFO("[WorldCacheSizePatch] Installing...");

        uintptr_t branch = gameAddresses->worldCacheSizePatch;

        // `branch`: JC +0x018f -> Force jump to success path + nop
        // This bypasses the cache size check xoxo. I had another version of this but lost the file tehehe~ Hope this works!

        std::vector<BYTE> newBytes = {
            0xE9, 0x90, 0x01, 0x00, 0x00,
            0x90
        };

        std::vector<BYTE> expectedOld = {
            0x0F, 0x82, 0x8F, 0x01, 0x00, 0x00  // JC instruction
        };

        if (!PatchHelper::WriteBytes(branch, newBytes, &patchedLocations, &expectedOld)) {
            return Fail(std::format("Failed to patch WorldCache size check at {:#010x}", branch));
        }

        isEnabled = true;
        LOG_INFO("[WorldCacheSizePatch] Successfully installed - WorldCache size limit removed");
        //and this, is to go to go even further beyond!!! AAAAAAAAAA <game crashes>
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();

        LOG_INFO("[WorldCacheSizePatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        LOG_INFO("[WorldCacheSizePatch] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(WorldCacheSizePatch, {
    .displayName = "WorldCache Size Uncap",
    .description = "Removes the 512MB limit on WorldCache files, allowing larger caches",
    .category = "Performance",
    .experimental = true,
    .technicalDetails = {
        "Patches the cache size check at 0x005bc8b4",
        "Allows WorldCache files (Documents\\EA\\The Sims 3\\WorldCaches) to grow beyond 512mb",
        "This hopefully removes some of the lag assosiated with constantly adding and removing object from the caches",
        "This removes the limit entirely from the entirely, which may break past 2gb. If this happens please let me know!",
        "No effect on stock EA worlds which don't use WorldCache, unless..." //yohoho
    }
})
