#include "preset_manager.h"
#include "utils.h"
#include "logger.h"
#include "optimization.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <Windows.h>

PresetManager& PresetManager::Get() {
    static PresetManager instance;
    return instance;
}

PresetManager::PresetManager() {
    // Get the DLL path
    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleA("settings.dll"), dllPath, MAX_PATH);
    
    // Create presets directory next to DLL TODO move this to like utils or something
    fs::path dllDir = fs::path(dllPath).parent_path();
    m_presetDir = dllDir / "presets";
}

void PresetManager::Initialize() {
    try {
        // Create presets directory if it doesn't exist
        if (!fs::exists(m_presetDir)) {
            fs::create_directory(m_presetDir);
        }
        
        // Ensure we have a default preset
        EnsureDefaultPreset();
    }
    catch (const std::exception& e) {
        LOG_ERROR(std::string("[PresetManager] Failed to initialize preset system: ") + e.what());
    }
}

bool PresetManager::SavePreset(const std::string& name, const std::string& description, std::string* error) {
    try {
        fs::path presetPath = m_presetDir / (name + ".ini");
        
        // Save settings using the settings manager
        std::string saveError;
        if (!SettingsManager::Get().SaveConfig(presetPath.string(), &saveError)) {
            if (error) *error = "Failed to save preset: " + saveError;
            return false;
        }
        
        // If there's a description, append it to the file
        if (!description.empty()) {
            std::ofstream file(presetPath, std::ios::app);
            if (file.is_open()) {
                file << "\n; Description: " << description << "\n";
            }
        }
        
        return true;
    }
    catch (const std::exception& e) {
        if (error) *error = "Error saving preset: " + std::string(e.what());
        return false;
    }
}

bool PresetManager::LoadPresetWithStrategy(const std::string& name, PresetLoadStrategy strategy, std::string* error) {
    try {
        fs::path presetPath = m_presetDir / (name + ".ini");
        if (!fs::exists(presetPath)) {
            if (error) *error = "Preset file does not exist: " + presetPath.string();
            return false;
        }
        
        LOG_INFO("[PresetManager] Loading preset from: " + presetPath.string());
        
        if (strategy == PresetLoadStrategy::Overwrite) {
            // First try to load values from defaults file if it exists
            std::string defaultsPath = "S3SS_defaults.ini";
            if (fs::exists(defaultsPath)) {
                std::string loadError;
                if (SettingsManager::Get().LoadDefaultValues(defaultsPath, &loadError)) {
                    LOG_INFO("[PresetManager] Loaded default values from S3SS_defaults.ini");
                } else {
                    LOG_WARNING("[PresetManager] Failed to load defaults file: " + loadError);
                }
            }
            
            // Reset all settings to defaults
            SettingsManager::Get().ResetAllSettings();
            
            // Also clear all overrides to ensure a clean slate
            auto& settingsManager = SettingsManager::Get();
            const auto& allSettings = settingsManager.GetAllSettings();
            for (const auto& [settingName, setting] : allSettings) {
                setting->SetOverridden(false);  // Remove override flag
                setting->SetUnsavedChanges(true);  // Mark as needing update
            }
            
            LOG_INFO("[PresetManager] Reset all settings to defaults before loading preset");
        }
        
        // Now load the preset on top of current settings
        bool success = SettingsManager::Get().LoadConfig(presetPath.string(), error);
        
        if (success) {
            LOG_INFO("Successfully loaded settings from preset");
            
            // Force UI update by marking settings as changed
            auto& settingsManager = SettingsManager::Get();
            const auto& allSettings = settingsManager.GetAllSettings();
            for (const auto& [settingName, setting] : allSettings) {
                if (setting->IsOverridden()) {
                    // Only mark overridden settings as changed to trigger UI refresh
                    setting->SetUnsavedChanges(true);
                }
            }
            
            // Auto-save after applying preset
            std::string saveError;
            bool saveSuccess = SettingsManager::Get().SaveConfig("S3SS.ini", &saveError);
            // Also save optimization settings
            saveSuccess &= OptimizationManager::Get().SaveState("S3SS.ini");
            
            if (!saveSuccess) {
                LOG_WARNING("[PresetManager] Auto-save after preset load failed: " + saveError);
            } else {
                LOG_INFO("[PresetManager] Auto-saved settings after loading preset");
            }
        } else {
            LOG_ERROR("[PresetManager] Failed to load settings from preset: " + (error ? *error : std::string("unknown error")));
        }
        
        return success;
    }
    catch (const std::exception& e) {
        if (error) *error = "Error loading preset: " + std::string(e.what());
        LOG_ERROR(std::string("[PresetManager] Exception while loading preset: ") + e.what());
        return false;
    }
}

bool PresetManager::LoadPreset(const std::string& name, std::string* error) {
    // Default to Merge strategy for backward compatibility
    return LoadPresetWithStrategy(name, PresetLoadStrategy::Merge, error);
}

std::vector<PresetManager::PresetInfo> PresetManager::GetAvailablePresets() {
    std::vector<PresetInfo> presets;
    
    try {
        for (const auto& entry : fs::directory_iterator(m_presetDir)) {
            if (entry.path().extension() == ".ini") {
                PresetInfo info;
                info.name = entry.path().stem().string();
                info.filename = entry.path().filename().string();
                info.timestamp = entry.last_write_time();
                
                // Try to read description from file
                std::ifstream file(entry.path());
                std::string line;
                while (std::getline(file, line)) {
                    if (line.find("; Description: ") == 0) {
                        info.description = line.substr(14);
                        break;
                    }
                }
                
                presets.push_back(info);
            }
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR(std::string("[PresetManager] Error getting presets: ") + e.what());
    }
    
    return presets;
}

void PresetManager::EnsureDefaultPreset() {
    fs::path defaultPreset = m_presetDir / (std::string(DEFAULT_PRESET_NAME) + ".ini");
    
    if (!fs::exists(defaultPreset)) {
        std::string error;
        if (!SavePreset(DEFAULT_PRESET_NAME, "Default settings configuration", &error)) {
            LOG_ERROR("[PresetManager] Failed to create default preset: " + error);
        }
    }
} 