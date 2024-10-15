//this could probably be split but idk, low priority for refactor
#define NOMINMAX
#include "hooks.h"
#include "renderer.h"
#include "gui.h"
#include "utils.h"
//#include "game_config.h"
#include "script_settings.h"
#include "config_value_cache.h"
#include <detours.h>
#include <sstream>
#include <iomanip>
#include <Windows.h>
#include <d3d9.h>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>
#include <bitset>
#include <locale>
#include <codecvt>
#include <stdexcept>
#include <mutex>
#include <iomanip>
#include <bitset>
#include <variant>
#include <array>
#include <bit>
#include <optional>


#include <Windows.h>
//#include <DbgHelp.h>
//#pragma comment(lib, "DbgHelp.lib")

extern std::unordered_map<std::string, uintptr_t> hookedFunctionBaseAddresses;
std::unordered_map<std::string, uintptr_t> hookedFunctionBaseAddresses;

// Original function pointers, one day I will rename them :)
FUN_006ef780_t original_FUN_006ef780 = nullptr;
FUN_006c35e0_t original_FUN_006c35e0 = nullptr;

FUN_007235b0_t original_FUN_007235b0 = nullptr;
FUN_006e7b60_t original_FUN_006e7b60 = nullptr;
FUN_00c128c0_t original_FUN_00c128c0 = nullptr;

FUN_006e9270_t original_FUN_006e9270 = nullptr;

FUN_00572490_t original_FUN_00572490 = nullptr;

FUN_00d6cc30_t original_FUN_00d6cc30 = nullptr;
FUN_00c671e0_t original_FUN_00c671e0 = nullptr;
FUN_009172b0_t original_FUN_009172b0 = nullptr;
FUN_00816610_t original_FUN_00816610 = nullptr;
FUN_0082f910_t original_FUN_0082f910 = nullptr;
FUN_0096dc30_t original_FUN_0096dc30 = nullptr;

//crashes but decentish info maybe idk
//FUN_0079a160_t original_FUN_0079a160 = nullptr;

EndScene_t original_EndScene = nullptr;
GetConfigBoolWithKeyConstruction_t original_GetConfigBoolWithKeyConstruction = nullptr;
RetrieveConfigValue_t original_RetrieveConfigValue = nullptr;

std::map<std::string, std::pair<std::string, uintptr_t>> uniqueConfigs;
std::mutex configMutex;

std::variant<bool, int, float, std::string> ParseSettingValue(SettingType type, const std::string& value) {
    switch (type) {
    case SettingType::Float:
        return std::stof(value);
    case SettingType::Integer:
        return std::stoi(value);
    case SettingType::UnsignedInteger:
        return static_cast<int>(std::stoul(value));
    case SettingType::Boolean:
        return (value == "true" || value == "1" || value == "True");
    case SettingType::WideString:
    case SettingType::Unknown:
        return value;
    default:
        throw std::runtime_error("Unable to parse! Invalid setting type.");
    }
}

void ApplySavedSettingsForFunction(const std::string& funcName) {
    auto baseAddress = hookedFunctionBaseAddresses[funcName];
    auto settingsMaps = GetSettingsMapsForFunction(funcName);

    for (const auto& [mapName, settingsMap] : settingsMaps) {
        if (settingsMap) {
            for (const auto& [settingName, settingInfo] : *settingsMap) {
                if (ScriptSettings::Instance().IsSettingSaved(mapName, settingName)) {
                    auto savedValue = ScriptSettings::Instance().GetSettingValue(mapName, settingName);
                    try {
                        auto parsedValue = ParseSettingValue(settingInfo.type, std::get<std::string>(savedValue));
                        EditSetting(settingName, *settingsMap, baseAddress, parsedValue);
                        Log("Applied saved setting " + mapName + "." + settingName);
                    }
                    catch (const std::exception& e) {
                        Log("Error applying saved setting " + mapName + "." + settingName + ": " + e.what());
                    }
                }
            }
        }
    }
}

