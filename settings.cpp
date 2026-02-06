#include <algorithm>
#include <optional>
#include <unordered_set>
#include "settings.h"
#include "utils.h"
#include "config/config_value_manager.h"
#include "config/config_store.h"
#include "logger.h"
#include <toml++/toml.hpp>
//File theme: https://www.youtube.com/watch?v=i2vctV4x5aQ

Setting::ValueType Setting::GetValue() const {
    return std::visit(
        [this](auto&& value) -> ValueType {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                return *static_cast<bool*>(m_address);
            } else if constexpr (std::is_same_v<T, int>) {
                return *static_cast<int*>(m_address);
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                return *static_cast<unsigned int*>(m_address);
            } else if constexpr (std::is_same_v<T, float>) {
                return *static_cast<float*>(m_address);
            } else if constexpr (std::is_same_v<T, Vector2>) {
                float* ptr = static_cast<float*>(m_address);
                return Vector2{ptr[0], ptr[1]};
            } else if constexpr (std::is_same_v<T, Vector3>) {
                float* ptr = static_cast<float*>(m_address);
                return Vector3{ptr[0], ptr[1], ptr[2]};
            } else if constexpr (std::is_same_v<T, Vector4>) {
                float* ptr = static_cast<float*>(m_address);
                return Vector4{ptr[0], ptr[1], ptr[2], ptr[3]};
            }
        },
        m_defaultValue);
}

void Setting::SetValue(const ValueType& value) {
    std::visit(
        [this](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                *static_cast<bool*>(m_address) = val;
            } else if constexpr (std::is_same_v<T, int>) {
                *static_cast<int*>(m_address) = val;
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                *static_cast<unsigned int*>(m_address) = val;
            } else if constexpr (std::is_same_v<T, float>) {
                *static_cast<float*>(m_address) = val;
            } else if constexpr (std::is_same_v<T, Vector2>) {
                float* ptr = static_cast<float*>(m_address);
                ptr[0] = val.x;
                ptr[1] = val.y;
            } else if constexpr (std::is_same_v<T, Vector3>) {
                float* ptr = static_cast<float*>(m_address);
                ptr[0] = val.x;
                ptr[1] = val.y;
                ptr[2] = val.z;
            } else if constexpr (std::is_same_v<T, Vector4>) {
                float* ptr = static_cast<float*>(m_address);
                ptr[0] = val.x;
                ptr[1] = val.y;
                ptr[2] = val.z;
                ptr[3] = val.w;
            }
        },
        value);
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

void SettingsManager::SetSettingCategory(const std::wstring& name, const std::wstring& category) {
    auto it = m_settings.find(name);
    if (it != m_settings.end()) { it->second->GetMetadata().category = category; }
}

std::vector<std::wstring> SettingsManager::GetUniqueCategories() const {
    std::vector<std::wstring> categories;
    std::unordered_set<std::wstring> uniqueCategories;

    for (const auto& [name, setting] : m_settings) {
        const auto& category = setting->GetMetadata().category;
        if (!category.empty() && uniqueCategories.insert(category).second) { categories.push_back(category); }
    }

    // Sort categories alphabetically
    std::sort(categories.begin(), categories.end());

    return categories;
}

