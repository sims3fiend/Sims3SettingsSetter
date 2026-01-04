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
    uintptr_t gcThreadLoopCheck;
    uintptr_t lotVisibilityCameraBranch;
    uintptr_t specWorldManagerUpdate;
    uintptr_t cameraEnableMapViewMode; // Pattern: 68 89 DD 0F 11 FF D0 85 C0 74 11 D9 44 24 04 6A 00 51 8B C8 D9 1C 24
                                       // The address of the containing function is what's important.
    uintptr_t cameraDisableMapViewMode; // Pattern: 68 89 DD 0F 11 FF D0 85 C0 74 23 D9 44 24 0C 6A 00 83 EC 0C D9 5C 24 08 8B C8 D9 44 24 18 D9 5C 24 04 D9 44 24 14 D9 1C 24
                                        // The address of the containing function is what's important.
    uintptr_t refPackDecompressor;
    uintptr_t smoothPatchClassic; // Pattern: 8B 44 24 04 8B 08 6A 01 51 FF
    uintptr_t smoothPatchDupe; // Pattern: [80 7E 60 00 0F 57 C0 F3 0F 11 44 24 10] + 0x25

    union {
        struct {
            uintptr_t monoMethodCacheTimer; // Pattern: 83 C4 10 8B 45 08 8B 08 8B B1 D0 02 00 00 68 <address>
            uintptr_t browserTimer; // Pattern: 5F 5D 5B B0 01 5E 83 C4 60 C3 68 <address>
            uintptr_t unknownTimer; // Pattern: 83 C4 04 89 43 08 8B 4E 0C 89 4B 0C 89 5E 0C 68 <address>
        };
        // The order of these timers must correspond with the order of the timers in `TimerOptimizationPatch::CS_TARGETS`.
        uintptr_t criticalSectionTimers[3];
    };

    uintptr_t worldCacheSizePatch;
};

constexpr GameAddresses addressesForSteam_1_67_2_024037 = {
    .configRetrieval = 0x0058c380,
    .intersectionPatch = 0x0067afb0,
    .vtableBase = 0x00fde0a0,
    .createFileWFlagsInit = 0x00404573,
    .randomAccessFlagBranch = 0x004db56c,
    .gcThreadLoopCheck = 0x00e511f5,
    .lotVisibilityCameraBranch = 0x00c63015,
    .specWorldManagerUpdate = 0x00c6d570,
    .cameraEnableMapViewMode = 0x0073dfb0,
    .cameraDisableMapViewMode = 0x0073e000,
    .refPackDecompressor = 0x004eb3b0,
    .smoothPatchClassic = 0x004e1320,
    .smoothPatchDupe = 0x008b2aa4,
    .monoMethodCacheTimer = 0x011f43e4,
    .browserTimer = 0x011ea210,
    .unknownTimer = 0x011f43a8,
    .worldCacheSizePatch = 0x005bc8b4,
};

constexpr GameAddresses addressesForRetail_1_67_2_024002 = {
    .configRetrieval = 0x0058c5e0,
    .intersectionPatch = 0x0067b130,
    .vtableBase = 0x00fdf100,
    .createFileWFlagsInit = 0x00404563,
    .randomAccessFlagBranch = 0x004db73c,
    .gcThreadLoopCheck = 0x00e514e5,
    .lotVisibilityCameraBranch = 0x00c62ae5,
    .specWorldManagerUpdate = 0x00c6d3b0,
    .cameraEnableMapViewMode = 0x0073e080,
    .cameraDisableMapViewMode = 0x0073e0d0,
    .refPackDecompressor = 0x004eb900,
    .smoothPatchClassic = 0x004e1830,
    .smoothPatchDupe = 0x008b1f84,
    .monoMethodCacheTimer = 0x011f53e4,
    .browserTimer = 0x011eb210,
    .unknownTimer = 0x011f53a8,
    .worldCacheSizePatch = 0x005bd114,
};

constexpr GameAddresses addressesForEA_1_69_47_024017 = {
    .configRetrieval = 0x0058b990,
    .intersectionPatch = 0x0067c380,
    .vtableBase = 0x01029618,
    .createFileWFlagsInit = 0x00404563,
    .randomAccessFlagBranch = 0x004db51c,
    .gcThreadLoopCheck = 0x00e51245,
    .lotVisibilityCameraBranch = 0x00c623a5,
    .specWorldManagerUpdate = 0x00c6c8f0,
    .cameraEnableMapViewMode = 0x0073ec70,
    .cameraDisableMapViewMode = 0x0073ecc0,
    .refPackDecompressor = 0x004eb4f0,
    .smoothPatchClassic = 0x004e14f0,
    .smoothPatchDupe = 0x008b37a4,
    .monoMethodCacheTimer = 0x0124e474,
    .browserTimer = 0x01244260,
    .unknownTimer = 0x0124e438,
    .worldCacheSizePatch = 0x005bd704,
};

extern const std::array<GameAddresses, gameVersionCount> gameAddressesByGameVersion;

extern const GameAddresses* gameAddresses;

