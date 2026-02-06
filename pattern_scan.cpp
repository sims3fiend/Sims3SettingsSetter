#include "pattern_scan.h"
#include <Psapi.h>
#include <sstream>

namespace Pattern {

std::vector<std::pair<uint8_t, bool>> ParsePattern(const char* pattern) {
    std::vector<std::pair<uint8_t, bool>> bytes;

    std::stringstream ss(pattern);
    std::string byte;

    while (ss >> byte) {
        if (byte == "?" || byte == "??") {
            bytes.push_back({0, false});
        } else {
            bytes.push_back({(uint8_t)std::stoi(byte, nullptr, 16), true});
        }
    }

    return bytes;
}

uintptr_t Scan(const char* pattern, const char* mask) {
    return ScanModule(GetModuleHandle(nullptr), pattern, mask);
}

uintptr_t ScanModule(HMODULE module, const char* pattern, const char* mask) {
    if (!module) return 0;

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo))) { return 0; }

    std::vector<std::pair<uint8_t, bool>> bytes;
    if (mask) {
        // Using raw pattern + mask
        const uint8_t* pat = (const uint8_t*)pattern;
        bytes.reserve(strlen(mask));
        for (size_t i = 0; mask[i]; i++) { bytes.push_back({pat[i], mask[i] == 'x'}); }
    } else {
        // Using space-separated hex string pattern
        bytes = ParsePattern(pattern);
    }

    uintptr_t start = (uintptr_t)module;
    size_t size = moduleInfo.SizeOfImage;

    for (uintptr_t i = 0; i < size - bytes.size(); i++) {
        bool found = true;
        for (size_t j = 0; j < bytes.size(); j++) {
            if (bytes[j].second) { // If this byte should be checked
                if (*(uint8_t*)(start + i + j) != bytes[j].first) {
                    found = false;
                    break;
                }
            }
        }
        if (found) { return start + i; }
    }

    return 0;
}
// AUUHHHHHHHGHHHHHHHHHHHH
std::string CreateMask(const char* pattern) {
    std::stringstream ss(pattern);
    std::string byte;
    std::string mask;

    while (ss >> byte) { mask += (byte == "?" || byte == "??") ? "?" : "x"; }

    return mask;
}

} // namespace Pattern