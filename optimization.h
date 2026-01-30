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

    static constexpr auto SETTING_CHANGE_DEBOUNCE = std::chrono::seconds(2);
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

    // Debounced reinstall state to mitigate people crashing themselves :)
    std::chrono::steady_clock::time_point lastSettingChange;
    bool pendingReinstall = false;

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
        auto setting = std::make_unique<FloatSetting>(ptr, name, defaultVal, minVal, maxVal, desc, presets, uiType);
        setting->SetChangedCallback([this]() { NotifySettingChanged(); });
        settings.push_back(std::move(setting));
    }

    void RegisterIntSetting(int* ptr, const std::string& name, int defaultVal,
                           int minVal, int maxVal, const std::string& desc = "",
                           const std::vector<std::pair<std::string, int>>& presets = {},
                           SettingUIType uiType = SettingUIType::Slider) {
        auto setting = std::make_unique<IntSetting>(ptr, name, defaultVal, minVal, maxVal, desc, presets, uiType);
        setting->SetChangedCallback([this]() { NotifySettingChanged(); });
        settings.push_back(std::move(setting));
    }

    void RegisterBoolSetting(bool* ptr, const std::string& name, bool defaultVal,
                            const std::string& desc = "") {
        auto setting = std::make_unique<BoolSetting>(ptr, name, defaultVal, desc);
        setting->SetChangedCallback([this]() { NotifySettingChanged(); });
        settings.push_back(std::move(setting));
    }

    void RegisterEnumSetting(int* ptr, const std::string& name, int defaultVal,
                            const std::string& desc, const std::vector<std::string>& choices) {
        auto setting = std::make_unique<EnumSetting>(ptr, name, defaultVal, desc, choices);
        setting->SetChangedCallback([this]() { NotifySettingChanged(); });
        settings.push_back(std::move(setting));
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

    // Override for patches that need periodic updates (e.g., deferred installation)
    // Called from the main message loop
    virtual void Update() {
        // Handle debounced reinstall when settings change
        if (pendingReinstall && isEnabled.load()) {
            auto now = std::chrono::steady_clock::now();
            if (now - lastSettingChange >= SETTING_CHANGE_DEBOUNCE) {
                pendingReinstall = false;
                LOG_DEBUG("[" + patchName + "] Reinstalling after setting change");
                Uninstall();
                Install();
            }
        }
    }

    // Called by settings when their value changes in the UI, debounces reinstall to avoid rapid reinstalls while user is typing which would be bad
    void NotifySettingChanged() {
        lastSettingChange = std::chrono::steady_clock::now();
        pendingReinstall = true;
    }

    const std::string& GetName() const { return patchName; }
    bool IsEnabled() const { return isEnabled.load(); }
    double GetLastSampleRate() const { return lastSampleRate; }
    const std::string& GetLastError() const { return lastError; }
    bool PendingReinstall() const { return pendingReinstall; }
    std::chrono::steady_clock::time_point GetLastSettingChange() const { return lastSettingChange; }

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
    bool hasAVX2{ false };

    CPUFeatures();
    static const CPUFeatures& Get();
};

// Manager for all game patches (keeps the name for backward compatibility, remove later)
class OptimizationManager {
    std::vector<std::unique_ptr<OptimizationPatch>> patches;
    bool m_hasUnsavedChanges = false;

public:
    static OptimizationManager& Get();

    void RegisterPatch(std::unique_ptr<OptimizationPatch> patch);
    const std::vector<std::unique_ptr<OptimizationPatch>>& GetPatches() const;

    bool EnablePatch(const std::string& name);
    bool DisablePatch(const std::string& name);

    bool SaveState(const std::string& filename);
    bool LoadState(const std::string& filename);

    // Unsaved changes tracking
    bool HasUnsavedChanges() const { return m_hasUnsavedChanges; }
    void SetUnsavedChanges(bool unsaved) { m_hasUnsavedChanges = unsaved; }
}; 