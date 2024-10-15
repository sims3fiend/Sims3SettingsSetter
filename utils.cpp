#define NOMINMAX
#include "utils.h"

#include <fstream>
#include <iomanip>
#include <Windows.h>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <string>
#include <limits>
#include <memory>
#include <vector>
#include <format>
#include <stdexcept>

// Global variables for logging
std::ofstream logFile;
std::mutex logMutex;

// Function to initialize logging
void InitializeLogging() {
    logFile.open("hook_log.txt", std::ios::trunc);
    if (!logFile.is_open()) {
        MessageBoxA(NULL, "Failed to open log file.", "Error", MB_ICONERROR);
    }
}

// Function to log messages
void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        logFile << "[" << std::setfill('0')
            << std::setw(2) << st.wHour << ":"
            << std::setw(2) << st.wMinute << ":"
            << std::setw(2) << st.wSecond << "."
            << std::setw(3) << st.wMilliseconds << "] "
            << "[Thread " << GetCurrentThreadId() << "] "
            << message << std::endl;
        logFile.flush();
    }
}

// Function to clean up logging
void CleanupLogging() {
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open()) {
        logFile << "Logging cleanup completed" << std::endl;
        logFile.close();
    }
}

// Function to check if it's safe to read memory
bool IsSafeToRead(const void* ptr, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    // Check if the memory is readable
    if (mbi.Protect & PAGE_NOACCESS) {
        return false;
    }

    // Check if the memory is committed
    if (!(mbi.State & MEM_COMMIT)) {
        return false;
    }

    // Check if the memory is within the bounds of the region
    if (reinterpret_cast<uintptr_t>(ptr) + size > reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        return false;
    }

    return true;
}

bool IsSafeToWrite(void* ptr, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) {
        return false;
    }

    // Check if the memory is writable
    if (!(mbi.Protect & PAGE_READWRITE) && !(mbi.Protect & PAGE_WRITECOPY) &&
        !(mbi.Protect & PAGE_EXECUTE_READWRITE) && !(mbi.Protect & PAGE_EXECUTE_WRITECOPY)) {
        return false;
    }

    // Check if the memory is committed
    if (mbi.State != MEM_COMMIT) {
        return false;
    }

    // Check if the memory is within the bounds of the region
    uintptr_t startAddress = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t endAddress = startAddress + size;
    uintptr_t regionEndAddress = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    if (endAddress > regionEndAddress) {
        return false;
    }

    return true;
}



