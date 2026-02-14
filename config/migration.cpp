#include "migration.h"
#include "config_paths.h"
#include "logger.h"
#include "utils.h"
#include "imgui.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

// Module state
static MigrationResult g_result;
static bool g_showPopup = false;

// Helpers:

static std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Simple INI section for game variable settings
struct INISetting {
    std::string name;
    std::string category;
    std::string value;
    std::string min;
    std::string max;
    std::string step;
};

// Simple INI section for Config: overrides
struct INIConfigValue {
    std::string name;
    std::string value;
};

// Simple INI section for QoL: config overrides
struct INIQoLValues {
    // UI
    std::string toggleKey;    // "45" (VK_INSERT)
    std::string disableHooks; // "true"/"false"
    // Memory Monitor
    std::string memEnabled;
    std::string memThreshold;
    std::string memWarningStyle;
    // Borderless Window
    std::string borderlessMode;
};

// Patch section from the INI
struct INIPatch {
    std::string name;
    std::string enabled;
    std::vector<std::pair<std::string, std::string>> settings; // Settings.* entries
};

// Parser:

static bool ParseOldINI(const std::string& iniPath, std::vector<INISetting>& outSettings, std::vector<INIConfigValue>& outConfigValues, INIQoLValues& outQoL, std::vector<INIPatch>& outPatches) {
    std::ifstream file(Utils::ToPath(iniPath));
    if (!file.is_open()) return false;

    enum class Section { None, Setting, Config, QoL, Patch };
    Section currentSection = Section::None;
    std::string currentSectionName;

    INISetting currentSetting;
    INIConfigValue currentConfig;
    INIPatch currentPatch;
    bool inPatchArea = false;

    auto flushSection = [&]() {
        switch (currentSection) {
        case Section::Setting:
            if (!currentSetting.name.empty() && !currentSetting.value.empty()) { outSettings.push_back(currentSetting); }
            break;
        case Section::Config:
            if (!currentConfig.name.empty() && !currentConfig.value.empty()) { outConfigValues.push_back(currentConfig); }
            break;
        case Section::Patch:
            if (!currentPatch.name.empty()) { outPatches.push_back(currentPatch); }
            break;
        default:
            break;
        }
        currentSection = Section::None;
        currentSetting = {};
        currentConfig = {};
        currentPatch = {};
    };

    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);

        // Detect the patch settings area marker
        if (line == "; Patch Settings" || line == "; Optimization Settings") {
            flushSection();
            inPatchArea = true;
            continue;
        }

        // Skip comments and empty lines
        if (line.empty() || line[0] == ';') continue;

        // Section header
        if (line[0] == '[' && line.back() == ']') {
            flushSection();
            std::string sectionName = line.substr(1, line.size() - 2);

            if (sectionName.substr(0, 7) == "Config:") {
                std::string key = sectionName.substr(7);

                // QoL: prefixed entries
                if (key.substr(0, 4) == "QoL:") {
                    currentSection = Section::QoL;
                    currentSectionName = key.substr(4); // Strip "QoL:" prefix
                } else {
                    currentSection = Section::Config;
                    currentConfig.name = key;
                }
            } else if (inPatchArea && sectionName.find("Optimization_") == 0) {
                currentSection = Section::Patch;
                currentPatch.name = sectionName.substr(13); // Strip "Optimization_"
            } else if (!inPatchArea) {
                // Regular game variable setting
                currentSection = Section::Setting;
                currentSetting.name = sectionName;
            }
            continue;
        }

        // Key=Value parsing
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));

        switch (currentSection) {
        case Section::Setting:
            if (key == "Category")
                currentSetting.category = value;
            else if (key == "Value")
                currentSetting.value = value;
            else if (key == "Min")
                currentSetting.min = value;
            else if (key == "Max")
                currentSetting.max = value;
            else if (key == "Step")
                currentSetting.step = value;
            break;

        case Section::Config:
            if (key == "Value") currentConfig.value = value;
            break;

        case Section::QoL:
            // Map old QoL: config values to structured fields
            if (key == "Value") {
                if (currentSectionName == "ToggleKey")
                    outQoL.toggleKey = value;
                else if (currentSectionName == "DisableHooks")
                    outQoL.disableHooks = value;
                else if (currentSectionName == "MemoryMonitor:Enabled")
                    outQoL.memEnabled = value;
                else if (currentSectionName == "MemoryMonitor:Threshold")
                    outQoL.memThreshold = value;
                else if (currentSectionName == "MemoryMonitor:WarningStyle")
                    outQoL.memWarningStyle = value;
                else if (currentSectionName == "BorderlessWindow:Mode")
                    outQoL.borderlessMode = value;
            }
            break;

        case Section::Patch:
            if (key == "Enabled") {
                currentPatch.enabled = value;
            } else if (key.find("Settings.") == 0) {
                currentPatch.settings.emplace_back(key.substr(9), value);
            }
            break;

        default:
            break;
        }
    }

    // Flush final section
    flushSection();
    return true;
}

