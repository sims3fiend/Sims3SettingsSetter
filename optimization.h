#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <memory>
#include <fstream>
#include <atomic>
#include "logger.h"
#include "patch_settings.h"

// Forward declaration
struct PatchMetadata;

// Base class for optimization patches
class OptimizationPatch {
public:
    struct SampleWindow {
        volatile LONG calls{ 0 };
        std::chrono::steady_clock::time_point start;
    };

protected:
    std::mutex statsMutex;
    std::mutex patchMutex;
    SampleWindow currentWindow;
    static constexpr auto SAMPLE_INTERVAL = std::chrono::seconds(5);

    void* originalFunc;
    std::string patchName;
    std::atomic<bool> isEnabled{ false };
    double lastSampleRate = 0.0;
    PatchMetadata* metadata = nullptr;
    std::string lastError;

    // Settings storage
    std::vector<std::unique_ptr<PatchSetting>> settings;

    void MaybeSampleMinimal(LONG currentCalls);

    // Helper to set error and return false (reduces boilerplate)
    bool Fail(const std::string& msg) {
        lastError = msg;
        LOG_ERROR("[" + patchName + "] " + msg);
        return false;
    }

    // Settings registration helpers
    void RegisterFloatSetting(float* ptr, const std::string& name, SettingUIType uiType,
                             float defaultVal, float minVal, float maxVal,
                             const std::string& desc = "",
                             const std::vector<std::pair<std::string, float>>& presets = {}) {
        settings.push_back(std::make_unique<FloatSetting>(ptr, name, defaultVal, minVal, maxVal, desc, presets, uiType));
    }

    void RegisterIntSetting(int* ptr, const std::string& name, int defaultVal,
                           int minVal, int maxVal, const std::string& desc = "",
                           const std::vector<std::pair<std::string, int>>& presets = {}) {
        settings.push_back(std::make_unique<IntSetting>(ptr, name, defaultVal, minVal, maxVal, desc, presets));
    }

    void RegisterBoolSetting(bool* ptr, const std::string& name, bool defaultVal,
                            const std::string& desc = "") {
        settings.push_back(std::make_unique<BoolSetting>(ptr, name, defaultVal, desc));
    }

    void RegisterEnumSetting(int* ptr, const std::string& name, int defaultVal,
                            const std::string& desc, const std::vector<std::string>& choices) {
        settings.push_back(std::make_unique<EnumSetting>(ptr, name, defaultVal, desc, choices));
    }

    // Bind a setting to a memory address for auto-reapplication
    void BindSettingToAddress(const std::string& settingName, void* memoryAddress) {
        for (auto& setting : settings) {
            if (setting->GetName() == settingName) {
                // Use dynamic_cast to get the specific type and bind
                if (auto* floatSetting = dynamic_cast<FloatSetting*>(setting.get())) {
                    floatSetting->BindToAddress(memoryAddress);
                }
                else if (auto* intSetting = dynamic_cast<IntSetting*>(setting.get())) {
                    intSetting->BindToAddress(memoryAddress);
                }
                else if (auto* boolSetting = dynamic_cast<BoolSetting*>(setting.get())) {
                    boolSetting->BindToAddress(memoryAddress);
                }
                else if (auto* enumSetting = dynamic_cast<EnumSetting*>(setting.get())) {
                    enumSetting->BindToAddress(memoryAddress);
                }
                return;
            }
        }
    }

public:
    OptimizationPatch(const std::string& name, void* original)
        : patchName(name), originalFunc(original), lastError("") {
        currentWindow.start = std::chrono::steady_clock::now();
        currentWindow.calls = 0;
    }
    
    virtual ~OptimizationPatch() = default;

    virtual bool Install() = 0;
    virtual bool Uninstall() = 0;
    
    const std::string& GetName() const { return patchName; }
    bool IsEnabled() const { return isEnabled.load(); }
    double GetLastSampleRate() const { return lastSampleRate; }
    const std::string& GetLastError() const { return lastError; }

    // Metadata management
    void SetMetadata(const PatchMetadata& meta);
    const PatchMetadata* GetMetadata() const { return metadata; }

    // Version compatibility check
    bool IsCompatibleWithCurrentVersion() const;
    
    // Override this for custom UI in ImGui (called when patch is enabled)
    // By default, auto-renders all registered settings
    virtual void RenderCustomUI() {
        #ifdef IMGUI_VERSION
        if (!settings.empty()) {
            ImGui::Text("Settings:");
            ImGui::Separator();
            for (auto& setting : settings) {
                setting->RenderUI();
            }
        }
        #endif
    }

    static constexpr size_t GetCurrentWindowOffset() { 
        return offsetof(OptimizationPatch, currentWindow); 
    }
    
    static constexpr size_t GetCallsOffset() { 
        return offsetof(SampleWindow, calls); 
    }

    // Add methods for serialization
    virtual void SaveState(std::ofstream& file) const {
        file << "[Optimization_" << patchName << "]\n";  // Keep old format for backward compatibility, will remove later
        file << "Enabled=" << (isEnabled.load() ? "true" : "false") << "\n";

        // Save all registered settings
        for (const auto& setting : settings) {
            setting->SaveToStream(file);
        }

        file << "\n";
    }

    virtual bool LoadState(const std::string& key, const std::string& value) {
        // Handle enabled/disabled state
        if (key == "Enabled") {
            bool currentlyEnabled = isEnabled.load();
            if (value == "true" && !currentlyEnabled) {
                return Install();
            }
            else if (value == "false" && currentlyEnabled) {
                return Uninstall();
            }
            return true;
        }

        // Handle custom settings (format: Settings.settingName)
        if (key.find("Settings.") == 0) {
            std::string settingName = key.substr(9); // Remove "Settings." prefix
            for (auto& setting : settings) {
                if (setting->GetName() == settingName) {
                    return setting->LoadFromString(value);
                }
            }
        }

        return true; // Ignore unknown keys
    }
};

// CPU feature detection
struct CPUFeatures {
    bool hasSSE41{ false };
    bool hasFMA{ false };

    CPUFeatures();
    static const CPUFeatures& Get();
};

// Manager for all game patches (keeps the name for backward compatibility, remove later)
class OptimizationManager {
    std::vector<std::unique_ptr<OptimizationPatch>> patches;

public:
    static OptimizationManager& Get();
    
    void RegisterPatch(std::unique_ptr<OptimizationPatch> patch);
    const std::vector<std::unique_ptr<OptimizationPatch>>& GetPatches() const;
    
    bool EnablePatch(const std::string& name);
    bool DisablePatch(const std::string& name);


    bool SaveState(const std::string& filename);
    bool LoadState(const std::string& filename);
}; 