std::wstring NarrowToWide(const std::string& narrow) {
    if (narrow.empty()) {
        return std::wstring();
    }

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &narrow[0], (int)narrow.size(), NULL, 0);
    std::wstring wide(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &narrow[0], (int)narrow.size(), &wide[0], size_needed);

    return wide;
}

std::string WideToNarrow(const std::wstring& wide) {
    if (wide.empty()) {
        return std::string();
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wide[0], (int)wide.size(), NULL, 0, NULL, NULL);
    std::string narrow(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wide[0], (int)wide.size(), &narrow[0], size_needed, NULL, NULL);

    return narrow;
}

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!IsGUIInitialized()) {
        InitializeGUI(pDevice);
    }
    RenderGUI(pDevice);
    //ScriptSettings::Instance().AutosaveConfig();
    return original_EndScene(pDevice);
}

bool IsSafeToRead(intptr_t address, size_t size) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)address, &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) ||
        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
        return false;
    }
    return (address + size) <= ((intptr_t)mbi.BaseAddress + mbi.RegionSize);
}

void LoadAndApplySavedSettings() {
    ScriptSettings& settings = ScriptSettings::Instance();

    for (const auto& [fullKey, entry] : settings.GetAllSettings()) {
        if (entry.saved) {
            size_t dotPos = fullKey.find('.');
            if (dotPos != std::string::npos) {
                std::string section = fullKey.substr(0, dotPos);
                std::string key = fullKey.substr(dotPos + 1);

                if (section == "GameConfig") {
                    // Handle game config settings
                    if (entry.address != 0) {
                        if (std::holds_alternative<bool>(entry.value)) {
                            *reinterpret_cast<bool*>(entry.address) = std::get<bool>(entry.value);
                        }
                        else {
							//tehehe, I don't know what this was for..
                        }
                    }
                }
                else {
                    // Handle script settings
                    auto settingsMaps = GetSettingsMapsForFunction(section);
                    if (!settingsMaps.empty() && settingsMaps[0].second->find(key) != settingsMaps[0].second->end()) {
                        const SettingInfo& settingInfo = settingsMaps[0].second->at(key);
                        WriteValue(settingInfo, hookedFunctionBaseAddresses[section], entry.value);
                    }
                }
            }
        }
    }

    Log("Loaded and applied saved settings");
}

void UpdateAvailabilityVector(const std::string& funcName) {
    auto settingsMaps = GetSettingsMapsForFunction(funcName);
    for (const auto& [mapName, settingsMap] : settingsMaps) {
        std::string availabilityKey = funcName + "_" + mapName;
        auto& availabilityVector = functionSettingsAvailability[availabilityKey];

        if (settingsMap) {
            size_t requiredSize = settingsMap->size();
            if (availabilityVector.size() < requiredSize) {
                availabilityVector.resize(requiredSize, true);
            }
            else {
                std::fill(availabilityVector.begin(), availabilityVector.end(), true);
            }
        }
    }

    // Apply saved settings if not already applied
    static std::unordered_map<std::string, bool> settingsApplied;
    if (!settingsApplied[funcName]) {
        ApplySavedSettingsForFunction(funcName);
        settingsApplied[funcName] = true;
    }
}

void ReapplyAllSettings() {
    Log("Reapplying all settings...");
    //loop hooked and apply them all
	for (const auto& [funcName, baseAddress] : hookedFunctionBaseAddresses) {
		ApplySavedSettingsForFunction(funcName);
	}
    Log("All settings reapplied.");
}



