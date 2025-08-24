#include <windows.h>
#include <detours.h>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "d3d9_hook.h"
#include "settings.h"
#include "preset_manager.h"
#include "pattern_scan.h"
#include "vtable_manager.h"
#include "optimization.h"
#include "intersection_patch.h"
#include "utils.h"
#include <iomanip>
#include <strsafe.h>
#include "qol.h"
#include "small_patches.h"
#include "config_value_cache.h"

//Avert thine gaze, I said I was going to make the code clean and I lied
//https://www.youtube.com/watch?v=C6iAzyhm0p0

enum class SettingType { //TODO check these please :) maybe the dll thing has them better defined
    Int32 = 0,
    Uint32 = 1, 
    Float = 2,
    String = 3,
    Bool = 4,
    Vector2 = 5,
    Vector3 = 6,
    Vector4 = 7,
    Unknown = 8
};

enum class RegistrationType {
    Direct = 0,
    ViaCommonHandler = -100000  // Magic value used by the game, literally no clue?
};

// Base class for settings hooks
class SettingsHook {
protected:
    void* originalFunc;
    std::string hookName;

    static std::string WideToNarrow(const wchar_t* str) {
        if (!str) return "null";
        int size = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);
        std::string result(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, str, -1, &result[0], size, nullptr, nullptr);
        return result;
    }

    template<typename T>
    static bool IsSafeToRead(void* ptr, size_t size = sizeof(T)) {
        if (!ptr) return false;
        __try {
            volatile char dummy;
            for (size_t i = 0; i < size; i++) {
                dummy = reinterpret_cast<char*>(ptr)[i];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void LogBasicInfo(std::stringstream& log, void* thisPtr, void* targetAddr, const wchar_t* name) {
        log << "\n=== " << hookName << " Access ===\n";
        log << "This ptr: 0x" << std::hex << thisPtr << "\n";
        log << "Target Address: 0x" << std::hex << targetAddr << "\n";

        if (name && IsSafeToRead<wchar_t>(const_cast<wchar_t*>(name))) {
            log << "Setting Name: \"" << WideToNarrow(name) << "\"\n";
        }
        else {
            log << "Setting Name: <INVALID PTR>\n";
        }
    }

    void RegisterSetting(void* targetAddr, const wchar_t* name, const Setting::ValueType& defaultValue,
        float min = 0.0f, float max = 1.0f, float step = 0.1f) {
        if (!targetAddr || !name) return;

        SettingMetadata metadata;
        metadata.name = name;
        metadata.min = min;
        metadata.max = max;
        metadata.step = step; //Step for WHAT why is this here like is there a UI I'm missing?????? Why is there a min/max!!!
        //I'm guessing theres some kind of debug UI, there is a call for it in VTBL_VARIABLE_COMMAND but I can't figure out how to actually trigger it

        // Add debug logging
        std::stringstream log;
        log << "Registering setting:\n"
            << "  Name: " << WideToNarrow(name) << "\n"
            << "  Address: 0x" << std::hex << targetAddr << "\n"
            << "  Min: " << std::fixed << min << "\n"
            << "  Max: " << max << "\n"
            << "  Step: " << step << "\n";
        OutputDebugStringA(log.str().c_str());

        auto setting = std::make_unique<Setting>(targetAddr, metadata, defaultValue);
        SettingsManager::Get().RegisterSetting(name, std::move(setting));
    }

public:
    SettingsHook(void* original, const char* name) : originalFunc(original), hookName(name) {}
    virtual ~SettingsHook() = default;

    virtual void Install() = 0;
    virtual void Uninstall() = 0;
};

// Initialize the static instance
Utils::Logger* Utils::Logger::s_instance = nullptr;

// VariableRegistryHook
class VariableRegistryHook : public SettingsHook {
    typedef void(__thiscall* FuncType)(void* thisPtr, int param2, void* ptr, 
        const wchar_t* name, int type, int param5, int param6, float param7, 
        float param8, float param9);
    static inline VariableRegistryHook* instance = nullptr;

    static void __fastcall HookFunc(void* thisPtr, void* edx, int param2, void* ptr, 
        const wchar_t* name, int type, int param5, int param6, float param7, 
        float param8, float param9) {
        
        // Determine actual type based on the parameters
        SettingType actualType;
        if (type == static_cast<int>(RegistrationType::ViaCommonHandler)) {
            if (param5 == 100000 && param6 == 1) {
                actualType = static_cast<SettingType>(param2);
            } else {
                actualType = SettingType::Unknown;
            }
        } else {
            actualType = static_cast<SettingType>(type);
        }

        // Value reading logic
        if (ptr && IsSafeToRead<void*>(ptr)) {
            switch (actualType) {
                case SettingType::Int32: {
                    int value = *static_cast<int*>(ptr);
                    instance->RegisterSetting(ptr, name, value,
                        static_cast<float>(param5), static_cast<float>(param6), param9);
                    break;
                }
                case SettingType::Uint32: {
                    unsigned int value = *static_cast<unsigned int*>(ptr);
                    
                    // Check if min/max are reversed for uint32 types
                    float minVal = static_cast<float>(param5);
                    float maxVal = static_cast<float>(param6);
                    
                    // If min > max, swap them to prevent std::clamp errors
                    if (minVal > maxVal) {
                        std::swap(minVal, maxVal);
                    }
                    
                    instance->RegisterSetting(ptr, name, value, minVal, maxVal, param9);
                    break;
                }
                case SettingType::Float: {
                    float value = *static_cast<float*>(ptr);
                    instance->RegisterSetting(ptr, name, value, param7, param8, param9);
                    break;
                }
                case SettingType::String: {
                    // Not registered for GUI because there aren't any
                    break;
                }
                case SettingType::Bool: {
                    bool value = *static_cast<bool*>(ptr);
                    instance->RegisterSetting(ptr, name, value);
                    break;
                }
                case SettingType::Vector2: {
                    float* values = static_cast<float*>(ptr);
                    instance->RegisterSetting(ptr, name, Vector2{values[0], values[1]}, param7, param8, param9);
                    break;
                }
                case SettingType::Vector3: {
                    float* values = static_cast<float*>(ptr);
                    instance->RegisterSetting(ptr, name, Vector3{values[0], values[1], values[2]}, 
                                           param7, param8, param9);
                    break;
                }
                case SettingType::Vector4: {
                    float* values = static_cast<float*>(ptr);
                    instance->RegisterSetting(ptr, name, Vector4{values[0], values[1], values[2], values[3]}, 
                                           param7, param8, param9);
                    break;
                }
                default: {
                    // Unknown type, don't register
                    break;
                }
            }
        }

        // Call original function
        auto original = reinterpret_cast<FuncType>(instance->originalFunc);
        original(thisPtr, param2, ptr, name, type, param5, param6, param7, param8, param9);

        // Check if this is the "MT Time Step" setting, which is typically one of the last settings to be registered
        // BZZTTTT wrong, someone reported it never getting initialized, dunno why so we added a manual button to initialize it... should check that it has stuff tho I guess
        if (name && wcscmp(name, L"MT Time Step") == 0) {
            // This is a good time to mark settings as initialized and save defaults
            auto& settingsManager = SettingsManager::Get();
            
            if (!settingsManager.IsInitialized()) {
                Utils::Logger::Get().Log("All settings appear to be registered, saving default values");
                settingsManager.SetInitialized(true);
                
                // Save default settings to a file
                std::string error;
                if (!settingsManager.SaveDefaultValues("S3SS_defaults.ini", &error)) {
                    Utils::Logger::Get().Log("Failed to save default settings: " + error);
                }
                else {
                    Utils::Logger::Get().Log("Successfully saved default settings");
                }
            }
        }
    }

public:
    VariableRegistryHook(void* original) : SettingsHook(original, "VariableRegistry") {
        instance = this;
    }

    void Install() override { DetourAttach(&originalFunc, HookFunc); }
    void Uninstall() override { DetourDetach(&originalFunc, HookFunc); }
};

// Config retrieval hook
class ConfigRetrievalHook : public SettingsHook {
    typedef int(__fastcall* FuncType)(void* param1, void* edx, wchar_t* param2, wchar_t* param3, wchar_t** param4);
    static inline ConfigRetrievalHook* instance = nullptr;

    static int __fastcall HookFunc(void* param1, void* edx, wchar_t* category, wchar_t* key, wchar_t** outValue) {
        // Call original first to get game's value
        auto original = (FuncType)instance->originalFunc;
        int result = original(param1, edx, category, key, outValue);

        // Process if we have valid strings
        if (category && key && IsSafeToRead<wchar_t>(category) && IsSafeToRead<wchar_t>(key)) {
            try {
                // Safely get string length for category and key
                size_t categoryLen = 0;
                size_t keyLen = 0;
                bool categoryValid = false;
                bool keyValid = false;

                // Check category string
                while (categoryLen < 1024 && IsSafeToRead<wchar_t>(category + categoryLen)) {
                    if (category[categoryLen] == L'\0') {
                        categoryValid = true;
                        break;
                    }
                    categoryLen++;
                }

                // Check key string
                while (keyLen < 1024 && IsSafeToRead<wchar_t>(key + keyLen)) {
                    if (key[keyLen] == L'\0') {
                        keyValid = true;
                        break;
                    }
                    keyLen++;
                }

                if (!categoryValid || !keyValid) {
                    return result;
                }

                std::wstring keyStr(key, keyLen);
                std::wstring categoryStr(category, categoryLen);

                // Ensure category is never empty
                if (categoryStr.empty()) {
                    categoryStr = L"Uncategorized";
                }

                // Skip Cfg and Assets prefixes
                if (keyStr.substr(0, 3) == L"Cfg" || keyStr.substr(0, 6) == L"Assets") {
                    return result;
                }

                // Create a unique key for the config value cache
                std::string fullKey = Utils::WideToUtf8(categoryStr) + "." + Utils::WideToUtf8(keyStr);

                // Only process if category is "Config", Options is too risky since it overwrites the actual file which is stupid
                if (categoryStr == L"Config") {
                    // Check if we have a saved override for this config value
                    auto& configValues = SettingsManager::Get().GetConfigValues();
                    auto it = configValues.find(keyStr);
                    if (it != configValues.end() && it->second.isModified) {
                        // We have a saved override, use our value instead
                        const std::wstring& savedValue = it->second.currentValue;
                        
                        // Use ConfigValueCache to get a persistent buffer for this value
                        wchar_t* newBuffer = ConfigValueCache::Instance().GetBuffer(fullKey, savedValue);
                        if (newBuffer) {
                            // Replace the original buffer with our cached one
                            if (*outValue) {
                                // We don't need to free the original buffer as the game handles that
                            }
                            *outValue = newBuffer;
                            return 1;
                        }
                    }

                    // Store the value info regardless of result
                    ConfigValueInfo info;
                    info.category = categoryStr;
                    if (result == 1 && outValue && *outValue) {
                        // Cache the original value for consistency
                        std::wstring originalValue = *outValue;
                        wchar_t* cachedBuffer = ConfigValueCache::Instance().GetBuffer(fullKey, originalValue);
                        *outValue = cachedBuffer;
                        
                        info.currentValue = originalValue;
                        info.bufferSize = wcslen(*outValue) + 1;
                        info.valueType = DetectValueType(*outValue);
                    } else {
                        // For non-existent values, store empty value but reasonable buffer size
                        info.currentValue = L"";
                        info.bufferSize = 256;  // Default reasonable size
                        info.valueType = ConfigValueType::Unknown;
                    }
                    info.isModified = false;
                    SettingsManager::Get().AddConfigValue(keyStr, info);
                }

                // Store the setting in our config settings map using validated strings
                SettingsManager::Get().AddConfigSetting(keyStr.c_str(), categoryStr.c_str());

                // Try to find and update the category for this setting
                if (auto* setting = SettingsManager::Get().GetSetting(keyStr.c_str())) {
                    setting->GetMetadata().category = categoryStr;
                }
            }
            catch (const std::exception& e) {
                Utils::Logger::Get().Log("Exception in ConfigRetrievalHook: " + std::string(e.what()));
            }
        }

        return result;
    }

    static ConfigValueType DetectValueType(const wchar_t* value) {
        if (!value || !*value) return ConfigValueType::Unknown;
        
        // Try to detect type based on value format
        std::wstring str = value;
        
        // Check for boolean
        if (str == L"true" || str == L"false" || str == L"0" || str == L"1") {
            return ConfigValueType::Boolean;
        }
        
        // Check if it's a number
        try {
            size_t pos;
            // Try integer first
            std::stoi(str, &pos);
            if (pos == str.length()) {
                return ConfigValueType::Integer;
            }
            
            // Try float
            std::stof(str, &pos);
            if (pos == str.length()) {
                return ConfigValueType::Float;
            }
        }
        catch (...) {
            // Not a number
        }
        
        // Check if it contains any non-ASCII characters
        bool hasWideChar = false;
        for (wchar_t c : str) {
            if (c > 127) {
                hasWideChar = true;
                break;
            }
        }
        
        return hasWideChar ? ConfigValueType::WideString : ConfigValueType::String;
    }

public:
    ConfigRetrievalHook() : SettingsHook(nullptr, "Config Retrieval") {
        // Pattern for the config function
        const char* pattern = "83 EC 2C 8B 44 24 ?? 53 55 56 57 33 DB 8B F1 BF ?? ?? ?? ?? 50 8D 4C 24 ?? 89 5C 24 ?? 89 5C 24 ?? 89 5C 24 ?? 89 7C 24 ??";

        // Find the function
        uintptr_t addr = Pattern::Scan(pattern);
        if (!addr) {
            throw std::runtime_error("Failed to find config function pattern");
        }

        // Verify the string reference
        uintptr_t stringAddr = *(uintptr_t*)(addr + 16);
        if (!IsSafeToRead<char>((char*)stringAddr)) {
            throw std::runtime_error("Invalid string reference");
        }

        // Verify it's the correct function by checking for the string
        const char* str = (const char*)stringAddr;
        if (strcmp(str, "Services/ConfigRegistry") != 0) {
            throw std::runtime_error("Invalid config function pattern match");
        }

        originalFunc = (void*)addr;
        instance = this;
    }

    void Install() override { DetourAttach(&originalFunc, HookFunc); }
    void Uninstall() override { DetourDetach(&originalFunc, HookFunc); }
};

class CustomDebugVarHook : public SettingsHook {
    typedef void(__thiscall* FuncType)(void* thisPtr, int param2, int param3, wchar_t* name,
        int param5, int param6, float param7, float param8, float param9);
    static inline CustomDebugVarHook* instance = nullptr;

    static void __fastcall HookFunc(void* thisPtr, void* edx, int param2, int param3, wchar_t* name,
        int param5, int param6, float param7, float param8, float param9) {
        
        // Log raw parameters
        std::stringstream rawLog;
        rawLog << "CustomDebugVar Raw - param2: " << param2 
               << ", param3: 0x" << std::hex << param3
               << ", param5: " << std::dec << param5 
               << ", param6: " << param6 
               << ", range: [" << param7 << " to " << param8 << "]"
               << ", step: " << param9 << "\n";
        OutputDebugStringA(rawLog.str().c_str());

        // param3 is the direct address of the value
        void* valueAddr = reinterpret_cast<void*>(param3);

        // Log basic info
        std::stringstream log;
        instance->LogBasicInfo(log, thisPtr, valueAddr, name);
        OutputDebugStringA(log.str().c_str());

        // Register the setting
        if (valueAddr && name) {
            //param2 is the type which relates to FUN_005a1340
            SettingType type = static_cast<SettingType>(param2);
            
            std::stringstream regLog;
            regLog << "Registering setting type " << static_cast<int>(type) 
                  << " at address 0x" << std::hex << valueAddr << "\n";
            OutputDebugStringA(regLog.str().c_str());
            
            switch (type) {
                case SettingType::Int32:
                    instance->RegisterSetting(valueAddr, name, 
                        *reinterpret_cast<int*>(valueAddr), 
                        static_cast<float>(param5), static_cast<float>(param6), param9);
                    break;
                case SettingType::Uint32:
                    instance->RegisterSetting(valueAddr, name, 
                        *reinterpret_cast<unsigned int*>(valueAddr),
                        static_cast<float>(param5), static_cast<float>(param6), param9);
                    break;
                case SettingType::Float:
                    instance->RegisterSetting(valueAddr, name, 
                        *reinterpret_cast<float*>(valueAddr), param7, param8, param9);
                    break;
                case SettingType::Bool:
                    instance->RegisterSetting(valueAddr, name, 
                        *reinterpret_cast<bool*>(valueAddr));
                    break;
                case SettingType::Vector2: {
                    float* values = reinterpret_cast<float*>(valueAddr);
                    instance->RegisterSetting(valueAddr, name,
                        Vector2{values[0], values[1]}, param7, param8, param9);
                    break;
                }
                case SettingType::Vector3: {
                    float* values = reinterpret_cast<float*>(valueAddr);
                    instance->RegisterSetting(valueAddr, name,
                        Vector3{values[0], values[1], values[2]}, param7, param8, param9);
                    break;
                }
                case SettingType::Vector4: {
                    float* values = reinterpret_cast<float*>(valueAddr);
                    instance->RegisterSetting(valueAddr, name,
                        Vector4{values[0], values[1], values[2], values[3]}, param7, param8, param9);
                    break;
                }
                default:
                    OutputDebugStringA(("Unknown setting type: " + std::to_string(param2) + "\n").c_str());
                    break;
            }
        }

        // Call original function
        auto original = reinterpret_cast<FuncType>(instance->originalFunc);
        original(thisPtr, param2, param3, name, param5, param6, param7, param8, param9);
    }

public:
    CustomDebugVarHook(void* original) : SettingsHook(original, "CustomDebugVar") {
        instance = this;
    }

    void Install() override { DetourAttach(&originalFunc, HookFunc); }
    void Uninstall() override { DetourDetach(&originalFunc, HookFunc); }
};

// Updated HookManager with simplified vtable offsets
class HookManager {
    VTableManager vtm;
    std::vector<std::unique_ptr<SettingsHook>> hooks;

    enum VTableOffsets : uintptr_t {
        VTBL_VARIABLE_REGISTRY = 0x3C,
        VTBL_CUSTOM_DEBUG_VAR = 0x44,
        VTBL_CUSTOM_DEBUG_VAR_ALT = 0x40,  // Points to functions instead of values, need to investigate more, used for debug UI Spy and some render toggle stuff
        VTBL_VARIABLE_COMMAND = 0x58       // Same as ALT but not, used for some interesting functions like Recompute Lighting, some dumper ones idk, probably worth a look moreso than ALT
    };

public:
    void Initialize() {
        if (!vtm.Initialize()) {
            OutputDebugStringA("HookManager: Failed to initialize VTABLE\n");
            return;
        }

        // Install variable registry hook
        AddHook<VariableRegistryHook>("VariableRegistry", VTBL_VARIABLE_REGISTRY);

        // Install custom debug var hook
        AddHook<CustomDebugVarHook>("CustomDebugVar", VTBL_CUSTOM_DEBUG_VAR);

        // Add config retrieval hook
        try {
            hooks.emplace_back(new ConfigRetrievalHook());
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("ConfigRetrievalHook Error: " + std::string(e.what()) + "\n").c_str());
        }

        // Commit all hooks
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        for (auto& hook : hooks) {
            if (hook) hook->Install();
        }

        DetourTransactionCommit();
    }

    void Cleanup() {
        if (hooks.empty()) return;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        for (auto& hook : hooks) {
            hook->Uninstall();
        }

        DetourTransactionCommit();
        hooks.clear();
    }

private:
    template <typename T>
    void AddHook(const char* name, uintptr_t offset) {
        if (auto addr = vtm.GetFunctionAddress(name, offset)) {
            hooks.emplace_back(new T(addr));
            OutputDebugStringA(("HookManager: Added " + std::string(name) + " hook\n").c_str());
        }
    }
};

// Global hook manager
HookManager g_hookManager;

HANDLE g_ThreadHandle = NULL;
DWORD g_ThreadId = 0;

DWORD WINAPI HookThread(LPVOID lpParameter) {
    try {
        // Initialize logger
        if (!Utils::Logger::Get().Initialize("S3SS_LOG.txt")) {
            OutputDebugStringA("Failed to initialize logger\n");
        }
        
        Utils::Logger::Get().Log("Hook thread started");

        // Initialize CPU feature detection
        const auto& cpuFeatures = CPUFeatures::Get();
        Utils::Logger::Get().Log(
            std::string("[Optimization] CPU Features - SSE4.1: ") + 
            (cpuFeatures.hasSSE41 ? "Yes" : "No") + 
            ", FMA: " + (cpuFeatures.hasFMA ? "Yes" : "No"));

        // Load UI settings first
        UISettings::Get().LoadFromINI("S3SS.ini");
        
        // If settings need to be saved (file doesn't exist or missing QoL section), save them
        if (UISettings::Get().HasUnsavedChanges()) {
            UISettings::Get().SaveToINI("S3SS.ini");
            UISettings::Get().MarkAsSaved();
        }
        
        Utils::Logger::Get().Log("UI settings initialized");

        // Initialize optimization patches
        try {
            auto& optimizationManager = OptimizationManager::Get();
            Utils::Logger::Get().Log("Optimization patches registered");
            
            // Load saved optimization states
            std::string error;
            if (!optimizationManager.LoadState("S3SS.ini")) {
                Utils::Logger::Get().Log("Failed to load optimization states at startup");
            }
            else {
                Utils::Logger::Get().Log("Successfully loaded optimization states");
            }
        }
        catch (const std::exception& e) {
            Utils::Logger::Get().Log("Failed to initialize optimization patches: " + std::string(e.what()));
        }

        // Initialize settings hooks
        try {
            g_hookManager.Initialize();
            Utils::Logger::Get().Log("Settings hooks initialized");

            // Load saved settings
            std::string error;
            if (!SettingsManager::Get().LoadConfig("S3SS.ini", &error)) {
                Utils::Logger::Get().Log("Failed to load settings at startup: " + error);
            }
            else {
                Utils::Logger::Get().Log("Successfully loaded settings");
            }

            // Load QoL settings
            MemoryMonitor::Get().LoadSettings("S3SS.ini");
            Utils::Logger::Get().Log("Successfully loaded QoL settings");
        }
        catch (const std::exception& e) {
            Utils::Logger::Get().Log("Failed to initialize settings hooks: " + std::string(e.what()));
            return FALSE;
        }

        // Initialize D3D hooks
        if (!InitializeD3D9Hook()) {
            Utils::Logger::Get().Log("Failed to initialize D3D9 hook");
            return FALSE;
        }

        Utils::Logger::Get().Log("Starting message loop");

        // Message loop with timeout to prevent stack overflow
        MSG msg;
        while (true) {
            if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    Utils::Logger::Get().Log("Received WM_QUIT, exiting hook thread");
                    return 0;
                }

                // Only process window messages
                if (msg.hwnd != NULL) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            else {
                // Update memory monitor
                MemoryMonitor::Get().Update();
                
                // Sleep when no messages
                Sleep(10);
            }
        }

        return 0;
    }
    catch (const std::exception& e) {
        Utils::Logger::Get().Log("Exception in HookThread: " + std::string(e.what()));
        return 1;
    }
    catch (...) {
        Utils::Logger::Get().Log("Unknown exception in HookThread");
        return 1;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        
        // Check if we're in the correct process
        char processName[MAX_PATH];
        GetModuleFileNameA(NULL, processName, MAX_PATH);
        
        // Extract just the filename from the path
        char* fileName = strrchr(processName, '\\');
        if (fileName) {
            fileName++; // Skip the backslash
        } else {
            fileName = processName;
        }
        
        // Check if it's one of the expected executables
        if (_stricmp(fileName, "TS3.exe") != 0 && _stricmp(fileName, "TS3W.exe") != 0) {
            OutputDebugStringA("S3SS: Error - Not injected into TS3.exe or TS3W.exe. Injection aborted.\n");
            return FALSE;
        }
        
        OutputDebugStringA(("S3SS: Successfully injected into " + std::string(fileName) + "\n").c_str());
        
        g_ThreadHandle = CreateThread(NULL, 0, HookThread, NULL, 0, &g_ThreadId);
        if (g_ThreadHandle == NULL) {
            char errorMsg[256];
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errorMsg, 255, NULL);
            OutputDebugStringA(("Failed to create hook thread: " + std::string(errorMsg)).c_str());
            return FALSE;
        }
        break;
    }

    case DLL_PROCESS_DETACH: {
        if (!lpReserved) {
            // Clean up optimization patches
            auto& optimizationManager = OptimizationManager::Get();
            for (const auto& patch : optimizationManager.GetPatches()) {
                if (patch->IsEnabled()) {
                    patch->Uninstall();
                }
            }

            if (g_ThreadHandle) {
                if (g_ThreadId != 0) {
                    PostThreadMessage(g_ThreadId, WM_QUIT, 0, 0);
                }

                // Wait for thread to exit
                WaitForSingleObject(g_ThreadHandle, 5000);
                CloseHandle(g_ThreadHandle);
                g_ThreadHandle = NULL;
                g_ThreadId = 0;
            }

            g_hookManager.Cleanup();
            
            // Close logger
            Utils::Logger::Get().Close();
        }
        break;
    }
    }
    return TRUE;
}