// TOML Builder:

// Try to detect if a string value is a number or bool to write native TOML types
static void InsertSmartValue(toml::table& table, const std::string& key, const std::string& value) {
    // Bool check
    if (value == "true") {
        table.insert(key, true);
        return;
    }
    if (value == "false") {
        table.insert(key, false);
        return;
    }

    // Comma separated = vector/array of floats
    if (value.find(',') != std::string::npos) {
        toml::array arr;
        std::stringstream ss(value);
        std::string token;
        while (std::getline(ss, token, ',')) {
            try {
                arr.push_back(std::stod(Trim(token)));
            } catch (...) {
                // Not a number, store the whole thing as a string
                table.insert(key, value);
                return;
            }
        }
        table.insert(key, std::move(arr));
        return;
    }

    // Integer check (try before float so "67" stays int)
    try {
        size_t pos;
        int64_t intVal = std::stoll(value, &pos);
        if (pos == value.size()) {
            table.insert(key, intVal);
            return;
        }
    } catch (...) {}

    // Float check
    try {
        size_t pos;
        double floatVal = std::stod(value, &pos);
        if (pos == value.size()) {
            table.insert(key, floatVal);
            return;
        }
    } catch (...) {}

    // Fallback: string
    table.insert(key, value);
}

static toml::table BuildToml(const std::vector<INISetting>& settings, const std::vector<INIConfigValue>& configValues, const INIQoLValues& qol, const std::vector<INIPatch>& patches) {
    toml::table root;

    // [meta]
    toml::table meta;
    meta.insert("version", 1);
    root.insert("meta", std::move(meta));

    // [settings.*]
    if (!settings.empty()) {
        toml::table settingsTable;
        for (const auto& s : settings) {
            toml::table entry;
            if (!s.category.empty()) entry.insert("category", s.category);
            InsertSmartValue(entry, "value", s.value);
            if (!s.min.empty()) {
                try {
                    entry.insert("min", std::stod(s.min));
                } catch (...) {}
            }
            if (!s.max.empty()) {
                try {
                    entry.insert("max", std::stod(s.max));
                } catch (...) {}
            }
            if (!s.step.empty()) {
                try {
                    entry.insert("step", std::stod(s.step));
                } catch (...) {}
            }
            settingsTable.insert(s.name, std::move(entry));
        }
        root.insert("settings", std::move(settingsTable));
    }

    // [config]
    if (!configValues.empty()) {
        toml::table configTable;
        for (const auto& cv : configValues) { configTable.insert(cv.name, cv.value); }
        root.insert("config", std::move(configTable));
    }

    // [qol.*]
    {
        toml::table qolTable;
        bool hasQoL = false;

        // ui
        if (!qol.toggleKey.empty() || !qol.disableHooks.empty()) {
            hasQoL = true;
            toml::table uiTable;
            if (!qol.toggleKey.empty()) {
                try {
                    uiTable.insert("toggle_key", std::stoll(qol.toggleKey));
                } catch (...) {}
            }
            if (!qol.disableHooks.empty()) { uiTable.insert("disable_hooks", qol.disableHooks == "true"); }
            qolTable.insert("ui", std::move(uiTable));
        }

        // memory_monitor
        if (!qol.memEnabled.empty() || !qol.memThreshold.empty() || !qol.memWarningStyle.empty()) {
            hasQoL = true;
            toml::table memTable;
            if (!qol.memEnabled.empty()) { memTable.insert("enabled", qol.memEnabled == "true"); }
            if (!qol.memThreshold.empty()) {
                try {
                    memTable.insert("warning_threshold", std::stod(qol.memThreshold));
                } catch (...) {}
            }
            if (!qol.memWarningStyle.empty()) { memTable.insert("warning_style", qol.memWarningStyle); }
            qolTable.insert("memory_monitor", std::move(memTable));
        }

        // borderless_window
        if (!qol.borderlessMode.empty()) {
            hasQoL = true;
            toml::table bwTable;
            bwTable.insert("mode", qol.borderlessMode);
            qolTable.insert("borderless_window", std::move(bwTable));
        }

        if (hasQoL) { root.insert("qol", std::move(qolTable)); }
    }

    // [patches.*]
    if (!patches.empty()) {
        toml::table patchesTable;
        for (const auto& p : patches) {
            toml::table patchTable;
            if (!p.enabled.empty()) { patchTable.insert("enabled", p.enabled == "true"); }
            for (const auto& [key, val] : p.settings) { InsertSmartValue(patchTable, key, val); }
            patchesTable.insert(p.name, std::move(patchTable));
        }
        root.insert("patches", std::move(patchesTable));
    }

    return root;
}

