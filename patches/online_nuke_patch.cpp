#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

/* 
Names are just random bs I made up, grain of salt obvi
- DownloadStoreSimPointsBalance (0x0088eb40) - queries SimPoints balance via Crossroads(???) login token, void(void), unconditional
- DownloadStoreSets (0x0088ea40) - downloads store set listings XML, void(void), checks DAT_011d9868
- DownloadFSIParams (0x0088e930) - downloads Featured Store Items parameters XML, void(void), checks DAT_011d9860, might be a better target for the patch
- StoreFeaturedWorlds (0x0088eac0) - downloads featured worlds/main menu data, void(void), checks DAT_011d986c

All of the above are void __cdecl(void) and can be patched with a single-byte RET.
Theese take parameters and are triggered by user actions in the store UI:
- StartLoadThumbnail (0x0087e550) - per-item thumbnail download, thiscall w/ params
- DownloadPngJob (0x00890250) - PNG image download, has params
- IgsPurchaseJob (0x008901d0) - store purchase, has params 

I'll probably make these all toggleable individually at some stage, but I'd like to address UI bloat before then, this covers the original request as is though I think
*/

class StoreFeaturedItemsDisablePatch : public OptimizationPatch {
  private:
    // Downloads the featured items listing from EA's (defunct) servers.
    // Populates the FeaturedItems folder with thumbnails.
    // Checks DAT_011d9864 state before dispatching.
    static inline const AddressInfo downloadStoreFeaturedItems = {
        .name = "StoreFeaturedItemsDisable::DownloadStoreFeaturedItems",
        .pattern = "51 56 57 E8 ?? ?? ?? ?? 84 C0 74 ?? A1 ?? ?? ?? ?? 85 C0 74 05 83 C0 E4 75",
        .expectedBytes = {0x51, 0x56, 0x57},
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    StoreFeaturedItemsDisablePatch() : OptimizationPatch("StoreFeaturedItemsDisablePatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[StoreFeaturedItemsDisable] Installing...");

        auto addr = downloadStoreFeaturedItems.Resolve();
        if (!addr) { return Fail("Could not resolve DownloadStoreFeaturedItems address"); }

        auto tx = PatchHelper::BeginTransaction();

        // RET (0xC3) at the start of the function - void __cdecl(void), no stack cleanup needed
        if (!PatchHelper::WriteByte(*addr, 0xC3, &tx.locations)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to write RET byte");
        }

        if (!PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to commit transaction");
        }

        patchedLocations = tx.locations;
        isEnabled = true;
        LOG_INFO(std::format("[StoreFeaturedItemsDisable] Patched DownloadStoreFeaturedItems at {:#010x}", *addr));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();
        LOG_INFO("[StoreFeaturedItemsDisable] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original code"); }

        isEnabled = false;
        LOG_INFO("[StoreFeaturedItemsDisable] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(StoreFeaturedItemsDisablePatch, {.displayName = "Disable Store Featured Items Download",
                                                   .description = "Blocks the game from downloading the featured store items listing, preventing thumbnail downloads into the FeaturedItems folder.",
                                                   .category = "Performance",
                                                   .experimental = true,
                                                   .supportedVersions = VERSION_ALL,
                                                   .technicalDetails = {
                                                       "Blocks DownloadStoreFeaturedItems init at 0x0088e9b0",
                                                   }})
