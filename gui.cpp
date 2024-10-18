#include "gui.h"
#include "renderer.h"
#include "utils.h"
#include "imgui.h"
#include "hooks.h"
#include "preset.h"
#include "script_settings.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

std::unordered_map<std::string, std::vector<bool>> functionSettingsAvailability;
extern std::unordered_map<std::string, uintptr_t> hookedFunctionBaseAddresses;

static bool showClearSettingsConfirmation = false;


// Debounce mechanism
std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastChangeTime;
const std::chrono::milliseconds debounceDelay(500); // 500ms debounce delay

// Function to save setting with debounce
void DebouncedSaveSetting(const std::string& section, const std::string& key, const std::variant<bool, std::string>& value) {
    auto now = std::chrono::steady_clock::now();
    lastChangeTime[section + "." + key] = now;

    std::thread([section, key, value, now]() {
        std::this_thread::sleep_for(debounceDelay);
        if (lastChangeTime[section + "." + key] == now) {
            ScriptSettings::Instance().SetSettingValue(section, key, value);
            ScriptSettings::Instance().SaveToFile();
            Log("Saved setting " + section + "." + key);
        }
        }).detach();
}

// Function to apply saved settings
void ApplySavedSettings(const std::string& funcName, const std::string& section, const std::unordered_map<std::string, SettingInfo>* settingsMap, uintptr_t baseAddress) {
    if (!settingsMap) return;

    for (const auto& [key, settingInfo] : *settingsMap) {
        if (ScriptSettings::Instance().IsSettingSaved(section, key)) {
            auto savedValue = ScriptSettings::Instance().GetSettingValue(section, key);
            try {
                std::variant<bool, int, float, std::string> parsedValue;

                if (std::holds_alternative<bool>(savedValue)) {
                    parsedValue = std::get<bool>(savedValue);
                }
                else {
                    parsedValue = ParseSettingValue(settingInfo.type, std::get<std::string>(savedValue));
                }

                EditSetting(key, *settingsMap, baseAddress, parsedValue);
                Log("Applied saved setting " + section + "." + key);
            }
            catch (const std::exception& e) {
                Log("Error applying saved setting " + section + "." + key + ": " + e.what());
            }
        }
    }
}

static bool imguiInitialized = false;
static bool showImGui = false;
static HWND hWnd = NULL;
static WNDPROC oWndProc = NULL;

D3DPRESENT_PARAMETERS g_d3dpp;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool IsGUIInitialized() {
    return imguiInitialized;
}

void LoadRenderSettings() {
    ScriptSettings& settings = ScriptSettings::Instance();
    void* rendererAddress = GetRendererStructureAddress();

    if (!rendererAddress) {
        Log("Renderer structure address not available. Cannot apply render settings.");
        return;
    }

    for (const auto& [type, info] : toggles) {
        auto value = settings.GetSettingValue("RenderSettings", info.name, ReadBoolValue(info.offset));
        if (std::holds_alternative<bool>(value)) {
            WriteBoolValue(info.offset, std::get<bool>(value));
            Log("Applied " + std::string(info.name) + " setting: " + (std::get<bool>(value) ? "true" : "false"));
        }
    }
}

void InitializeGUI(LPDIRECT3DDEVICE9 pDevice) {
    D3DDEVICE_CREATION_PARAMETERS params;
    pDevice->GetCreationParameters(&params);
    hWnd = params.hFocusWindow;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    oWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX9_Init(pDevice);

    LoadRenderSettings();

    imguiInitialized = true;
}