//FUN_0096dc30
uint32_t* __fastcall HookedFUN_0096dc30(uint32_t* param_1) {
    hookedFunctionBaseAddresses["FUN_0096dc30"] = reinterpret_cast<uintptr_t>(param_1);
    std::stringstream logStream;
    logStream << "FUN_0096dc30 called with param_1: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1) << std::dec << "\n";

    // Get the settings maps for the function
    auto settingsMaps = GetSettingsMapsForFunction("FUN_0096dc30");

    // Log initial values
    logStream << "Initial values:\n";
    for (const auto& [mapName, settingsMap] : settingsMaps) {
        logStream << LogSettings(*settingsMap, reinterpret_cast<uintptr_t>(param_1));
    }

    // Call the original function
    uint32_t* result = original_FUN_0096dc30(param_1);

    // Log values after original function
    logStream << "Values after original function:\n";
    for (const auto& [mapName, settingsMap] : settingsMaps) {
        logStream << LogSettings(*settingsMap, reinterpret_cast<uintptr_t>(param_1));
    }

    // Log the result
    logStream << "Function result: 0x" << std::hex << reinterpret_cast<uintptr_t>(result) << std::dec << "\n";

    Log(logStream.str());

    // Update availability vector
    UpdateAvailabilityVector("FUN_0096dc30");

    //cba finding a proper "is game loaded" thing and this is called last sooo
    std::thread([] {
        //delaying just in case
        std::this_thread::sleep_for(std::chrono::seconds(5));
        ReapplyAllSettings();
        }).detach();

    return result;
}

//FUN_0082f910 - Need to implement hookedFunctionBaseAddresses correctly for the three maps, or we could merge into 1 - done delete this
uint32_t* __fastcall HookedFUN_0082f910(uint32_t* param_1) {
    hookedFunctionBaseAddresses["FUN_0082f910"] = reinterpret_cast<uintptr_t>(param_1);
    std::stringstream logStream;
    logStream << "FUN_0082f910 called with param_1: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1) << std::dec << "\n";

    // Get the settings maps for this function
    auto settingsMaps = GetSettingsMapsForFunction("FUN_0082f910");

    // Log initial values
    logStream << "Initial values:\n";
    for (const auto& [mapName, settingsMap] : settingsMaps) {
        logStream << mapName << ":\n";
        logStream << LogSettings(*settingsMap, reinterpret_cast<uintptr_t>(param_1));
    }

    // Call the original function
    uint32_t* result = original_FUN_0082f910(param_1);

    // Log values after original function
    logStream << "Values after original function:\n";
    for (const auto& [mapName, settingsMap] : settingsMaps) {
        logStream << mapName << ":\n";
        logStream << LogSettings(*settingsMap, reinterpret_cast<uintptr_t>(param_1));
    }

    // Log the result
    logStream << "Function result: 0x" << std::hex << reinterpret_cast<uintptr_t>(result) << std::dec << "\n";

    Log(logStream.str());

    // Update availability vectors
    UpdateAvailabilityVector("FUN_0082f910");
    return result;
}

//FUN_00816610
uint32_t* __fastcall HookedFUN_00816610(uint32_t* param_1) {
	hookedFunctionBaseAddresses["FUN_00816610"] = reinterpret_cast<uintptr_t>(param_1);
	std::stringstream logStream;
	logStream << "FUN_00816610 called with param_1: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1) << std::dec << "\n";

	// Log initial values
	logStream << "Initial values:\n";
	logStream << LogSettings(routingDebugSettings, reinterpret_cast<uintptr_t>(param_1));

	// Call the original function
	uint32_t* result = original_FUN_00816610(param_1);

	// Log values after original function
	logStream << "Values after original function:\n";
	logStream << LogSettings(routingDebugSettings, reinterpret_cast<uintptr_t>(param_1));

	// Log the result
	logStream << "Function result: 0x" << std::hex << reinterpret_cast<uintptr_t>(result) << std::dec << "\n";

	Log(logStream.str());

	UpdateAvailabilityVector("FUN_00816610");

	return result;
}


