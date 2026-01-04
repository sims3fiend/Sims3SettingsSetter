#include <algorithm>
#include <unordered_set>
#include <set>
#include "settings.h"
#include <fstream>
#include <sstream>
#include <iomanip> 
#include <chrono>
#include <ctime>
#include "utils.h"
#include "config_value_cache.h"
#include "logger.h"

// Helper function to write a vector to string
template<typename T>
std::string VectorToString(const T& vec) {
    std::stringstream ss;
    if constexpr (std::is_same_v<T, Vector2>) {
        ss << vec.x << "," << vec.y;
    }
    else if constexpr (std::is_same_v<T, Vector3>) {
        ss << vec.x << "," << vec.y << "," << vec.z;
    }
    else if constexpr (std::is_same_v<T, Vector4>) {
        ss << vec.x << "," << vec.y << "," << vec.z << "," << vec.w;
    }
    return ss.str();
}

// Helper function to parse a vector from string
template<typename T>
T StringToVector(const std::string& str) {
    std::stringstream ss(str);
    std::string item;
    std::vector<float> values;
    
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        size_t start = item.find_first_not_of(" \t\r\n");
        size_t end = item.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue; // skip empty tokens
        }
        std::string token = item.substr(start, end - start + 1);
        try {
            values.push_back(std::stof(token));
        } catch (...) {
            // Ignore malformed component
        }
    }
    
    if constexpr (std::is_same_v<T, Vector2>) {
        return values.size() >= 2 ? Vector2{values[0], values[1]} : Vector2{0, 0};
    }
    else if constexpr (std::is_same_v<T, Vector3>) {
        return values.size() >= 3 ? Vector3{values[0], values[1], values[2]} : Vector3{0, 0, 0};
    }
    else if constexpr (std::is_same_v<T, Vector4>) {
        return values.size() >= 4 ? Vector4{values[0], values[1], values[2], values[3]} : Vector4{0, 0, 0, 0};
    }
}

// 
Setting::ValueType Setting::GetValue() const {
    return std::visit([this](auto&& value) -> ValueType {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
            return *static_cast<bool*>(m_address);
        }
        else if constexpr (std::is_same_v<T, int>) {
            return *static_cast<int*>(m_address);
        }
        else if constexpr (std::is_same_v<T, unsigned int>) {
            return *static_cast<unsigned int*>(m_address);
        }
        else if constexpr (std::is_same_v<T, float>) {
            return *static_cast<float*>(m_address);
        }
        else if constexpr (std::is_same_v<T, Vector2>) {
            float* ptr = static_cast<float*>(m_address);
            return Vector2{ptr[0], ptr[1]};
        }
        else if constexpr (std::is_same_v<T, Vector3>) {
            float* ptr = static_cast<float*>(m_address);
            return Vector3{ptr[0], ptr[1], ptr[2]};
        }
        else if constexpr (std::is_same_v<T, Vector4>) {
            float* ptr = static_cast<float*>(m_address);
            return Vector4{ptr[0], ptr[1], ptr[2], ptr[3]};
        }
    }, m_defaultValue);
}

void Setting::SetValue(const ValueType& value) {
    std::visit([this](auto&& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>) {
            *static_cast<bool*>(m_address) = val;
        }
        else if constexpr (std::is_same_v<T, int>) {
            *static_cast<int*>(m_address) = val;
        }
        else if constexpr (std::is_same_v<T, unsigned int>) {
            *static_cast<unsigned int*>(m_address) = val;
        }
        else if constexpr (std::is_same_v<T, float>) {
            *static_cast<float*>(m_address) = val;
        }
        else if constexpr (std::is_same_v<T, Vector2>) {
            float* ptr = static_cast<float*>(m_address);
            ptr[0] = val.x;
            ptr[1] = val.y;
        }
        else if constexpr (std::is_same_v<T, Vector3>) {
            float* ptr = static_cast<float*>(m_address);
            ptr[0] = val.x;
            ptr[1] = val.y;
            ptr[2] = val.z;
        }
        else if constexpr (std::is_same_v<T, Vector4>) {
            float* ptr = static_cast<float*>(m_address);
            ptr[0] = val.x;
            ptr[1] = val.y;
            ptr[2] = val.z;
            ptr[3] = val.w;
        }
    }, value);
}