void SettingsManager::StorePendingSavedValue(const std::wstring& name, const Setting::ValueType& value) {
    m_pendingSavedValues[name].value = value;
    m_pendingSavedValues[name].isOverridden = true;

    // Warn if pending values grow unexpectedly large; log on threshold and each doubling to avoid spam
    static size_t s_nextWarnAt = 100; // initial threshold
    const size_t currentSize = m_pendingSavedValues.size();
    if (currentSize >= s_nextWarnAt) {
        LOG_WARNING("[SettingsManager] pendingSavedValues size=" + std::to_string(currentSize) + ", possible obsolete or unknown settings in config");
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

            std::visit(
                [&](auto&& targetDefault) {
                    using TargetT = std::decay_t<decltype(targetDefault)>;

                    // Try to convert from pending value to target type
                    std::visit(
                        [&](auto&& pendingVal) {
                            using PendingT = std::decay_t<decltype(pendingVal)>;

                            if constexpr (std::is_same_v<PendingT, TargetT>) {
                                // Types match, direct assignment
                                convertedValue = pendingVal;
                                conversionSuccess = true;
                            } else if constexpr (std::is_same_v<TargetT, int>) {
                                // Convert to int
                                if constexpr (std::is_same_v<PendingT, float>) {
                                    convertedValue = static_cast<int>(pendingVal);
                                    conversionSuccess = true;
                                } else if constexpr (std::is_same_v<PendingT, unsigned int>) {
                                    convertedValue = static_cast<int>(pendingVal);
                                    conversionSuccess = true;
                                }
                            } else if constexpr (std::is_same_v<TargetT, unsigned int>) {
                                // Convert to unsigned int
                                if constexpr (std::is_same_v<PendingT, float>) {
                                    convertedValue = static_cast<unsigned int>(pendingVal);
                                    conversionSuccess = true;
                                } else if constexpr (std::is_same_v<PendingT, int>) {
                                    convertedValue = static_cast<unsigned int>(pendingVal);
                                    conversionSuccess = true;
                                }
                            } else if constexpr (std::is_same_v<TargetT, float>) {
                                // Convert to float
                                if constexpr (std::is_same_v<PendingT, int>) {
                                    convertedValue = static_cast<float>(pendingVal);
                                    conversionSuccess = true;
                                } else if constexpr (std::is_same_v<PendingT, unsigned int>) {
                                    convertedValue = static_cast<float>(pendingVal);
                                    conversionSuccess = true;
                                }
                            }
                        },
                        it->second.value);
                },
                setting->GetDefaultValue());

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

// TOML Serialization
namespace {

// Write a Setting::ValueType into a toml::table entry under the key "value".
void InsertValueToToml(toml::table& entry, const Setting::ValueType& value) {
    std::visit(
        [&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>) {
                entry.insert("value", v);
            } else if constexpr (std::is_same_v<T, int>) {
                entry.insert("value", static_cast<int64_t>(v));
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                entry.insert("value", static_cast<int64_t>(v));
            } else if constexpr (std::is_same_v<T, float>) {
                entry.insert("value", static_cast<double>(v));
            } else if constexpr (std::is_same_v<T, Vector2>) {
                entry.insert("value", toml::array{static_cast<double>(v.x), static_cast<double>(v.y)});
            } else if constexpr (std::is_same_v<T, Vector3>) {
                entry.insert("value", toml::array{static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)});
            } else if constexpr (std::is_same_v<T, Vector4>) {
                entry.insert("value", toml::array{static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z), static_cast<double>(v.w)});
            }
        },
        value);
}

// Read a TOML "value" node into a ValueType, guided by the target type from a default value.
// Returns std::nullopt if the node can't be converted (e.g. array too short).
std::optional<Setting::ValueType> ReadValueFromToml(const toml::node_view<const toml::node>& valueNode, const Setting::ValueType& targetDefault) {
    Setting::ValueType result;
    bool ok = false;

    std::visit(
        [&](auto&& defaultVal) {
            using T = std::decay_t<decltype(defaultVal)>;
            if constexpr (std::is_same_v<T, bool>) {
                result = valueNode.value_or(false);
                ok = true;
            } else if constexpr (std::is_same_v<T, int>) {
                result = static_cast<int>(valueNode.value_or(int64_t(0)));
                ok = true;
            } else if constexpr (std::is_same_v<T, unsigned int>) {
                result = static_cast<unsigned int>(valueNode.value_or(int64_t(0)));
                ok = true;
            } else if constexpr (std::is_same_v<T, float>) {
                result = static_cast<float>(valueNode.value_or(0.0));
                ok = true;
            } else if constexpr (std::is_same_v<T, Vector2>) {
                if (auto arr = valueNode.as_array(); arr && arr->size() >= 2) {
                    result = Vector2{static_cast<float>((*arr)[0].value_or(0.0)), static_cast<float>((*arr)[1].value_or(0.0))};
                    ok = true;
                }
            } else if constexpr (std::is_same_v<T, Vector3>) {
                if (auto arr = valueNode.as_array(); arr && arr->size() >= 3) {
                    result = Vector3{static_cast<float>((*arr)[0].value_or(0.0)), static_cast<float>((*arr)[1].value_or(0.0)), static_cast<float>((*arr)[2].value_or(0.0))};
                    ok = true;
                }
            } else if constexpr (std::is_same_v<T, Vector4>) {
                if (auto arr = valueNode.as_array(); arr && arr->size() >= 4) {
                    result = Vector4{static_cast<float>((*arr)[0].value_or(0.0)), static_cast<float>((*arr)[1].value_or(0.0)), static_cast<float>((*arr)[2].value_or(0.0)), static_cast<float>((*arr)[3].value_or(0.0))};
                    ok = true;
                }
            }
        },
        targetDefault);

    if (ok) return result;
    return std::nullopt;
}

// Infer a ValueType from a TOML node's own type (for pending values where we don't know the target type yet).
std::optional<Setting::ValueType> InferValueFromToml(const toml::node_view<const toml::node>& valueNode) {
    if (valueNode.is_boolean()) {
        return valueNode.value_or(false);
    } else if (valueNode.is_array()) {
        auto arr = valueNode.as_array();
        if (arr) {
            if (arr->size() == 2) {
                return Vector2{static_cast<float>((*arr)[0].value_or(0.0)), static_cast<float>((*arr)[1].value_or(0.0))};
            } else if (arr->size() == 3) {
                return Vector3{static_cast<float>((*arr)[0].value_or(0.0)), static_cast<float>((*arr)[1].value_or(0.0)), static_cast<float>((*arr)[2].value_or(0.0))};
            } else if (arr->size() == 4) {
                return Vector4{static_cast<float>((*arr)[0].value_or(0.0)), static_cast<float>((*arr)[1].value_or(0.0)), static_cast<float>((*arr)[2].value_or(0.0)), static_cast<float>((*arr)[3].value_or(0.0))};
            }
        }
    } else if (valueNode.is_floating_point()) {
        return static_cast<float>(valueNode.value_or(0.0));
    } else if (valueNode.is_integer()) {
        return static_cast<int>(valueNode.value_or(int64_t(0)));
    }
    return std::nullopt;
}

} // namespace

