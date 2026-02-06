#pragma once
#include "optimization.h"
#include "patch_helpers.h"

class IntersectionPatch : public OptimizationPatch {
  public:
    IntersectionPatch();
    bool Install() override;
    bool Uninstall() override;

  private:
    static inline const AddressInfo intersectionAddr = {
        .name = "IntersectionPatch::hookAddr",
        .addresses =
            {
                {GameVersion::Retail, 0x0067b130},
                {GameVersion::Steam, 0x0067afb0},
                {GameVersion::EA, 0x0067c380},
            },
        .pattern = "55 8B EC 83 E4 F0 8B 45 08 8B 08 8B 40 04 2B C1 83 EC 3C C1 F8 03 83 F8 03 56 0F 86",
        .expectedBytes = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0},
    };

    static bool __cdecl OptimizedHook();
    static IntersectionPatch* instance;
};