SettingsManager& SettingsManager::Get() {
    static SettingsManager instance;
    return instance;
}

void SettingsManager::RegisterSetting(const std::wstring& name, std::unique_ptr<Setting> setting) {
    m_settings[name] = std::move(setting);
    
    // Try to apply any pending saved value for this setting
    ApplyPendingSavedValue(name);
}

Setting* SettingsManager::GetSetting(const std::wstring& name) {
    auto it = m_settings.find(name);
    return it != m_settings.end() ? it->second.get() : nullptr;
}

const std::unordered_map<std::wstring, std::unique_ptr<Setting>>& SettingsManager::GetAllSettings() const {
    return m_settings;
}

bool SettingsManager::SaveConfig(const std::string& filename, std::string* error) const {
    try {
        // Debug logging
        LOG_DEBUG("[SettingsManager] Begin SaveConfig");

        // Read existing content to preserve settings we're not rewriting
        std::vector<std::string> preservedSettingsContent;  // Settings not being rewritten
        std::set<std::string> settingsToWrite;  // Track which settings we'll write from m_settings

        // Build set of settings we're going to write
        for (const auto& [name, setting] : m_settings) {
            if (setting->HasUnsavedChanges() && setting->IsOverridden()) {
                settingsToWrite.insert(Utils::WideToUtf8(name));
            }
        }

        {
            std::ifstream inFile(filename);
            if (inFile.is_open()) {
                std::string line;
                bool inUnknownSetting = false;
                std::string currentSettingName;
                std::vector<std::string> currentSettingLines;

                while (std::getline(inFile, line)) {
                    // Stop when we hit patch/optimization section - those are managed by OptimizationManager
                    if (line == "; Patch Settings" || line == "; Optimization Settings") {
                        // Flush any pending setting before stopping
                        if (inUnknownSetting && !currentSettingLines.empty()) {
                            for (const auto& l : currentSettingLines) {
                                preservedSettingsContent.push_back(l);
                            }
                            preservedSettingsContent.push_back("");  // Empty line separator
                        }
                        break;  // Stop reading - OptimizationManager handles the rest
                    }

                    // Check for setting section header [SettingName]
                    if (!line.empty() && line[0] == '[' && line.back() == ']') {
                        // Flush previous unknown setting if any
                        if (inUnknownSetting && !currentSettingLines.empty()) {
                            for (const auto& l : currentSettingLines) {
                                preservedSettingsContent.push_back(l);
                            }
                            preservedSettingsContent.push_back("");  // Empty line separator
                        }

                        currentSettingName = line.substr(1, line.size() - 2);
                        currentSettingLines.clear();
                        currentSettingLines.push_back(line);

                        // Determine if we should preserve this setting from the INI
                        // Skip (don't preserve) if we're gunna write it fresh (in settingsToWrite) or Config: section (handled separately under)
                        // Preserve if it's not in settingsToWrite (either unknown or known but unchanged)
                        if (currentSettingName.find("Config:") == 0) {
                            // Config sections are handled separately, don't preserve
                            inUnknownSetting = false;
                        } else if (settingsToWrite.find(currentSettingName) != settingsToWrite.end()) {
                            // We're writing this fresh, don't preserve old version
                            inUnknownSetting = false;
                        } else {
                            // Preserve this setting - either unknown or known but not being rewritten
                            inUnknownSetting = true;
                        }
                    }
                    else if (inUnknownSetting) {
                        // Continue collecting lines for unknown setting
                        if (!line.empty()) {
                            currentSettingLines.push_back(line);
                        }
                    }
                }

                // Flush final unknown setting if any
                if (inUnknownSetting && !currentSettingLines.empty()) {
                    for (const auto& l : currentSettingLines) {
                        preservedSettingsContent.push_back(l);
                    }
                    preservedSettingsContent.push_back("");
                }
            }
        }

        std::ofstream file(filename);
        if (!file.is_open()) {
            if (error) *error = "Failed to open file for writing: " + filename;
            LOG_ERROR("[SettingsManager] Failed to open settings file for writing: " + filename);
            return false;
        }

        // Write header with timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        file << "; Sims 3 Settings Configuration\n";
        file << "; Saved on: " << std::ctime(&time);
        file << "; Format: [SettingName]\nValue=value\n\n";

        int savedCount = 0;

        // Write preserved settings that aren't in m_settings (from previous saves)
        if (!preservedSettingsContent.empty()) {
            for (const auto& line : preservedSettingsContent) {
                file << line << "\n";
            }
            LOG_DEBUG("[SettingsManager] Preserved " + std::to_string(preservedSettingsContent.size()) + " lines of unknown settings");
        }

        for (const auto& [name, setting] : m_settings) {
            // Debug log current setting status
            std::string settingName = Utils::WideToUtf8(name);

            // Only save settings that have unsaved changes
            if (!setting->HasUnsavedChanges()) {
                // skip log
                continue;
            }

            // If override was explicitly cleared, skip saving it
            if (!setting->IsOverridden()) {
                setting->SetUnsavedChanges(false); // Clear the unsaved flag
                // skip log
                continue;
            }

            // Log saving this setting
            // skip per-setting log
            savedCount++;

            file << "[" << settingName << "]\n";

            // Write category if available
            const auto& metadata = setting->GetMetadata();
            if (!metadata.category.empty()) {
                file << "Category=" << Utils::WideToUtf8(metadata.category) << "\n";
            }

            // Write value
            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                file << "Value=";
                if constexpr (std::is_same_v<T, bool>) {
                    file << (value ? "true" : "false");
                }
                else if constexpr (std::is_same_v<T, float>) {
                    file << std::fixed << std::setprecision(6) << value;
                }
                else if constexpr (std::is_same_v<T, Vector2> ||
                                 std::is_same_v<T, Vector3> ||
                                 std::is_same_v<T, Vector4>) {
                    file << VectorToString(value);
                }
                else {
                    file << value; // For int, unsigned int
                }
                file << "\n";
            }, setting->GetValue());

            // Write metadata
            file << "Min=" << metadata.min << "\n";
            file << "Max=" << metadata.max << "\n";
            file << "Step=" << metadata.step << "\n\n";

            // Mark as saved and overridden
            setting->SetUnsavedChanges(false);
            setting->SetOverridden(true);
        }

        LOG_INFO("[SettingsManager] Saved " + std::to_string(savedCount) + " settings");

        // Save modified config values
        int configCount = 0;
        for (const auto& [name, info] : m_configValues) {
            // Only save if the value has been modified from what the game provided
            if (info.isModified) {
                configCount++;
                std::string settingName = Utils::WideToUtf8(name);
                file << "[Config:" << settingName << "]\n";
                file << "Value=" << Utils::WideToUtf8(info.currentValue) << "\n\n";
            }
        }
        
        LOG_INFO("[SettingsManager] Saved " + std::to_string(configCount) + " config values");

        // Patch/Optimization sections are NOT written here - they are managed by OptimizationManager::SaveState() which "should" be called after SaveConfig()

        LOG_DEBUG("[SettingsManager] End SaveConfig");

        return true;
    }
    catch (const std::exception& e) {
        if (error) *error = "Error saving config: " + std::string(e.what());
        LOG_ERROR(std::string("[SettingsManager] Error saving config: ") + e.what());
        return false;
    }
}

