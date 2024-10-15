#include "game_config.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>


GameConfig& GameConfig::Instance() {
    static GameConfig instance;
    return instance;
}

GameConfig::GameConfig() : scriptSettings(ScriptSettings::Instance()) {
    LoadDefaults();
    LoadUserSettings();
}

void GameConfig::LoadDefaults() {
    LoadFromDetectedConfigs();
}

void GameConfig::LoadUserSettings() {
    LoadFromScriptSettings();
}

void GameConfig::SetConfigSaved(const std::string& category, const std::string& key, bool saved) {
    std::string fullKey = category + "." + key;
    auto it = configs.find(fullKey);
    if (it != configs.end()) {
        it->second.saved = saved;
        if (saved) {
            scriptSettings.SetSettingValue("GameConfig", fullKey, it->second.value);
        }
        else {
            scriptSettings.RemoveSetting("GameConfig", fullKey);
        }
    }
}

bool GameConfig::IsConfigSaved(const std::string& category, const std::string& key) const {
    std::string fullKey = category + "." + key;
    auto it = configs.find(fullKey);
    return (it != configs.end()) ? it->second.saved : false;
}

void GameConfig::AddOrUpdateConfig(const std::string& category, const std::string& key, const std::variant<bool, std::string>& value, uintptr_t address) {
    std::string fullKey = category + "." + key;
    configs[fullKey] = { category, key, value, address, IsConfigSaved(category, key) };

    // Update ScriptSettings if the config is marked as saved
    if (configs[fullKey].saved) {
        scriptSettings.SetSettingValue("GameConfig", fullKey, value);
    }
}

std::variant<bool, std::string> GameConfig::GetConfigValue(const std::string& category, const std::string& key, const std::variant<bool, std::string>& defaultValue) const {
    std::string fullKey = category + "." + key;
    auto it = configs.find(fullKey);
    return (it != configs.end()) ? it->second.value : defaultValue;
}

void GameConfig::SetConfigValue(const std::string& category, const std::string& key, const std::variant<bool, std::string>& value) {
    std::string fullKey = category + "." + key;
    auto it = configs.find(fullKey);
    if (it != configs.end()) {
        it->second.value = value;
    }
}

const std::unordered_map<std::string, GameConfigEntry>& GameConfig::GetAllConfigs() const {
    return configs;
}

void GameConfig::SaveToFile() const {
    std::ofstream file(configFilePath);
    if (file.is_open()) {
        for (const auto& [fullKey, entry] : configs) {
            if (entry.saved) {
                file << entry.category << "." << entry.key << "=";
                if (std::holds_alternative<bool>(entry.value)) {
                    file << (std::get<bool>(entry.value) ? "true" : "false");
                }
                else {
                    file << std::get<std::string>(entry.value);
                }
                file << std::endl;
            }
        }
        file.close();
        Log("Game config saved to file: " + configFilePath);
    }
    else {
        Log("Failed to save game config to file: " + configFilePath);
    }
}

void GameConfig::LoadFromDetectedConfigs() {
    std::ifstream file(configFilePath);
    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string fullKey = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                size_t dotPos = fullKey.find('.');
                if (dotPos != std::string::npos) {
                    std::string category = fullKey.substr(0, dotPos);
                    std::string key = fullKey.substr(dotPos + 1);

                    if (value == "true" || value == "false") {
                        bool boolValue = (value == "true");
                        AddOrUpdateConfig(category, key, boolValue, 0); // Address is unknown at this point
                    }
                    else {
                        AddOrUpdateConfig(category, key, value, 0); // Address is unknown at this point
                    }
                }
            }
        }
        file.close();
        Log("Default game configs loaded from file: " + configFilePath);
    }
    else {
        Log("Failed to load default game configs from file: " + configFilePath);
    }
}

void GameConfig::LoadFromScriptSettings() {
    const auto& allSettings = scriptSettings.GetAllSettings();
    for (const auto& [fullKey, entry] : allSettings) {
        if (entry.section == "GameConfig") {
            size_t dotPos = fullKey.find('.');
            if (dotPos != std::string::npos) {
                std::string category = fullKey.substr(0, dotPos);
                std::string key = fullKey.substr(dotPos + 1);
                AddOrUpdateConfig(category, key, entry.value, 0); // Address will be updated later
                SetConfigSaved(category, key, true);
            }
        }
    }
    Log("User game configs loaded from script_settings.ini");
}

void GameConfig::AutosaveConfig() {
    static auto lastSaveTime = std::chrono::steady_clock::now();
    auto currentTime = std::chrono::steady_clock::now();

    // Save every 5 minutes
    if (std::chrono::duration_cast<std::chrono::minutes>(currentTime - lastSaveTime).count() >= 5) {
        SaveToFile();
        lastSaveTime = currentTime;
    }
}

