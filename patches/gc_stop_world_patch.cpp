#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class GCStopWorldPatch : public OptimizationPatch {
  private:
    // Address definition: explicit version addresses + optional pattern for unknown versions
    static inline const AddressInfo threadLoopCheck = {
        .name = "GCStopWorld::threadLoopCheck",
        .addresses =
            {
                {GameVersion::Retail, 0x00e514e5},
                {GameVersion::Steam, 0x00e511f5},
                {GameVersion::EA, 0x00e51245},
            },
        .pattern = "3D 00 01 00 00 7C ?? B8 FF 00 00 00 3B F8 7F",
        .expectedBytes = {0x3D, 0x00, 0x01, 0x00, 0x00, 0x7C},
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    GCStopWorldPatch() : OptimizationPatch("GCStopWorld", nullptr) {} //literally who cares about this??? most useless patch omfg

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[GCStopWorld] Installing...");

        // Resolve address (pattern scan first, then fallback)
        auto addr = threadLoopCheck.Resolve();
        if (!addr) { return Fail("Could not resolve threadLoopCheck address"); }

        // Replace with:
        // 85 C0        ; TEST EAX, EAX
        // 74 7D        ; JZ 0x00E51276 (skip thread loop if count is 0)
        // 90 90 90     ; NOP padding
        std::vector<BYTE> newBytes = {0x85, 0xC0, 0x74, 0x7D, 0x90, 0x90, 0x90};

        if (!PatchHelper::WriteBytes(*addr, newBytes, &patchedLocations)) { return Fail(std::format("Failed to write patch at {:#010x}", *addr)); }

        isEnabled = true;
        LOG_INFO(std::format("[GCStopWorld] Installed at {:#010x}", *addr));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[GCStopWorld] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original bytes"); }

        isEnabled = false;
        LOG_INFO("[GCStopWorld] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(GCStopWorldPatch, {.displayName = "GC_stop_world() Optimization",
                                     .description = "Optimizes GC_stop_world() by skipping unnecessary thread iteration when count is 0",
                                     .category = "Performance",
                                     .experimental = true,
                                     .supportedVersions = VERSION_ALL,
                                     .technicalDetails = {
                                         "Replaces 'CMP EAX, 0x100' check with 'TEST EAX, EAX'",
                                         "Adds early exit (JZ) when thread count is 0",
                                         "Skips EnterCriticalSection/thread iteration overhead for empty slot 0",
                                     }})
