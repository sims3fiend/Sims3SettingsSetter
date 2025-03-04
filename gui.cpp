#include "gui.h"
#include <d3d9.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "utils.h"
#include "settings_gui.h"
#include <map>
#include <sstream>
#include "preset_manager.h"
#include "optimization.h"
#include "qol.h"
#include "small_patches.h"
#include "intersection_patch.h"

//I hate ImGui I hate ImGui I hate ImGui
//https://www.youtube.com/watch?v=lKntlVofKqU
//literally my fav song btw x


extern LPDIRECT3DDEVICE9 g_pd3dDevice;

namespace SettingsGui {
    bool m_visible = false;
    bool m_needsSort = true;
    bool m_showPresetLoadDialog = false;
    std::string m_pendingPresetToLoad;
    
    // function for case-insensitive search
    bool CaseInsensitiveSearch(const std::string& haystack, const std::string& needle) {
        auto toLower = [](const std::string& s) {
            std::string lower = s;
            std::transform(lower.begin(), lower.end(), lower.begin(), 
                           [](unsigned char c){ return std::tolower(c); });
            return lower;
        };
        
        return toLower(haystack).find(toLower(needle)) != std::string::npos;
    }
    
    void Initialize() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        
        ImGui_ImplWin32_Init(GetForegroundWindow());
        ImGui_ImplDX9_Init(g_pd3dDevice);
    }
    
    void Shutdown() {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    
    void RenderUI() {
        if (!m_visible) return;
        
        static bool wasVisible = false;
        if (!wasVisible && m_visible) {
            m_needsSort = true;
        }
        wasVisible = m_visible;
    //AAAAAAAAAAAAAAAAAAAAAHHHHHHHHHH
        ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Sims 3 Settings", &m_visible, ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("Save Settings")) {
                        std::string error;
                        bool success = SettingsManager::Get().SaveConfig("S3SS.ini", &error);
                        // Also save optimization settings
                        success &= OptimizationManager::Get().SaveState("S3SS.ini");
                        if (!success) {
                            Utils::Logger::Get().Log("Failed to save settings: " + error);
                        }
                    }
                    if (ImGui::MenuItem("Load Settings")) {
                        std::string error;
                        bool success = SettingsManager::Get().LoadConfig("S3SS.ini", &error);
                        // Also load optimization settings
                        success &= OptimizationManager::Get().LoadState("S3SS.ini");
                        if (!success) {
                            Utils::Logger::Get().Log("Failed to load settings: " + error);
                        }
                    }
                    
                    // Add Reset All option
                    if (ImGui::MenuItem("Reset All to Defaults", nullptr, false, SettingsManager::Get().HasDefaultValues())) {
                        SettingsManager::Get().ResetAllSettings();
                    }
                    
                    ImGui::Separator();
                    
                    if (ImGui::BeginMenu("Presets")) {
                        static char presetName[256] = "";
                        static char presetDesc[1024] = "";
                        
                        // Save new preset
                        ImGui::InputText("Name", presetName, sizeof(presetName));
                        ImGui::InputText("Description", presetDesc, sizeof(presetDesc));
                        if (ImGui::Button("Save New Preset")) {
                            if (strlen(presetName) > 0) {
                                std::string error;
                                bool success = PresetManager::Get().SavePreset(presetName, presetDesc, &error);
                                // Save optimization settings to the preset
                                if (success) {
                                    std::string presetPath = PresetManager::Get().GetPresetPath(presetName);
                                    success &= OptimizationManager::Get().SaveState(presetPath);
                                }
                                if (!success) {
                                    Utils::Logger::Get().Log("Failed to save preset: " + error);
                                }
                            }
                        }
                        
                        ImGui::Separator();
                        
                        // List available presets
                        auto presets = PresetManager::Get().GetAvailablePresets();
                        for (const auto& preset : presets) {
                            if (ImGui::MenuItem(preset.name.c_str(), preset.description.c_str())) {
                                // Instead of loading immediately, set the pending preset and show dialog
                                m_pendingPresetToLoad = preset.name;
                                m_showPresetLoadDialog = true;
                                Utils::Logger::Get().Log("Selected preset: " + preset.name + ", showing dialog");
                            }
                            if (ImGui::IsItemHovered() && !preset.description.empty()) {
                                ImGui::SetTooltip("%s", preset.description.c_str());
                            }
                        }
                        
                        ImGui::EndMenu();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            if (ImGui::BeginTabBar("SettingsTabs")) {
                // Regular settings tab
                if (ImGui::BeginTabItem("Settings")) {
                    // Check if settings are fully initialized
                    if (!SettingsManager::Get().IsInitialized()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                        ImGui::TextWrapped("Settings are currently being initialized. Please wait...");
                        ImGui::TextWrapped("The Settings tab will become available once all game settings have been loaded.");
                        ImGui::Separator();
                        ImGui::TextWrapped("Other tabs are still available.");
                        ImGui::PopStyleColor();
                    }
                    else {
                        //cute little message owo
                        ImGui::TextWrapped("These are 'Variable' settings, and can be edited live ingame. Right click to edit a value beyond its bounds, reset to default, or remove from ini (clear override). You can save/load settings in File, and you can right click a setting to clear its override/remove it from the ini (will need to save to apply)");
                        ImGui::Separator();
                        static char searchBuffer[256] = "";
                        ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
                        std::string searchStr = searchBuffer;

                        ImGui::Separator();

                        // Settings list with scrolling
                        ImGui::BeginChild("SettingsList", ImVec2(0, 0), true);
                        {
                            if (m_needsSort) {
                                OutputDebugStringA("Resorting settings categories...\n");
                                m_needsSort = false;
                            }

                            // Group settings by category
                            std::map<std::wstring, std::vector<Setting*>> categorizedSettings;
                            
                            // First, sort settings into categories
                            auto& allSettings = SettingsManager::Get().GetAllSettings();
                            for (const auto& pair : allSettings) {
                                const auto& name = pair.first;
                                const auto& setting = pair.second;
                                
                                const auto& metadata = setting->GetMetadata();
                                std::wstring category = metadata.category.empty() ? L"Uncategorized" : metadata.category;
                                
                                // Apply search filter
                                if (!searchStr.empty()) {
                                    std::string settingName = Utils::WideToUtf8(name);
                                    std::string categoryName = Utils::WideToUtf8(category);
                                    if (!CaseInsensitiveSearch(settingName, searchStr) && 
                                        !CaseInsensitiveSearch(categoryName, searchStr)) {
                                        continue;
                                    }
                                }
                                
                                categorizedSettings[category].push_back(setting.get());
                            }

                            // Then render each category
                            for (const auto& pair : categorizedSettings) {
                                const auto& category = pair.first;
                                const auto& settings = pair.second;
                                
                                std::string categoryName = Utils::WideToUtf8(category);
                                std::string headerText = categoryName + " (" + std::to_string(settings.size()) + ")";
                                
                                if (ImGui::TreeNode(headerText.c_str())) {
                                    ImGui::Indent(10.0f);
                                    
                                    for (Setting* setting : settings) {
                                        RenderSetting(setting);
                                    }
                                    
                                    ImGui::Unindent(10.0f);
                                    ImGui::TreePop();
                                }
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::EndTabItem();
                }

                // Patches tab (the hell formerly known as Optimizations)
                if (ImGui::BeginTabItem("Patches")) {
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 
                        "WARNING! Patches only work with the Steam version of the game!!");
                    ImGui::Separator();
                    const auto& patches = OptimizationManager::Get().GetPatches();

                    // Display all patches with categorization
                    int patchIndex = 0;  // Add a unique index for each patch

                    // Display LotVisibilityPatch first
                    for (const auto& patch : patches) {
                        ImGui::PushID(patchIndex++);
                        
                        if (auto lotVisibilityPatch = dynamic_cast<LotVisibilityPatch*>(patch.get())) {
                            bool lotEnabled = lotVisibilityPatch->IsEnabled();
                            if (ImGui::Checkbox("Lot Visibility", &lotEnabled)) {
                                if (lotEnabled) lotVisibilityPatch->Install(); else lotVisibilityPatch->Uninstall();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Changes how the game handles distant lots visibility so that it no longer loads them based on view\nModifies a conditional jump at 0x00c63015\nImproves performance with high lot counts especially, this is part of stutter reducerer");
                            }
                        }
                        
                        ImGui::PopID();
                    }

                    // Display IntersectionPatch next
                    for (const auto& patch : patches) {
                        ImGui::PushID(patchIndex++);
                        
                        if (auto intersectionPatch = dynamic_cast<IntersectionPatch*>(patch.get())) {
                            bool intersectionEnabled = intersectionPatch->IsEnabled();
                            if (ImGui::Checkbox("Intersection Optimization", &intersectionEnabled)) {
                                if (intersectionEnabled) intersectionPatch->Install(); else intersectionPatch->Uninstall();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Optimizes intersection calculations with SIMD instructions\nImproves performance for initial navmesh creation and pathfinding (negligible)");
                            }
                        }
                        
                        ImGui::PopID();
                    }

                    /* Rest in peace
                    // Display FrameRatePatch before the sleep-related section
                    for (const auto& patch : patches) {
                        ImGui::PushID(patchIndex++);
                        
                        if (auto frameRatePatch = dynamic_cast<FrameRatePatch*>(patch.get())) {
                            bool frameEnabled = frameRatePatch->IsEnabled();
                            if (ImGui::Checkbox("Target Framerate [UNTESTED]", &frameEnabled)) {
                                if (frameEnabled) frameRatePatch->Install(); else frameRatePatch->Uninstall();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Modifies the game's target frame rate\nChanges the internal timing constant used for frame pacing\nWARNING: This patch is untested and may cause issues");
                            }
                            
                            if (frameEnabled) {
                                ImGui::Indent();
                                
                                // Target FPS
                                int targetFPS = frameRatePatch->GetTargetFPS();
                                if (ImGui::SliderInt("Target FPS", &targetFPS, 30, 240)) {
                                    frameRatePatch->SetTargetFPS(targetFPS);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("Target framerate\nDefault: 30 FPS, New: %d FPS\nModifies memory address 0x0108b1a0", targetFPS);
                                }
                                
                                ImGui::Unindent();
                            }
                        }
                        
                        ImGui::PopID();
                    }
                    */

                    ImGui::Separator();

                    ImGui::EndTabItem();
                }

                // Config Values tab
                if (ImGui::BeginTabItem("Config Values")) {

                    ImGui::TextWrapped("These are Config category values, typically found in the GraphicsRules.sgr file, this reflects what is actually loaded, which may be different from the file itself. To reset a value, right click the value and click 'Clear Override', or delete from the ini file.\n\nPlease note some will crash your game when set to certain values, if you find one of these lmk");
                    ImGui::Separator();

                    static char searchBuffer[256] = "";
                    ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer));
                    std::string searchStr = searchBuffer;

                    ImGui::Separator();


                    ImGui::BeginChild("ConfigList", ImVec2(0, 0), true);
                    {
                        const auto& configValues = SettingsManager::Get().GetConfigValues();
                        
                        // Create a sorted list of configs for consistent ordering
                        std::vector<std::pair<std::wstring, const ConfigValueInfo*>> sortedConfigs;
                        
                        for (const auto& [name, info] : configValues) {
                            // Apply search filter
                            if (!searchStr.empty()) {
                                std::string settingName = Utils::WideToUtf8(name);
                                if (!CaseInsensitiveSearch(settingName, searchStr)) {
                                    continue;
                                }
                            }
                            
                            sortedConfigs.push_back({name, &info});
                        }

                        // Sort by name
                        std::sort(sortedConfigs.begin(), sortedConfigs.end(),
                            [](const auto& a, const auto& b) {
                                return a.first < b.first;
                            });

                        // Render each config value
                        for (const auto& [name, info] : sortedConfigs) {
                            std::string label = Utils::WideToUtf8(name);
                            std::string currentValue = Utils::WideToUtf8(info->currentValue);
                            
                            // Set text color based on modification state
                            bool colorPushed = false;
                            if (info->isModified) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green for modified
                                colorPushed = true;
                            }

                            char buffer[1024];
                            strncpy_s(buffer, currentValue.c_str(), sizeof(buffer) - 1);
                            
                            ImGui::PushID(label.c_str());
                            if (ImGui::InputText("##value", buffer, sizeof(buffer))) {
                                // Always update the value using the ConfigValueCache system
                                SettingsManager::Get().UpdateConfigValue(name, Utils::Utf8ToWide(buffer));
                            }
                            ImGui::SameLine();
                            ImGui::Text("%s", label.c_str());
                            ImGui::PopID();

                            // Right-click menu
                            if (ImGui::BeginPopupContextItem(label.c_str())) {
                                ImGui::TextDisabled("%s", label.c_str());
                                ImGui::Separator();

                                if (ImGui::Button("Clear Override")) {
                                    ConfigValueInfo resetInfo = *info;
                                    resetInfo.isModified = false;
                                    resetInfo.currentValue = L"";  // Reset to empty
                                    SettingsManager::Get().AddConfigValue(name, resetInfo);
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Remove this setting from the ini file\nand let the game manage its value\nChanges will apply on next save");
                                    ImGui::EndTooltip();
                                }

                                ImGui::EndPopup();
                            }
                            //tooltip
                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                if (info->isModified) {
                                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Modified value");
                                }
                                ImGui::Text("Right-click to reset");
                                ImGui::EndTooltip();
                            }

                            if (colorPushed) {
                                ImGui::PopStyleColor();
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }

                // QoL tab
                if (ImGui::BeginTabItem("Other/QoL")) {
                    ImGui::TextWrapped("Quality of Life improvements and other features");
                    ImGui::Separator();

                    // Memory Monitor section
                    if (ImGui::CollapsingHeader("Memory Monitor", ImGuiTreeNodeFlags_DefaultOpen)) {
                        auto& memoryMonitor = MemoryMonitor::Get();
                        
                        bool enabled = memoryMonitor.IsEnabled();
                        if (ImGui::Checkbox("Enable Memory Warning", &enabled)) {
                            memoryMonitor.SetEnabled(enabled);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Warns when the game's memory usage gets close to the 4GB limit\nSetting is saved automatically");
                        }

                        float threshold = memoryMonitor.GetWarningThreshold();
                        if (ImGui::SliderFloat("Warning Threshold (GB)", &threshold, 2.0f, 3.9f, "%.1f")) {
                            memoryMonitor.SetWarningThreshold(threshold);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Show warning when memory usage exceeds this amount\nSetting is saved automatically");
                        }

                        // Add warning style selector
                        int style = static_cast<int>(memoryMonitor.GetWarningStyle());
                        const char* styles[] = { "Auto-dismiss Overlay", "Modal Dialog" };
                        if (ImGui::Combo("Warning Style", &style, styles, IM_ARRAYSIZE(styles))) {
                            memoryMonitor.SetWarningStyle(static_cast<WarningStyle>(style));
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Choose between an auto-dismissing overlay\nor a modal dialog that requires confirmation");
                        }

                        // Display current memory usage
                        float currentUsage = memoryMonitor.GetCurrentMemoryUsageGB();
                        ImGui::Text("Current Memory Usage: %.2f GB", currentUsage);
                        
                        // Memory usage bar
                        float progress = currentUsage / 4.0f; // 4GB is the max
                        ImGui::ProgressBar(progress, ImVec2(-1, 0), 
                            std::to_string(currentUsage).substr(0,4).c_str());
                        
                        if (progress > 0.9f) {
                            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 
                                "Warning: Memory usage is very high!");
                        }
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    void Render() {
        // Always render the memory warning if needed
        RenderMemoryWarning();
        
        // Render the preset load dialog if needed
        RenderPresetLoadDialog();

        // Only render the main UI if visible
        RenderUI();
    }
    
    void RenderSetting(Setting* setting) {
        if (!setting) return;
        
        const auto& metadata = setting->GetMetadata();
        std::string label = Utils::WideToUtf8(metadata.name);
        
        // Track if we pushed a color
        bool colorPushed = false;
        
        // Set text color based on setting state
        if (setting->HasUnsavedChanges()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow for unsaved changes
            colorPushed = true;
        }
        else if (setting->IsOverridden()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green for saved changes
            colorPushed = true;
        }
        
        // Render the control
        bool valueChanged = false;
        std::visit([&](auto&& value) {
            using T = std::decay_t<decltype(value)>;
            
            if constexpr (std::is_same_v<T, bool>) {
                bool currentBool = std::get<bool>(setting->GetValue());
                if (ImGui::Checkbox(label.c_str(), &currentBool)) {
                    setting->SetValue(currentBool);
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
            else if constexpr (std::is_same_v<T, int>) {
                int currentInt = std::get<int>(setting->GetValue());
                if (ImGui::SliderInt(label.c_str(), &currentInt, 
                                    static_cast<int>(metadata.min), 
                                    static_cast<int>(metadata.max),
                                    "%d",
                                    ImGuiSliderFlags_AlwaysClamp)) {
                    setting->SetValue(currentInt);
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
            else if constexpr (std::is_same_v<T, unsigned int>) {
                int currentInt = static_cast<int>(std::get<unsigned int>(setting->GetValue()));
                if (ImGui::SliderInt(label.c_str(), &currentInt,
                                    static_cast<int>(metadata.min),
                                    static_cast<int>(metadata.max),
                                    "%d",
                                    ImGuiSliderFlags_AlwaysClamp)) {
                    setting->SetValue(static_cast<unsigned int>(currentInt));
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
            else if constexpr (std::is_same_v<T, float>) {
                float currentFloat = std::get<float>(setting->GetValue());
                if (ImGui::SliderFloat(label.c_str(), &currentFloat, 
                                     metadata.min, metadata.max, "%.3f", 
                                     ImGuiSliderFlags_AlwaysClamp)) {
                    setting->SetValue(currentFloat);
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
            else if constexpr (std::is_same_v<T, Vector2>) {
                Vector2 currentVec = std::get<Vector2>(setting->GetValue());
                float values[2] = {currentVec.x, currentVec.y};
                if (ImGui::SliderFloat2(label.c_str(), values, 
                                      metadata.min, metadata.max, "%.3f", 
                                      ImGuiSliderFlags_AlwaysClamp)) {
                    setting->SetValue(Vector2{values[0], values[1]});
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
            else if constexpr (std::is_same_v<T, Vector3>) {
                Vector3 currentVec = std::get<Vector3>(setting->GetValue());
                float values[3] = {currentVec.x, currentVec.y, currentVec.z};
                if (ImGui::SliderFloat3(label.c_str(), values, 
                                      metadata.min, metadata.max, "%.3f", 
                                      ImGuiSliderFlags_AlwaysClamp)) {
                    setting->SetValue(Vector3{values[0], values[1], values[2]});
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
            else if constexpr (std::is_same_v<T, Vector4>) {
                Vector4 currentVec = std::get<Vector4>(setting->GetValue());
                float values[4] = {currentVec.x, currentVec.y, currentVec.z, currentVec.w};
                if (ImGui::SliderFloat4(label.c_str(), values, 
                                      metadata.min, metadata.max, "%.3f", 
                                      ImGuiSliderFlags_AlwaysClamp)) {
                    setting->SetValue(Vector4{values[0], values[1], values[2], values[3]});
                    setting->SetUnsavedChanges(true);
                    setting->SetOverridden(true);
                    valueChanged = true;
                }
            }
        }, setting->GetValue());

        // Pop color if we pushed it
        if (colorPushed) {
            ImGui::PopStyleColor();
        }

        // Right-click menu
        if (ImGui::BeginPopupContextItem(label.c_str())) {
            ImGui::TextDisabled("%s", label.c_str());
            ImGui::Separator();

            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                
                if constexpr (std::is_same_v<T, bool>) {
                    bool newValue = std::get<bool>(setting->GetValue());
                    if (ImGui::Checkbox("Value", &newValue)) {
                        setting->SetValue(newValue);
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
                else if constexpr (std::is_same_v<T, int>) {
                    int newValue = std::get<int>(setting->GetValue());
                    if (ImGui::InputInt("Value", &newValue, 1, 10)) {
                        setting->SetValue(newValue);
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
                else if constexpr (std::is_same_v<T, unsigned int>) {
                    int newValue = static_cast<int>(std::get<unsigned int>(setting->GetValue()));
                    if (ImGui::InputInt("Value", &newValue, 1, 10)) {
                        // only ensure unsigned values are positive
                        if (newValue < 0) newValue = 0;
                        setting->SetValue(static_cast<unsigned int>(newValue));
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
                else if constexpr (std::is_same_v<T, float>) {
                    float newValue = std::get<float>(setting->GetValue());
                    if (ImGui::InputFloat("Value", &newValue, 0.0f, 0.0f, "%.6f")) {
                        setting->SetValue(newValue);
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
                else if constexpr (std::is_same_v<T, Vector2>) {
                    Vector2 vec = std::get<Vector2>(setting->GetValue());
                    float values[2] = {vec.x, vec.y};
                    if (ImGui::InputFloat2("Value", values)) {
                        setting->SetValue(Vector2{values[0], values[1]});
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
                else if constexpr (std::is_same_v<T, Vector3>) {
                    Vector3 vec = std::get<Vector3>(setting->GetValue());
                    float values[3] = {vec.x, vec.y, vec.z};
                    if (ImGui::InputFloat3("Value", values)) {
                        setting->SetValue(Vector3{values[0], values[1], values[2]});
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
                else if constexpr (std::is_same_v<T, Vector4>) {
                    Vector4 vec = std::get<Vector4>(setting->GetValue());
                    float values[4] = {vec.x, vec.y, vec.z, vec.w};
                    if (ImGui::InputFloat4("Value", values)) {
                        setting->SetValue(Vector4{values[0], values[1], values[2], values[3]});
                        setting->SetUnsavedChanges(true);
                        setting->SetOverridden(true);
                    }
                }
            }, setting->GetValue());

            ImGui::Separator();
            
            // Clear override button
            if (ImGui::Button("Clear Override")) {
                // Clear the override flags
                setting->SetOverridden(false);
                setting->SetUnsavedChanges(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Remove this setting from the ini file\nand let the game manage its value\nChanges will apply on next save");
                ImGui::EndTooltip();
            }

            // Add Reset to Default button
            if (ImGui::Button("Reset to Default") && SettingsManager::Get().HasDefaultValues()) {
                SettingsManager::Get().ResetSettingToDefault(metadata.name);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Reset this setting to its default value\nThis will be saved to the ini file");
                ImGui::EndTooltip();
            }

            ImGui::EndPopup();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            if (setting->HasUnsavedChanges()) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Unsaved changes");
            }
            else if (setting->IsOverridden()) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Saved override");
            }
            
            if (!std::holds_alternative<bool>(setting->GetValue())) {
                ImGui::Text("Right-click to edit");
                std::visit([&](auto&& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, unsigned int>) {
                        ImGui::Text("Min: %d", static_cast<int>(metadata.min));
                        ImGui::Text("Max: %d", static_cast<int>(metadata.max));
                    } else if constexpr (!std::is_same_v<T, bool>) {
                        ImGui::Text("Min: %.3f", metadata.min);
                        ImGui::Text("Max: %.3f", metadata.max);
                        ImGui::Text("Step: %.3f", metadata.step);
                    }
                }, setting->GetValue());
            }
            ImGui::EndTooltip();
        }
    }

    void RenderMemoryWarning() {
        auto& memoryMonitor = MemoryMonitor::Get();
        if (!memoryMonitor.ShouldShowWarning()) return;

        float currentUsage = memoryMonitor.GetCurrentMemoryUsageGB();

        if (memoryMonitor.GetWarningStyle() == WarningStyle::Modal) {
            // Open a popup if it's not already open
            if (!ImGui::IsPopupOpen("Memory Warning")) {
                ImGui::OpenPopup("Memory Warning");
            }

            // Center the popup
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            // Create the popup
            if (ImGui::BeginPopupModal("Memory Warning", nullptr, 
                ImGuiWindowFlags_AlwaysAutoResize | 
                ImGuiWindowFlags_NoMove)) {
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                ImGui::Text("Memory Usage Warning!");
                ImGui::PopStyleColor();

                ImGui::Text("Current Usage: %.2f GB", currentUsage);
                ImGui::Text("The game may crash when it reaches 4GB");
                ImGui::Text("Consider saving your game");
                
                // Progress bar showing how close to 4GB
                float progress = currentUsage / 4.0f;
                ImGui::ProgressBar(progress, ImVec2(-1, 0));

                // Center the OK button
                float buttonWidth = 120.0f;
                float windowWidth = ImGui::GetWindowSize().x;
                ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                
                if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
                    memoryMonitor.ResetWarning();
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }
        else {
            // Original overlay style
            ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.9f);

            if (ImGui::Begin("##MemoryWarning", nullptr, 
                ImGuiWindowFlags_NoDecoration | 
                ImGuiWindowFlags_AlwaysAutoResize | 
                ImGuiWindowFlags_NoSavedSettings | 
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoMouseInputs)) {
                
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
                ImGui::Text("Memory Usage Warning!");
                ImGui::PopStyleColor();

                ImGui::Text("Current Usage: %.2f GB", currentUsage);
                ImGui::Text("The game may crash when it reaches 4GB");
                ImGui::Text("Consider saving your game");
                
                float progress = currentUsage / 4.0f;
                ImGui::ProgressBar(progress, ImVec2(-1, 0));

                ImGui::TextDisabled("(Warning will close in %.1f seconds)", 
                    memoryMonitor.GetWarningTimeRemaining());
            }
            ImGui::End();
        }
    }

    // Update the RenderPresetLoadDialog function to mention auto-save
    void RenderPresetLoadDialog() {
        if (!m_showPresetLoadDialog) return;
        
        // Center the popup
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        
        // First, make sure the popup is open
        if (!ImGui::IsPopupOpen("Load Preset")) {
            ImGui::OpenPopup("Load Preset");
            Utils::Logger::Get().Log("Opening preset load dialog");
        }
        
        if (ImGui::BeginPopupModal("Load Preset", &m_showPresetLoadDialog, 
                                  ImGuiWindowFlags_AlwaysAutoResize | 
                                  ImGuiWindowFlags_NoMove)) {
            
            ImGui::Text("How would you like to load the preset \"%s\"?", m_pendingPresetToLoad.c_str());
            ImGui::Separator();
            
            ImGui::TextWrapped(
                "Merge: Apply preset values on top of current settings\n"
                "(Existing settings not in the preset will be preserved)");
            
            ImGui::TextWrapped(
                "Overwrite: Reset all settings to defaults, then apply preset\n"
                "(This gives a clean slate with only the preset's values)");
            
            ImGui::Separator();
            ImGui::TextDisabled("Settings will be automatically saved after applying the preset");
            ImGui::Separator();
            
            // Create two buttons side by side
            float windowWidth = ImGui::GetWindowSize().x;
            float buttonWidth = (windowWidth - ImGui::GetStyle().ItemSpacing.x - 20.0f) / 2.0f;
            
            if (ImGui::Button("Merge", ImVec2(buttonWidth, 0))) {
                std::string error;
                bool success = PresetManager::Get().LoadPresetWithStrategy(
                    m_pendingPresetToLoad, PresetLoadStrategy::Merge, &error);
                    
                // Load optimization settings from the preset
                if (success) {
                    std::string presetPath = PresetManager::Get().GetPresetPath(m_pendingPresetToLoad);
                    success &= OptimizationManager::Get().LoadState(presetPath);
                }
                
                if (!success) {
                    Utils::Logger::Get().Log("Failed to load preset: " + error);
                }
                
                m_showPresetLoadDialog = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Overwrite", ImVec2(buttonWidth, 0))) {
                std::string error;
                bool success = PresetManager::Get().LoadPresetWithStrategy(
                    m_pendingPresetToLoad, PresetLoadStrategy::Overwrite, &error);
                    
                // Load optimization settings from the preset
                if (success) {
                    std::string presetPath = PresetManager::Get().GetPresetPath(m_pendingPresetToLoad);
                    success &= OptimizationManager::Get().LoadState(presetPath);
                }
                
                if (!success) {
                    Utils::Logger::Get().Log("Failed to load preset: " + error);
                }
                
                m_showPresetLoadDialog = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
    }
}