void GameConfig::SaveOnlyMarked() const {
    for (const auto& [fullKey, entry] : configs) {
        if (entry.saved) {
            scriptSettings.SetSettingValue(entry.category, entry.key, entry.value);
        }
    }
    scriptSettings.SaveToFile();
}

//these can all go

void GameConfig::AnalyzeAndSortConfigs() {
    std::vector<GameConfigEntry> sortedConfigs;
    for (const auto& [key, info] : configs) {
        sortedConfigs.push_back(info);
    }

    std::sort(sortedConfigs.begin(), sortedConfigs.end(),
        [](const GameConfigEntry& a, const GameConfigEntry& b) {
            return a.address < b.address;
        });

    const uintptr_t proximityThreshold = 0x1000;
    std::vector<std::vector<GameConfigEntry>> configGroups;

    for (const auto& config : sortedConfigs) {
        if (configGroups.empty() || config.address - configGroups.back().back().address > proximityThreshold) {
            configGroups.push_back({ config });
        }
        else {
            configGroups.back().push_back(config);
        }
    }

    AnalyzeConfigGroups(configGroups);

    // Log the sorted configs
    std::stringstream ss;
    ss << "Sorted Configs:" << std::endl;
    ss << std::setw(40) << std::left << "Key" << std::setw(20) << "Value" << "Address" << std::endl;
    ss << std::string(70, '-') << std::endl;

    for (const auto& info : sortedConfigs) {
        ss << std::setw(40) << std::left << info.key << std::setw(20);
        if (std::holds_alternative<bool>(info.value)) {
            ss << (std::get<bool>(info.value) ? "true" : "false");
        }
        else {
            ss << std::get<std::string>(info.value);
        }
        ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << info.address << std::endl;
    }

    Log(ss.str());

    SaveToFile();
}

void GameConfig::AnalyzeConfigGroups(const std::vector<std::vector<GameConfigEntry>>& configGroups) {
    std::stringstream ss;
    ss << "Config Group Analysis:" << std::endl;

    std::vector<std::vector<GameConfigEntry>> mergedGroups;
    const uintptr_t mergeThreshold = 0x1000; // Adjust this value to control group merging

    for (const auto& group : configGroups) {
        if (mergedGroups.empty() ||
            group.front().address - mergedGroups.back().back().address > mergeThreshold) {
            mergedGroups.push_back(group);
        }
        else {
            mergedGroups.back().insert(mergedGroups.back().end(), group.begin(), group.end());
        }
    }

    for (size_t i = 0; i < mergedGroups.size(); ++i) {
        const auto& group = mergedGroups[i];
        ss << "Group " << i + 1 << ":" << std::endl;
        ss << "  Start address: 0x" << std::hex << group.front().address << std::endl;
        ss << "  End address: 0x" << std::hex << group.back().address << std::endl;
        ss << "  Size: " << std::dec << group.size() << " configs" << std::endl;
        ss << "  Address range: 0x" << std::hex << (group.back().address - group.front().address) << std::endl;

        std::string commonPrefix = FindCommonPrefix(group);
        if (!commonPrefix.empty()) {
            ss << "  Common prefix: " << commonPrefix << std::endl;
        }

        AnalyzeGroupTypes(ss, group);

        ss << "  Configs:" << std::endl;
        for (const auto& config : group) {
            ss << "    " << std::setw(40) << std::left << config.key << ": ";
            if (std::holds_alternative<bool>(config.value)) {
                ss << (std::get<bool>(config.value) ? "true" : "false");
            }
            else {
                ss << std::get<std::string>(config.value);
            }
            ss << " (0x" << std::hex << config.address << ")" << std::endl;
        }

        ss << std::endl;
    }

    Log(ss.str());
}

std::string GameConfig::FindCommonPrefix(const std::vector<GameConfigEntry>& group) {
    if (group.empty()) return "";
    std::string commonPrefix = group.front().key;
    for (const auto& config : group) {
        while (config.key.substr(0, commonPrefix.length()) != commonPrefix) {
            commonPrefix = commonPrefix.substr(0, commonPrefix.length() - 1);
            if (commonPrefix.empty()) return "";
        }
    }
    return commonPrefix;
}

void GameConfig::AnalyzeGroupTypes(std::stringstream& ss, const std::vector<GameConfigEntry>& group) {
    int boolCount = 0, stringCount = 0, numericCount = 0;
    for (const auto& config : group) {
        if (std::holds_alternative<bool>(config.value)) {
            boolCount++;
        }
        else {
            const std::string& strValue = std::get<std::string>(config.value);
            if (strValue.find_first_not_of("0123456789.-") == std::string::npos) {
                numericCount++;
            }
            else {
                stringCount++;
            }
        }
    }
    ss << "  Type distribution: " << boolCount << " boolean, "
        << numericCount << " numeric, " << stringCount << " string" << std::endl;
}