bool SettingsManager::LoadConfig(const std::string& filename, std::string* error) {
    try {
        LOG_INFO("[SettingsManager] Loading from: " + filename);
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            if (error) *error = "Failed to open file: " + filename;
            LOG_ERROR("[SettingsManager] Failed to open file: " + filename);
            return false;
        }

        int settingsCount = 0;
        int configCount = 0;
        std::string line;
        std::wstring currentSetting;
        bool inConfigSection = false;
        ConfigValueInfo currentConfigInfo;
        
        // omit verbose parse begin log
        
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == ';') continue;

            // Check for section header
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    std::string section = line.substr(1, end - 1);
                    
                    // Check if this is a config section
                    if (section.substr(0, 7) == "Config:") {
                        inConfigSection = true;
                        currentSetting = Utils::Utf8ToWide(section.substr(7));
                        currentConfigInfo = ConfigValueInfo();
                        // Mark as modified since it's coming from saved file
                        currentConfigInfo.isModified = true;
                        // omit verbose per-section log
                    } else {
                        inConfigSection = false;
                        currentSetting = Utils::Utf8ToWide(section);
                        // omit verbose per-section log
                    }
                }
                continue;
            }

            // Parse key=value pairs
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos && !currentSetting.empty()) {
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                if (inConfigSection) {
                    if (key == "Value") {
                        currentConfigInfo.currentValue = Utils::Utf8ToWide(value);
                        // Find existing config info to preserve other fields
                        auto it = m_configValues.find(currentSetting);
                        if (it != m_configValues.end()) {
                            currentConfigInfo.category = it->second.category;
                            currentConfigInfo.bufferSize = it->second.bufferSize;
                            currentConfigInfo.valueType = it->second.valueType;
                        }
                        AddConfigValue(currentSetting, currentConfigInfo);
                        configCount++;
                        // omit verbose per-value log
                    }
                } else if (key == "Value") {
                    // Try to apply immediately if setting exists
                    auto* setting = GetSetting(currentSetting);
                    if (setting) {
                        try {
                            settingsCount++;
                            // omit verbose per-setting log
                            
                            std::visit([&](auto&& defaultVal) {
                                using T = std::decay_t<decltype(defaultVal)>;
                                Setting::ValueType newValue;
                                
                                if constexpr (std::is_same_v<T, bool>) {
                                    newValue = value == "true";
                                }
                else if constexpr (std::is_same_v<T, float>) {
                    newValue = std::stof(value);
                                }
                                else if constexpr (std::is_same_v<T, Vector2>) {
                                    newValue = StringToVector<Vector2>(value);
                                }
                                else if constexpr (std::is_same_v<T, Vector3>) {
                                    newValue = StringToVector<Vector3>(value);
                                }
                                else if constexpr (std::is_same_v<T, Vector4>) {
                                    newValue = StringToVector<Vector4>(value);
                                }
                                else if constexpr (std::is_same_v<T, int>) {
                                    newValue = std::stoi(value);
                                }
                                else if constexpr (std::is_same_v<T, unsigned int>) {
                                    newValue = static_cast<unsigned int>(std::stoul(value));
                                }
                                
                                // Set value and mark as overridden
                                setting->SetValue(newValue);
                                setting->SetOverridden(true);
                                
                            }, setting->GetDefaultValue());
                        }
                        catch (const std::exception& e) {
                            LOG_WARNING(std::string("[SettingsManager] Error parsing value for ") + Utils::WideToUtf8(currentSetting) + ": " + e.what());
                        }
                    } else {
                        // If setting doesn't exist yet, try to parse and store the value
                        // omit verbose pending value log
                        try {
                            // Try each possible type
                            if (value == "true" || value == "false") {
                                StorePendingSavedValue(currentSetting, value == "true");
                            } else if (value.find(',') != std::string::npos) {
                                // Attempt to parse as vector
                                auto parts = Utils::SplitString(value, ',');
                                if (parts.size() == 2) {
                                    StorePendingSavedValue(currentSetting, StringToVector<Vector2>(value));
                                } else if (parts.size() == 3) {
                                    StorePendingSavedValue(currentSetting, StringToVector<Vector3>(value));
                                } else if (parts.size() == 4) {
                                    StorePendingSavedValue(currentSetting, StringToVector<Vector4>(value));
                                }
                            } else {
                                // Try as float first, then fallback to int
                                try {
                                    StorePendingSavedValue(currentSetting, std::stof(value));
                                } catch (...) {
                                    try {
                                        StorePendingSavedValue(currentSetting, std::stoi(value));
                                    } catch (...) {
                                        LOG_WARNING("[SettingsManager] Could not parse value: " + value);
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            LOG_WARNING(std::string("[SettingsManager] Failed to parse pending value for ") + Utils::WideToUtf8(currentSetting) + ": " + e.what());
                        }
                    }
                }
            }
        }

        LOG_INFO("[SettingsManager] Applied " + std::to_string(settingsCount) + " settings and " + std::to_string(configCount) + " config values");
        return true;
    }
    catch (const std::exception& e) {
        if (error) *error = "Error loading config: " + std::string(e.what());
        LOG_ERROR(std::string("[SettingsManager] Error loading config: ") + e.what());
        return false;
    }
}

