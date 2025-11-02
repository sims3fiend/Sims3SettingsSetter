#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <Windows.h>
#include "imgui.h"

// UI type for settings
enum class SettingUIType {
    InputBox,   // Text input box for exact values
    Slider,     // Slider for dragging values
    Drag        // DragFloat widget (compact inline control)
};

// Base class for all patch settings
class PatchSetting {
public:
    virtual ~PatchSetting() = default;

    virtual void RenderUI() = 0;
    virtual void SaveToStream(std::ofstream& file) const = 0;
    virtual bool LoadFromString(const std::string& value) = 0;
    virtual std::string GetName() const = 0;
    virtual void ResetToDefault() = 0;

protected:
    // Helper to write to protected memory
    void WriteToMemory(void* address, const void* data, size_t size) {
        if (!address) return;

        DWORD oldProtect;
        if (VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(address, data, size);
            VirtualProtect(address, size, oldProtect, &oldProtect);
        }
    }
};

// Float setting with configurable UI type and optional presets
class FloatSetting : public PatchSetting {
private:
    float* valuePtr;
    std::string name;
    float defaultValue;
    float minValue;
    float maxValue;
    std::string description;
    std::vector<std::pair<std::string, float>> presets;
    SettingUIType uiType;
    void* boundAddress = nullptr;

public:
    FloatSetting(float* ptr, const std::string& name, float defaultVal,
                 float minVal, float maxVal, const std::string& desc = "",
                 const std::vector<std::pair<std::string, float>>& presets = {},
                 SettingUIType uiType = SettingUIType::InputBox)
        : valuePtr(ptr), name(name), defaultValue(defaultVal),
          minValue(minVal), maxValue(maxVal), description(desc), presets(presets), uiType(uiType) {
        *valuePtr = defaultValue;
    }

    void BindToAddress(void* address) { boundAddress = address; }

