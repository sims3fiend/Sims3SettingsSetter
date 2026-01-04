#include "patch_system.h"

const std::array<const char*, gameVersionCount> gameVersionNames = []() {
    std::array<const char*, gameVersionCount> a;
    a[GameVersion::Retail_1_67_2_024002] = "Retail 1.67.2.024002";
    a[GameVersion::Steam_1_67_2_024037] = "Steam 1.67.2.024037";
    a[GameVersion::EA_1_69_47_024017] = "EA 1.69.47.024017";
    return a;
}();

GameVersion gameVersion = {};