//FUN_009172b0
uint32_t* __fastcall HookedFUN_009172b0(uint32_t* param_1) {
	hookedFunctionBaseAddresses["FUN_009172b0"] = reinterpret_cast<uintptr_t>(param_1);
	std::stringstream logStream;
	logStream << "FUN_009172b0 called with param_1: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1) << std::dec << "\n";

	// Log initial values
	logStream << "Initial values:\n";
	logStream << LogSettings(visualEffectsSettings, reinterpret_cast<uintptr_t>(param_1));

	// Call the original function
	uint32_t* result = original_FUN_009172b0(param_1);

	// Log values after original function
	logStream << "Values after original function:\n";
	logStream << LogSettings(visualEffectsSettings, reinterpret_cast<uintptr_t>(param_1));

	// Log the result
	logStream << "Function result: 0x" << std::hex << reinterpret_cast<uintptr_t>(result) << std::dec << "\n";

	Log(logStream.str());

	UpdateAvailabilityVector("FUN_009172b0");

	return result;
}

uint32_t* __fastcall HookedFUN_00c671e0(uint32_t* param_1) {
	hookedFunctionBaseAddresses["FUN_00c671e0"] = reinterpret_cast<uintptr_t>(param_1);
	std::stringstream logStream;
	logStream << "FUN_00c671e0 called with param_1: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1) << std::dec << "\n";

	// Log initial values
	logStream << "Initial values:\n";
	logStream << LogSettings(streamingSettings, reinterpret_cast<uintptr_t>(param_1));

	// Call the original function
	uint32_t* result = original_FUN_00c671e0(param_1);

	// Log values after original function
	logStream << "Values after original function:\n";
	logStream << LogSettings(streamingSettings, reinterpret_cast<uintptr_t>(param_1));

	// Log the result
	logStream << "Function result: 0x" << std::hex << reinterpret_cast<uintptr_t>(result) << std::dec << "\n";

	Log(logStream.str());

	UpdateAvailabilityVector("FUN_00c671e0");

	return result;
}

uint32_t* __fastcall HookedFUN_00d6cc30(uint32_t* param_1) {
	hookedFunctionBaseAddresses["FUN_00d6cc30"] = reinterpret_cast<uintptr_t>(param_1);
    std::stringstream logStream;
    logStream << "FUN_00d6cc30 called with param_1: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1) << std::dec << "\n";

    // Log initial values
    logStream << "Initial values:\n";
    logStream << LogSettings(weatherSettings, reinterpret_cast<uintptr_t>(param_1));

    // Call the original function
    uint32_t* result = original_FUN_00d6cc30(param_1);

    // Log values after original function
    logStream << "Values after original function:\n";
     logStream << LogSettings(weatherSettings, reinterpret_cast<uintptr_t>(param_1));

    // Log the result
    logStream << "Function result: 0x" << std::hex << reinterpret_cast<uintptr_t>(result) << std::dec << "\n";

    Log(logStream.str());

	UpdateAvailabilityVector("FUN_00d6cc30");

    return result;
}


// Updated FUN_00572490 hook
int __fastcall HookedFUN_00572490(int param_1) {
    hookedFunctionBaseAddresses["FUN_00572490"] = param_1;
    std::stringstream logStream;
    Log("FUN_00572490 called with param_1: 0x" + std::to_string(param_1));

    logStream << "Initial values:\n";
	logStream << LogSettings(animationSettings, param_1);
	// Call the original function
	int result = original_FUN_00572490(param_1);

	// Log values after original function
	logStream << "Values after original function:\n";
	logStream << LogSettings(animationSettings, param_1);

	Log(logStream.str());

	UpdateAvailabilityVector("FUN_00572490");
    
    return result;

}

//Sky Common settings hook
void __fastcall HookedFUN_00c128c0(int param_1) {
	hookedFunctionBaseAddresses["FUN_00c128c0"] = param_1;
    std::stringstream logStream;
    logStream << "FUN_00c128c0 called with param_1: 0x" << std::hex << param_1 << std::dec << "\n";

    // Log initial values
    logStream << "Initial values:\n";
    logStream << LogSettings(skyCommonSettings, param_1);

    // Call the original function
    original_FUN_00c128c0(param_1);

    // Log values after original function
    logStream << "Values after original function:\n";
    logStream << LogSettings(skyCommonSettings, param_1);

    Log(logStream.str());

	UpdateAvailabilityVector("FUN_00c128c0");
}