void RenderGUI(LPDIRECT3DDEVICE9 pDevice) {
    HRESULT hr = pDevice->TestCooperativeLevel();
    if (hr != D3D_OK) {
        // Device is lost, skip rendering
        return;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (showImGui) {
        DrawImGuiInterface();
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
}

void CleanupGUI() {
    Log("Cleaning up GUI");
    if (imguiInitialized) {
        ImGui_ImplWin32_Shutdown();
        if (oWndProc && hWnd) {
            SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        }
        imguiInitialized = false;
        showImGui = false;
        hWnd = NULL;
        oWndProc = NULL;
    }
    Log("GUI cleanup completed");
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Pass messages to ImGui
    ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

    // Toggle UI with VK_INSERT
    if (msg == WM_KEYDOWN && wParam == VK_INSERT) {
        showImGui = !showImGui;
        Log("Toggled ImGui visibility: " + std::to_string(showImGui));
        return 0; // We handled the message
    }

    ImGuiIO& io = ImGui::GetIO();

    // When ImGui wants to capture input
    if (showImGui && (io.WantCaptureMouse || io.WantCaptureKeyboard)) {
        // Return 0 for input messages
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
            (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) {
            return 0;
        }
    }

    // Pass unhandled messages to the original window procedure
    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}



struct FunctionSettingsMaps {
    std::vector<std::pair<std::string, const std::unordered_map<std::string, SettingInfo>*>> maps;
    bool isMultiMap;
};

void ClearAllSettings() {
    ScriptSettings& scriptSettings = ScriptSettings::Instance();
    auto& allSettings = scriptSettings.GetAllSettings();

    for (const auto& [fullKey, entry] : allSettings) {
        size_t dotPos = fullKey.find('.');
        if (dotPos != std::string::npos) {
            std::string section = fullKey.substr(0, dotPos);
            std::string key = fullKey.substr(dotPos + 1);
            scriptSettings.SetSettingSaved(section, key, false);
        }
    }

    scriptSettings.SaveToFile();
    Log("All settings cleared and saved.");
}

const std::vector<std::pair<std::string, const std::unordered_map<std::string, SettingInfo>*>> GetSettingsMapsForFunction(const std::string& funcName) {
    static const std::unordered_map<std::string, std::vector<std::pair<std::string, const std::unordered_map<std::string, SettingInfo>*>>> settingsMaps = {
        {"FUN_0082f910", {
            {"World Router Tuning", &routingWorldRouterTuningSettings},
            {"Lot Transition Tuning", &routingLotTransitionTuningSettings},
            {"World Builder", &routingWorldBuilderSettings}
        }},
        {"FUN_00816610", {{"Routing Debug", &routingDebugSettings}}},
        {"FUN_009172b0", {{"Visual Effects", &visualEffectsSettings}}},
        {"FUN_00c671e0", {{"Streaming", &streamingSettings}}},
        {"FUN_00d6cc30", {{"Weather", &weatherSettings}}},
        {"FUN_00572490", {{"Animation", &animationSettings}}},
        {"FUN_00c128c0", {{"Sky Common", &skyCommonSettings}}},
        {"FUN_006c35e0", {{"World Lighting", &worldLightingSettings}}},
        {"FUN_007235b0", {{"Shadows", &shadowsSettings}}},
        {"FUN_006e7b60", {{"Streaming LOD", &streamingLODSettings}}}
    };

    auto it = settingsMaps.find(funcName);
    if (it != settingsMaps.end()) {
        return it->second;
    }
    return {};
}

std::string GetFriendlyNameForHook(const std::string& funcName) {
    static const std::unordered_map<std::string, std::string> friendlyNames = {
        {"FUN_0096dc30", "Camera Settings"},
        {"FUN_0082f910", "Routing Settings"},
        {"FUN_00816610", "Routing Debug Settings"},
        {"FUN_009172b0", "Visual Effects Settings"},
        {"FUN_00c671e0", "Streaming Settings"},
        {"FUN_00d6cc30", "Weather Settings"},
        {"FUN_00572490", "Animation Settings"},
        {"FUN_00c128c0", "Sky Common Settings"},
        {"FUN_006c35e0", "World Lighting Settings"},
        {"FUN_007235b0", "Shadow Settings"},
        {"FUN_006e7b60", "Streaming LOD Settings"}
    };

    auto it = friendlyNames.find(funcName);
    if (it != friendlyNames.end()) {
        return it->second;
    }
    return funcName; // Return original name if no friendly name is found
}

std::variant<bool, std::string> ConvertToScriptSettingValue(const std::variant<bool, int, float, std::string>& value) {
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value);
    }
    else if (std::holds_alternative<int>(value)) {
        return std::to_string(std::get<int>(value));
    }
    else if (std::holds_alternative<float>(value)) {
        return std::to_string(std::get<float>(value));
    }
    else {
        return std::get<std::string>(value);
    }
}

