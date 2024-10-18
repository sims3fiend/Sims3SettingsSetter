#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <chrono>
#include <filesystem>
#include "preset.h"

struct SettingEntry {
    std::variant<bool, std::string> value;
    std::string section;
    bool saved = false;
    uintptr_t address = 0;
};

class ScriptSettings {
public:
    static ScriptSettings& Instance();

    void AddOrUpdateSetting(const std::string& section, const std::string& key, const std::variant<bool, std::string>& value, uintptr_t address = 0, bool saved = false);
    std::variant<bool, std::string> GetSettingValue(const std::string& section, const std::string& key, const std::variant<bool, std::string>& defaultValue = false) const;
    void SetSettingValue(const std::string& section, const std::string& key, const std::variant<bool, std::string>& value);

    const std::unordered_map<std::string, SettingEntry>& GetAllSettings() const;
    bool IsSettingSaved(const std::string& section, const std::string& key) const;
    void SetSettingSaved(const std::string& section, const std::string& key, bool saved);

    void SaveToFile() const;
    void LoadFromFile();
    void RemoveSetting(const std::string& section, const std::string& key);
    void AutosaveConfig();
    void SaveDetectedConfigs() const;
    void LoadFromDetectedConfigs();

    // Preset management
    bool SavePreset(const Preset& preset);
    bool LoadPreset(const Preset& preset);
    //h-heh
    bool LoadPreset(const std::string& presetName);
    std::vector<Preset> GetAllPresets() const;
    bool RemovePreset(const std::string& presetName);
    const std::filesystem::path& GetPresetsDirectory() const { return presetsDirectory; }

private:
    ScriptSettings();
    ~ScriptSettings() = default;

    ScriptSettings(const ScriptSettings&) = delete;
    ScriptSettings& operator=(const ScriptSettings&) = delete;

    void LoadPresetsFromDirectory();
    std::filesystem::path GetPresetFilePath(const std::string& presetName) const;
    static bool CaseInsensitiveCompare(const std::string& a, const std::string& b);

    std::chrono::steady_clock::time_point lastAutosaveTime;
    std::unordered_map<std::string, SettingEntry> settings;
    const std::filesystem::path settingsFilePath = "s3ss_settings.ini";
    const std::filesystem::path configFilePath = "detected_configs.txt";
    std::vector<Preset> presets;
    const std::filesystem::path presetsDirectory = "s3ss_presets";
};
