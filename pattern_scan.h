#pragma once
#include <Windows.h>
#include <vector>
#include <string>

namespace Pattern {
// Convert string pattern to byte pattern
std::vector<std::pair<uint8_t, bool>> ParsePattern(const char* pattern);

// Find a pattern in the main module
uintptr_t Scan(const char* pattern, const char* mask = nullptr);

// Find a pattern in a specific module
uintptr_t ScanModule(HMODULE module, const char* pattern, const char* mask = nullptr);

// Utility to create a mask from a pattern string
std::string CreateMask(const char* pattern);
} // namespace Pattern