uint32_t __fastcall HookedFUN_006ef780(uint32_t param_1) {
	//hookedFunctionBaseAddresses["FUN_006ef780"] = param_1;
    std::stringstream logStream;
    logStream << "FUN_006ef780 called with param_1: 0x" << std::hex << param_1 << std::dec << "\n";

    // Log initial values
    logStream << "Initial values:\n";
    //logStream << LogSettings(renderMain, param_1);

    // Call the original function
    uint32_t result = original_FUN_006ef780(param_1);

    // Log values after original function
    logStream << "Values after original function:\n";
    //logStream << LogSettings(renderMain, param_1);

    // Log the result
    logStream << "Function result: 0x" << std::hex << result << std::dec << "\n";

    Log(logStream.str());

	//UpdateAvailabilityVector("FUN_006ef780");

    return result;
}

//World Lighting setting hook thing
void __fastcall HookedFUN_006c35e0(void* param_1_00, void* dummy, unsigned int param_2)
{
	hookedFunctionBaseAddresses["FUN_006c35e0"] = reinterpret_cast<uintptr_t>(param_1_00);
    std::stringstream logStream;
    logStream << "FUN_006c35e0 called with param_1_00: 0x" << std::hex << reinterpret_cast<uintptr_t>(param_1_00)
        << ", param_2: 0x" << param_2 << std::dec << "\n";

    // Log initial values
    logStream << "Initial values:\n";
    logStream << LogSettings(worldLightingSettings, reinterpret_cast<uintptr_t>(param_1_00));

    // Call the original function
    original_FUN_006c35e0(param_1_00, param_2);

    // Log values after original function
    logStream << "Values after original function:\n";
    logStream << LogSettings(worldLightingSettings, reinterpret_cast<uintptr_t>(param_1_00));

    // Log specific changes
    logStream << "Specific changes:\n";
    logStream << "GammaCurveValues pointer: 0x" << std::hex
        << *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(param_1_00) + 0x394) << std::dec << "\n";
    logStream << "dword ptr [param_1_00 + 0x3b0] incremented by 1\n";

    Log(logStream.str());

	UpdateAvailabilityVector("FUN_006c35e0");

}

void __fastcall HookedFUN_006e9270(int param_1) {
    std::stringstream logStream;
    logStream << "FUN_006e9270 called with param_1: 0x" << std::hex << param_1 << std::dec << "\n";

    // Log render1 settings before function call
    logStream << "Render1 settings before function call:\n";
   // logStream << LogSettings(renderSettings, param_1);

    // Call the original function
    original_FUN_006e9270(param_1);

    // Log render1 settings after function call
    logStream << "\nRender1 settings after function call:\n";
   // logStream << LogSettings(renderSettings, param_1);

    Log(logStream.str());
}


void __cdecl HookedFUN_007235b0(int param_1)
{
	hookedFunctionBaseAddresses["FUN_007235b0"] = param_1;
    std::stringstream logStream;
    logStream << "FUN_007235b0 called with param_1: 0x" << std::hex << param_1 << std::dec << "\n";

    // Log shadow settings before function call
    logStream << "Shadow settings before function call:\n";
    logStream << LogSettings(shadowsSettings, param_1);

    // Call the original function
    original_FUN_007235b0(param_1);

    // Log shadow settings after function call
    logStream << "\nShadow settings after function call:\n";
    logStream << LogSettings(shadowsSettings, param_1);

    Log(logStream.str());

	UpdateAvailabilityVector("FUN_007235b0");
}