//will merge one day...
void DrawRendererToggles() {
    if (ImGui::CollapsingHeader("Render Toggles")) {
        void* rendererAddress = GetRendererStructureAddress();
        if (!rendererAddress) {
            ImGui::Text("Render structure not initialized.");
            return;
        }

        for (const auto& [toggleType, toggleInfo] : toggles) {
            bool value = ReadBoolValue(toggleInfo.offset);
            if (ImGui::Checkbox(toggleInfo.name, &value)) {
                WriteBoolValue(toggleInfo.offset, value);
                Log("Updated " + std::string(toggleInfo.name) + " to " + (value ? "true" : "false"));
            }
        }
    }
}

void DrawSettingsForMap(const std::string& section, const std::unordered_map<std::string, SettingInfo>* settingsMap,
    const std::string& funcName, uintptr_t baseAddress,
    std::vector<bool>& availabilityVector) {

    if (!settingsMap) {
        ImGui::Text("Settings not available yet.");
        return;
    }

    int index = 0;
    for (const auto& [key, settingInfo] : *settingsMap) {
        ImGui::PushID(key.c_str());

        uintptr_t address = (settingInfo.addressType == AddressType::Offset) ?
            baseAddress + settingInfo.address : settingInfo.address;

        if (index < availabilityVector.size() && availabilityVector[index]) {
            ImGui::Text("%s:", key.c_str());

            bool isSaved = ScriptSettings::Instance().IsSettingSaved(section, key);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 0));
            if (ImGui::Checkbox("##Save", &isSaved)) {
                ScriptSettings::Instance().SetSettingSaved(section, key, isSaved);
                if (isSaved) {
                    try {
                        auto currentValue = ReadSettingValue(settingInfo, baseAddress);
                        std::variant<bool, std::string> convertedValue = ConvertToScriptSettingValue(currentValue);
                        DebouncedSaveSetting(section, key, convertedValue);
                    }
                    catch (const std::exception& e) {
                        Log("Error saving setting " + section + "." + key + ": " + e.what());
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::SameLine();

            ImGui::PushItemWidth(-1);
            bool valueChanged = false;
            switch (settingInfo.type) {
            case SettingType::Boolean: {
                bool value = ReadValue<bool>(settingInfo, baseAddress);
                const char* items[] = { "False", "True" };
                int current_item = value ? 1 : 0;
                if (ImGui::Combo("##bool", &current_item, items, IM_ARRAYSIZE(items))) {
                    bool newValue = (current_item == 1);
                    if (newValue != value) {
                        WriteValue<bool>(settingInfo, baseAddress, newValue);
                        Log("Updated " + key + " to " + (newValue ? "true" : "false"));
                        valueChanged = true;
                    }
                }
                break;
            }
            case SettingType::Float: {
                float value = ReadValue<float>(settingInfo, baseAddress);
                float newValue = value;
                if (ImGui::DragFloat("##float", &newValue, 0.01f,
                    settingInfo.minMax ? settingInfo.minMax->first : 0.0f,
                    settingInfo.minMax ? settingInfo.minMax->second : 0.0f)) {
                    if (newValue != value) {
                        WriteValue<float>(settingInfo, baseAddress, newValue);
                        Log("Updated " + key + " to " + std::to_string(newValue));
                        valueChanged = true;
                    }
                }
                break;
            }
            case SettingType::Integer: {
                int value = ReadValue<int>(settingInfo, baseAddress);
                if (ImGui::DragInt("##int", &value, 0.1f,
                    settingInfo.minMax ? static_cast<int>(settingInfo.minMax->first) : 0,
                    settingInfo.minMax ? static_cast<int>(settingInfo.minMax->second) : 0)) {
                    WriteValue<int>(settingInfo, baseAddress, value);
                    Log("Updated " + key + " to " + std::to_string(value));
                    valueChanged = true;
                }
                break;
            }
            case SettingType::UnsignedInteger: {
                unsigned int value = ReadValue<unsigned int>(settingInfo, baseAddress);
                if (ImGui::DragScalar("##uint", ImGuiDataType_U32, &value, 0.1f,
                    settingInfo.minMax ? &settingInfo.minMax->first : nullptr,
                    settingInfo.minMax ? &settingInfo.minMax->second : nullptr)) {
                    WriteValue<unsigned int>(settingInfo, baseAddress, value);
                    Log("Updated " + key + " to " + std::to_string(value));
                    valueChanged = true;
                }
                break;
            }
            case SettingType::FloatArray2: {
                float values[2];
                memcpy(values, reinterpret_cast<void*>(address), sizeof(float) * 2);
                if (ImGui::DragFloat2("##float2", values, 0.01f)) {
                    memcpy(reinterpret_cast<void*>(address), values, sizeof(float) * 2);
                    Log("Updated " + key);
                    valueChanged = true;
                }
                break;
            }
            case SettingType::FloatArray3: {
                float values[3];
                memcpy(values, reinterpret_cast<void*>(address), sizeof(float) * 3);
                if (ImGui::DragFloat3("##float3", values, 0.01f)) {
                    memcpy(reinterpret_cast<void*>(address), values, sizeof(float) * 3);
                    Log("Updated " + key);
                    valueChanged = true;
                }
                break;
            }
            case SettingType::FloatArray4: {
                float values[4];
                memcpy(values, reinterpret_cast<void*>(address), sizeof(float) * 4);
                if (ImGui::DragFloat4("##float4", values, 0.01f)) {
                    memcpy(reinterpret_cast<void*>(address), values, sizeof(float) * 4);
                    Log("Updated " + key);
                    valueChanged = true;
                }
                break;
            }
            case SettingType::WideString: {
                // Display wide string (read-only for now)
                std::wstring wideValue = *reinterpret_cast<std::wstring*>(address);
                ImGui::Text("%s", LoggingHelpers::WideToNarrow(wideValue).c_str());
                break;
            }
            case SettingType::Pointer:
            case SettingType::Unknown:
            default: {
                // For unsupported types, just display the current value
                std::string currentValue = settingInfo.logFunction(reinterpret_cast<void*>(address));
                ImGui::Text("%s", currentValue.c_str());
                break;
            }
            }
            ImGui::PopItemWidth();

            if (valueChanged && isSaved) {
                auto currentValue = ReadSettingValue(settingInfo, baseAddress);
                std::variant<bool, std::string> convertedValue = ConvertToScriptSettingValue(currentValue);
                DebouncedSaveSetting(section, key, convertedValue);
            }
        }
        else {
            ImGui::Text("%s: Not yet available", key.c_str());
        }

        ImGui::PopID();
        index++;
    }
}

void DrawPresetsTab() {
    ScriptSettings& scriptSettings = ScriptSettings::Instance();
    std::vector<Preset> presets = scriptSettings.GetAllPresets();

    static char presetNameBuffer[128] = "";
    static bool showCreatePresetPopup = false;
    static bool showExportPresetPopup = false;
    static bool showImportPresetPopup = false;
    static std::string selectedPresetToExport = "";
    static std::string selectedPresetToDelete = "";

    // Variables for Load Confirmation
    static bool showLoadConfirmationPopup = false;
    static Preset presetToLoad;

    // Variables for Success Notification
    static bool showSuccessNotification = false;
    static std::string successMessage = "";

    // Button to open Create Preset popup
    if (ImGui::Button("Create New Preset")) {
        memset(presetNameBuffer, 0, sizeof(presetNameBuffer));
        ImGui::OpenPopup("Create Preset");
    }

    // Create Preset Popup
    if (ImGui::BeginPopupModal("Create Preset", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Preset Name", presetNameBuffer, IM_ARRAYSIZE(presetNameBuffer));

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string presetName(presetNameBuffer);
            if (!presetName.empty()) {
                // Sanitize preset name (this is in utils btw)
                presetName = SanitizeFileName(presetName);

                // Check for duplicate names
                bool exists = std::any_of(presets.begin(), presets.end(), [&](const Preset& p) {
                    return p.name == presetName;
                    });
                if (exists) {
                    Log("Preset with name '" + presetName + "' already exists.");
                }
                else {
                    Preset newPreset;
                    newPreset.name = presetName;

                    // Capture all settings
                    for (const auto& [fullKey, entry] : scriptSettings.GetAllSettings()) {
                        if (entry.saved) {
                            newPreset.settings[fullKey] = entry.value;
                        }
                    }

                    // Save the preset
                    if (scriptSettings.SavePreset(newPreset)) {
                        Log("Preset '" + presetName + "' created successfully.");
                        showSuccessNotification = true;
                        successMessage = "Preset '" + presetName + "' created successfully.";
                    }
                    else {
                        Log("Failed to create preset '" + presetName + "'.");
                    }

                    memset(presetNameBuffer, 0, sizeof(presetNameBuffer)); // Clear buffer
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            memset(presetNameBuffer, 0, sizeof(presetNameBuffer)); // Clear buffer
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // List all presets
    for (size_t i = 0; i < presets.size(); ++i) {
        const Preset& preset = presets[i];
        std::string header = preset.name;
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::Text("Settings:");
            for (const auto& [key, value] : preset.settings) {
                ImGui::BulletText("%s: %s", key.c_str(),
                    std::holds_alternative<bool>(value) ? (std::get<bool>(value) ? "true" : "false") : std::get<std::string>(value).c_str());
            }

            // Buttons for Load, Export, and Delete
            if (ImGui::Button(("Load##" + std::to_string(i)).c_str())) {
                presetToLoad = preset; // Set the preset to load
                showLoadConfirmationPopup = true; // Open confirmation popup
            }
            ImGui::SameLine();
            if (ImGui::Button(("Export##" + std::to_string(i)).c_str())) {
                selectedPresetToExport = preset.name;
                ImGui::OpenPopup("Export Preset");
            }
            ImGui::SameLine();
            if (ImGui::Button(("Delete##" + std::to_string(i)).c_str())) {
                // Confirm deletion
                selectedPresetToDelete = preset.name;
                ImGui::OpenPopup("Confirm Delete");
            }

            ImGui::Separator();
        }
    }

    // Load Confirmation Popup
    if (showLoadConfirmationPopup) {
        ImGui::OpenPopup("Load Preset Confirmation");
        showLoadConfirmationPopup = false;
    }

    if (ImGui::BeginPopupModal("Load Preset Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Loading preset '%s' will overwrite your current settings.", presetToLoad.name.c_str());
        ImGui::Text("Are you sure you want to proceed?");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            if (scriptSettings.LoadPreset(presetToLoad)) {
                scriptSettings.SaveToFile();
                Log("Preset '" + presetToLoad.name + "' loaded and settings saved.");
                showSuccessNotification = true;
                successMessage = "Preset '" + presetToLoad.name + "' loaded successfully.";
            }
            else {
                Log("Failed to load preset '" + presetToLoad.name + "'.");
                showSuccessNotification = true;
                successMessage = "Failed to load preset '" + presetToLoad.name + "'.";
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Export Preset Popup
    if (ImGui::BeginPopupModal("Export Preset", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export Preset: %s", selectedPresetToExport.c_str());

        static char exportPathBuffer[256] = "presets/";
        ImGui::InputText("Export Path", exportPathBuffer, IM_ARRAYSIZE(exportPathBuffer));

        if (ImGui::Button("Export", ImVec2(120, 0))) {
            std::string exportPath(exportPathBuffer);
            if (exportPath.empty()) {
                Log("Export path is empty.");
                showSuccessNotification = true;
                successMessage = "Export path is empty.";
            }
            else {
                // Ensure the export path has .ini extension
                fs::path exportFilePath = exportPath;
                if (exportFilePath.extension() != ".ini") {
                    exportFilePath += ".ini";
                }
                fs::path presetFilePath = scriptSettings.GetPresetsDirectory() / (selectedPresetToExport + ".ini");
                if (fs::exists(presetFilePath)) {
                    try {
                        fs::copy_file(presetFilePath, exportFilePath, fs::copy_options::overwrite_existing);
                        Log("Preset exported to: " + exportFilePath.string());
                        showSuccessNotification = true;
                        successMessage = "Preset exported to: " + exportFilePath.string();
                    }
                    catch (const fs::filesystem_error& e) {
                        Log("Failed to export preset: " + std::string(e.what()));
                        showSuccessNotification = true;
                        successMessage = "Failed to export preset: " + std::string(e.what());
                    }
                }
                else {
                    Log("Preset file does not exist: " + presetFilePath.string());
                    showSuccessNotification = true;
                    successMessage = "Preset file does not exist: " + presetFilePath.string();
                }
            }
            memset(exportPathBuffer, 0, sizeof(exportPathBuffer));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            memset(exportPathBuffer, 0, sizeof(exportPathBuffer));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Delete Confirmation Popup
    if (ImGui::BeginPopupModal("Confirm Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete preset '%s'?", selectedPresetToDelete.c_str());

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            if (scriptSettings.RemovePreset(selectedPresetToDelete)) {
                Log("Preset '" + selectedPresetToDelete + "' deleted successfully.");
                showSuccessNotification = true;
                successMessage = "Preset '" + selectedPresetToDelete + "' deleted successfully.";
            }
            else {
                Log("Failed to delete preset '" + selectedPresetToDelete + "'.");
                showSuccessNotification = true;
                successMessage = "Failed to delete preset '" + selectedPresetToDelete + "'.";
            }
            selectedPresetToDelete = "";
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            selectedPresetToDelete = "";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Import Preset Button
    if (ImGui::Button("Import Preset")) {
        ImGui::OpenPopup("Import Preset");
    }

    // Import Preset Popup
    if (ImGui::BeginPopupModal("Import Preset", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char importPathBuffer[256] = "presets/imported_preset.ini";
        ImGui::InputText("Import Path", importPathBuffer, IM_ARRAYSIZE(importPathBuffer));

        if (ImGui::Button("Import", ImVec2(120, 0))) {
            std::string importPath(importPathBuffer);
            if (importPath.empty()) {
                Log("Import path is empty.");
                showSuccessNotification = true;
                successMessage = "Import path is empty.";
            }
            else {
                Preset importedPreset;
                if (LoadPresetFromIni(importedPreset, importPath)) {
                    // Sanitize preset name
                    importedPreset.name = SanitizeFileName(importedPreset.name);
                    if (scriptSettings.SavePreset(importedPreset)) {
                        Log("Preset imported: " + importedPreset.name);
                        showSuccessNotification = true;
                        successMessage = "Preset imported: " + importedPreset.name;
                    }
                    else {
                        Log("Failed to save imported preset: " + importedPreset.name);
                        showSuccessNotification = true;
                        successMessage = "Failed to save imported preset: " + importedPreset.name;
                    }
                }
                else {
                    Log("Failed to load preset from: " + importPath);
                    showSuccessNotification = true;
                    successMessage = "Failed to load preset from: " + importPath;
                }
            }
            memset(importPathBuffer, 0, sizeof(importPathBuffer));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            memset(importPathBuffer, 0, sizeof(importPathBuffer));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Success Notification Popup
    if (showSuccessNotification) {
        ImGui::OpenPopup("Notification");
        showSuccessNotification = false;
    }

    if (ImGui::BeginPopupModal("Notification", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", successMessage.c_str());
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawLiveEditTab() {
    for (const auto& [funcName, baseAddress] : hookedFunctionBaseAddresses) {
        std::string friendlyName = GetFriendlyNameForHook(funcName);
        if (ImGui::CollapsingHeader(friendlyName.c_str())) {
            ImGui::Text("Base Address: 0x%X", baseAddress);
            auto settingsMaps = GetSettingsMapsForFunction(funcName);
            if (settingsMaps.size() > 1) {
                // Special handling for functions with multiple settings maps
                ImGui::Indent(20.0f);
                for (const auto& [section, settingsMap] : settingsMaps) {
                    if (settingsMap) {
                        std::string availabilityKey = funcName + "_" + section;
                        if (functionSettingsAvailability.find(availabilityKey) == functionSettingsAvailability.end()) {
                            functionSettingsAvailability[availabilityKey] = std::vector<bool>(settingsMap->size(), false);
                        }
                        // Apply saved settings when they become available
                        bool anyAvailable = std::any_of(functionSettingsAvailability[availabilityKey].begin(),
                            functionSettingsAvailability[availabilityKey].end(),
                            [](bool v) { return v; });
                        if (anyAvailable) {
                            ApplySavedSettings(funcName, section, settingsMap, baseAddress);
                        }
                        if (ImGui::TreeNode(section.c_str())) {
                            DrawSettingsForMap(section, settingsMap, funcName, baseAddress, functionSettingsAvailability[availabilityKey]);
                            ImGui::TreePop();
                        }
                    }
                }
                ImGui::Unindent(20.0f);
            }
            else {
                // For functions with a single settings map
                for (const auto& [section, settingsMap] : settingsMaps) {
                    if (settingsMap) {
                        std::string availabilityKey = funcName + "_" + section;
                        if (functionSettingsAvailability.find(availabilityKey) == functionSettingsAvailability.end()) {
                            functionSettingsAvailability[availabilityKey] = std::vector<bool>(settingsMap->size(), false);
                        }
                        // Apply saved settings when they become available
                        bool anyAvailable = std::any_of(functionSettingsAvailability[availabilityKey].begin(),
                            functionSettingsAvailability[availabilityKey].end(),
                            [](bool v) { return v; });
                        if (anyAvailable) {
                            ApplySavedSettings(funcName, section, settingsMap, baseAddress);
                        }
                        DrawSettingsForMap(section, settingsMap, funcName, baseAddress, functionSettingsAvailability[availabilityKey]);
                    }
                }
            }
        }
    }
    DrawRendererToggles();
}

void DrawImGuiInterface() {
    void* rendererAddress = GetRendererStructureAddress();
    if (!rendererAddress) {
        ImGui::Begin("Renderer Not Initialized");
        ImGui::Text("Waiting for renderer structure address...");
        ImGui::End();
        return;
    }
    static bool showUnsettableOptions = false;

    ImGui::Begin("S3Settings Setter");

    ImGui::Text("Press Insert to toggle UI visibility");
    ImGui::Text("THIS IS A BETA VERSION");
    ImGui::Text("If you experience a crash please report it :)");
    ImGui::Text("Renderer Structure Address: 0x%X", reinterpret_cast<uintptr_t>(rendererAddress));
    ImGui::Separator();

    if (ImGui::BeginTabBar("MainTabBar")) {
        if (ImGui::BeginTabItem("Live Edit")) {
            ImGui::Text("Note: Some settings won't appear until the game is loaded");
            DrawLiveEditTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Game Config")) {
            ScriptSettings& scriptSettings = ScriptSettings::Instance();
            const auto& settings = scriptSettings.GetAllSettings();

            ImGui::Text("Game configuration settings");
            ImGui::TextWrapped("These settings are populated from the currently used settings.\nWARNING: Options settings will overwrite the value in your Options.ini file!");
            ImGui::TextWrapped("You have to hit save down the bottom to save them, the box must also be clicked next to it (same for live settings)");

            // Debug information
            ImGui::Text("Total settings: %d", static_cast<int>(settings.size()));

            // Organize settings by section
            std::unordered_map<std::string, std::vector<std::pair<std::string, const SettingEntry*>>> categorizedSettings;
            for (const auto& [fullKey, settingEntry] : settings) {
                size_t dotPos = fullKey.find('.');
                if (dotPos != std::string::npos) {
                    std::string section = fullKey.substr(0, dotPos);
                    std::string key = fullKey.substr(dotPos + 1);
                    bool isSettableSection = (section == "Config" || section == "Options");
                    bool isHiddenPrefix = (key.substr(0, 3) == "Cfg" || key.substr(0, 6) == "Assets");
                    if (showUnsettableOptions || (isSettableSection && !isHiddenPrefix)) {
                        categorizedSettings[section].push_back({ key, &settingEntry });
                    }
                }
            }

            // Display settings by section
            for (const auto& [section, entries] : categorizedSettings) {
                if (ImGui::CollapsingHeader(section.c_str())) {
                    for (const auto& [key, settingEntry] : entries) {
                        ImGui::PushID(key.c_str());

                        // Add "Save" checkbox
                        bool isSaved = settingEntry->saved;

                        if (ImGui::Checkbox("##Save", &isSaved)) {
                            scriptSettings.SetSettingSaved(section, key, isSaved);
                        }
                        ImGui::SameLine();

                        if (std::holds_alternative<bool>(settingEntry->value)) {
                            bool currentValue = std::get<bool>(settingEntry->value);
                            if (ImGui::Checkbox(key.c_str(), &currentValue)) {
                                scriptSettings.SetSettingValue(section, key, currentValue);
                                if (isSaved) {
                                    scriptSettings.SetSettingSaved(section, key, true);
                                }
                            }
                        }
                        else if (std::holds_alternative<std::string>(settingEntry->value)) {
                            std::string currentValue = std::get<std::string>(settingEntry->value);
                            char buffer[256];
                            strcpy_s(buffer, currentValue.c_str());
                            if (ImGui::InputText(key.c_str(), buffer, sizeof(buffer))) {
                                std::string newValue(buffer);
                                scriptSettings.SetSettingValue(section, key, newValue);
                                if (isSaved) {
                                    scriptSettings.SetSettingSaved(section, key, true);
                                }
                            }
                        }

                        ImGui::SameLine();
                        ImGui::Text("(0x%llX)", static_cast<unsigned long long>(settingEntry->address));

                        ImGui::PopID();
                    }
                }
            }

            ImGui::Separator();

            if (ImGui::Button("Save Config")) {
                scriptSettings.SaveToFile();
                Log("Game configs saved to file.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Config")) {
                scriptSettings.LoadFromFile();
                Log("Game config loaded from file.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                scriptSettings.LoadFromDetectedConfigs();
                scriptSettings.SaveToFile();
                Log("Game config refreshed and saved.");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Settings")) {
            ImGui::Checkbox("Show Unsettable Game Config Options", &showUnsettableOptions);
            ImGui::TextWrapped("Enabling this option will show all game config options,\nincluding those not in the Config or Options categories.\nI don't think they work but go nuts.");

            if (ImGui::Button("Clear All Settings")) {
                showClearSettingsConfirmation = true;
            }
			ImGui::TextWrapped("Clears all saved/active settings, no undo");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Presets")) {
            ImGui::TextWrapped("Presets allow you to save and load sets of settings. Please be mindful of loading random people's settings, and always make backups of your Options.ini (though you can just delete it).");
            DrawPresetsTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    if (showClearSettingsConfirmation) {
        ImGui::OpenPopup("Clear Settings Confirmation");
    }

    if (ImGui::BeginPopupModal("Clear Settings Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to clear all settings?");
        ImGui::Text("This action cannot be undone.");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            ClearAllSettings();
            showClearSettingsConfirmation = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            showClearSettingsConfirmation = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
