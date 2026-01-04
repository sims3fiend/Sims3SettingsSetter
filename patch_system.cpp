#include "patch_system.h"

const std::array<const char*, gameVersionCount> gameVersionNames = []() {
    std::array<const char*, gameVersionCount> a;
    a[GameVersion::Retail_1_67_2_024002] = "Retail 1.67.2.024002";
    a[GameVersion::Steam_1_67_2_024037] = "Steam 1.67.2.024037";
    a[GameVersion::EA_1_69_47_024017] = "EA 1.69.47.024017";
    return a;
}();

// These are the timestamps found in the `TimeDateStamp` field of the executables' PE file-headers.
const std::array<uint32_t, gameVersionCount> gameVersionTimestamps = []() {
    std::array<uint32_t, gameVersionCount> a;
    a[GameVersion::Retail_1_67_2_024002] = 0x52D872DA;
    a[GameVersion::Steam_1_67_2_024037] = 0x52DEC247;
    a[GameVersion::EA_1_69_47_024017] = 0x6707155C;
    return a;
}();

GameVersion gameVersion = {};