//never called?
void __fastcall HookedFUN_006e7b60(int param_1)
{
	std::stringstream logStream;
	logStream << "FUN_006e7b60 called with param_1: 0x" << std::hex << param_1 << std::dec << "\n";

	// Log initial values
	logStream << "Initial values:\n";
	logStream << LogSettings(streamingLODSettings, param_1);

	// Call the original function
	original_FUN_006e7b60(param_1);

	// Log values after original function
	logStream << "Values after original function:\n";
	logStream << LogSettings(streamingLODSettings, param_1);

	Log(logStream.str());

	UpdateAvailabilityVector("FUN_006e7b60");

}

unsigned int __fastcall HookedRetrieveConfigValue(
    int param_1_00,
    int dummy,
    short* param_2,
    int* param_3,
    void** param_4)
{
    static int settingsCount = 0;
    settingsCount++;
    std::stringstream logStream;
    logStream << "RetrieveConfigValue called (Count: " << settingsCount << "):\n";

    // Read category and key
    std::string category = (param_2 && IsSafeToRead(reinterpret_cast<intptr_t>(param_2), 2))
        ? WideToNarrow(std::wstring(reinterpret_cast<wchar_t*>(param_2)))
        : "Unknown";
    std::string key = (param_3 && IsSafeToRead(reinterpret_cast<intptr_t>(param_3), 2))
        ? WideToNarrow(std::wstring(reinterpret_cast<wchar_t*>(param_3)))
        : "Unknown";
    std::string fullKey = category + "." + key;

    logStream << "  Category: \"" << category << "\"\n";
    logStream << "  Key: \"" << key << "\"\n";

    // Call original function
    unsigned int result = original_RetrieveConfigValue(param_1_00, dummy, param_2, param_3, param_4);
    logStream << "  Original Result: 0x" << std::hex << result << "\n";

    ScriptSettings& scriptSettings = ScriptSettings::Instance();

    // Process the retrieved value
    std::string retrievedValue;
    if (result && param_4 && *param_4 && IsSafeToRead(reinterpret_cast<intptr_t>(*param_4), 2)) {
        retrievedValue = WideToNarrow(std::wstring(reinterpret_cast<wchar_t*>(*param_4)));
        logStream << "  Retrieved Value: \"" << retrievedValue << "\"\n";
    }
    else {
        logStream << "  Retrieved Value: NULL\n";
    }

    // Check for override
    if (scriptSettings.IsSettingSaved(category, key)) {
        logStream << "  Override found in ScriptSettings.\n";
        auto overrideValue = scriptSettings.GetSettingValue(category, key);
        std::string newValue = std::holds_alternative<bool>(overrideValue)
            ? (std::get<bool>(overrideValue) ? "1" : "0")
            : std::get<std::string>(overrideValue);

        logStream << "  Overridden Value: \"" << newValue << "\"\n";

        // Apply override
        std::wstring overriddenValueWStr = NarrowToWide(newValue);
        wchar_t* newBuffer = ConfigValueCache::Instance().GetBuffer(fullKey, overriddenValueWStr);

        *param_4 = newBuffer;
        result = 1;  // Indicate value was found
        logStream << "  Override applied. Updated Result: 0x" << std::hex << result << "\n";
    }
    else {
        logStream << "  No override found in ScriptSettings.\n";
        if (result && param_4 && *param_4) {
            // Store the original value in our cache for consistency
            std::wstring originalValue = reinterpret_cast<wchar_t*>(*param_4);
            wchar_t* cachedBuffer = ConfigValueCache::Instance().GetBuffer(fullKey, originalValue);
            *param_4 = cachedBuffer;
        }
    }

    // Update ScriptSettings with the final value (either original or overridden)
    uintptr_t address = (param_4 && *param_4) ? reinterpret_cast<uintptr_t>(*param_4) : 0;
    std::string finalValue = (result && param_4 && *param_4)
        ? WideToNarrow(std::wstring(reinterpret_cast<wchar_t*>(*param_4)))
        : retrievedValue;
    scriptSettings.AddOrUpdateSetting(category, key, finalValue, address, false);

    logStream << "  Final value stored in ScriptSettings: \"" << finalValue << "\"\n";

    Log(logStream.str());
    return result;
}


