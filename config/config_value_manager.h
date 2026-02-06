#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cwchar>
#include "settings.h" // For ConfigValueInfo, ConfigValueType

// Forward declare toml table to avoid header dependency
namespace toml { inline namespace v3 { class table; } }

// Manages GraphicsRules config values intercepted from the game
// Also owns the stable wchar_t* buffer cache that the game reads from
class ConfigValueManager {
public:
    static ConfigValueManager& Get();

    void AddConfigValue(const std::wstring& name, const ConfigValueInfo& info);
    const std::unordered_map<std::wstring, ConfigValueInfo>& GetConfigValues() const;
    bool UpdateConfigValue(const std::wstring& name, const std::wstring& newValue);

    // Get or create a stable wchar_t* buffer for a config value
    // The buffer persists for the lifetime of the process so the game can read from it
    // minCapacity is in wchar_t units, including space for the null terminator
    wchar_t* GetOrCreateBuffer(const std::string& key, const std::wstring& value, size_t minCapacity);

    // TOML serialization - writes/reads the [config] section
    void SaveToToml(toml::table& root) const;
    void LoadFromToml(const toml::table& root);

private:
    ConfigValueManager() = default;
    std::unordered_map<std::wstring, ConfigValueInfo> m_configValues;

    // Stable buffer cache - the game holds pointers into these buffers
    struct CacheEntry {
        std::unique_ptr<wchar_t[]> buffer;
        size_t capacity = 0;
    };
    std::unordered_map<std::string, CacheEntry> m_bufferCache;
    std::mutex m_cacheMutex;
    static constexpr size_t MAX_BUFFER_SIZE = 65536;
};
