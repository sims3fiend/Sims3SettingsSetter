#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>
#include <optional>
#include <variant>
#include <stdexcept>


// Enum for different setting types
enum class SettingType {
    Float,
    Integer,
    UnsignedInteger,
    Boolean,
    Pointer,
    FloatArray2,
    FloatArray3,
    //xxm registers
    FloatArray4,
    WideString,
    Unknown
};

enum class AddressType {
    Offset,
    Static,
    PointerChain
};

// Struct to hold setting information
struct SettingInfo {
    std::string name;
    SettingType type;
    AddressType addressType;
    uint32_t address;
    std::function<std::string(const void*)> logFunction;
    size_t size;
    std::optional<std::pair<double, double>> minMax = std::nullopt;
    bool isAvailable = false;
    //PLEASE PLEASE PLEASE IGNORE THIS PLEASE AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
    std::vector<uint32_t> pointerOffsets;
    bool initialAddressRelative = true;
};

// Namespace for helper functions
namespace LoggingHelpers {
    std::string logFloat(const void* ptr);
    std::string logInteger(const void* ptr);
    std::string logUnsignedInteger(const void* ptr);
    std::string logBoolean(const void* ptr);
    std::string logPointerAndValue(const void* ptr);
    std::string logMatrixPointer(const void* ptr);
    std::string logFloatArray2(const void* ptr);
    std::string logFloatArray3(const void* ptr);
    std::string logFloatArray4(const void* ptr);
    std::string logWideString(const void* ptr);
    std::string logStaticPointer(const void* ptr);
    std::string logUnknown(const void* ptr, size_t size);
    std::string WideToNarrow(const std::wstring& wstr);
    std::function<std::string(const void*)> createStaticPointerLogger(uint32_t staticAddress);
};

uintptr_t CalculateAddress(const SettingInfo& setting, uintptr_t baseAddress);
//bats lashes... blank stare.
uintptr_t CalculateAddress(const SettingInfo& setting, uintptr_t baseAddress);
bool ReadMemorySafe(const void* src, void* dest, size_t size);
bool WriteMemorySafe(void* dest, const void* src, size_t size);
bool IsSafeToRead(const void* ptr, size_t size);
bool IsSafeToWrite(void* ptr, size_t size);

template<typename T>
T ReadValue(const SettingInfo& setting, uintptr_t baseAddress) {
    uintptr_t address = CalculateAddress(setting, baseAddress);
    T value;
    if (!ReadMemorySafe(reinterpret_cast<void*>(address), &value, sizeof(T))) {
        throw std::runtime_error("Failed to read memory safely in ReadValue.");
    }
    return value;
}

template<typename T>
void WriteValue(const SettingInfo& setting, uintptr_t baseAddress, T value) {
    uintptr_t address = CalculateAddress(setting, baseAddress);
    if (!WriteMemorySafe(reinterpret_cast<void*>(address), &value, sizeof(T))) {
        throw std::runtime_error("Failed to write memory safely in WriteValue.");
    }
}

std::variant<bool, int, float, std::string> ReadSettingValue(const SettingInfo& settingInfo, uintptr_t baseAddress);

// Function to edit a setting
void EditSetting(const std::string& settingName, const std::unordered_map<std::string, SettingInfo>& settingsMap, uintptr_t baseAddress, const std::variant<bool, int, float, std::string>& newValue);

// Function to log settings
std::string LogSettings(const std::unordered_map<std::string, SettingInfo>& settings, uintptr_t baseAddress);


// Function to log all settings
void LogAllSettings(uintptr_t baseAddress);

// Logging control functions
void InitializeLogging();
void Log(const std::string& message);
void CleanupLogging();

std::string Trim(const std::string& s);
std::string SanitizeFileName(const std::string& name);

// Declare the settings maps

//new and coherent hopefully lmao
extern std::unordered_map<std::string, SettingInfo> animationSettings;
extern std::unordered_map<std::string, SettingInfo> worldLightingSettings;
extern std::unordered_map<std::string, SettingInfo> skyCommonSettings;
extern std::unordered_map<std::string, SettingInfo> shadowsSettings;
extern std::unordered_map<std::string, SettingInfo> weatherSettings;
extern std::unordered_map<std::string, SettingInfo> streamingSettings;
extern std::unordered_map<std::string, SettingInfo> visualEffectsSettings;
extern std::unordered_map<std::string, SettingInfo> routingDebugSettings;
extern std::unordered_map<std::string, SettingInfo> routingWorldRouterTuningSettings;
extern std::unordered_map<std::string, SettingInfo> routingLotTransitionTuningSettings;
extern std::unordered_map<std::string, SettingInfo> routingWorldBuilderSettings;
extern std::unordered_map<std::string, SettingInfo> cameraSettings;
extern std::unordered_map<std::string, SettingInfo> streamingLODSettings;