#pragma once
#include "optimization.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <windows.h>

// Specific game versions we support
enum class GameVersion : uint8_t {
    Retail  = 0,   // 1.67.2.024002 - Disc
    Steam   = 1,   // 1.67.2.024037 - Steam
    EA      = 2,   // 1.69.47.024017 - EA App
    EA_1_69_43 = 3,   // 1.69.43.024017 - EA App/Origin
    Unknown = 255
};

constexpr size_t GAME_VERSION_COUNT = 4;

// Bitmask for declaring which versions a patch supports
using GameVersionMask = uint8_t;
constexpr GameVersionMask VERSION_RETAIL = (1 << static_cast<uint8_t>(GameVersion::Retail));
constexpr GameVersionMask VERSION_STEAM  = (1 << static_cast<uint8_t>(GameVersion::Steam));
constexpr GameVersionMask VERSION_EA     = (1 << static_cast<uint8_t>(GameVersion::EA));
constexpr GameVersionMask VERSION_ALL    = VERSION_RETAIL | VERSION_STEAM | VERSION_EA;

// TimeDateStamp field from PE header
constexpr std::array<uint32_t, GAME_VERSION_COUNT> VERSION_TIMESTAMPS = {
    0x52D872DA,  // Retail 1.67.2.024002
    0x52DEC247,  // Steam 1.67.2.024037
    0x6707155C,  // EA 1.69.47.024017
    0x568D4BAC,  // EA 1.69.43.024017
};

constexpr std::array<const char*, GAME_VERSION_COUNT> VERSION_NAMES = {
    "Retail 1.67.2.024002",
    "Steam 1.67.2.024037",
    "EA 1.69.47.024017",
    "EA 1.69.43.024017",
};

// Global game version - set once at init via DetectGameVersion()
inline GameVersion g_gameVersion = GameVersion::Unknown;
inline uint32_t g_exeTimeDateStamp = 0;

// Detect current game version from PE header timestamp
// Return true if version was recognized, false otherwise
inline bool DetectGameVersion() {
    const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);

    uint32_t timestamp = pe->FileHeader.TimeDateStamp;
    g_exeTimeDateStamp = timestamp;

    for (size_t i = 0; i < GAME_VERSION_COUNT; i++) {
        if (VERSION_TIMESTAMPS[i] == timestamp) {
            g_gameVersion = static_cast<GameVersion>(i);
            return true;
        }
    }

    g_gameVersion = GameVersion::Unknown;
    return false;
}

// Helper to get version name string
inline const char* GetGameVersionName() {
    if (g_gameVersion == GameVersion::Unknown) {
        return "Unknown";
    }
    return VERSION_NAMES[static_cast<size_t>(g_gameVersion)];
}

// Check if a version is outdated for its distribution platform
inline bool IsVersionOutdated(GameVersion version, uint32_t timeDateStamp) {
    return !(version <= GameVersion::EA) & (timeDateStamp < VERSION_TIMESTAMPS[static_cast<size_t>(GameVersion::EA)]);
}

// Check if current version matches a version mask
inline bool IsVersionSupported(GameVersionMask mask) {
    if (g_gameVersion == GameVersion::Unknown) {
        return false;
    }
    return (mask & (1 << static_cast<uint8_t>(g_gameVersion))) != 0;
}

// Metadata for patch description and categorization
struct PatchMetadata {
    std::string displayName;
    std::string description;
    std::string category = "General";
    bool experimental = false;
    GameVersionMask supportedVersions = VERSION_ALL;
    std::vector<std::string> technicalDetails;
};

// Global patch registry
class PatchRegistry {
public:
    using PatchFactory = std::function<std::unique_ptr<OptimizationPatch>()>;
    
    struct PatchEntry {
        PatchFactory factory;
        PatchMetadata metadata;
    };

private:
    static std::vector<PatchEntry>& GetEntries() {
        static std::vector<PatchEntry> entries;
        return entries;
    }

public:
    // Register a patch factory with metadata
    static void Register(PatchFactory factory, PatchMetadata metadata) {
        GetEntries().push_back({factory, metadata});
    }

    // Create all registered patches and add them to OptimizationManager
    static void InstantiateAll(OptimizationManager& manager) {
        for (auto& entry : GetEntries()) {
            auto patch = entry.factory();
            if (patch) {
                // Store metadata in the patch
                patch->SetMetadata(entry.metadata);
                manager.RegisterPatch(std::move(patch));
            }
        }
    }

    // Get all registered entries (useful for inspection)
    static const std::vector<PatchEntry>& GetAll() {
        return GetEntries();
    }
};

// Auto-registration helper
struct PatchRegistrar {
    PatchRegistrar(PatchRegistry::PatchFactory factory, PatchMetadata metadata) {
        PatchRegistry::Register(factory, metadata);
    }
};

// Macro for registering a custom patch class
#define REGISTER_PATCH(ClassName, ...) \
    static PatchRegistrar _patch_registrar_##ClassName([]() -> std::unique_ptr<OptimizationPatch> { \
        return std::make_unique<ClassName>(); \
    }, __VA_ARGS__);

// Macro for registering a custom patch with a different factory name
#define REGISTER_CUSTOM_PATCH(Name, ClassName, ...) \
    static PatchRegistrar _patch_registrar_##Name([]() -> std::unique_ptr<OptimizationPatch> { \
        return std::make_unique<ClassName>(); \
    }, __VA_ARGS__);

