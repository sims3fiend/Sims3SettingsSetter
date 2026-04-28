#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <cstring>

class MirrorSettingsPatch : public OptimizationPatch {
  private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    float baseDistance = 10.0f;
    float distanceScale = 15.0f;

    static inline const AddressInfo distanceScaleRef = {
        .name = "MirrorSettings::distanceScaleRef",
        .pattern = "F3 0F 10 05 ?? ?? ?? ?? 0F C6 C0 00 0F 59 D8 0F 5C D9",
        .patternOffset = 4,  // skip MOVSS opcode bytes, land on the address operand
        .expectedBytes = {}, // address bytes will vary, no fixed validation
    };

  public:
    MirrorSettingsPatch() : OptimizationPatch("MirrorSettings", nullptr) {
        RegisterFloatSetting(&baseDistance, "baseDistance", SettingUIType::Slider, 10.0f, 1.0f, 200.0f,
            "Base fade distance for mirror reflections (default: 10.0).\n"
            "Larger values keep reflections visible further from the camera.\n"
            "Combined with distanceScale: visibleRange = mirrorSize * distanceScale + baseDistance");

        RegisterFloatSetting(&distanceScale, "distanceScale", SettingUIType::Slider, 15.0f, 0.0f, 200.0f,
            "Distance scale based on mirror size (default: 15.0).\n"
            "Larger mirrors are visible from further away by this multiplier.\n"
            "Increase to make all mirrors render reflections at greater distances.");
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[MirrorSettings] Installing...");

        auto distScaleRefAddr = distanceScaleRef.Resolve();
        if (!distScaleRefAddr) return Fail("Could not resolve mirror distance scale reference");

        // Read the 4-byte address operand from the MOVSS instruction
        uintptr_t distanceScaleAddr = PatchHelper::ReadDWORD(*distScaleRefAddr);
        uintptr_t baseDistanceAddr = distanceScaleAddr - 4; // adjacent in memory

        // Sanity check the resolved addresses against original float values
        float origDistScale = *reinterpret_cast<float*>(distanceScaleAddr);
        float origBaseDist = *reinterpret_cast<float*>(baseDistanceAddr);

        LOG_INFO(std::format("[MirrorSettings] sfMirrorBaseDistance  at {:#010x} = {}", baseDistanceAddr, origBaseDist));
        LOG_INFO(std::format("[MirrorSettings] sfMirrorDistanceScale at {:#010x} = {}", distanceScaleAddr, origDistScale));

        // Validate that the values look reasonable (avoids patching garbage)
        if (origBaseDist < 0.001f || origBaseDist > 1000.0f || origDistScale < 0.001f || origDistScale > 1000.0f) {
            return Fail(std::format("Resolved addresses contain unexpected values: base={}, scale={}", origBaseDist, origDistScale));
        }

        auto tx = PatchHelper::BeginTransaction();
        bool ok = true;

        DWORD newBase, newScale;
        std::memcpy(&newBase, &baseDistance, 4);
        std::memcpy(&newScale, &distanceScale, 4);

        DWORD oldBase, oldScale;
        std::memcpy(&oldBase, &origBaseDist, 4);
        std::memcpy(&oldScale, &origDistScale, 4);

        ok &= PatchHelper::WriteDWORD(baseDistanceAddr, newBase, &tx.locations, &oldBase);
        ok &= PatchHelper::WriteDWORD(distanceScaleAddr, newScale, &tx.locations, &oldScale);

        if (!ok || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to write mirror setting values");
        }
        patchedLocations = tx.locations;

        LOG_INFO(std::format("[MirrorSettings] Applied: base={}, scale={}", baseDistance, distanceScale));

        isEnabled = true;
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[MirrorSettings] Uninstalling...");
        if (!PatchHelper::RestoreAll(patchedLocations)) return Fail("Failed to restore original values");

        isEnabled = false;
        return true;
    }
};

REGISTER_PATCH(MirrorSettingsPatch,
    {.displayName = "Mirror Reflection Settings",
        .description = "Tune mirror reflection visibility distance\n"
                       "Will decrease performance the more mirrors there are, as each is a separate camera",
        .category = "Graphics",
        .experimental = false,
        .supportedVersions = VERSION_ALL,
        .technicalDetails = {"Patches two floats in .data: sfMirrorBaseDistance and sfMirrorDistanceScale", "Fade formula: clamp01((mirrorSize * distanceScale - distance + baseDistance) / baseDistance)",
            "Increases visible range of mirror reflections at the cost of more frequent reflection renders"}})