void SettingsManager::SaveToToml(toml::table& root) const {
    toml::table settingsTable;

    for (const auto& [name, setting] : m_settings) {
        if (!setting->IsOverridden()) { continue; }

        std::string settingName = Utils::WideToUtf8(name);
        toml::table entry;

        InsertValueToToml(entry, setting->GetValue());

        settingsTable.insert(settingName, std::move(entry));

        // Mark as saved (clears the UI "unsaved" indicator)
        setting->SetUnsavedChanges(false);
    }

    if (!settingsTable.empty()) { root.insert("settings", std::move(settingsTable)); }
}

void SettingsManager::LoadFromToml(const toml::table& root) {
    auto settingsNode = root["settings"].as_table();
    if (!settingsNode) { return; }

    int settingsCount = 0;
    for (const auto& [key, node] : *settingsNode) {
        auto* entryTable = node.as_table();
        if (!entryTable) continue;

        std::wstring settingName = Utils::Utf8ToWide(std::string(key.str()));
        auto valueNode = (*entryTable)["value"];

        auto* setting = GetSetting(settingName);
        if (setting) {
            try {
                auto parsed = ReadValueFromToml(valueNode, setting->GetDefaultValue());
                if (parsed) {
                    setting->SetValue(*parsed);
                    setting->SetOverridden(true);
                    settingsCount++;
                }
            } catch (const std::exception& e) { LOG_WARNING("[SettingsManager] Error loading TOML value for " + Utils::WideToUtf8(settingName) + ": " + e.what()); }
        } else {
            // Setting not registered yet, store as pending
            try {
                auto inferred = InferValueFromToml(valueNode);
                if (inferred) { StorePendingSavedValue(settingName, *inferred); }
            } catch (const std::exception& e) { LOG_WARNING("[SettingsManager] Failed to parse pending TOML value for " + Utils::WideToUtf8(settingName) + ": " + e.what()); }
        }
    }

    LOG_INFO("[SettingsManager] Loaded " + std::to_string(settingsCount) + " settings from TOML");
}

void SettingsManager::SaveDefaultsToToml(toml::table& root) {
    toml::table settingsTable;

    for (const auto& [name, setting] : m_settings) {
        std::string settingName = Utils::WideToUtf8(name);
        toml::table entry;

        InsertValueToToml(entry, setting->GetValue());

        settingsTable.insert(settingName, std::move(entry));

        // Also store in memory
        m_defaultValues[name] = setting->GetValue();
    }

    root.insert("settings", std::move(settingsTable));
}

void SettingsManager::LoadDefaultsFromToml(const toml::table& root) {
    auto settingsNode = root["settings"].as_table();
    if (!settingsNode) { return; }

    m_defaultValues.clear();

    for (const auto& [key, node] : *settingsNode) {
        auto* entryTable = node.as_table();
        if (!entryTable) continue;

        std::wstring settingName = Utils::Utf8ToWide(std::string(key.str()));
        auto valueNode = (*entryTable)["value"];

        auto* setting = GetSetting(settingName);
        if (!setting) continue;

        try {
            auto parsed = ReadValueFromToml(valueNode, setting->GetDefaultValue());
            if (parsed) { m_defaultValues[settingName] = *parsed; }
        } catch (const std::exception& e) { LOG_WARNING("[SettingsManager] Error parsing default TOML value for " + Utils::WideToUtf8(settingName) + ": " + e.what()); }
    }
}

void SettingsManager::ManualInitialize() {
    if (m_initialized) {
        LOG_DEBUG("Settings already initialized, ignoring manual initialization request");
        return;
    }

    LOG_INFO("Manual initialization requested by user");

    // Set initialized flag
    m_initialized = true;

    // Save default settings via ConfigStore
    std::string error;
    if (!ConfigStore::Get().SaveDefaults(&error)) {
        LOG_ERROR("Failed to save default settings during manual init: " + error);
    } else {
        LOG_INFO("Successfully saved default settings during manual init");
    }

    LOG_INFO("Manual initialization completed");
}

bool SettingsManager::HasAnyUnsavedChanges() const {
    for (const auto& [name, setting] : m_settings) {
        if (setting->HasUnsavedChanges()) { return true; }
    }
    return false;
}