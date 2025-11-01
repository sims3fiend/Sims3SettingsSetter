#pragma once

#include <unordered_map>
#include <string>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cwchar>

//We do this because the original buffer is expected to be the same size as the original string, e.g. "100" can become "999" but not "1000"
class ConfigValueCache {
private:
    struct Entry {
        std::unique_ptr<wchar_t[]> buffer;    // stable backing storage
        size_t capacity = 0;                   // number of wchar_t elements (including space for null)
    };

    std::unordered_map<std::string, Entry> cache;
    std::mutex cacheMutex;

    static constexpr size_t MAX_BUFFER_SIZE = 65536; // 64KB safety cap, probably overkill but whatever

    ConfigValueCache() {}

public:
    static ConfigValueCache& Instance() {
        static ConfigValueCache instance;
        return instance;
    }

    // Ensure buffer has at least minCapacity (in wchar_t units, including space for null terminator)
    wchar_t* GetBuffer(const std::string& key, const std::wstring& value, size_t minCapacity) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        Entry& entry = cache[key];

        size_t required = (std::max)(minCapacity, value.size() + 1);

        // Safety check: cap at maximum buffer size
        if (required > MAX_BUFFER_SIZE) {
            required = MAX_BUFFER_SIZE;
        }

        if (entry.capacity < required) {
            // Grow with 2x expansion, minimum 256
            size_t newCapacity = (std::max)(required * 2, size_t(256));

            // Apply cap
            if (newCapacity > MAX_BUFFER_SIZE) {
                newCapacity = MAX_BUFFER_SIZE;
            }

            entry.buffer = std::make_unique<wchar_t[]>(newCapacity);
            entry.capacity = newCapacity;
        }

        // Copy current value (truncate if necessary)
        size_t copySize = (std::min)(value.size(), entry.capacity - 1);
        wmemcpy(entry.buffer.get(), value.c_str(), copySize);
        entry.buffer[copySize] = L'\0';

        return entry.buffer.get();
    }
};
