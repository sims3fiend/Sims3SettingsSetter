#pragma once
#include <array>
#include "patch_system.h"

struct GameAddresses {
    uintptr_t configRetrieval; // Pattern: 83 EC 2C 8B 44 24 ?? 53 55 56 57 33 DB 8B F1 BF ?? ?? ?? ?? 50 8D 4C 24 ?? 89 5C 24 ?? 89 5C 24 ?? 89 5C 24 ?? 89 7C 24 ??
};

constexpr GameAddresses addressesForSteam_1_67_2_024037 = {
    .configRetrieval = 0x0058c380,
};

constexpr GameAddresses addressesForRetail_1_67_2_024002 = {
    .configRetrieval = 0x0058c5e0,
};

constexpr GameAddresses addressesForEA_1_69_47_024017 = {
    .configRetrieval = 0x0058b990,
};

extern const std::array<GameAddresses, gameVersionCount> gameAddressesByGameVersion;

extern const GameAddresses* gameAddresses;

