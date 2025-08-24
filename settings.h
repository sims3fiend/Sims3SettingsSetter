#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <variant>
#include <vector>

struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Vector4 { float x, y, z, w; };

struct SettingMetadata {
    std::wstring name;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.1f;
    std::wstring category;
    std::wstring description;
    std::wstring defaultValue;
    bool isReadOnly = false;
};

class Setting {
public:
    using ValueType = std::variant<bool, int, unsigned int, float, Vector2, Vector3, Vector4>;
    
    Setting(void* address, const SettingMetadata& metadata, ValueType defaultValue)
        : m_address(address)
        , m_metadata(metadata)
        , m_defaultValue(defaultValue)
        , m_isOverridden(false)
        , m_hasUnsavedChanges(false)
    {}
    
    void* GetAddress() const { return m_address; }
    SettingMetadata& GetMetadata() { return m_metadata; }
    const SettingMetadata& GetMetadata() const { return m_metadata; }
    ValueType GetValue() const;
    void SetValue(const ValueType& value);
    const ValueType& GetDefaultValue() const { return m_defaultValue; }
    
    bool IsOverridden() const { return m_isOverridden; }
    void SetOverridden(bool overridden) { m_isOverridden = overridden; }
    
    bool HasUnsavedChanges() const { return m_hasUnsavedChanges; }
    void SetUnsavedChanges(bool unsaved) { m_hasUnsavedChanges = unsaved; }

private:
    void* m_address;
    SettingMetadata m_metadata;
    ValueType m_defaultValue;
    bool m_isOverridden;
    bool m_hasUnsavedChanges;
};

// Add this struct to store both value and override state
struct PendingSetting {
    Setting::ValueType value;
    bool isOverridden = true;
};

// Add near the top with other enums
enum class ConfigValueType {
    Integer,
    Float,
    Boolean,
    WideString,
    String,
    Unknown
};

// Update the existing ConfigValueInfo struct
struct ConfigValueInfo {
    std::wstring category;
    std::wstring currentValue;
    size_t bufferSize;
    ConfigValueType valueType;
    bool isModified = false;
};

class SettingsManager {
public:
    static SettingsManager& Get();
    
    void RegisterSetting(const std::wstring& name, std::unique_ptr<Setting> setting);
    Setting* GetSetting(const std::wstring& name);
    const std::unordered_map<std::wstring, std::unique_ptr<Setting>>& GetAllSettings() const;
    
    void LoadConfig(const std::string& filename);
    void SaveConfig(const std::string& filename) const;

    void SetSettingCategory(const std::wstring& name, const std::wstring& category);
    std::vector<std::wstring> GetUniqueCategories() const;

    void AddConfigSetting(const std::wstring& name, const std::wstring& category);
    const std::unordered_map<std::wstring, std::wstring>& GetConfigSettings() const;

    struct ConfigSettingInfo {
        std::wstring category;
        std::wstring description;
        std::wstring defaultValue;
        bool isReadOnly = false;
    };
    
    void AddConfigSettingInfo(const std::wstring& name, const ConfigSettingInfo& info);
    const std::unordered_map<std::wstring, ConfigSettingInfo>& GetConfigSettingsInfo() const;

    bool SaveConfig(const std::string& filename, std::string* error = nullptr) const;
    bool LoadConfig(const std::string& filename, std::string* error = nullptr);

    void StorePendingSavedValue(const std::wstring& name, const Setting::ValueType& value);
    void ApplyPendingSavedValue(const std::wstring& name);

    void AddConfigValue(const std::wstring& name, const ConfigValueInfo& info);
    const std::unordered_map<std::wstring, ConfigValueInfo>& GetConfigValues() const;
    bool UpdateConfigValue(const std::wstring& name, const std::wstring& newValue);

    // New methods for default values
    bool SaveDefaultValues(const std::string& filename, std::string* error = nullptr);
    bool LoadDefaultValues(const std::string& filename, std::string* error = nullptr);
    void ResetSettingToDefault(const std::wstring& name);
    void ResetAllSettings();
    bool HasDefaultValues() const { return !m_defaultValues.empty(); }
    bool IsInitialized() const { return m_initialized; }
    void SetInitialized(bool initialized) { m_initialized = initialized; }

    // Manual initialization method
    void ManualInitialize();

private:
    SettingsManager() : m_initialized(false) {}
    std::unordered_map<std::wstring, std::unique_ptr<Setting>> m_settings;
    std::unordered_map<std::wstring, std::wstring> m_configSettings;
    std::unordered_map<std::wstring, ConfigSettingInfo> m_configSettingsInfo;
    std::unordered_map<std::wstring, PendingSetting> m_pendingSavedValues;
    std::unordered_map<std::wstring, ConfigValueInfo> m_configValues;
    
    // Map to store default values for each setting
    std::unordered_map<std::wstring, Setting::ValueType> m_defaultValues;
    bool m_initialized;
};
//operator overloads
inline bool operator!=(const Vector2& a, const Vector2& b) {
    return a.x != b.x || a.y != b.y;
}

inline bool operator!=(const Vector3& a, const Vector3& b) {
    return a.x != b.x || a.y != b.y || a.z != b.z;
}

inline bool operator!=(const Vector4& a, const Vector4& b) {
    return a.x != b.x || a.y != b.y || a.z != b.z || a.w != b.w;
}