std::variant<bool, int, float, std::string> ReadSettingValue(const SettingInfo& settingInfo, uintptr_t baseAddress) {
    uintptr_t address = (settingInfo.addressType == AddressType::Offset) ?
        baseAddress + settingInfo.address : settingInfo.address;
    switch (settingInfo.type) {
    case SettingType::Boolean:
        return *reinterpret_cast<bool*>(address);
    case SettingType::Integer:
        return *reinterpret_cast<int*>(address);
    case SettingType::UnsignedInteger:
        return static_cast<int>(*reinterpret_cast<unsigned int*>(address));
    case SettingType::Float:
        return *reinterpret_cast<float*>(address);
    case SettingType::WideString: {
        std::wstring wstrValue = *reinterpret_cast<std::wstring*>(address);
        return LoggingHelpers::WideToNarrow(wstrValue);
    }
    default:
        throw std::runtime_error("Unsupported setting type for reading.");
    }
}
// Helper function to safely read memory
bool ReadMemorySafe(const void* src, void* dest, size_t size) {
    if (!IsSafeToRead(src, size)) {
        return false;
    }
    __try {
        memcpy(dest, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool WriteMemorySafe(void* dest, const void* src, size_t size) {
    if (!IsSafeToWrite(dest, size)) {
        return false;
    }
    __try {
        memcpy(dest, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


uintptr_t CalculateAddress(const SettingInfo& setting, uintptr_t baseAddress) {
    uintptr_t address = 0;
    std::ostringstream oss;
    oss << "Calculating address for setting '" << setting.name << "':\n";
    oss << "  Base Address: 0x" << std::hex << baseAddress << "\n";

    switch (setting.addressType) {
    case AddressType::Offset:
        address = baseAddress + setting.address;
        oss << "  Offset Address: 0x" << std::hex << address << "\n";
        break;
    case AddressType::Static:
        address = setting.address;
        oss << "  Static Address: 0x" << std::hex << address << "\n";
        break;
    case AddressType::PointerChain:
        if (setting.initialAddressRelative) {
            address = baseAddress + setting.address;
            oss << "  Initial Relative Address: 0x" << std::hex << address << "\n";
        }
        else {
            address = setting.address;
            oss << "  Initial Absolute Address: 0x" << std::hex << address << "\n";
        }
        for (size_t i = 0; i < setting.pointerOffsets.size(); ++i) {
            uintptr_t tempAddress = 0;
            if (!ReadMemorySafe(reinterpret_cast<void*>(address), &tempAddress, sizeof(tempAddress))) {
                oss << "  Failed to read memory at 0x" << std::hex << address << "\n";
                Log(oss.str());
                throw std::runtime_error("Failed to read memory during pointer chain traversal");
            }
            oss << "  Dereferenced Address at 0x" << std::hex << address << ": 0x" << tempAddress << "\n";
            address = tempAddress + setting.pointerOffsets[i];
            oss << "  Added Offset 0x" << std::hex << setting.pointerOffsets[i] << ", New Address: 0x" << address << "\n";
        }
        break;
    }
    Log(oss.str());
    return address;
}



// Namespace for helper functions
namespace LoggingHelpers {

    // Converts a wide string to a narrow string (UTF-8)
    std::string WideToNarrow(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }

    // Logs a float value
    std::string logFloat(const void* ptr) {
        float value = 0.0f;
        memcpy(&value, ptr, sizeof(value));
        return std::to_string(value);
    }

    // Logs an integer value
    std::string logInteger(const void* ptr) {
        int32_t value = 0;
        memcpy(&value, ptr, sizeof(value));
        return std::to_string(value);
    }

    // Logs an unsigned integer value
    std::string logUnsignedInteger(const void* ptr) {
        uint32_t value = 0;
        memcpy(&value, ptr, sizeof(value));
        return std::to_string(value);
    }

    // Logs a boolean value
    std::string logBoolean(const void* ptr) {
        bool value = false;
        memcpy(&value, ptr, sizeof(value));
        return value ? "true" : "false";
    }

    // Logs a pointer value and attempts to read the value it points to
    std::string logPointerAndValue(const void* ptr) {
        uintptr_t pointerValue = 0;
        memcpy(&pointerValue, ptr, sizeof(pointerValue));
        std::stringstream ss;
        ss << "0x" << std::hex << pointerValue;

        if (IsSafeToRead(reinterpret_cast<void*>(pointerValue), sizeof(uintptr_t))) {
            uintptr_t pointedValue = *reinterpret_cast<uintptr_t*>(pointerValue);
            ss << " (pointing to: 0x" << std::hex << pointedValue << ")";
        }
        else {
            ss << " (unable to read pointed value safely)";
        }

        return ss.str();
    }

    // Logs a matrix pointer and the first few elements of the matrix
    std::string logMatrixPointer(const void* ptr) {
        uintptr_t pointerValue = 0;
        memcpy(&pointerValue, ptr, sizeof(pointerValue));
        std::stringstream ss;
        ss << "0x" << std::hex << pointerValue;

        if (IsSafeToRead(reinterpret_cast<void*>(pointerValue), sizeof(float) * 4)) {
            float matrix[4];
            memcpy(matrix, reinterpret_cast<void*>(pointerValue), sizeof(float) * 4);
            ss << " (First 4 elements: "
                << std::fixed << std::setprecision(4)
                << matrix[0] << ", " << matrix[1] << ", "
                << matrix[2] << ", " << matrix[3] << ")";
        }
        else {
            ss << " (unable to read matrix elements safely)";
        }

        return ss.str();
    }

    // Logs a float array of size 2
    std::string logFloatArray2(const void* ptr) {
        float arr[2] = { 0.0f, 0.0f };
        memcpy(arr, ptr, sizeof(arr));
        return "[" + std::to_string(arr[0]) + ", " + std::to_string(arr[1]) + "]";
    }

    // Logs a float array of size 3
    std::string logFloatArray3(const void* ptr) {
        float arr[3] = { 0.0f, 0.0f, 0.0f };
        memcpy(arr, ptr, sizeof(arr));
        return "[" + std::to_string(arr[0]) + ", " + std::to_string(arr[1]) + ", " + std::to_string(arr[2]) + "]";
    }

	//Logs a float array of size 4
    std::string logFloatArray4(const void* ptr) {
        float arr[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        memcpy(arr, ptr, sizeof(arr));
        return "[" + std::to_string(arr[0]) + ", " + std::to_string(arr[1]) + ", "
            + std::to_string(arr[2]) + ", " + std::to_string(arr[3]) + "]";
    }

    // Logs a wide string
    std::string logWideString(const void* ptr) {
        // ptr is a pointer to a wchar_t*
        wchar_t* wstrPtr = nullptr;
        memcpy(&wstrPtr, ptr, sizeof(wstrPtr));
        if (!wstrPtr) return "(null)";

        // For safety, limit the string length or ensure null-termination
        std::wstring wstr(wstrPtr);

        return WideToNarrow(wstr);
    }

    // Logs an unknown data type by dumping the data in hex format
    std::string logUnknown(const void* ptr, size_t size) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(ptr);
        std::stringstream ss;
        ss << "0x";
        for (size_t i = 0; i < size; ++i) {
            ss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(data[i]);
        }
        return ss.str();
    }

    std::string logStaticPointer(const void* ptr) {
        uintptr_t value = 0;
        memcpy(&value, ptr, sizeof(value));
        return std::format("0x{:08X}", value);
    }

    std::function<std::string(const void*)> createStaticPointerLogger(uint32_t staticAddress) {
        return [staticAddress](const void* ptr) {
            return std::format("{} (static address: 0x{:08X})", logStaticPointer(ptr), staticAddress);
            };
    }
}



// Function to log settings
std::string LogSettings(const std::unordered_map<std::string, SettingInfo>& settings, uintptr_t baseAddress) {
    std::stringstream logStream;

    for (const auto& [key, setting] : settings) {
        logStream << std::left << std::setw(30) << setting.name << ": ";

        const void* valuePtr;
        if (setting.addressType == AddressType::Offset) {
            valuePtr = reinterpret_cast<const void*>(baseAddress + setting.address);
        }
        else { // AddressType::Static
            valuePtr = reinterpret_cast<const void*>(setting.address);
        }

        std::string valueStr;
        std::vector<uint8_t> buffer(setting.size);

        if (ReadMemorySafe(valuePtr, buffer.data(), setting.size)) {
            valueStr = setting.logFunction(buffer.data());

            if (setting.minMax) {
                logStream << valueStr << " (Min: " << setting.minMax->first
                    << ", Max: " << setting.minMax->second << ")";
            }
            else {
                logStream << valueStr;
            }

            if (setting.addressType == AddressType::Static) {
                logStream << " (static address: 0x" << std::hex << setting.address << std::dec << ")";
            }
        }
        else {
            logStream << "<invalid memory>";
        }

        logStream << "\n";
    }

    return logStream.str();
}

// Function to log all settings
void LogAllSettings(uintptr_t baseAddress) {
    std::stringstream logStream;
    logStream << "Animation Settings:\n";
    logStream << LogSettings(animationSettings, baseAddress);
    // Add other settings if necessary

    Log(logStream.str());
}

void EditSetting(const std::string& settingName, const std::unordered_map<std::string, SettingInfo>& settingsMap, uintptr_t baseAddress, const std::variant<bool, int, float, std::string>& newValue) {
    auto it = settingsMap.find(settingName);
    if (it == settingsMap.end()) {
        throw std::runtime_error("Setting not found: " + settingName);
    }

    const SettingInfo& setting = it->second;

    switch (setting.type) {
    case SettingType::Float:
        if (std::holds_alternative<float>(newValue)) {
            WriteValue<float>(setting, baseAddress, std::get<float>(newValue));
        }
        break;
    case SettingType::Integer:
        if (std::holds_alternative<int>(newValue)) {
            WriteValue<int>(setting, baseAddress, std::get<int>(newValue));
        }
        break;
    case SettingType::UnsignedInteger:
        if (std::holds_alternative<int>(newValue)) {
            WriteValue<unsigned int>(setting, baseAddress, static_cast<unsigned int>(std::get<int>(newValue)));
        }
        break;
    case SettingType::Boolean:
        if (std::holds_alternative<bool>(newValue)) {
            WriteValue<bool>(setting, baseAddress, std::get<bool>(newValue));
        }
        break;
        // Add more cases for other types as needed
    default:
        throw std::runtime_error("Unsupported setting type for editing: " + settingName);
    }
}

std::string Trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    auto end = s.find_last_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

std::string SanitizeFileName(const std::string& name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (char c : name) {
        // Replace invalid filename characters with '_'
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            sanitized.push_back('_');
        }
        else {
            sanitized.push_back(c);
        }
    }
    return sanitized;
}

// Initialize the settings maps
using namespace LoggingHelpers;

std::unordered_map<std::string, SettingInfo> animationSettings = {
    {"DrawSkeletons", SettingInfo{"Draw Skeletons", SettingType::Boolean, AddressType::Offset, 0x2F, logBoolean, sizeof(bool)}},
    {"SmoothAnimation", SettingInfo{"Smooth Animation", SettingType::Boolean, AddressType::Offset, 0x36, logBoolean, sizeof(bool)}},
    {"PoseCaching", SettingInfo{"Pose Caching", SettingType::Boolean, AddressType::Offset, 0x37, logBoolean, sizeof(bool)}},
    {"KillInactiveObjectPoses", SettingInfo{"Kill Inactive Object Poses", SettingType::Boolean, AddressType::Offset, 0x38, logBoolean, sizeof(bool)}},
    {"RenderRootIk", SettingInfo{"Render Root IK", SettingType::Boolean, AddressType::Offset, 0x34, logBoolean, sizeof(bool)}},
    {"RenderLimbIk", SettingInfo{"Render Limb IK", SettingType::Boolean, AddressType::Offset, 0x35, logBoolean, sizeof(bool)}},
    {"RenderLookAt", SettingInfo{"Render Look At", SettingType::Boolean, AddressType::Offset, 0x32, logBoolean, sizeof(bool)}},
    {"DisplayPropLifetime", SettingInfo{"Display Prop Lifetime", SettingType::Boolean, AddressType::Offset, 0x31, logBoolean, sizeof(bool)}},
    {"DisplayLookAt", SettingInfo{"Display Look At", SettingType::Boolean, AddressType::Offset, 0x30, logBoolean, sizeof(bool)}},
    {"DisplayIk", SettingInfo{"Display IK", SettingType::Boolean, AddressType::Offset, 0x2C, logBoolean, sizeof(bool)}},
    {"DisplayIkSlots", SettingInfo{"Display IK Slots", SettingType::Boolean, AddressType::Offset, 0x2E, logBoolean, sizeof(bool)}},
    {"DisplayMotionScaling", SettingInfo{"Display Motion Scaling", SettingType::Boolean, AddressType::Offset, 0x2D, logBoolean, sizeof(bool)}},
    {"OverrideMotionScaling", SettingInfo{"Override Motion Scaling", SettingType::Float, AddressType::Static, 0x011CD7FC, logFloat, sizeof(float)}},
    {"MotionScaleOverride", SettingInfo{"Motion Scale Override", SettingType::Float, AddressType::Static, 0x0114F620, logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"MinRootOffset", SettingInfo{"Min. Root Offset", SettingType::Float, AddressType::Static, 0x011CD7F0, logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"MaxRootOffset", SettingInfo{"Max. Root Offset", SettingType::Float, AddressType::Static, 0x0114F61C, logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"RootMotionScaleEffect", SettingInfo{"Root Motion Scale Effect", SettingType::Float, AddressType::Static, 0x011CD7F4, logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"ArmMotionScaleEffect", SettingInfo{"Arm Motion Scale Effect", SettingType::Float, AddressType::Static, 0x011CD7F8, logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"VisualMultiplierChangeRate", SettingInfo{"Visual Multiplier Change Rate", SettingType::Float, AddressType::Static, 0x0114F624, logFloat, sizeof(float), std::make_pair(1.0, 30.0)}},
    {"VisualMultiplierMin", SettingInfo{"Visual Multiplier Min.", SettingType::Float, AddressType::Static, 0x0114F630, logFloat, sizeof(float), std::make_pair(0.0, 0.99)}},
    {"VisualMultiplierMax", SettingInfo{"Visual Multiplier Max.", SettingType::Float, AddressType::Static, 0x0114F634, logFloat, sizeof(float), std::make_pair(2.0, 2.5)}},
    {"MultiplierMin", SettingInfo{"Multiplier Min.", SettingType::Float, AddressType::Static, 0x0114F628, logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"MultiplierMax", SettingInfo{"Multiplier Max.", SettingType::Float, AddressType::Static, 0x0114F62C, logFloat, sizeof(float), std::make_pair(1.0, 2.0)}},
    {"AnimationCounterWeight", SettingInfo{"Animation Counter Weight", SettingType::Float, AddressType::Static, 0x0114F2D8, logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"RampInOutTime", SettingInfo{"Ramp In/Out Time", SettingType::Float, AddressType::Static, 0x0114F2B0, logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"HeadTurnTime", SettingInfo{"Head Turn Time", SettingType::Float, AddressType::Static, 0x0114F2B4, logFloat, sizeof(float), std::make_pair(0.0, 2.0)}},
    {"TurnToRollTransfer", SettingInfo{"Turn to Roll Transfer", SettingType::Float, AddressType::Static, 0x0114F2D4, logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"MaxHeadTurn", SettingInfo{"Max. Head Turn", SettingType::Float, AddressType::Static, 0x0114F2B8, logFloat, sizeof(float), std::make_pair(0.0, 90.0)}},
    {"MaxHeadTilt", SettingInfo{"Max. Head Tilt", SettingType::Float, AddressType::Static, 0x0114F2BC, logFloat, sizeof(float), std::make_pair(0.0, 90.0)}},
    {"MaxHeadRoll", SettingInfo{"Max. Head Roll", SettingType::Float, AddressType::Static, 0x0114F2C0, logFloat, sizeof(float), std::make_pair(0.0, 90.0)}},
};

std::unordered_map<std::string, SettingInfo> worldLightingSettings = {
    // World Lighting
    {"GammaCurveTweak", SettingInfo{"Gamma Curve Tweak", SettingType::FloatArray4, AddressType::Offset, 0x20, LoggingHelpers::logFloatArray4, sizeof(float) * 4, std::make_pair(0.009f, 2.0f)}},
    {"BloomTriggerThreshold", SettingInfo{"Bloom Trigger Threshold", SettingType::Float, AddressType::Offset, 0x0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.1f, 4.0f)}},
    {"BloomRampIntensity", SettingInfo{"Bloom Ramp Intensity", SettingType::Float, AddressType::Offset, 0x4, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.1f, 4.0f)}},
    {"AmbientProbeScale", SettingInfo{"Ambient Probe Scale", SettingType::Float, AddressType::Offset, 0x40, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 4.0f)}},
    {"SunlightScale", SettingInfo{"Sunlight Scale", SettingType::Float, AddressType::Offset, 0x48, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 4.0f)}},
    {"LightProbeSaturation", SettingInfo{"Light Probe Saturation", SettingType::Float, AddressType::Offset, 0x10, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.1f, 2.0f)}},
    {"GrayscaleProbeIntensity", SettingInfo{"Grayscale Probe Intensity", SettingType::Float, AddressType::Offset, 0x14, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.1f, 2.0f)}},
    {"MinspecDirectScale", SettingInfo{"Minspec Direct Scale", SettingType::Float, AddressType::Offset, 0x220, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 2.0f)}},
    {"MinspecAmbientScale", SettingInfo{"Minspec Ambient Scale", SettingType::Float, AddressType::Offset, 0x228, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 2.0f)}},

    // Light Debug Tools
    {"DrawOccluders", SettingInfo{"Draw Occluders", SettingType::Boolean, AddressType::Offset, 0x389, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DrawLightRigs", SettingInfo{"Draw Light Rigs", SettingType::Boolean, AddressType::Offset, 0x388, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DrawProxmanLights", SettingInfo{"Draw Proxman Lights (slow)", SettingType::Boolean, AddressType::Offset, 0x38b, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"AllowTrueSpecularProbes", SettingInfo{"Allow True Specular Probes", SettingType::Boolean, AddressType::Offset, 0x38a, LoggingHelpers::logBoolean, sizeof(bool)}},

    // Light Scales
    {"GlobalIntensityScale", SettingInfo{"Global Intensity Scale", SettingType::Float, AddressType::Offset, 0x198, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.3f, 10.0f)}},

    // Interior Object Lighting
    {"SpecularProbeScale", SettingInfo{"Specular Probe Scale", SettingType::Float, AddressType::Offset, 0x248, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"DiffuseProbeScale", SettingInfo{"Diffuse Probe Scale", SettingType::Float, AddressType::Offset, 0x338, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"ObjectFloorLightmapBrightness", SettingInfo{"Object Floor-Lightmap Brightness", SettingType::Float, AddressType::Offset, 0x250, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"InteriorLightRigMaximum", SettingInfo{"Interior Light Rig Maximum", SettingType::Float, AddressType::Offset, 0x2d8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 10.0f)}},
    {"LightRigBBBThreshold", SettingInfo{"Light Rig BBB Threshold", SettingType::Float, AddressType::Offset, 0x2e8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 10.0f)}},
    {"VertexGlowScale", SettingInfo{"Vertex Glow Scale", SettingType::Float, AddressType::Offset, 0x260, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 4.0f)}},
    {"MinspecIntDirectScale", SettingInfo{"Minspec Int. Direct Scale", SettingType::Float, AddressType::Offset, 0x230, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 0.4f)}},
    {"MinspecIntDirectAmbientScale", SettingInfo{"Minspec Int. Direct Ambient Scale", SettingType::Float, AddressType::Offset, 0x238, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 0.4f)}},
    {"MinspecIntAmbientScale", SettingInfo{"Minspec Int. Ambient Scale", SettingType::Float, AddressType::Offset, 0x240, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 0.4f)}},
    {"FogOfExplorationObjectBrightness", SettingInfo{"Fog of exploration object brightness", SettingType::Float, AddressType::Offset, 0x380, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 1.0f)}},

    // Light Rig Tweak Settings
    {"MidgetLightMaximum", SettingInfo{"Midget Light Maximum", SettingType::Float, AddressType::Offset, 0x2f0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 2.0f)}},
    {"MidgetLightInteriorStrength", SettingInfo{"Midget Light Interior Strength", SettingType::Float, AddressType::Offset, 0x2f8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 0.4f)}},
    {"MidgetLightExteriorStrength", SettingInfo{"Midget Light Exterior Strength", SettingType::Float, AddressType::Offset, 0x300, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 0.4f)}},
    {"LightRigScale", SettingInfo{"Light Rig Scale", SettingType::Float, AddressType::Offset, 0x2e0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 10.0f)}},

    // Sim Lighting And Shading
    {"SimLightRigSquash", SettingInfo{"Sim Light Rig Squash", SettingType::Float, AddressType::Offset, 0x308, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 1.0f)}},
    {"SimBacklightCorrectionStrength", SettingInfo{"Sim Backlight Correction Strength", SettingType::Float, AddressType::Offset, 0x310, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"IndoorRig", SettingInfo{"Indoor Rig", SettingType::Float, AddressType::Offset, 0x328, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"IndoorProbe", SettingInfo{"Indoor Probe", SettingType::Float, AddressType::Offset, 0x318, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"OutdoorRig", SettingInfo{"Outdoor Rig", SettingType::Float, AddressType::Offset, 0x330, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"OutdoorProbe", SettingInfo{"Outdoor Probe", SettingType::Float, AddressType::Offset, 0x320, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"SimEyeDirectLighting", SettingInfo{"Sim Eye Direct Lighting", SettingType::Float, AddressType::Offset, 0x340, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"SimEyeAmbientLighting", SettingInfo{"Sim Eye Ambient Lighting", SettingType::Float, AddressType::Offset, 0x348, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"SimEyeSpecularLighting", SettingInfo{"Sim Eye Specular Lighting", SettingType::Float, AddressType::Offset, 0x350, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 4.0f)}},
    {"BloodMapCompensation", SettingInfo{"Blood Map Compensation", SettingType::FloatArray4, AddressType::Offset, 0x360, LoggingHelpers::logFloatArray4, sizeof(float) * 4, std::make_pair(0.009f, 2.0f)}},

    // Lightmap Settings
    {"MultiLightRescaleStrength", SettingInfo{"Multi-Light Rescale Strength", SettingType::Float, AddressType::Offset, 0x1a0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.009f, 2.0f)}},
    {"LightmapShaderBrightness", SettingInfo{"Lightmap Shader Brightness", SettingType::Float, AddressType::Offset, 0x258, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.02f, 4.0f)}},

    // Lightmap Ambient Settings
    {"BradyBunchBlueRGB1", SettingInfo{"BradyBunchBlue RGB", SettingType::FloatArray3, AddressType::Offset, 0x270, LoggingHelpers::logFloatArray3, sizeof(float) * 3}},
    {"BradyBunchBlueRGB2", SettingInfo{"BradyBunchBlue RGB", SettingType::FloatArray3, AddressType::Offset, 0x290, LoggingHelpers::logFloatArray3, sizeof(float) * 3}},
    {"MinimumBrightness", SettingInfo{"Minimum Brightness", SettingType::Float, AddressType::Offset, 0x2b8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 2.0f)}},
    {"VerticalGradientIntensity", SettingInfo{"Vertical Gradient Intensity", SettingType::Float, AddressType::Offset, 0x2c8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 1.0f)}},
    {"SummationScale", SettingInfo{"Summation Scale", SettingType::Float, AddressType::Offset, 0x2b0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.3f, 10.0f)}},
    {"VariationTolerance", SettingInfo{"Variation Tolerance", SettingType::Float, AddressType::Offset, 0x2c0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 1.0f)}},
    {"DirectLightingDominance", SettingInfo{"Direct Lighting Dominance", SettingType::Float, AddressType::Offset, 0x2d0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 1.0f)}},

    // Window Lighting
    {"SillShadowDistance", SettingInfo{"Sill Shadow Distance", SettingType::Float, AddressType::Offset, 0x1b0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 2.0f)}},
    {"AreaConversionFactor", SettingInfo{"Area Conversion Factor", SettingType::Float, AddressType::Offset, 0x1b8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 50.0f)}},
    {"MaxRange", SettingInfo{"Max Range", SettingType::Float, AddressType::Offset, 0x1c0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.1f, 1000.0f)}},
    {"DarkSideScale", SettingInfo{"Dark Side Scale", SettingType::Float, AddressType::Offset, 0x1d0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 10.0f)}},
    {"BrightSideScale", SettingInfo{"Bright Side Scale", SettingType::Float, AddressType::Offset, 0x1d8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 10.0f)}},
    {"SunScale", SettingInfo{"Sun Scale", SettingType::Float, AddressType::Offset, 0x1e0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 100.0f)}},
    {"GroundBounceRGB", SettingInfo{"Ground Bounce RGB", SettingType::FloatArray3, AddressType::Offset, 0x1f0, LoggingHelpers::logFloatArray3, sizeof(float) * 3}},
    {"OutdoorSpillMultiplier", SettingInfo{"Outdoor Spill Multiplier", SettingType::Float, AddressType::Offset, 0x210, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 10.0f)}},
    {"IndoorSkyColorSaturation", SettingInfo{"Indoor Sky Color Saturation", SettingType::Float, AddressType::Offset, 0x218, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 1.0f)}},
    {"WindowRescaleCheat", SettingInfo{"Window Rescale Cheat", SettingType::Float, AddressType::Offset, 0x1a8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0f, 1.0f)}},

    // Gamma Curve Values
    {"GammaCurveValues", SettingInfo{"Gamma Curve Values", SettingType::Pointer, AddressType::Offset, 0x394, LoggingHelpers::logPointerAndValue, sizeof(void*)}},
};


