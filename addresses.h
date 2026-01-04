#pragma once
#include <array>
#include "patch_system.h"

struct GameAddresses {
    uintptr_t configRetrieval; // Pattern: 83 EC 2C 8B 44 24 ?? 53 55 56 57 33 DB 8B F1 BF ?? ?? ?? ?? 50 8D 4C 24 ?? 89 5C 24 ?? 89 5C 24 ?? 89 5C 24 ?? 89 7C 24 ??
    uintptr_t intersectionPatch;
    uintptr_t vtableBase; // Pattern: 56 8B F1 C7 06 ?? ?? ?? ?? 33 C9 89 4E 0C
                          // The address of the vtable is loaded into `[esi]`.
    uintptr_t createFileWFlagsInit;
    uintptr_t randomAccessFlagBranch;
};

constexpr GameAddresses addressesForSteam_1_67_2_024037 = {
    .configRetrieval = 0x0058c380,
    .intersectionPatch = 0x0067afb0,
    .vtableBase = 0x00fde0a0,
    .createFileWFlagsInit = 0x00404573,
    .randomAccessFlagBranch = 0x004db56c,
};

constexpr GameAddresses addressesForRetail_1_67_2_024002 = {
    .configRetrieval = 0x0058c5e0,
    .intersectionPatch = 0x0067b130,
    .vtableBase = 0x00fdf100,
    .createFileWFlagsInit = 0x00404563,
    .randomAccessFlagBranch = 0x004db73c,
};

constexpr GameAddresses addressesForEA_1_69_47_024017 = {
    .configRetrieval = 0x0058b990,
    .intersectionPatch = 0x0067c380,
    .vtableBase = 0x01029618,
    .createFileWFlagsInit = 0x00404563,
    .randomAccessFlagBranch = 0x004db51c,
};

extern const std::array<GameAddresses, gameVersionCount> gameAddressesByGameVersion;

extern const GameAddresses* gameAddresses;

