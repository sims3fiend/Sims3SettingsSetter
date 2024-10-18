#include "script_settings.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;


ScriptSettings& ScriptSettings::Instance() {
    static ScriptSettings instance;
    return instance;
}

ScriptSettings::ScriptSettings() {
    LoadFromDetectedConfigs();
    LoadFromFile();
    LoadPresetsFromDirectory();
}

bool ScriptSettings::CaseInsensitiveCompare(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                std::tolower(static_cast<unsigned char>(b));
        });
}

void ScriptSettings::AddOrUpdateSetting(const std::string& section, const std::string& key,
    const std::variant<bool, std::string>& value, uintptr_t address, bool saved) {
    std::string fullKey = section + "." + key;
    auto it = settings.find(fullKey);
    if (it != settings.end()) {
        it->second.value = value;
        it->second.address = address;
        Log("Updated setting: " + fullKey + ", Total settings: " + std::to_string(settings.size()));
    }
    else {
        settings.emplace(fullKey, SettingEntry{ value, section, saved, address });
        Log("Added new setting: " + fullKey + ", Total settings: " + std::to_string(settings.size()));
    }
}

std::variant<bool, std::string> ScriptSettings::GetSettingValue(const std::string& section, const std::string& key, const std::variant<bool, std::string>& defaultValue) const {
    std::string fullKey = section + "." + key;
    auto it = settings.find(fullKey);
    return (it != settings.end()) ? it->second.value : defaultValue;
}

void ScriptSettings::SetSettingValue(const std::string& section, const std::string& key, const std::variant<bool, std::string>& value) {
    std::string fullKey = section + "." + key;
    auto it = settings.find(fullKey);
    if (it != settings.end()) {
        it->second.value = value;
        Log("Setting value updated: " + fullKey);
    }
    else {
        settings.emplace(fullKey, SettingEntry{ value, section, false, 0 });
        Log("New setting added: " + fullKey + " (Saved: false)");
    }
}

const std::unordered_map<std::string, SettingEntry>& ScriptSettings::GetAllSettings() const {
    return settings;
}

bool ScriptSettings::IsSettingSaved(const std::string& section, const std::string& key) const {
    std::string fullKey = section + "." + key;
    auto it = settings.find(fullKey);
    if (it != settings.end()) {
        Log("IsSettingSaved check for " + fullKey + ": " + (it->second.saved ? "true" : "false"));
        return it->second.saved;
    }
    Log("IsSettingSaved check for " + fullKey + ": false (key not found)");
    return false;
}

void ScriptSettings::SetSettingSaved(const std::string& section, const std::string& key, bool saved) {
    std::string fullKey = section + "." + key;
    auto it = settings.find(fullKey);
    if (it != settings.end()) {
        it->second.saved = saved;
        Log("Setting saved status updated: " + fullKey + " = " + (saved ? "true" : "false"));
    }
    else {
        Log("Attempted to set saved status for non-existent setting: " + fullKey);
    }
}

void ScriptSettings::SaveToFile() const {
    try {
        std::ofstream file(settingsFilePath);
        if (!file) {
            throw std::runtime_error("Failed to open file for writing");
        }

        std::string currentSection;
        for (const auto& [fullKey, entry] : settings) {
            if (entry.saved) {
                if (entry.section != currentSection) {
                    if (!currentSection.empty()) {
                        file << std::endl;
                    }
                    file << "[" << entry.section << "]" << std::endl;
                    currentSection = entry.section;
                }

                size_t dotPos = fullKey.find('.');
                std::string key = fullKey.substr(dotPos + 1);

                file << key << "=";
                if (std::holds_alternative<bool>(entry.value)) {
                    file << std::boolalpha << std::get<bool>(entry.value);
                }
                else {
                    file << std::get<std::string>(entry.value);
                }
                file << std::endl;
            }
        }

        Log("Settings saved successfully to file: " + settingsFilePath.string());
    }
    catch (const std::exception& e) {
        Log("Error saving settings to file: " + std::string(e.what()));
    }
}

void ScriptSettings::LoadFromFile() {
    Log("LoadFromFile started. Current settings count: " + std::to_string(settings.size()));
    std::ifstream file(settingsFilePath);
    if (file) {
        std::string line;
        std::string currentSection;
        int loadedSettingsCount = 0;

        Log("Loading settings from file:");
        while (std::getline(file, line)) {
            line = Trim(line);

            if (line.empty() || line[0] == ';') {
                continue;  // Skip empty lines and comments
            }

            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.length() - 2);
                Log("  Section: " + currentSection);
                continue;
            }

            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string key = Trim(line.substr(0, equalPos));
                std::string value = Trim(line.substr(equalPos + 1));

                std::string fullKey = currentSection + "." + key;

                if (value == "true" || value == "false") {
                    bool boolValue = (value == "true");
                    settings[fullKey] = { boolValue, currentSection, true, 0 };
                }
                else {
                    settings[fullKey] = { value, currentSection, true, 0 };
                }

                ++loadedSettingsCount;
                Log("Loaded and marked as saved: " + fullKey + " = " + value + " (Saved: true)");
            }
        }
        Log("Loaded and marked as saved " + std::to_string(loadedSettingsCount) + " settings from file: " + settingsFilePath.string());
    }
    else {
        Log("No existing settings file found. Keeping current settings.");
    }
    Log("LoadFromFile completed. Final settings count: " + std::to_string(settings.size()));

    Log("Final settings after loading:");
    for (const auto& [key, entry] : settings) {
        Log("  " + key + " = " + (std::holds_alternative<bool>(entry.value) ?
            (std::get<bool>(entry.value) ? "true" : "false") :
            std::get<std::string>(entry.value)) +
            " (Saved: " + (entry.saved ? "true" : "false") + ")");
    }
}