std::unordered_map<std::string, SettingInfo> skyCommonSettings = {
    {"SunRadius", SettingInfo{"Sun Radius", SettingType::Float, AddressType::Offset, 0x994, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunHaloRadius", SettingInfo{"Sun Halo Radius", SettingType::Float, AddressType::Offset, 0x998, LoggingHelpers::logFloat, sizeof(float)}},
    {"MoonRadius", SettingInfo{"Moon Radius", SettingType::Float, AddressType::Offset, 0x99C, LoggingHelpers::logFloat, sizeof(float)}},
    //conditional, Lunar Lakes
    {"EnableMoonHalo", SettingInfo{"Enable Moon Halo", SettingType::Boolean, AddressType::Offset, 0x9A0, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"SunDegreeOffset", SettingInfo{"Sun Degree Offset", SettingType::FloatArray3, AddressType::Offset, 0x9A4, LoggingHelpers::logFloatArray3, sizeof(float) * 3}},
    {"MoonDegreeOffset", SettingInfo{"Moon Degree Offset", SettingType::FloatArray3, AddressType::Offset, 0x9A8, LoggingHelpers::logFloatArray3, sizeof(float) * 3}},
    {"StarfieldDegreeOffset", SettingInfo{"Starfield Degree Offset", SettingType::Float, AddressType::Offset, 0x9AC, LoggingHelpers::logFloat, sizeof(float)}},
    {"MoonReflectionFactor", SettingInfo{"Moon Reflection Factor", SettingType::Float, AddressType::Offset, 0x9B0, LoggingHelpers::logFloat, sizeof(float)}},
    {"HorizonLightSkewFactor", SettingInfo{"Horizon Light Skew Factor", SettingType::Float, AddressType::Offset, 0x9B4, LoggingHelpers::logFloat, sizeof(float)}},
    {"HorizonLightMinimum", SettingInfo{"Horizon Light Minimum", SettingType::Float, AddressType::Offset, 0x9B8, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunsetDimmingSpeed", SettingInfo{"Sunset Dimming Speed", SettingType::Float, AddressType::Offset, 0x9BC, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunsetTime", SettingInfo{"Sunset Time", SettingType::Float, AddressType::Offset, 0x9C0, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunriseTime", SettingInfo{"Sunrise Time", SettingType::Float, AddressType::Offset, 0x9C4, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunDominanceTime", SettingInfo{"Sun Dominance Time", SettingType::Float, AddressType::Offset, 0x9D0, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunDegreeMovementStep", SettingInfo{"Sun Degree Movement Step", SettingType::Float, AddressType::Offset, 0x9D4, LoggingHelpers::logFloat, sizeof(float)}},
    {"SunDegreeMovementBlendThreshold", SettingInfo{"Sun Degree Movement Blend Threshold", SettingType::Float, AddressType::Offset, 0x9D8, LoggingHelpers::logFloat, sizeof(float)}},
    {"WeatherRandomizeInterval", SettingInfo{"Weather Randomize Interval", SettingType::Float, AddressType::Offset, 0x9DC, LoggingHelpers::logFloat, sizeof(float)}},
    {"HeatShimmerDistance", SettingInfo{"Heat Shimmer Distance", SettingType::Float, AddressType::Offset, 0x9E4, LoggingHelpers::logFloat, sizeof(float)}},
    {"BloomThreshold", SettingInfo{"Bloom Threshold", SettingType::Float, AddressType::Offset, 0x9E8, LoggingHelpers::logFloat, sizeof(float)}},
    {"BloomSlope", SettingInfo{"Bloom Slope", SettingType::Float, AddressType::Offset, 0x9EC, LoggingHelpers::logFloat, sizeof(float)}},
    {"UnderSeaFogStartDist", SettingInfo{"Under Sea Fog Start Distance", SettingType::Float, AddressType::Offset, 0x7C0, LoggingHelpers::logFloat, sizeof(float)}},
    {"UnderSeaFogEndDist", SettingInfo{"Under Sea Fog End Distance", SettingType::Float, AddressType::Offset, 0x7C4, LoggingHelpers::logFloat, sizeof(float)}},
    {"UnderSeaFogFalloffPower", SettingInfo{"Under Sea Fog Falloff Power", SettingType::Float, AddressType::Offset, 0x7C8, LoggingHelpers::logFloat, sizeof(float)}},
    {"FogDistances", SettingInfo{"Fog Distances", SettingType::FloatArray4, AddressType::Offset, 0x840, LoggingHelpers::logFloatArray4, sizeof(float) * 4}},
    {"FogCurveModifier", SettingInfo{"Fog Curve Modifier", SettingType::FloatArray4, AddressType::Offset, 0x860, LoggingHelpers::logFloatArray4, sizeof(float) * 4}}
};


std::unordered_map<std::string, SettingInfo> shadowsSettings = {
    {"MinimumFadeDistanceVertical", SettingInfo{"Minimum Fade Distance (vertical)", SettingType::Float, AddressType::Offset, 0x0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"MinimumFadeDistanceHorizontal", SettingInfo{"Minimum Fade Distance (horizontal)", SettingType::Float, AddressType::Offset, 0x4, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"PositionSnapDistance", SettingInfo{"Position Snap Distance", SettingType::Float, AddressType::Offset, 0x8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"CameraSnapDistance", SettingInfo{"Camera Snap Distance", SettingType::Float, AddressType::Offset, 0xC, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"SlopeScaleBias", SettingInfo{"Slope Scale Bias", SettingType::Float, AddressType::Offset, 0x10, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 111.0)}},
    {"DepthBias", SettingInfo{"Depth Bias", SettingType::Float, AddressType::Offset, 0x14, LoggingHelpers::logFloat, sizeof(float), std::make_pair(-0.01, 10.0)}},
};

std::unordered_map<std::string, SettingInfo> weatherSettings = {
    {"PrecipitationType", SettingInfo{"Precipitation Type", SettingType::UnsignedInteger, AddressType::Offset, 0xa48, LoggingHelpers::logUnsignedInteger, sizeof(uint32_t), std::make_pair(0.0, 6.0)}},
    {"PrecipitationStrength", SettingInfo{"Precipitation Strength", SettingType::Float, AddressType::Offset, 0xa4c, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"FrostAmount", SettingInfo{"Frost Amount", SettingType::Float, AddressType::Offset, 0xa50, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"SnowThicknessAmount", SettingInfo{"Snow Thickness Amount", SettingType::Float, AddressType::Offset, 0xa60, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"WetnessAmount", SettingInfo{"Wetness Amount", SettingType::Float, AddressType::Offset, 0xa68, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"SnowRoadTracksDepth", SettingInfo{"Snow Road Tracks Depth", SettingType::Float, AddressType::Offset, 0xa64, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"SnowIsMelting", SettingInfo{"Snow is Melting", SettingType::Boolean, AddressType::Offset, 0xa6c, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"TreeLeafToTwigFallStyle", SettingInfo{"Tree Leaf to Twig Fall Style", SettingType::Boolean, AddressType::Offset, 0xa74, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"TreeLeafToTwig", SettingInfo{"Tree Leaf to Twig", SettingType::Float, AddressType::Offset, 0xa70, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"TreeFallColors", SettingInfo{"Tree Fall Colors", SettingType::Float, AddressType::Offset, 0xa78, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"PondFreezeAmount", SettingInfo{"PondFreezeAmount", SettingType::Float, AddressType::Offset, 0xa90, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"HeatShimmerDist", SettingInfo{"Heat Shimmer Dist", SettingType::Float, AddressType::Offset, 0xa7c, LoggingHelpers::logFloat, sizeof(float), std::make_pair(-1.0, 10.0)}},
    {"CloudinessTarget", SettingInfo{"Cloudiness Target", SettingType::Float, AddressType::Offset, 0xa98, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"SunriseTimeOffset", SettingInfo{"Sunrise Time Offset", SettingType::Float, AddressType::Offset, 0xa58, LoggingHelpers::logFloat, sizeof(float), std::make_pair(-4.0, 4.0)}},
    {"SunsetTimeOffset", SettingInfo{"Sunset Time Offset", SettingType::Float, AddressType::Offset, 0xa5c, LoggingHelpers::logFloat, sizeof(float), std::make_pair(-4.0, 4.0)}},
    {"WorldLeavesAmount", SettingInfo{"World Leaves Amount", SettingType::Float, AddressType::Offset, 0xa94, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"SnowBumpScale", SettingInfo{"Snow bump scale", SettingType::Float, AddressType::Static, 0x01191d84, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"PrecipitationFogOnOff", SettingInfo{"Precipitation Fog On/Off", SettingType::Boolean, AddressType::Offset, 0xbb8, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"PrecipitationFogInfluence", SettingInfo{"Precipitation Fog Influence", SettingType::Float, AddressType::Offset, 0xbbc, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 111.0)}},
    {"PrecipitationFogStartDistance", SettingInfo{"Precipitation Fog Start Distance", SettingType::Float, AddressType::Offset, 0xbc0, LoggingHelpers::logFloat, sizeof(float), std::make_pair(10.0, 100.0)}},
    {"PrecipitationFogFadeInDistance", SettingInfo{"Precipitation Fog Fade In Distance", SettingType::Float, AddressType::Offset, 0xbc4, LoggingHelpers::logFloat, sizeof(float), std::make_pair(10.0, 1000.0)}},
    {"PrecipitationFogInternalStart", SettingInfo{"Precipitation Fog Internal Start", SettingType::Float, AddressType::Offset, 0xbc8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1000.0)}},
    {"PrecipitationFogInternalFadeIn", SettingInfo{"Precipitation Fog Internal Fade In", SettingType::Float, AddressType::Offset, 0xbcc, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1000.0)}},
    {"PrecipitationFogStrength", SettingInfo{"Precipitation Fog Strength", SettingType::Float, AddressType::Offset, 0xa8c, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"TerrainRainDropsEnabled", SettingInfo{"Terrain rain drops enabled", SettingType::Boolean, AddressType::Offset, 0xbe8, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"RoofRainDripsEnabled", SettingInfo{"Roof rain drips enabled", SettingType::Boolean, AddressType::Offset, 0xbe9, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"FootstepEffectsEnabled", SettingInfo{"Footstep effects enabled", SettingType::Boolean, AddressType::Offset, 0xbea, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"LeafFallEnabled", SettingInfo{"Leaf fall enabled", SettingType::Boolean, AddressType::Offset, 0xbeb, LoggingHelpers::logBoolean, sizeof(bool)}}
};

std::unordered_map<std::string, SettingInfo> streamingSettings = {
    {"WorldStreamingThreshold", SettingInfo{"World Streaming Threshold", SettingType::Float, AddressType::Offset, 0xC4, LoggingHelpers::logFloat, sizeof(float)}},
    {"MaxActiveLots", SettingInfo{"Max Active Lots", SettingType::Integer, AddressType::Static, 0x0118432C, LoggingHelpers::logInteger, sizeof(int), std::make_pair(1.0, 12.0)}},
    //Max Lots wasn't in function, idk the proper name but I think that works? There's on in the sgr but cba checking 
    {"MaxLots", SettingInfo{"Max Lots", SettingType::Integer, AddressType::Offset, 0xE4, LoggingHelpers::logInteger, sizeof(int), std::make_pair(0.0, 10000.0)}},
    {"CameraSpeedThreshold", SettingInfo{"Camera Speed Threshold", SettingType::Float, AddressType::Offset, 0xEC, LoggingHelpers::logFloat, sizeof(float)}},
    {"TerrainHeightThreshold", SettingInfo{"Terrain Height Threshold", SettingType::Float, AddressType::Offset, 0xE8, LoggingHelpers::logFloat, sizeof(float)}},
    {"LotLodDistance", SettingInfo{"Lot LOD Distance", SettingType::Float, AddressType::Offset, 0xDC, LoggingHelpers::logFloat, sizeof(float)}},
    {"ActiveLotBias", SettingInfo{"Active Lot Bias", SettingType::Float, AddressType::Offset, 0xE0, LoggingHelpers::logFloat, sizeof(float)}},
    {"ThrottleLotLodTransitions", SettingInfo{"Throttle Lot LoD Transitions", SettingType::Boolean, AddressType::Static, 0x011ECBC0, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ThrottleLotLodTransitionsMax", SettingInfo{"Throttle Lot LoD Transitions Max", SettingType::Integer, AddressType::Static, 0x01184328, LoggingHelpers::logInteger, sizeof(int), std::make_pair(1.0, 12.0)}},
    //{"ForceLodMinPriority", SettingInfo{"Force LoD Min Priority", SettingType::Integer, AddressType::Static, 0x0118432C, LoggingHelpers::logInteger, sizeof(int), std::make_pair(0.0, 4.0)}},
    //No clue what any of these are 
    //{"WorldStats", SettingInfo{"World Stats", SettingType::UnsignedInteger, AddressType::Offset, 0x0, LoggingHelpers::logUnsignedInteger, sizeof(unsigned int)}},
    //{"ExportStats", SettingInfo{"Export Stats", SettingType::Pointer, AddressType::Offset, 0x0, LoggingHelpers::logPointerAndValue, sizeof(void*)}},
    //{"DumpMemoryDBTextures", SettingInfo{"Dump Memory DB Textures", SettingType::Pointer, AddressType::Offset, 0x0, LoggingHelpers::logPointerAndValue, sizeof(void*)}},
    //{"unk1", SettingInfo{"Unknown 1", SettingType::Unknown, AddressType::Offset, 0x168, LoggingHelpers::logUnknown, sizeof(uint32_t)}},
    //{"unk2", SettingInfo{"Unknown 2", SettingType::Pointer, AddressType::Static, 0x011843F4, LoggingHelpers::logStaticPointer, sizeof(void*)}},
    //{"unk3", SettingInfo{"Unknown 3", SettingType::Pointer, AddressType::Static, 0x011ECBC4, LoggingHelpers::logStaticPointer, sizeof(void*)}}
};


std::unordered_map<std::string, SettingInfo> streamingLODSettings = {
    //commented out because one was crashing idfk
/*    {"ModelLODFarRadius", SettingInfo{"Model LOD Far Radius", SettingType::Float, AddressType::Static, 0x011d1814, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10000.0)}},
    {"ModelLODFarRadiusLargeObjects", SettingInfo{"Model LOD Far Radius: Large Objects", SettingType::Float, AddressType::Static, 0x011d1820, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10000.0)}},
    {"ModelLODFarRadiusFences", SettingInfo{"Model LOD Far Radius: Fences", SettingType::Float, AddressType::Static, 0x011d1838, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10000.0)}},
    {"ModelLODFarRadiusPortals", SettingInfo{"Model LOD Far Radius: Portals", SettingType::Float, AddressType::Static, 0x011d182c, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1000.0)}},
    {"ModelLODFarRadiusSims", SettingInfo{"Model LOD Far Radius: Sims", SettingType::Float, AddressType::Static, 0x011d1844, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1000.0)}},
    {"ModelLODAdjust", SettingInfo{"Model LOD Adjust", SettingType::FloatArray3, AddressType::Offset, 0x2c, LoggingHelpers::logFloatArray3, sizeof(float) * 3, std::make_pair(-2.0, 2.0)}},
    {"ObjectSizeHideFactor", SettingInfo{"Object Size Hide Factor", SettingType::Float, AddressType::Static, 0x011d1850, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1000.0)}},
    {"StreamingTimeout", SettingInfo{"Streaming Timeout", SettingType::Integer, AddressType::Offset, 0x0, LoggingHelpers::logInteger, sizeof(int), std::make_pair(0.0, 10000.0)}},
    */
    //Added these, not in original function
    //RenderSim should be float 4 but for some reason in memory it's a 3??
	{"RenderSimLODDistances", SettingInfo{"RenderSimLODDistances", SettingType::FloatArray3, AddressType::Static, 0x01155B6C, LoggingHelpers::logFloatArray3, sizeof(float) * 3}}
    //Trees at 011D2A38 but it seems like there's a 1 float gap between some values for some reason??

};

std::unordered_map<std::string, SettingInfo> visualEffectsSettings = {
    {"EmitThreshold", SettingInfo{"Emit Threshold", SettingType::Integer, AddressType::Offset, 0x248, logInteger, sizeof(int), std::make_pair(0, 100)}},
    {"MaxParticleSize", SettingInfo{"Max Particle Size", SettingType::Float, AddressType::Offset, 0x24C, logFloat, sizeof(float), std::make_pair(0.0f, 1000.0f)}},
    {"MaxParticles", SettingInfo{"Max Particles", SettingType::Integer, AddressType::Offset, 0x250, logInteger, sizeof(int), std::make_pair(10, 1000)}},
    {"MaxEffects", SettingInfo{"Max Effects", SettingType::Integer, AddressType::Offset, 0x254, logInteger, sizeof(int), std::make_pair(0, 100)}},
    {"MaxDistributedSamples", SettingInfo{"Max Distributed Samples", SettingType::Integer, AddressType::Offset, 0x258, logInteger, sizeof(int), std::make_pair(0, 100)}},
    {"ShowBoundingBoxes", SettingInfo{"Show Bounding Boxes", SettingType::Boolean, AddressType::Offset, 0x354, logBoolean, sizeof(bool)}},
    {"ShowSurfaces", SettingInfo{"Show Surfaces", SettingType::Boolean, AddressType::Offset, 0x355, logBoolean, sizeof(bool)}},
    {"ShowDebugBox", SettingInfo{"Show Debug Box", SettingType::Boolean, AddressType::Offset, 0x356, logBoolean, sizeof(bool)}},
    {"ShowEffectsScale", SettingInfo{"Show Effects Scale", SettingType::Float, AddressType::Offset, 0x350, logFloat, sizeof(float), std::make_pair(-100.0f, 100.0f)}},
    {"DisableParticleAlpha", SettingInfo{"Disable Particle Alpha", SettingType::Boolean, AddressType::Offset, 0x357, logBoolean, sizeof(bool)}},
    {"DrawParticles", SettingInfo{"Draw Particles", SettingType::Boolean, AddressType::Offset, 0x359, logBoolean, sizeof(bool)}},
    {"DrawMeshes", SettingInfo{"Draw Meshes", SettingType::Boolean, AddressType::Offset, 0x35A, logBoolean, sizeof(bool)}},
    {"DrawModels", SettingInfo{"Draw Models", SettingType::Boolean, AddressType::Offset, 0x35B, logBoolean, sizeof(bool)}},
    {"DrawDecals", SettingInfo{"Draw Decals", SettingType::Boolean, AddressType::Offset, 0x35C, logBoolean, sizeof(bool)}},
    {"unk1", SettingInfo{"Unknown 1", SettingType::Integer, AddressType::Static, 0x011C7E54, logInteger, sizeof(int)}},
    {"unk2", SettingInfo{"Unknown 2", SettingType::Integer, AddressType::Static, 0x011C7E55, logInteger, sizeof(int)}}
};

//does nothing, maybe remove idfk
std::unordered_map<std::string, SettingInfo> routingDebugSettings = {
    {"DisplayQuadtree", SettingInfo{"Display Quadtree", SettingType::Boolean, AddressType::Offset, 0x140, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DisplayTrails", SettingInfo{"Display Trails", SettingType::Boolean, AddressType::Offset, 0x141, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DisplayFootprints", SettingInfo{"Display Footprints", SettingType::Boolean, AddressType::Offset, 0x142, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DisplayRoutingSlots", SettingInfo{"Display Routing Slots", SettingType::Boolean, AddressType::Offset, 0x143, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DisplayContainmentSlots", SettingInfo{"Display Containment Slots", SettingType::Boolean, AddressType::Offset, 0x145, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DisplayRoutingMap", SettingInfo{"Display Routing Map", SettingType::Boolean, AddressType::Offset, 0x146, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"DebugLogLevel", SettingInfo{"Debug Log Level", SettingType::Integer, AddressType::Static, 0x0114c900, LoggingHelpers::logInteger, sizeof(int)}},
    {"Unk1", SettingInfo{"Unknown 1", SettingType::Float, AddressType::Offset, 0x178, LoggingHelpers::logFloat, sizeof(float)}},
    {"Unk2", SettingInfo{"Unknown 2", SettingType::Float, AddressType::Offset, 0x17C, LoggingHelpers::logFloat, sizeof(float)}},
    {"DebugString", SettingInfo{"Debug String", SettingType::Pointer, AddressType::Static, 0x01020e90, LoggingHelpers::logStaticPointer, sizeof(void*)}},
};

//These are all from FUN_0082f910, which apparently never gets called fml lmfao
//L"Routing: World Routing Tuning"
std::unordered_map<std::string, SettingInfo> routingWorldRouterTuningSettings = {
    {"WRMinimalCarTrip", SettingInfo{"WR Minimal Car Trip", SettingType::Float, AddressType::Offset, 0x968, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 100.0)}},
    {"WRWBMaximalRoutableSlope", SettingInfo{"WR WB Maximal Routable Slope", SettingType::Float, AddressType::Offset, 0x970, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    //idk why it's showing 0x0, obvi can't use that
    //{"DumpTopoMap", SettingInfo{"Dump topo map", SettingType::Boolean, AddressType::Offset, 0x0, LoggingHelpers::logBoolean, sizeof(bool)}},
};
//L"Routing : Lot Transition Tuning"
std::unordered_map<std::string, SettingInfo> routingLotTransitionTuningSettings = {
    {"NearLotEdge", SettingInfo{"Near Lot Edge", SettingType::Float, AddressType::Offset, 0x24, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"LotEdgeFootprintLength", SettingInfo{"Lot Edge Footprint Length", SettingType::Float, AddressType::Offset, 0x28, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"LotEdgeFootprintHalfWidth", SettingInfo{"Lot Edge Footprint HalfWidth", SettingType::Float, AddressType::Offset, 0x2C, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"LotEdgeFootprintsInvalidGoal", SettingInfo{"Lot Edge Footprints Invalid Goal", SettingType::Boolean, AddressType::Offset, 0x30, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"LotEdgeTestLength", SettingInfo{"Lot Edge Test Length", SettingType::Float, AddressType::Offset, 0x34, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"LotEdgeTestHalfWidth", SettingInfo{"Lot Edge Test HalfWidth", SettingType::Float, AddressType::Offset, 0x38, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 10.0)}},
    {"EntrancePreferredOverRoad", SettingInfo{"Entrance Preferred Over Road", SettingType::Boolean, AddressType::Offset, 0x49, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"PreferSWOnlyIfNoRoad", SettingInfo{"Prefer SW Only If No Road", SettingType::Boolean, AddressType::Offset, 0x48, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"UseNewShortPathCalculation", SettingInfo{"Use New ShortPath Calculation", SettingType::Boolean, AddressType::Offset, 0x50, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathDistanceThreshold", SettingInfo{"ShortPath Distance Threshold", SettingType::Float, AddressType::Offset, 0x3C, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 100.0)}},
    {"ShortPathReverseInitialChoice", SettingInfo{"ShortPath Reverse Initial Choice", SettingType::Boolean, AddressType::Offset, 0x47, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathAlwaysIgnoreRoads", SettingInfo{"ShortPath Always Ignore Roads", SettingType::Boolean, AddressType::Offset, 0x41, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathIgnoreRoadFarFromEntrance", SettingInfo{"ShortPath Ignore Road Far From Entrance", SettingType::Boolean, AddressType::Offset, 0x44, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathAlwaysIgnoreSW", SettingInfo{"ShortPath Always Ignore SW", SettingType::Boolean, AddressType::Offset, 0x40, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathIgnoreSWFarFromEntrance", SettingInfo{"ShortPath Ignore SW Far From Entrance", SettingType::Boolean, AddressType::Offset, 0x43, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathAlwaysIgnoreEntrance", SettingInfo{"ShortPath Always Ignore Entrance", SettingType::Boolean, AddressType::Offset, 0x42, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"ShortPathIgnoreEntranceFarFromGoal", SettingInfo{"ShortPath Ignore Entrance Far From Goal", SettingType::Boolean, AddressType::Offset, 0x45, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"LongPathIgnoreEntranceFarFromGoal", SettingInfo{"LongPath Ignore Entrance Far From Goal", SettingType::Boolean, AddressType::Offset, 0x46, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"StartLotPreferEntrance", SettingInfo{"StartLot Prefer Entrance", SettingType::Boolean, AddressType::Offset, 0x4A, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"StartLotPreferRoads", SettingInfo{"StartLot Prefer Roads", SettingType::Boolean, AddressType::Offset, 0x4B, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"StartLotPreferSW", SettingInfo{"StartLot Prefer SW", SettingType::Boolean, AddressType::Offset, 0x4C, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"GoalLotPreferEntrance", SettingInfo{"GoalLot Prefer Entrance", SettingType::Boolean, AddressType::Offset, 0x4D, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"GoalLotPreferRoads", SettingInfo{"GoalLot Prefer Roads", SettingType::Boolean, AddressType::Offset, 0x4E, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"GoalLotPreferSW", SettingInfo{"GoalLot Prefer SW", SettingType::Boolean, AddressType::Offset, 0x4F, LoggingHelpers::logBoolean, sizeof(bool)}},
    {"UseStreetCrossingCode", SettingInfo{"Use Street Crossing Code", SettingType::Boolean, AddressType::Offset, 0x51, LoggingHelpers::logBoolean, sizeof(bool)}},
};

//L"Routing : WorldBuilder"
std::unordered_map<std::string, SettingInfo> routingWorldBuilderSettings = {
    {"WBWaypointAliasingThreshold", SettingInfo{"WB Waypoint Aliasing Threshold", SettingType::Float, AddressType::Offset, 0x4, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"WBLotEdgeDensity", SettingInfo{"WB Lot Edge Density", SettingType::Float, AddressType::Offset, 0x8, LoggingHelpers::logFloat, sizeof(float), std::make_pair(1.0, 8.0)}},
    {"WBRoadDensity", SettingInfo{"WB Road Density", SettingType::Float, AddressType::Offset, 0xC, LoggingHelpers::logFloat, sizeof(float), std::make_pair(2.0, 20.0)}},
    {"WBRoadInterconnectThreshold", SettingInfo{"WB Road Interconnect Threshold", SettingType::Float, AddressType::Offset, 0x10, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
    {"WBBaseDensity", SettingInfo{"WB Base Density", SettingType::Integer, AddressType::Offset, 0x14, LoggingHelpers::logInteger, sizeof(int), std::make_pair(0.0, 200.0)}},
    {"WBHighInterestMultiplier", SettingInfo{"WB High Interest Multiplier", SettingType::Float, AddressType::Offset, 0x18, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 4.0)}},
    {"WBMedInterestMultiplier", SettingInfo{"WB Med Interest Multiplier", SettingType::Float, AddressType::Offset, 0x1C, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 2.0)}},
    {"WBLowInterestMultiplier", SettingInfo{"WB Low Interest Multiplier", SettingType::Float, AddressType::Offset, 0x20, LoggingHelpers::logFloat, sizeof(float), std::make_pair(0.0, 1.0)}},
};

//FUN_0096dc30, garbage who cares
std::unordered_map<std::string, SettingInfo> cameraSettings = {
    //this is just the string dummy lmfao
    {"MirrorCameras", SettingInfo{"Mirror Cameras", SettingType::Pointer, AddressType::Offset, 0x84, LoggingHelpers::logStaticPointer, sizeof(void*)}},
    {"NearDistance", SettingInfo{"Near Distance", SettingType::Float, AddressType::Offset, 0x4, LoggingHelpers::logFloat, sizeof(float)}},
    {"FarDistance", SettingInfo{"Far Distance", SettingType::Float, AddressType::Offset, 0x8, LoggingHelpers::logFloat, sizeof(float)}}
    //{"Unk1", SettingInfo{"Unknown 1", SettingType::Boolean, AddressType::Offset, 0xbd, LoggingHelpers::logBoolean, sizeof(bool)}},
    //{"Unk2", SettingInfo{"Unknown 2", SettingType::Boolean, AddressType::Offset, 0xe0, LoggingHelpers::logBoolean, sizeof(bool)}},
    //{"Unk3", SettingInfo{"Unknown 3", SettingType::Boolean, AddressType::Offset, 0xe1, LoggingHelpers::logBoolean, sizeof(bool)}},
    //{"Unk4", SettingInfo{"Unknown 4", SettingType::Boolean, AddressType::Offset, 0xe2, LoggingHelpers::logBoolean, sizeof(bool)}},
    //{"Unk5", SettingInfo{"Unknown 5", SettingType::Boolean, AddressType::Offset, 0xe3, LoggingHelpers::logBoolean, sizeof(bool)}}
};