void SettingsManager::SetSettingCategory(const std::wstring& name, const std::wstring& category) {
    auto it = m_settings.find(name);
    if (it != m_settings.end()) {
        it->second->GetMetadata().category = category;
    }
}

std::vector<std::wstring> SettingsManager::GetUniqueCategories() const {
    std::vector<std::wstring> categories;
    std::unordered_set<std::wstring> uniqueCategories;

    for (const auto& [name, setting] : m_settings) {
        const auto& category = setting->GetMetadata().category;
        if (!category.empty() && uniqueCategories.insert(category).second) {
            categories.push_back(category);
        }
    }

    // Sort categories alphabetically
    std::sort(categories.begin(), categories.end());
    
    return categories;
}

void SettingsManager::AddConfigSetting(const std::wstring& name, const std::wstring& category) {
    m_configSettings[name] = category;
}

const std::unordered_map<std::wstring, std::wstring>& SettingsManager::GetConfigSettings() const {
    return m_configSettings;
}

void SettingsManager::AddConfigSettingInfo(const std::wstring& name, const ConfigSettingInfo& info) {
    m_configSettingsInfo[name] = info;
}

const std::unordered_map<std::wstring, SettingsManager::ConfigSettingInfo>& 
SettingsManager::GetConfigSettingsInfo() const {
    return m_configSettingsInfo;
}