    void RenderUI() override {
        #ifdef IMGUI_VERSION
        if (!description.empty()) {
            ImGui::Text("%s", description.c_str());
        }

        bool changed = false;

        // Render presets if available
        if (!presets.empty()) {
            ImGui::Text("Presets:");
            for (const auto& [label, value] : presets) {
                if (ImGui::Button(label.c_str())) {
                    *valuePtr = value;
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::Separator();
            ImGui::Spacing();
        }

        // Render input widget based on UI type
        ImGui::Text("Custom Value:");
        switch (uiType) {
            case SettingUIType::Slider:
                if (ImGui::SliderFloat(name.c_str(), valuePtr, minValue, maxValue, "%.5f")) {
                    changed = true;
                }
                break;

            case SettingUIType::Drag:
                if (ImGui::DragFloat(name.c_str(), valuePtr, (maxValue - minValue) / 100.0f, minValue, maxValue, "%.5f")) {
                    changed = true;
                }
                break;

            case SettingUIType::InputBox:
            default:
                if (ImGui::InputFloat(name.c_str(), valuePtr, 0.0f, 0.0f, "%.5f")) {
                    // Clamp to bounds
                    if (*valuePtr < minValue) *valuePtr = minValue;
                    if (*valuePtr > maxValue) *valuePtr = maxValue;
                    changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(%.5f - %.5f)", minValue, maxValue);
                break;
        }

        // Auto-reapply to memory if bound
        if (changed && boundAddress) {
            WriteToMemory(boundAddress, valuePtr, sizeof(float));
        }
        #endif
    }

    void SaveToStream(std::ofstream& file) const override {
        file << "Settings." << name << "=" << *valuePtr << "\n";
    }

    bool LoadFromString(const std::string& value) override {
        try {
            float newValue = std::stof(value);
            // Clamp to bounds
            if (newValue < minValue) newValue = minValue;
            if (newValue > maxValue) newValue = maxValue;
            *valuePtr = newValue;

            // Apply to memory if bound
            if (boundAddress) {
                WriteToMemory(boundAddress, valuePtr, sizeof(float));
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string GetName() const override { return name; }
    void ResetToDefault() override { *valuePtr = defaultValue; }
};

// Int setting with slider and optional presets
class IntSetting : public PatchSetting {
private:
    int* valuePtr;
    std::string name;
    int defaultValue;
    int minValue;
    int maxValue;
    std::string description;
    std::vector<std::pair<std::string, int>> presets;
    void* boundAddress = nullptr;

public:
    IntSetting(int* ptr, const std::string& name, int defaultVal,
               int minVal, int maxVal, const std::string& desc = "",
               const std::vector<std::pair<std::string, int>>& presets = {})
        : valuePtr(ptr), name(name), defaultValue(defaultVal),
          minValue(minVal), maxValue(maxVal), description(desc), presets(presets) {
        *valuePtr = defaultValue;
    }

    void BindToAddress(void* address) { boundAddress = address; }

    void RenderUI() override {
        #ifdef IMGUI_VERSION
        if (!description.empty()) {
            ImGui::Text("%s", description.c_str());
        }

        bool changed = false;

        // Render presets if available
        if (!presets.empty()) {
            ImGui::Text("Presets:");
            for (const auto& [label, value] : presets) {
                if (ImGui::Button(label.c_str())) {
                    *valuePtr = value;
                    changed = true;
                }
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::Separator();
        }

        // Render slider
        if (ImGui::SliderInt(name.c_str(), valuePtr, minValue, maxValue)) {
            changed = true;
        }

        // Auto-reapply to memory if bound
        if (changed && boundAddress) {
            WriteToMemory(boundAddress, valuePtr, sizeof(int));
        }
        #endif
    }

    void SaveToStream(std::ofstream& file) const override {
        file << "Settings." << name << "=" << *valuePtr << "\n";
    }

    bool LoadFromString(const std::string& value) override {
        try {
            int newValue = std::stoi(value);
            // Clamp to bounds
            if (newValue < minValue) newValue = minValue;
            if (newValue > maxValue) newValue = maxValue;
            *valuePtr = newValue;

            // Apply to memory if bound
            if (boundAddress) {
                WriteToMemory(boundAddress, valuePtr, sizeof(int));
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    std::string GetName() const override { return name; }
    void ResetToDefault() override { *valuePtr = defaultValue; }
};

// Bool setting with checkbox
class BoolSetting : public PatchSetting {
private:
    bool* valuePtr;
    std::string name;
    bool defaultValue;
    std::string description;
    void* boundAddress = nullptr;

public:
    BoolSetting(bool* ptr, const std::string& name, bool defaultVal,
                const std::string& desc = "")
        : valuePtr(ptr), name(name), defaultValue(defaultVal), description(desc) {
        *valuePtr = defaultValue;
    }

    void BindToAddress(void* address) { boundAddress = address; }

    void RenderUI() override {
        #ifdef IMGUI_VERSION
        bool changed = false;

        if (ImGui::Checkbox(name.c_str(), valuePtr)) {
            changed = true;
        }

        if (!description.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", description.c_str());
            }
        }

        // Auto-reapply to memory if bound
        if (changed && boundAddress) {
            WriteToMemory(boundAddress, valuePtr, sizeof(bool));
        }
        #endif
    }

    void SaveToStream(std::ofstream& file) const override {
        file << "Settings." << name << "=" << (*valuePtr ? "true" : "false") << "\n";
    }

    bool LoadFromString(const std::string& value) override {
        if (value == "true" || value == "1") {
            *valuePtr = true;
        } else if (value == "false" || value == "0") {
            *valuePtr = false;
        } else {
            return false;
        }

        // Apply to memory if bound
        if (boundAddress) {
            WriteToMemory(boundAddress, valuePtr, sizeof(bool));
        }
        return true;
    }

    std::string GetName() const override { return name; }
    void ResetToDefault() override { *valuePtr = defaultValue; }
};

// Enum/Choice setting with dropdown
class EnumSetting : public PatchSetting {
private:
    int* valuePtr;
    std::string name;
    int defaultValue;
    std::string description;
    std::vector<std::string> choices;
    void* boundAddress = nullptr;

public:
    EnumSetting(int* ptr, const std::string& name, int defaultVal,
                const std::string& desc, const std::vector<std::string>& choices)
        : valuePtr(ptr), name(name), defaultValue(defaultVal),
          description(desc), choices(choices) {
        *valuePtr = defaultValue;
    }

    void BindToAddress(void* address) { boundAddress = address; }

    void RenderUI() override {
        #ifdef IMGUI_VERSION
        if (!description.empty()) {
            ImGui::Text("%s", description.c_str());
        }

        bool changed = false;

        // Create combo box
        const char* currentChoice = (*valuePtr >= 0 && *valuePtr < static_cast<int>(choices.size()))
                                    ? choices[*valuePtr].c_str()
                                    : "Invalid";

        if (ImGui::BeginCombo(name.c_str(), currentChoice)) {
            for (size_t i = 0; i < choices.size(); i++) {
                bool isSelected = (*valuePtr == static_cast<int>(i));
                if (ImGui::Selectable(choices[i].c_str(), isSelected)) {
                    *valuePtr = static_cast<int>(i);
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // Auto-reapply to memory if bound
        if (changed && boundAddress) {
            WriteToMemory(boundAddress, valuePtr, sizeof(int));
        }
        #endif
    }

    void SaveToStream(std::ofstream& file) const override {
        file << "Settings." << name << "=" << *valuePtr << "\n";
    }

    bool LoadFromString(const std::string& value) override {
        try {
            int newValue = std::stoi(value);
            if (newValue >= 0 && newValue < static_cast<int>(choices.size())) {
                *valuePtr = newValue;

                // Apply to memory if bound
                if (boundAddress) {
                    WriteToMemory(boundAddress, valuePtr, sizeof(int));
                }
                return true;
            }
        } catch (...) {}
        return false;
    }

    std::string GetName() const override { return name; }
    void ResetToDefault() override { *valuePtr = defaultValue; }
};
