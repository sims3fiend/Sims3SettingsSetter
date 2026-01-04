#pragma once
#include <array>
#include "patch_system.h"

struct GameAddresses {
};

constexpr GameAddresses addressesForSteam_1_67_2_024037 = {
};

constexpr GameAddresses addressesForRetail_1_67_2_024002 = {
};

constexpr GameAddresses addressesForEA_1_69_47_024017 = {
};

extern const std::array<GameAddresses, gameVersionCount> gameAddressesByGameVersion;

extern const GameAddresses* gameAddresses;