void SettingsManager::StorePendingSavedValue(const std::wstring& name, const Setting::ValueType& value) {
    m_pendingSavedValues[name].value = value;
    m_pendingSavedValues[name].isOverridden = true;

    // Warn if pending values grow unexpectedly large; log on threshold and each doubling to avoid spam
    static size_t s_nextWarnAt = 100; // initial threshold
    const size_t currentSize = m_pendingSavedValues.size();
    if (currentSize >= s_nextWarnAt) {
        LOG_WARNING("[SettingsManager] pendingSavedValues size=" + std::to_string(currentSize) +
            ", possible obsolete or unknown settings in preset");
        if (s_nextWarnAt <= (SIZE_MAX / 2)) {
            s_nextWarnAt *= 2; // escalate threshold
        } else {
            s_nextWarnAt = SIZE_MAX; // stop escalating to avoid overflow
        }
    }
}

void SettingsManager::ApplyPendingSavedValue(const std::wstring& name) {
    auto it = m_pendingSavedValues.find(name);
    if (it != m_pendingSavedValues.end()) {
        if (auto* setting = GetSetting(name)) {
            // Convert pending value to match the registered setting's actual type YOU FOOL!!! AAAAAAAAAAAAAA
            //TODO: Will clean this up eventually, for now this is good enough, can't think of a better sollution
            Setting::ValueType convertedValue;
            bool conversionSuccess = false;

            std::visit([&](auto&& targetDefault) {
                using TargetT = std::decay_t<decltype(targetDefault)>;

                // Try to convert from pending value to target type
                std::visit([&](auto&& pendingVal) {
                    using PendingT = std::decay_t<decltype(pendingVal)>;

                    if constexpr (std::is_same_v<PendingT, TargetT>) {
                        // Types match, direct assignment
                        convertedValue = pendingVal;
                        conversionSuccess = true;
                    }
                    else if constexpr (std::is_same_v<TargetT, int>) {
                        // Convert to int
                        if constexpr (std::is_same_v<PendingT, float>) {
                            convertedValue = static_cast<int>(pendingVal);
                            conversionSuccess = true;
                        } else if constexpr (std::is_same_v<PendingT, unsigned int>) {
                            convertedValue = static_cast<int>(pendingVal);
                            conversionSuccess = true;
                        }
                    }
                    else if constexpr (std::is_same_v<TargetT, unsigned int>) {
                        // Convert to unsigned int
                        if constexpr (std::is_same_v<PendingT, float>) {
                            convertedValue = static_cast<unsigned int>(pendingVal);
                            conversionSuccess = true;
                        } else if constexpr (std::is_same_v<PendingT, int>) {
                            convertedValue = static_cast<unsigned int>(pendingVal);
                            conversionSuccess = true;
                        }
                    }
                    else if constexpr (std::is_same_v<TargetT, float>) {
                        // Convert to float
                        if constexpr (std::is_same_v<PendingT, int>) {
                            convertedValue = static_cast<float>(pendingVal);
                            conversionSuccess = true;
                        } else if constexpr (std::is_same_v<PendingT, unsigned int>) {
                            convertedValue = static_cast<float>(pendingVal);
                            conversionSuccess = true;
                        }
                    }
                }, it->second.value);
            }, setting->GetDefaultValue());

            if (conversionSuccess) {
                setting->SetValue(convertedValue);
                setting->SetOverridden(it->second.isOverridden);
            } else {
                LOG_WARNING("[SettingsManager] Type mismatch for pending value: " + Utils::WideToUtf8(name));
            }

            m_pendingSavedValues.erase(it);
        }
    }
}