void ScriptSettings::RemoveSetting(const std::string& section, const std::string& key) {
    std::string fullKey = section + "." + key;
    settings.erase(fullKey);
}

void ScriptSettings::LoadFromDetectedConfigs() {
    std::ifstream file(configFilePath);
    if (file) {
        std::string line;
        while (std::getline(file, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == ';') {
                continue;
            }

            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string fullKey = Trim(line.substr(0, equalPos));
                std::string value = Trim(line.substr(equalPos + 1));

                size_t dotPos = fullKey.find('.');
                if (dotPos != std::string::npos) {
                    std::string category = fullKey.substr(0, dotPos);
                    std::string key = fullKey.substr(dotPos + 1);

                    if (value == "true" || value == "false") {
                        bool boolValue = (value == "true");
                        AddOrUpdateSetting(category, key, boolValue, 0);
                    }
                    else {
                        AddOrUpdateSetting(category, key, value, 0);
                    }
                }
            }
        }
        Log("Default configs loaded from file: " + configFilePath.string());
    }
    else {
        Log("Failed to load default configs from file: " + configFilePath.string());
    }
}

void ScriptSettings::AutosaveConfig() {
    static const std::chrono::minutes autosaveInterval(5); // Autosave every 5 minutes

    auto currentTime = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::minutes>(currentTime - lastAutosaveTime) >= autosaveInterval) {
        SaveToFile();
        lastAutosaveTime = currentTime;
    }
}

// Preset management
fs::path ScriptSettings::GetPresetFilePath(const std::string& presetName) const {
    std::string sanitizedPresetName = SanitizeFileName(presetName);
    return presetsDirectory / (sanitizedPresetName + ".ini");
}

bool ScriptSettings::SavePreset(const Preset& preset) {
    if (!fs::exists(presetsDirectory)) {
        fs::create_directories(presetsDirectory);
    }

    fs::path filepath = GetPresetFilePath(preset.name);

    if (SavePresetToIni(preset, filepath.string())) {
        auto it = std::find_if(presets.begin(), presets.end(),
            [&](const Preset& p) { return p.name == preset.name; });
        if (it == presets.end()) {
            presets.push_back(preset);
        }
        return true;
    }
    return false;
}

bool ScriptSettings::LoadPreset(const Preset& preset) {
    // Existing implementation
    for (const auto& [key, value] : preset.settings) {
        size_t dotPos = key.find('.');
        if (dotPos != std::string::npos) {
            std::string section = key.substr(0, dotPos);
            std::string settingKey = key.substr(dotPos + 1);
            SetSettingValue(section, settingKey, value);
            SetSettingSaved(section, settingKey, true);
        }
    }
    Log("Successfully loaded preset: " + preset.name);
    return true;
}

bool ScriptSettings::LoadPreset(const std::string& presetName) {
    // First, try to find the preset by exact name match
    auto it = std::find_if(presets.begin(), presets.end(),
        [&](const Preset& p) { return p.name == presetName; });

    // If not found, try case-insensitive search and partial matches
    if (it == presets.end()) {
        it = std::find_if(presets.begin(), presets.end(),
            [&](const Preset& p) {
                return CaseInsensitiveCompare(p.name, presetName) ||
                    p.name.find(presetName) != std::string::npos ||
                    presetName.find(p.name) != std::string::npos;
            });
    }

    if (it == presets.end()) {
        Log("Preset not found: " + presetName);
        return false;
    }

    Preset loadedPreset;
    fs::path filepath = GetPresetFilePath(it->name);  // Use the actual filename from the found preset
    if (LoadPresetFromIni(loadedPreset, filepath.string())) {
        return LoadPreset(loadedPreset);  // Use the existing LoadPreset(const Preset&) function so I don't have to rewrite :)
    }
    Log("Failed to load preset from file: " + filepath.string());
    return false;
}


std::vector<Preset> ScriptSettings::GetAllPresets() const {
    return presets;
}

bool ScriptSettings::RemovePreset(const std::string& presetName) {
    auto it = std::find_if(presets.begin(), presets.end(),
        [&](const Preset& p) { return p.name == presetName; });
    if (it != presets.end()) {
        fs::path filepath = GetPresetFilePath(presetName);
        if (fs::exists(filepath)) {
            fs::remove(filepath);
        }
        presets.erase(it);
        Log("Preset removed: " + presetName);
        return true;
    }
    Log("Preset not found for removal: " + presetName);
    return false;
}

void ScriptSettings::LoadPresetsFromDirectory() {
    presets.clear();

    if (!fs::exists(presetsDirectory)) {
        fs::create_directories(presetsDirectory);
        Log("Created presets directory: " + presetsDirectory.string());
        return;
    }

    for (const auto& entry : fs::directory_iterator(presetsDirectory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".ini") {
            Preset preset;
            if (LoadPresetFromIni(preset, entry.path().string())) {
                presets.push_back(preset);
                Log("Loaded preset: " + preset.name);
            }
            else {
                Log("Failed to load preset from file: " + entry.path().string());
            }
        }
    }

    Log("Total presets loaded: " + std::to_string(presets.size()));
}
