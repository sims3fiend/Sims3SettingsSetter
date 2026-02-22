#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class GCTryToCollectPatch : public OptimizationPatch {
  private:
    // In MonoScriptHost::Simulate, the CALL to GC_try_to_collect
    // PUSH callback; MOV [elapsed], EAX; CALL GC_try_to_collect; MOV EAX, [global]; ADD ESP, 4
    // Return value in EAX is immediately overwritten so NOPing the CALL is safe
    // PUSH before + ADD ESP,4 after are untouched
    static inline const AddressInfo callSite = {
        .name = "GCTryToCollect::callSite",
        .addresses =
            {
                {GameVersion::Steam, 0x00d819aa},
            },
        .pattern = "68 ?? ?? ?? ?? A3 ?? ?? ?? ?? E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 83 C4 04",
        .patternOffset = 10,
        .expectedBytes = {0xE8},
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    GCTryToCollectPatch() : OptimizationPatch("GCTryToCollect", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[GCTryToCollect] Installing...");

        auto addr = callSite.Resolve();
        if (!addr) { return Fail("Could not resolve GC_try_to_collect call site"); }

        // NOP the 5-byte CALL instruction (E8 xx xx xx xx -> 90 90 90 90 90)
        if (!PatchHelper::WriteNOP(*addr, 5, &patchedLocations)) { return Fail(std::format("Failed to NOP call at {:#010x}", *addr)); }

        isEnabled = true;
        LOG_INFO(std::format("[GCTryToCollect] Installed at {:#010x}", *addr));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[GCTryToCollect] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original bytes"); }

        isEnabled = false;
        LOG_INFO("[GCTryToCollect] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(GCTryToCollectPatch, {.displayName = "Chunky Patch - Disable GC_try_to_collect()",
                                        .description = "Removes explicit garbage collection from the simulation loop, relying on GC_malloc to trigger collection instead",
                                        .category = "Performance",
                                        .experimental = true,
                                        .supportedVersions = VERSION_ALL,
                                        .technicalDetails = {"NOPs the CALL to GC_try_to_collect in MonoScriptHost::Simulate", "GC_try_to_collect dominates simulation thread time so we just don't",
                                            "Collection should still occur naturally via GC_malloc when memory pressure requires it"}})