void SettingsManager::AddConfigValue(const std::wstring& name, const ConfigValueInfo& info) {
    m_configValues[name] = info;
}

const std::unordered_map<std::wstring, ConfigValueInfo>& SettingsManager::GetConfigValues() const {
    return m_configValues;
}

bool SettingsManager::UpdateConfigValue(const std::wstring& name, const std::wstring& newValue) {
    auto it = m_configValues.find(name);
    if (it == m_configValues.end()) {
        return false;
    }

    // Create a unique key for the config value cache
    std::string fullKey = "Config." + Utils::WideToUtf8(name);
    
    // Update the value in our cache using the ConfigValueCache
    size_t minCapacity = it->second.bufferSize > 0 ? it->second.bufferSize : (newValue.size() + 1);
    ConfigValueCache::Instance().GetBuffer(fullKey, newValue, minCapacity);
    
    // Update our local copy as well
    it->second.currentValue = newValue;
    it->second.isModified = true;
    return true;
}

bool SettingsManager::SaveDefaultValues(const std::string& filename, std::string* error) {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            if (error) *error = "Failed to open file for writing: " + filename;
            return false;
        }

        // Write header with timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        file << "; Sims 3 Default Settings Configuration\n";
        file << "; Saved on: " << std::ctime(&time);
        file << "; Format: [SettingName]\nValue=value\n\n";

        // Save all current values as defaults
        for (const auto& [name, setting] : m_settings) {
            std::string settingName = Utils::WideToUtf8(name);
            
            file << "[" << settingName << "]\n";
            
            // Write value
            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                file << "Value=";
                if constexpr (std::is_same_v<T, bool>) {
                    file << (value ? "true" : "false");
                }
                else if constexpr (std::is_same_v<T, float>) {
                    file << std::fixed << std::setprecision(6) << value;
                }
                else if constexpr (std::is_same_v<T, Vector2> || 
                                 std::is_same_v<T, Vector3> || 
                                 std::is_same_v<T, Vector4>) {
                    file << VectorToString(value);
                }
                else {
                    file << value; // For int, unsigned int
                }
                file << "\n\n";
            }, setting->GetValue());
            
            // Also store in memory
            m_defaultValues[name] = setting->GetValue();
        }

        return true;
    }
    catch (const std::exception& e) {
        if (error) *error = "Error saving default values: " + std::string(e.what());
        return false;
    }
}

