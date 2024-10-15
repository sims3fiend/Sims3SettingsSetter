#include "preset.h"
#include "utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>

bool SavePresetToIni(const Preset& preset, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file) {
        Log("Failed to open preset file for writing: " + filepath);
        return false;
    }

    file << "[Preset]" << std::endl;
    file << "Name=" << preset.name << std::endl;
    file << std::endl;

    file << "[Settings]" << std::endl;
    for (const auto& [key, value] : preset.settings) {
        file << key << "=";
        if (std::holds_alternative<bool>(value)) {
            file << std::boolalpha << std::get<bool>(value);
        }
        else {
            file << std::get<std::string>(value);
        }
        file << std::endl;
    }

    Log("Preset saved to: " + filepath);
    return true;
}

bool LoadPresetFromIni(Preset& preset, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        Log("Failed to open preset file for reading: " + filepath);
        return false;
    }

    std::string line;
    std::string currentSection;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        size_t equalPos = line.find('=');
        if (equalPos == std::string::npos) {
            continue;
        }

        std::string key = Trim(line.substr(0, equalPos));
        std::string valueStr = Trim(line.substr(equalPos + 1));

        if (currentSection == "Preset" && key == "Name") {
            preset.name = valueStr;
        }
        else if (currentSection == "Settings") {
            std::variant<bool, std::string> value;
            if (valueStr == "true") {
                value = true;
            }
            else if (valueStr == "false") {
                value = false;
            }
            else {
                value = valueStr;
            }
            preset.settings[key] = value;
        }
    }

    Log("Preset loaded from: " + filepath);
    return true;
}
