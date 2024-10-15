#pragma once

#include <unordered_map>
#include <string>
#include <mutex>

//We do this because the original buffer is expected to be the same size as the original string, e.g. "100" can become "999" but not "1000"
class ConfigValueCache {
private:
    std::unordered_map<std::string, std::wstring> cache;
    std::mutex cacheMutex;

    ConfigValueCache() {}

public:
    static ConfigValueCache& Instance() {
        static ConfigValueCache instance;
        return instance;
    }

    wchar_t* GetBuffer(const std::string& key, const std::wstring& value) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        // Store the value in the cache
        cache[key] = value;
        // Return a pointer to the internal buffer of the wstring
        return const_cast<wchar_t*>(cache[key].c_str());
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache.clear();
    }
};