// API stuff:

namespace Migration {

void CheckAndMigrate() {
    if (!ConfigPaths::NeedsMigration()) { return; }

    std::string iniPath = ConfigPaths::GetLegacyINIPath();
    std::string tomlPath = ConfigPaths::GetConfigPath();

    LOG_INFO("[Migration] Old INI found at " + iniPath + ", migrating to " + tomlPath);

    // Ensure destination directory exists
    if (!ConfigPaths::EnsureDirectoryExists()) {
        LOG_ERROR("[Migration] Failed to create config directory, migration aborted");
        return;
    }

    // Parse old INI
    std::vector<INISetting> settings;
    std::vector<INIConfigValue> configValues;
    INIQoLValues qolValues;
    std::vector<INIPatch> patches;

    if (!ParseOldINI(iniPath, settings, configValues, qolValues, patches)) {
        LOG_ERROR("[Migration] Failed to parse old INI, migration aborted");
        return;
    }

    // Build TOML
    toml::table root = BuildToml(settings, configValues, qolValues, patches);

    std::string writeError;
    if (!ConfigPaths::AtomicWriteToml(tomlPath, root, &writeError)) {
        LOG_ERROR("[Migration] " + writeError);
        return;
    }

    // Record results for popup
    bool hasQoL = !qolValues.toggleKey.empty() || !qolValues.disableHooks.empty() || !qolValues.memEnabled.empty() || !qolValues.borderlessMode.empty();

    g_result.migrated = true;
    g_result.oldPath = iniPath;
    g_result.newPath = tomlPath;
    g_result.settingsCount = static_cast<int>(settings.size());
    g_result.configValuesCount = static_cast<int>(configValues.size());
    g_result.patchesCount = static_cast<int>(patches.size());
    g_result.qolMigrated = hasQoL;
    g_showPopup = true;

    LOG_INFO("[Migration] Migration complete: " + std::to_string(settings.size()) + " settings, " + std::to_string(configValues.size()) + " config values, " + std::to_string(patches.size()) + " patches" +
             (hasQoL ? ", QoL settings migrated" : ""));
}

bool ShouldShowMigrationPopup() {
    return g_showPopup;
}

void RenderMigrationPopup() {
    if (!g_showPopup) return;

    if (!ImGui::IsPopupOpen("Settings Migrated")) { ImGui::OpenPopup("Settings Migrated"); }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Settings Migrated", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f)); //neat
        ImGui::Text("Yipee! Settings have been migrated to TOML!");
        ImGui::PopStyleColor();

        ImGui::Separator();

        ImGui::Text("Your settings have been moved from:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  %s", g_result.oldPath.c_str());
        ImGui::Text("To:");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "  %s", g_result.newPath.c_str());

        ImGui::Separator();

        // Migration stats
        ImGui::Text("Migrated:");
        if (g_result.settingsCount > 0) { ImGui::BulletText("%d game variable settings", g_result.settingsCount); }
        if (g_result.configValuesCount > 0) { ImGui::BulletText("%d config value overrides", g_result.configValuesCount); }
        if (g_result.patchesCount > 0) { ImGui::BulletText("%d patch configurations", g_result.patchesCount); }
        if (g_result.qolMigrated) { ImGui::BulletText("QoL settings (UI toggle, memory monitor, etc.)"); }

        ImGui::Separator();

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.6f, 1.0f), "You can safely delete the old files from your game's Bin folder:");
        ImGui::BulletText("S3SS.ini");
        ImGui::BulletText("S3SS_defaults.ini");
        ImGui::BulletText("S3SS_LOG.txt");

        ImGui::Separator();

        // Center OK button
        float buttonWidth = 120.0f;
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);

        if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
            g_showPopup = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void DismissMigrationPopup() {
    g_showPopup = false;
}

} // namespace Migration