#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "settings.h"

namespace fs = std::filesystem;

enum class PresetLoadStrategy {
    Merge,      // Apply preset on top of existing settings (preserve unmentioned settings)
    Overwrite   // Reset to defaults first, then apply preset (clean slate approach)
};

class PresetManager {
public:
    static PresetManager& Get();

    struct PresetInfo {
        std::string name;
        std::string filename;
        std::string description;  // Read from preset file if available
        fs::file_time_type timestamp;
    };

    void Initialize();    
    bool SavePreset(const std::string& name, const std::string& description = "", std::string* error = nullptr);
    bool LoadPreset(const std::string& name, std::string* error = nullptr);
    bool LoadPresetWithStrategy(const std::string& name, PresetLoadStrategy strategy, std::string* error = nullptr);
    std::vector<PresetInfo> GetAvailablePresets();
    void EnsureDefaultPreset();
    fs::path GetPresetDirectory() const { return m_presetDir; }

    std::string GetPresetPath(const std::string& presetName) const {
        return (m_presetDir / (presetName + ".ini")).string();
    }

private:
    PresetManager();
    
    fs::path m_presetDir;
    const char* DEFAULT_PRESET_NAME = "default";
}; 