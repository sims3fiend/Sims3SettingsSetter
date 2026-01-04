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

extern GameVersion gameVersion;

// Detect current game version from executable name
inline GameVersion DetectGameVersion() {
    static GameVersion cachedVersion = []() {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        // Extract just the filename
        std::wstring exeName = exePath;
        size_t lastSlash = exeName.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            exeName = exeName.substr(lastSlash + 1);
        }

        // Convert to lowercase for comparison
        for (auto& c : exeName) {
            c = towlower(c);
        }

        if (exeName == L"ts3w.exe") {
            return GameVersion::Steam_1_67_2_024037;
        } else if (exeName == L"ts3.exe") {
            return GameVersion::EA_1_69_47_024017;
        }

        // Default to EA if we can't determine
        return GameVersion::EA_1_69_47_024017;
    }();

    return cachedVersion;
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