// Helper functions

std::string DumpMemory(const void* ptr, size_t size) {
    std::stringstream ss;
    if (!ptr || !IsSafeToRead(ptr, size)) {
        return "Unable to read memory safely";
    }
    const unsigned char* bytePtr = static_cast<const unsigned char*>(ptr);
    for (size_t i = 0; i < size; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytePtr[i]) << " ";
    }
    return ss.str();
}

bool InitializeDetours() {
    Log("Initializing Detours hooks...");

    // Initialize original function pointers
    original_RetrieveConfigValue = reinterpret_cast<RetrieveConfigValue_t>(0x0058c380);
    original_GetConfigBoolWithKeyConstruction = reinterpret_cast<GetConfigBoolWithKeyConstruction_t>(0x0058b100);
    original_FUN_006ef780 = reinterpret_cast<FUN_006ef780_t>(0x006ef780);
    original_FUN_006c35e0 = reinterpret_cast<FUN_006c35e0_t>(0x006c35e0);
    original_FUN_007235b0 = reinterpret_cast<FUN_007235b0_t>(0x007235b0);
    original_FUN_006e7b60 = reinterpret_cast<FUN_006e7b60_t>(0x006e7b60);
    original_FUN_00c128c0 = reinterpret_cast<FUN_00c128c0_t>(0x00c128c0);
    original_FUN_006e9270 = reinterpret_cast<FUN_006e9270_t>(0x006e9270);
    original_FUN_00572490 = reinterpret_cast<FUN_00572490_t>(0x00572490);
    original_FUN_00d6cc30 = reinterpret_cast<FUN_00d6cc30_t>(0x00d6cc30);
    original_FUN_00c671e0 = reinterpret_cast<FUN_00c671e0_t>(0x00c671e0);
    original_FUN_009172b0 = reinterpret_cast<FUN_009172b0_t>(0x009172b0);
    original_FUN_00816610 = reinterpret_cast<FUN_00816610_t>(0x00816610);
    original_FUN_0082f910 = reinterpret_cast<FUN_0082f910_t>(0x0082f910);
    original_FUN_0096dc30 = reinterpret_cast<FUN_0096dc30_t>(0x0096dc30);

    // Obtain the EndScene function pointer from the Direct3D device vtable
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        Log("Failed to create Direct3D9 object");
        return false;
    }

    IDirect3DDevice9* pDevice = nullptr;
    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetForegroundWindow();

    HRESULT hr = pD3D->CreateDevice(
        D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &d3dpp, &pDevice);

    if (FAILED(hr) || !pDevice) {
        Log("Failed to create Direct3D9 device");
        pD3D->Release();
        return false;
    }

    void** vTable = *reinterpret_cast<void***>(pDevice);
    original_EndScene = reinterpret_cast<EndScene_t>(vTable[42]);

    pDevice->Release();
    pD3D->Release();

    // Check that critical function pointers are valid
    if (!original_RetrieveConfigValue ||
        !original_GetConfigBoolWithKeyConstruction ||
        !original_EndScene) {
        Log("Failed to get addresses of one or more original functions");
        return false;
    }

    // Begin the detour transaction
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Helper lambda for attaching detours with error checking
    auto AttachDetour = [](PVOID* originalFuncPtr, PVOID detourFunc, const std::string& funcName) -> bool {
        LONG error = DetourAttach(originalFuncPtr, detourFunc);
        if (error != NO_ERROR) {
            Log("Failed to attach Detour for " + funcName + ": " + std::to_string(error));
            return false;
        }
        return true;
        };

    // Attach detours for the functions
    if (!AttachDetour(&(PVOID&)original_RetrieveConfigValue, HookedRetrieveConfigValue, "RetrieveConfigValue") ||
        !AttachDetour(&(PVOID&)original_FUN_006c35e0, HookedFUN_006c35e0, "FUN_006c35e0") ||
        !AttachDetour(&(PVOID&)original_FUN_007235b0, HookedFUN_007235b0, "FUN_007235b0") ||
        !AttachDetour(&(PVOID&)original_FUN_00c128c0, HookedFUN_00c128c0, "FUN_00c128c0") ||
        !AttachDetour(&(PVOID&)original_EndScene, HookedEndScene, "EndScene") ||
        !AttachDetour(&(PVOID&)original_FUN_006ef780, HookedFUN_006ef780, "FUN_006ef780") ||
        !AttachDetour(&(PVOID&)original_FUN_00572490, HookedFUN_00572490, "FUN_00572490") ||
        !AttachDetour(&(PVOID&)original_FUN_00d6cc30, HookedFUN_00d6cc30, "FUN_00d6cc30") ||
        !AttachDetour(&(PVOID&)original_FUN_00c671e0, HookedFUN_00c671e0, "FUN_00c671e0") ||
        !AttachDetour(&(PVOID&)original_FUN_009172b0, HookedFUN_009172b0, "FUN_009172b0") ||
        !AttachDetour(&(PVOID&)original_FUN_00816610, HookedFUN_00816610, "FUN_00816610") ||
        !AttachDetour(&(PVOID&)original_FUN_0082f910, HookedFUN_0082f910, "FUN_0082f910") ||
        !AttachDetour(&(PVOID&)original_FUN_0096dc30, HookedFUN_0096dc30, "FUN_0096dc30") ||
        !AttachDetour(&(PVOID&)original_FUN_006e7b60, HookedFUN_006e7b60, "FUN_006e7b60"))
    {
        DetourTransactionAbort();
        return false;
    }

    // Commit the detour transaction
    LONG error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        Log("Failed to commit Detour transaction: " + std::to_string(error));
        return false;
    }

    // Load and apply saved settings
    LoadAndApplySavedSettings();

    Log("Detours hooks initialized successfully");
    return true;
}


