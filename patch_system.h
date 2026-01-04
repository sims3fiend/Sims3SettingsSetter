#pragma once
#include "optimization.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <windows.h>

// Game version targeting for patches
enum GameVersion : uint8_t {
    Retail_1_67_2_024002 = 0, // The last version available for disc distributions of the game.
    Steam_1_67_2_024037 = 1,
    EA_1_69_47_024017 = 2,
};

constexpr uint8_t gameVersionCount = 3;

using GameVersionMask = uint8_t;

constexpr GameVersionMask allGameVersionsMask = (
      (1 << GameVersion::Retail_1_67_2_024002)
    | (1 << GameVersion::Steam_1_67_2_024037)
    | (1 << GameVersion::EA_1_69_47_024017)
);

extern const std::array<const char*, gameVersionCount> gameVersionNames;
extern const std::array<uint32_t, gameVersionCount> gameVersionTimestamps;

extern GameVersion gameVersion;

// Detect current game version from the executable's timestamp.
inline bool DetectGameVersion(GameVersion* detectedVersion) {
    const uintptr_t exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const IMAGE_DOS_HEADER* dosStub = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
    const IMAGE_NT_HEADERS* pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dosStub->e_lfanew);

    uint32_t timestamp = pe->FileHeader.TimeDateStamp;

    const auto match = std::find(gameVersionTimestamps.begin(), gameVersionTimestamps.end(), timestamp);

    if (match != gameVersionTimestamps.end()) {
        size_t index = match - gameVersionTimestamps.begin();
        LOG_INFO(std::format("A recognized executable timestamp of {:#010x} was encountered. Detected game version: {}",
                             timestamp, gameVersionNames[index]));
        // Bosh.
        *detectedVersion = static_cast<GameVersion>(index);
        return true;
    }

    LOG_ERROR(std::format("An unrecognized executable timestamp of {:#010x} was encountered.", timestamp));

    // Default to EA if we can't determine
    *detectedVersion = GameVersion::EA_1_69_47_024017;
    return false;
}

// Metadata for patch description and categorization
struct PatchMetadata {
    std::string displayName;
    std::string description;
    std::string category = "General";
    bool experimental = false;
    GameVersionMask supportedVersions = allGameVersionsMask;
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

