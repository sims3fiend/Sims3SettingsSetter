#pragma once
#include "optimization.h"
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <windows.h>

// Game version targeting for patches
enum class GameVersion {
    All,    // Works on all versions (pattern-based or IAT hooks)
    Steam,  // ts3w.exe only
    EA      // ts3.exe only (includes disk versions)
};

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
            return GameVersion::Steam;
        } else if (exeName == L"ts3.exe") {
            return GameVersion::EA;
        }

        // Default to EA if we can't determine
        return GameVersion::EA;
    }();

    return cachedVersion;
}

// Metadata for patch description and categorization
struct PatchMetadata {
    std::string displayName;
    std::string description;
    std::string category = "General";
    bool experimental = false;
    GameVersion targetVersion = GameVersion::All;
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
    static void InstantiateAll() {
        for (auto& entry : GetEntries()) {
            auto patch = entry.factory();
            if (patch) {
                // Store metadata in the patch
                patch->SetMetadata(entry.metadata);
                OptimizationManager::Get().RegisterPatch(std::move(patch));
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

