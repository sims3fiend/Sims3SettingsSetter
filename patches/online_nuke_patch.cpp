#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class OnlineDisablePatch : public OptimizationPatch {
  private:
    // Online SIGS/FolderManager job init (checks DAT_011d9860 state, allocates job), very hacky and probably doesn't cover what it needs to, might redo for just the folder thing idk
    static inline const AddressInfo onlineJobInit1 = {
        .name = "OnlineDisable::JobInit1",
        .pattern = "51 56 E8 ?? ?? ?? ?? 84 C0 74 ?? A1 ?? ?? ?? ?? 85 C0 74 05 83 C0 E4 75",
        .expectedBytes = {0x51, 0x56},
    };

    // Online SIGS/FolderManager job submission (unconditional alloc + dispatch)
    static inline const AddressInfo onlineJobInit2 = {
        .name = "OnlineDisable::JobInit2",
        .pattern = "51 56 57 6A 00 6A 00 6A 00 6A 00 68 ?? ?? ?? ?? 6A 20 E8",
        .expectedBytes = {0x51, 0x56, 0x57},
    };

    // Online SIGS/FolderManager job submission (checks DAT_011d986c state)
    static inline const AddressInfo onlineJobInit3 = {
        .name = "OnlineDisable::JobInit3",
        .pattern = "51 A1 ?? ?? ?? ?? 85 C0 56 57 74 05 83 C0 E4 75",
        .expectedBytes = {0x51, 0xA1},
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    OnlineDisablePatch() : OptimizationPatch("OnlineDisablePatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[OnlineDisable] Installing...");

        auto addr1 = onlineJobInit1.Resolve();
        auto addr2 = onlineJobInit2.Resolve();
        auto addr3 = onlineJobInit3.Resolve();

        if (!addr1) { return Fail("Could not resolve JobInit1 address"); }
        if (!addr2) { return Fail("Could not resolve JobInit2 address"); }
        if (!addr3) { return Fail("Could not resolve JobInit3 address"); }

        auto tx = PatchHelper::BeginTransaction();
        bool success = true;

        // RET (0xC3) at the start of each function - all are void __cdecl(void), no stack cleanup needed
        success &= PatchHelper::WriteByte(*addr1, 0xC3, &tx.locations);
        success &= PatchHelper::WriteByte(*addr2, 0xC3, &tx.locations);
        success &= PatchHelper::WriteByte(*addr3, 0xC3, &tx.locations);

        if (!success || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to write RET bytes");
        }

        patchedLocations = tx.locations;
        isEnabled = true;
        LOG_INFO(std::format("[OnlineDisable] Patched 3 online job functions at {:#010x}, {:#010x}, {:#010x}", *addr1, *addr2, *addr3));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();
        LOG_INFO("[OnlineDisable] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original code"); }

        isEnabled = false;
        LOG_INFO("[OnlineDisable] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(OnlineDisablePatch, {.displayName = "Disable Online Features",
                                       .description = "Prevents the game from initializing online service jobs (SIGS/FolderManager) entirely.",
                                       .category = "Performance",
                                       .experimental = true,
                                       .supportedVersions = VERSION_ALL,
                                       .technicalDetails = {
                                           "Patches 3 online job initialization functions to return immediately (RET)",
                                           "All functions are void __cdecl(void) so single-byte RET is safe",
                                       }})
