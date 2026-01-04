#include "addresses.h"

const std::array<GameAddresses, gameVersionCount> gameAddressesByGameVersion = []() {
    std::array<GameAddresses, gameVersionCount> a;
    a[GameVersion::Retail_1_67_2_024002] = addressesForRetail_1_67_2_024002;
    a[GameVersion::Steam_1_67_2_024037] = addressesForSteam_1_67_2_024037;
    a[GameVersion::EA_1_69_47_024017] = addressesForEA_1_69_47_024017;
    return a;
}();

const GameAddresses* gameAddresses;

