#pragma once
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <sstream>
#include "script_settings.h"

struct GameConfigEntry {
    std::string category;
    std::string key;
    std::variant<bool, std::string> value;
    uintptr_t address;
    bool saved;
};

class GameConfig {
public:
    static GameConfig& Instance();

    void AddOrUpdateConfig(const std::string& category, const std::string& key, const std::variant<bool, std::string>& value, uintptr_t address);
    std::variant<bool, std::string> GetConfigValue(const std::string& category, const std::string& key, const std::variant<bool, std::string>& defaultValue = false) const;
    void SetConfigValue(const std::string& category, const std::string& key, const std::variant<bool, std::string>& value);

    void SetConfigSaved(const std::string& category, const std::string& key, bool saved);
    bool IsConfigSaved(const std::string& category, const std::string& key) const;
    void SaveOnlyMarked() const;
    const std::unordered_map<std::string, GameConfigEntry>& GetAllConfigs() const;

    void SaveToFile() const;
    void LoadFromFile();

    void AnalyzeAndSortConfigs();
    void AutosaveConfig();

    void LoadDefaults();
    void LoadUserSettings();


private:
    GameConfig();
    ~GameConfig() = default;

    GameConfig(const GameConfig&) = delete;
    GameConfig& operator=(const GameConfig&) = delete;

    void LoadFromDetectedConfigs();
    void LoadFromScriptSettings();

    void AnalyzeConfigGroups(const std::vector<std::vector<GameConfigEntry>>& configGroups);
    std::string FindCommonPrefix(const std::vector<GameConfigEntry>& group);
    void AnalyzeGroupTypes(std::stringstream& ss, const std::vector<GameConfigEntry>& group);

    std::unordered_map<std::string, GameConfigEntry> configs;
    const std::string configFilePath = "detected_configs.txt";
    ScriptSettings& scriptSettings;
};