void CleanupDetours() {
    Log("Removing Detours hooks...");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourDetach(&(PVOID&)original_RetrieveConfigValue, HookedRetrieveConfigValue);
    DetourDetach(&(PVOID&)original_FUN_006c35e0, HookedFUN_006c35e0);
    DetourDetach(&(PVOID&)original_FUN_007235b0, HookedFUN_007235b0);
    DetourDetach(&(PVOID&)original_FUN_00c128c0, HookedFUN_00c128c0);
    DetourDetach(&(PVOID&)original_EndScene, HookedEndScene);
    DetourDetach(&(PVOID&)original_FUN_006ef780, HookedFUN_006ef780);
    DetourDetach(&(PVOID&)original_FUN_00572490, HookedFUN_00572490);
    DetourDetach(&(PVOID&)original_FUN_00d6cc30, HookedFUN_00d6cc30);
    DetourDetach(&(PVOID&)original_FUN_00c671e0, HookedFUN_00c671e0);
    DetourDetach(&(PVOID&)original_FUN_009172b0, HookedFUN_009172b0);
    DetourDetach(&(PVOID&)original_FUN_00816610, HookedFUN_00816610);
    DetourDetach(&(PVOID&)original_FUN_0082f910, HookedFUN_0082f910);
    DetourDetach(&(PVOID&)original_FUN_0096dc30, HookedFUN_0096dc30);
    DetourDetach(&(PVOID&)original_FUN_006e7b60, HookedFUN_006e7b60);

    LONG error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        Log("Failed to commit Detour transaction during cleanup: " + std::to_string(error));
    }
    else {
        Log("Detours hooks removed successfully");
    }
}

bool InitializeHooks() {
    //SymInitialize(GetCurrentProcess(), NULL, TRUE);
    //ScriptSettings::Instance().LoadFromFile();
    //SetupConfigOverride();
    return InitializeDetours();
}

void CleanupHooks() {
    CleanupDetours();
    ConfigValueCache::Instance().Clear();
}