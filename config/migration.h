#pragma once
#include <string>

// Result of an INI->TOML migration, shown in a one-time popup.
struct MigrationResult {
    bool migrated = false;
    std::string oldPath;
    std::string newPath;
    int settingsCount = 0;
    int configValuesCount = 0;
    int patchesCount = 0;
    bool qolMigrated = false;
};

namespace Migration {
// Run during startup.  Reads old INI from the game's Bin directory, writes a new TOML to Documents\...\S3SS\, and records stats for the popup.
// Safe to call multiple times; does nothing if the TOML already exists... Hopefully.
void CheckAndMigrate();

// ImGui popup state
bool ShouldShowMigrationPopup();
void RenderMigrationPopup();
void DismissMigrationPopup();
} // namespace Migration
