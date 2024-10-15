#pragma once
#include <string>
#include <unordered_map>
#include <variant>

struct Preset {
    std::string name;
    std::unordered_map<std::string, std::variant<bool, std::string>> settings;
};

bool SavePresetToIni(const Preset& preset, const std::string& filepath);
bool LoadPresetFromIni(Preset& preset, const std::string& filepath);