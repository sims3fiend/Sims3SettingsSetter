#include "config_value_manager.h"
#include "utils.h"
#include "logger.h"
#include <toml++/toml.hpp>

ConfigValueManager& ConfigValueManager::Get() {
    static ConfigValueManager instance;
    return instance;
}

void ConfigValueManager::AddConfigValue(const std::wstring& name, const ConfigValueInfo& info) {
    m_configValues[name] = info;
}

const std::unordered_map<std::wstring, ConfigValueInfo>& ConfigValueManager::GetConfigValues() const {
    return m_configValues;
}

bool ConfigValueManager::UpdateConfigValue(const std::wstring& name, const std::wstring& newValue) {
    auto it = m_configValues.find(name);
    if (it == m_configValues.end()) { return false; }

    // Create a unique key for the buffer cache
    std::string fullKey = "Config." + Utils::WideToUtf8(name);

    // Update the stable wchar_t* buffer that the game reads from
    size_t minCapacity = it->second.bufferSize > 0 ? it->second.bufferSize : (newValue.size() + 1);
    GetOrCreateBuffer(fullKey, newValue, minCapacity);

    // Update our local copy
    it->second.currentValue = newValue;
    it->second.isModified = true;
    return true;
}

wchar_t* ConfigValueManager::GetOrCreateBuffer(const std::string& key, const std::wstring& value, size_t minCapacity) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    CacheEntry& entry = m_bufferCache[key];

    size_t required = (std::max)(minCapacity, value.size() + 1);

    if (required > MAX_BUFFER_SIZE) { required = MAX_BUFFER_SIZE; }

    if (entry.capacity < required) {
        size_t newCapacity = (std::max)(required * 2, size_t(256));
        if (newCapacity > MAX_BUFFER_SIZE) { newCapacity = MAX_BUFFER_SIZE; }
        entry.buffer = std::make_unique<wchar_t[]>(newCapacity);
        entry.capacity = newCapacity;
    }

    size_t copySize = (std::min)(value.size(), entry.capacity - 1);
    wmemcpy(entry.buffer.get(), value.c_str(), copySize);
    entry.buffer[copySize] = L'\0';

    return entry.buffer.get();
}

void ConfigValueManager::SaveToToml(toml::table& root) const {
    toml::table configTable;

    for (const auto& [name, info] : m_configValues) {
        if (info.isModified) {
            std::string key = Utils::WideToUtf8(name);
            std::string value = Utils::WideToUtf8(info.currentValue);
            configTable.insert(key, value);
        }
    }

    if (!configTable.empty()) { root.insert("config", std::move(configTable)); }
}

void ConfigValueManager::LoadFromToml(const toml::table& root) {
    auto configNode = root["config"].as_table();
    if (!configNode) { return; }

    int count = 0;
    for (const auto& [key, value] : *configNode) {
        std::wstring name = Utils::Utf8ToWide(std::string(key.str()));
        std::wstring val = Utils::Utf8ToWide(value.value_or(std::string("")));

        // Check if this config value already exists (registered by game hook)
        auto it = m_configValues.find(name);
        if (it != m_configValues.end()) {
            it->second.currentValue = val;
            it->second.isModified = true;
        } else {
            // Create a new entry for values not yet seen from the game
            ConfigValueInfo info;
            info.currentValue = val;
            info.isModified = true;
            info.bufferSize = 256;
            info.valueType = ConfigValueType::Unknown;
            m_configValues[name] = info;
        }
        count++;
    }

    if (count > 0) { LOG_INFO("[ConfigValueManager] Loaded " + std::to_string(count) + " config values from TOML"); }
}