bool SettingsManager::LoadDefaultValues(const std::string& filename, std::string* error) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            if (error) *error = "Failed to open defaults file: " + filename;
            return false;
        }

        m_defaultValues.clear();
        
        std::string line;
        std::wstring currentSetting;
        
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == ';') continue;

            // Check for section header
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    std::string section = line.substr(1, end - 1);
                    currentSetting = Utils::Utf8ToWide(section);
                }
                continue;
            }

            // Parse key=value pairs
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos && !currentSetting.empty()) {
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                if (key == "Value") {
                    // Try to parse and store the value
                    try {
                        auto* setting = GetSetting(currentSetting);
                        if (setting) {
                            std::visit([&](auto&& defaultVal) {
                                using T = std::decay_t<decltype(defaultVal)>;
                                Setting::ValueType newValue;
                                
                                if constexpr (std::is_same_v<T, bool>) {
                                    newValue = value == "true";
                                }
                                else if constexpr (std::is_same_v<T, int>) {
                                    newValue = std::stoi(value);
                                }
                                else if constexpr (std::is_same_v<T, unsigned int>) {
                                    newValue = static_cast<unsigned int>(std::stoul(value));
                                }
                                else if constexpr (std::is_same_v<T, float>) {
                                    newValue = std::stof(value);
                                }
                                else if constexpr (std::is_same_v<T, Vector2>) {
                                    newValue = StringToVector<Vector2>(value);
                                }
                                else if constexpr (std::is_same_v<T, Vector3>) {
                                    newValue = StringToVector<Vector3>(value);
                                }
                                else if constexpr (std::is_same_v<T, Vector4>) {
                                    newValue = StringToVector<Vector4>(value);
                                }
                                
                                // Store the default value
                                m_defaultValues[currentSetting] = newValue;
                                
                            }, setting->GetDefaultValue());
                        }
                    }
                    catch (const std::exception& e) {
                        LOG_WARNING("Error parsing default value for " + 
                            Utils::WideToUtf8(currentSetting) + ": " + e.what());
                    }
                }
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        if (error) *error = "Error loading default values: " + std::string(e.what());
        return false;
    }
}

void SettingsManager::ResetSettingToDefault(const std::wstring& name) {
    auto settingIt = m_settings.find(name);
    auto defaultIt = m_defaultValues.find(name);
    
    if (settingIt != m_settings.end() && defaultIt != m_defaultValues.end()) {
        // Reset to the default value
        settingIt->second->SetValue(defaultIt->second);
        settingIt->second->SetUnsavedChanges(true);
        settingIt->second->SetOverridden(true);
        
        LOG_DEBUG("Reset setting " + Utils::WideToUtf8(name) + " to default value");
    }
}

void SettingsManager::ResetAllSettings() {
    for (const auto& [name, value] : m_defaultValues) {
        auto settingIt = m_settings.find(name);
        if (settingIt != m_settings.end()) {
            // Reset to the default value
            settingIt->second->SetValue(value);
            settingIt->second->SetUnsavedChanges(true);
            settingIt->second->SetOverridden(true);
        }
    }
    
    LOG_INFO("Reset all settings to default values");
} 

void SettingsManager::ManualInitialize() {
    if (m_initialized) {
        LOG_DEBUG("Settings already initialized, ignoring manual initialization request");
        return;
    }
    
    LOG_INFO("Manual initialization requested by user");
    
    // Set initialized flag
    m_initialized = true;
    
    // Save default settings to a file
    std::string error;
    if (!SaveDefaultValues("S3SS_defaults.ini", &error)) {
        LOG_ERROR("Failed to save default settings during manual init: " + error);
    }
    else {
        LOG_INFO("Successfully saved default settings during manual init");
    }
    
    LOG_INFO("Manual initialization completed");
} 