#include "gui.h"
#include <d3d9.h>
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "utils.h"
#include "logger.h"
#include "settings_gui.h"
#include <map>
#include <sstream>
#include <unordered_map>
#include "preset_manager.h"
#include "optimization.h"
#include "patch_system.h"
#include "qol.h"
#include "intersection_patch.h"
#include "cpu_optimization.h"
#include "d3d9_hook.h"

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
                        bool settingsSuccess = SettingsManager::Get().SaveConfig("S3SS.ini", &error);
                        // Also save optimization settings
                        bool optimizationSuccess = OptimizationManager::Get().SaveState("S3SS.ini");
                        // Ensure UI settings are in the file (append if needed)
                        UISettings::Get().EnsureInINI("S3SS.ini");

                        if (settingsSuccess && optimizationSuccess) {
                            UISettings::Get().MarkAsSaved();
                            LOG_INFO("Settings saved successfully");
                        } else if (settingsSuccess && !optimizationSuccess) {
                            UISettings::Get().MarkAsSaved();  // Settings did save
                            LOG_WARNING("Settings saved but optimization state save failed");
                        } else if (!settingsSuccess && optimizationSuccess) {
                            LOG_WARNING("Optimization state saved but settings save failed: " + error);
                        } else {
                            LOG_ERROR("Failed to save both settings and optimization state: " + error);
                        }
                    }
                    if (ImGui::MenuItem("Load Settings")) {
                        std::string error;
                        bool settingsSuccess = SettingsManager::Get().LoadConfig("S3SS.ini", &error);
                        // Also load optimization settings
                        bool optimizationSuccess = OptimizationManager::Get().LoadState("S3SS.ini");

                        if (settingsSuccess && optimizationSuccess) {
                            LOG_INFO("Settings loaded successfully");
                        } else if (settingsSuccess && !optimizationSuccess) {
                            LOG_WARNING("Settings loaded but optimization state load failed");
                        } else if (!settingsSuccess && optimizationSuccess) {
                            LOG_WARNING("Optimization state loaded but settings load failed: " + error);
                        } else {
                            LOG_ERROR("Failed to load both settings and optimization state: " + error);
                        }
                    }
                    
                    // Add Reset All option
                    if (ImGui::MenuItem("Reset All to Defaults", nullptr, false, SettingsManager::Get().HasDefaultValues())) {
                        SettingsManager::Get().ResetAllSettings();
                    }

                    // TODO: Preset system temporarily disabled - needs save/load logic fixes, nobody uses this anyway soooo
                    /*
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
                                bool presetSuccess = PresetManager::Get().SavePreset(presetName, presetDesc, &error);
                                // Save optimization settings to the preset
                                bool optimizationSuccess = true;
                                if (presetSuccess) {
                                    std::string presetPath = PresetManager::Get().GetPresetPath(presetName);
                                    optimizationSuccess = OptimizationManager::Get().SaveState(presetPath);
                                }

                                if (presetSuccess && optimizationSuccess) {
                                    LOG_INFO("Preset saved successfully");
                                } else if (presetSuccess && !optimizationSuccess) {
                                    LOG_WARNING("Preset saved but optimization state save failed");
                                } else {
                                    LOG_ERROR("Failed to save preset: " + error);
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
                                LOG_INFO("Selected preset: " + preset.name + ", showing dialog");
                            }
                            if (ImGui::IsItemHovered() && !preset.description.empty()) {
                                ImGui::SetTooltip("%s", preset.description.c_str());
                            }
                        }

                        ImGui::EndMenu();
                    }
                    */
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
                        
                        ImGui::Separator();
                        
                        // Manual initialization button
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                        ImGui::TextWrapped("If settings don't initialize automatically  you can manually initialize them:");
                        ImGui::PopStyleColor();
                        
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
                        ImGui::TextWrapped("WARNING: Only press this button when you are IN-GAME and past the loading screen, NOT in the main menu!");
                        ImGui::PopStyleColor();
                        
                        if (ImGui::Button("Manual Initialize Settings")) {
                            SettingsManager::Get().ManualInitialize();
                        }
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
                                LOG_DEBUG("Resorting settings categories...");
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

                // Patches tab (the hell formerly known as Optimizations) - Rewrite inc, this is way too bloated a section lol, maybe helper? Or method to OptimiationPatch?
                if (ImGui::BeginTabItem("Patches")) {
                    // Show detected game version
                    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Detected Version: %s", GetGameVersionName());
                    ImGui::TextDisabled("Incompatible patches will be greyed out and disabled");
                    ImGui::Separator();

                    // Display patches with interaction, organized by category
                    try {
                        const auto& patches = OptimizationManager::Get().GetPatches();

                        // Group patches by category, with experimental patches in their own section
                        // Experimental patches ignore their category and go straight to the Experimental section
                        std::map<std::string, std::vector<OptimizationPatch*>> patchesByCategory;
                        std::vector<OptimizationPatch*> experimentalPatches;

                        for (const auto& patch : patches) {
                            if (!patch) continue;
                            const auto* meta = patch->GetMetadata();
                            bool isExperimental = meta ? meta->experimental : false;

                            if (isExperimental) {
                                // Experimental patches go to their own section, category is ignored
                                experimentalPatches.push_back(patch.get());
                            } else {
                                std::string category = (meta && !meta->category.empty()) ? meta->category : "General";
                                patchesByCategory[category].push_back(patch.get());
                            }
                        }

                        // Render each category
                        for (const auto& [category, categoryPatches] : patchesByCategory) {
                            // Category header with count
                            std::string headerLabel = category + " (" + std::to_string(categoryPatches.size()) + ")";

                            if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::Indent(15.0f);

                                // Render patches in this category
                                for (auto* patch : categoryPatches) {
                                    if (!patch) continue;

                                    // Use patch pointer as unique ID to avoid collisions
                                    ImGui::PushID(patch);

                                    try {
                                        const PatchMetadata* meta = patch->GetMetadata();
                                        std::string name = meta && !meta->displayName.empty() ?
                                                          meta->displayName : patch->GetName();
                                        std::string desc = meta ? meta->description : "";
                                        bool experimental = meta ? meta->experimental : false;
                                        bool compatible = patch->IsCompatibleWithCurrentVersion();

                                        bool enabled = patch->IsEnabled();
                                        bool hasError = false;
                                        try {
                                            const std::string& errStr = patch->GetLastError();
                                            hasError = !errStr.empty();
                                        } catch (...) {
                                            hasError = false;  // Ignore errors during error checking
                                        }

                                        // Color experimental patches, patches with errors, or incompatible patches
                                        if (hasError) {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                        } else if (!compatible) {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                                        } else if (experimental) {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                                        }

                                        // Disable checkbox if incompatible
                                        if (!compatible) {
                                            ImGui::BeginDisabled();
                                        }

                                        // Use a temporary for checkbox to handle state correctly
                                        bool checkboxState = enabled;
                                        if (ImGui::Checkbox("##checkbox", &checkboxState)) {
                                            if (checkboxState) {
                                                if (!patch->Install()) {
                                                    // Install failed, checkbox should revert to unchecked
                                                    // The patch->IsEnabled() will be false, so next frame it'll be correct
                                                }
                                            } else {
                                                if (!patch->Uninstall()) {
                                                    // Uninstall failed, checkbox should revert to checked
                                                    // The patch->IsEnabled() will be true, so next frame it'll be correct
                                                }
                                            }
                                        }

                                        if (!compatible) {
                                            ImGui::EndDisabled();
                                        }

                                        ImGui::SameLine();
                                        ImGui::Text("%s", name.c_str());
                                        bool nameHovered = ImGui::IsItemHovered();

                                        if (!compatible) {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("[INCOMPATIBLE]");
                                        } else if (experimental) {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("[EXPERIMENTAL]");
                                        }

                                        if (hasError || !compatible || experimental) {
                                            ImGui::PopStyleColor();
                                        }

                                        // Tooltip with description and error info
                                        if (nameHovered || ImGui::IsItemHovered()) {
                                            ImGui::BeginTooltip();
                                            ImGui::PushTextWrapPos(400.0f);

                                            // Show incompatibility warning first
                                            if (!compatible) {
                                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
                                                ImGui::Text("INCOMPATIBLE: This patch does not support %s", GetGameVersionName());
                                                ImGui::PopStyleColor();
                                                if (!desc.empty() || hasError) {
                                                    ImGui::Separator();
                                                }
                                            }

                                            // Show error if present
                                            if (hasError) {
                                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                                try {
                                                    ImGui::Text("ERROR: %s", patch->GetLastError().c_str());
                                                } catch (...) {
                                                    ImGui::Text("ERROR: (error message unavailable)");
                                                }
                                                ImGui::PopStyleColor();
                                                if (!desc.empty()) {
                                                    ImGui::Separator();
                                                }
                                            }

                                            // Show description
                                            if (!desc.empty()) {
                                                ImGui::Text("%s", desc.c_str());
                                            }

                                            // Show technical details
                                            if (meta && !meta->technicalDetails.empty()) {
                                                ImGui::Separator();
                                                ImGui::TextDisabled("Technical Details:");
                                                for (const auto& detail : meta->technicalDetails) {
                                                    ImGui::BulletText("%s", detail.c_str());
                                                }
                                            }

                                            ImGui::PopTextWrapPos();
                                            ImGui::EndTooltip();
                                        }

                                        // Render custom UI if available
                                        if (enabled) {
                                            ImGui::Indent();
                                            patch->RenderCustomUI();
                                            ImGui::Unindent();
                                        }

                                    } catch (const std::exception& e) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                                                          "Error with patch: %s", e.what());
                                    } catch (...) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                                                          "Unknown error with patch");
                                    }

                                    ImGui::PopID();  // Pop the unique patch ID
                                }

                                ImGui::Unindent(15.0f);
                            }
                        }

                        // Render experimental patches in their own section
                        if (!experimentalPatches.empty()) {
                            std::string headerLabel = "Experimental (" + std::to_string(experimentalPatches.size()) + ")";

                            if (ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_None)) {
                                ImGui::Indent(15.0f);

                                for (auto* patch : experimentalPatches) {
                                    if (!patch) continue;

                                    ImGui::PushID(patch);

                                    try {
                                        const PatchMetadata* meta = patch->GetMetadata();
                                        std::string name = meta && !meta->displayName.empty() ?
                                                          meta->displayName : patch->GetName();
                                        std::string desc = meta ? meta->description : "";
                                        bool compatible = patch->IsCompatibleWithCurrentVersion();

                                        bool enabled = patch->IsEnabled();
                                        bool hasError = false;
                                        try {
                                            const std::string& errStr = patch->GetLastError();
                                            hasError = !errStr.empty();
                                        } catch (...) {
                                            hasError = false;
                                        }

                                        // Color experimental patches, patches with errors, or incompatible patches
                                        if (hasError) {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                        } else if (!compatible) {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                                        } else {
                                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
                                        }

                                        if (!compatible) {
                                            ImGui::BeginDisabled();
                                        }

                                        bool checkboxState = enabled;
                                        if (ImGui::Checkbox("##checkbox", &checkboxState)) {
                                            if (checkboxState) {
                                                patch->Install();
                                            } else {
                                                patch->Uninstall();
                                            }
                                        }

                                        if (!compatible) {
                                            ImGui::EndDisabled();
                                        }

                                        ImGui::SameLine();
                                        ImGui::Text("%s", name.c_str());
                                        bool nameHovered = ImGui::IsItemHovered();

                                        if (!compatible) {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("[INCOMPATIBLE]");
                                        } else {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("[EXPERIMENTAL]");
                                        }

                                        ImGui::PopStyleColor();

                                        // Tooltip with description and error info
                                        if (nameHovered || ImGui::IsItemHovered()) {
                                            ImGui::BeginTooltip();
                                            ImGui::PushTextWrapPos(400.0f);

                                            if (!compatible) {
                                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
                                                ImGui::Text("INCOMPATIBLE: This patch does not support %s", GetGameVersionName());
                                                ImGui::PopStyleColor();
                                                if (!desc.empty() || hasError) {
                                                    ImGui::Separator();
                                                }
                                            }

                                            if (hasError) {
                                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                                                try {
                                                    ImGui::Text("ERROR: %s", patch->GetLastError().c_str());
                                                } catch (...) {
                                                    ImGui::Text("ERROR: (error message unavailable)");
                                                }
                                                ImGui::PopStyleColor();
                                                if (!desc.empty()) {
                                                    ImGui::Separator();
                                                }
                                            }

                                            if (!desc.empty()) {
                                                ImGui::Text("%s", desc.c_str());
                                            }

                                            if (meta && !meta->technicalDetails.empty()) {
                                                ImGui::Separator();
                                                ImGui::TextDisabled("Technical Details:");
                                                for (const auto& detail : meta->technicalDetails) {
                                                    ImGui::BulletText("%s", detail.c_str());
                                                }
                                            }

                                            ImGui::PopTextWrapPos();
                                            ImGui::EndTooltip();
                                        }

                                        if (enabled) {
                                            ImGui::Indent();
                                            patch->RenderCustomUI();
                                            ImGui::Unindent();
                                        }

                                    } catch (const std::exception& e) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                                                          "Error with patch: %s", e.what());
                                    } catch (...) {
                                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                                                          "Unknown error with patch");
                                    }

                                    ImGui::PopID();
                                }

                                ImGui::Unindent(15.0f);
                            }
                        }
                    } catch (...) {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                            "ERROR: Failed to load patches list");
                    }

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

                    ImGui::Separator();

                    // Borderless Window section
                    if (ImGui::CollapsingHeader("Borderless Window")) {
                        auto& borderless = BorderlessWindow::Get();
                        BorderlessMode currentMode = borderless.GetMode();

                        const char* modeNames[] = { "Disabled", "Decorations Only", "Maximized", "Fullscreen" };
                        int modeIndex = static_cast<int>(currentMode);

                        if (ImGui::Combo("Mode", &modeIndex, modeNames, IM_ARRAYSIZE(modeNames))) {
                            borderless.SetMode(static_cast<BorderlessMode>(modeIndex));
                        }
                        if (ImGui::IsItemHovered()) {
                            const char* tooltips[] = {
                                "Normal windowed mode with title bar and borders",
                                "Removes title bar and borders, keeps current window size and position",
                                "Removes decorations and fills the screen (leaves taskbar visible)",
                                "Removes decorations and covers the entire monitor (hides taskbar)"
                            };
                            ImGui::SetTooltip("%s", tooltips[modeIndex]);
                        }
                    }

                    ImGui::Separator();

                    // UI Settings section
                    if (ImGui::CollapsingHeader("UI Settings")) {
                        // Toggle key setting
                        static bool waitingForKey = false;
                        UINT currentKey = UISettings::Get().GetUIToggleKey();
                        std::string keyName = UISettings::Get().GetKeyName(currentKey);
                        
                        ImGui::Text("UI Toggle Key: %s", keyName.c_str());
                        ImGui::SameLine();
                        
                        if (waitingForKey) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.5f, 0.0f, 1.0f));
                            if (ImGui::Button("Press any key...")) {
                                waitingForKey = false;
                            }
                            ImGui::PopStyleColor();
                            
                            // Check for key press
                            for (int key = VK_F1; key <= VK_F12; key++) {
                                if (GetAsyncKeyState(key) & 0x8000) {
                                    UISettings::Get().SetUIToggleKey(key);
                                    waitingForKey = false;
                                    // Auto-save when changed
                                    UISettings::Get().EnsureInINI("S3SS.ini");
                                    UISettings::Get().MarkAsSaved();
                                    break;
                                }
                            }
                            // Check other common keys
                            static const UINT commonKeys[] = {
                                VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
                                VK_PAUSE, VK_SCROLL, VK_OEM_3, VK_OEM_MINUS, VK_OEM_PLUS,
                                VK_OEM_4, VK_OEM_6, VK_OEM_5, VK_OEM_1, VK_OEM_7
                            };
                            for (UINT key : commonKeys) {
                                if (GetAsyncKeyState(key) & 0x8000) {
                                    UISettings::Get().SetUIToggleKey(key);
                                    waitingForKey = false;
                                    // Auto-save when changed
                                    UISettings::Get().EnsureInINI("S3SS.ini");
                                    UISettings::Get().MarkAsSaved();
                                    break;
                                }
                            }
                        } else {
                            if (ImGui::Button("Change")) {
                                waitingForKey = true;
                            }
                        }
                        
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Click to change the key used to toggle this UI\nDefault: Insert\nChanges are saved automatically");
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
        
        // TODO: Preset system temporarily disabled
        // RenderPresetLoadDialog();

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

            ImGui::Separator();

            // Show type information
            const char* typeStr = "Unknown";
            std::visit([&](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, bool>) {
                    typeStr = "bool";
                } else if constexpr (std::is_same_v<T, int>) {
                    typeStr = "int";
                } else if constexpr (std::is_same_v<T, unsigned int>) {
                    typeStr = "unsigned int";
                } else if constexpr (std::is_same_v<T, float>) {
                    typeStr = "float";
                } else if constexpr (std::is_same_v<T, Vector2>) {
                    typeStr = "Vector2";
                } else if constexpr (std::is_same_v<T, Vector3>) {
                    typeStr = "Vector3";
                } else if constexpr (std::is_same_v<T, Vector4>) {
                    typeStr = "Vector4";
                }
            }, setting->GetValue());

            ImGui::TextDisabled("Type:");
            ImGui::SameLine();
            ImGui::Text("%s", typeStr);

            // Show memory address
            void* address = setting->GetAddress();
            char addressStr[32];
            sprintf_s(addressStr, "0x%08X", (uintptr_t)address);

            ImGui::TextDisabled("Memory Address:");
            ImGui::SameLine();
            ImGui::Text("%s", addressStr);

            // Copy address to clipboard button
            if (ImGui::Button("Copy Address")) {
                ImGui::SetClipboardText(addressStr);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Copy memory address to clipboard");
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
            LOG_INFO("Opening preset load dialog");
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
                bool presetSuccess = PresetManager::Get().LoadPresetWithStrategy(
                    m_pendingPresetToLoad, PresetLoadStrategy::Merge, &error);

                // Load optimization settings from the preset
                bool optimizationSuccess = true;
                if (presetSuccess) {
                    std::string presetPath = PresetManager::Get().GetPresetPath(m_pendingPresetToLoad);
                    optimizationSuccess = OptimizationManager::Get().LoadState(presetPath);
                }

                if (presetSuccess && optimizationSuccess) {
                    LOG_INFO("Preset loaded successfully (merge)");
                } else if (presetSuccess && !optimizationSuccess) {
                    LOG_WARNING("Preset loaded but optimization state load failed");
                } else {
                    LOG_ERROR("Failed to load preset: " + error);
                }

                m_showPresetLoadDialog = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SameLine();
            
            if (ImGui::Button("Overwrite", ImVec2(buttonWidth, 0))) {
                std::string error;
                bool presetSuccess = PresetManager::Get().LoadPresetWithStrategy(
                    m_pendingPresetToLoad, PresetLoadStrategy::Overwrite, &error);

                // Load optimization settings from the preset
                bool optimizationSuccess = true;
                if (presetSuccess) {
                    std::string presetPath = PresetManager::Get().GetPresetPath(m_pendingPresetToLoad);
                    optimizationSuccess = OptimizationManager::Get().LoadState(presetPath);
                }

                if (presetSuccess && optimizationSuccess) {
                    LOG_INFO("Preset loaded successfully (overwrite)");
                } else if (presetSuccess && !optimizationSuccess) {
                    LOG_WARNING("Preset loaded but optimization state load failed");
                } else {
                    LOG_ERROR("Failed to load preset: " + error);
                }

                m_showPresetLoadDialog = false;
                ImGui::CloseCurrentPopup();
            }
            
            ImGui::SetItemDefaultFocus();
            ImGui::EndPopup();
        }
    }
}