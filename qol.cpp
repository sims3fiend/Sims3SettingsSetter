#include "qol.h"
#include "utils.h"
#include "settings.h"
#include <Psapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>

MemoryMonitor& MemoryMonitor::Get() {
    static MemoryMonitor instance;
    return instance;
}

void MemoryMonitor::Update() {
    if (!m_enabled) {
        m_hasWarned = false;
        m_warningDismissed = false;
        return;
    }

    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        // Convert to GB
        m_currentMemoryGB = pmc.PrivateUsage / (1024.0f * 1024.0f * 1024.0f);

        // Reset warning state if memory drops below threshold
        if (m_currentMemoryGB < m_warningThresholdGB * 0.9f) {
            m_hasWarned = false;
            m_warningDismissed = false;
        }

        // Check if we should warn
        if (!m_hasWarned && !m_warningDismissed && m_currentMemoryGB >= m_warningThresholdGB) {
            m_hasWarned = true;
            m_warningDisplayTime = m_WARNING_DISPLAY_DURATION;
        }

        // Update warning display time
        if (m_warningDisplayTime > 0.0f) {
            m_warningDisplayTime -= 1.0f / 60.0f;
        }
    }
}

void MemoryMonitor::SetWarningThreshold(float gigabytes) {
    m_warningThresholdGB = gigabytes;
    m_hasWarned = false; // Reset warning state when threshold changes
    
    // Register setting with SettingsManager
    auto& settingsManager = SettingsManager::Get();
    std::wstring settingName = L"QoL:MemoryMonitor:WarningThreshold";
    settingsManager.UpdateConfigValue(settingName, std::to_wstring(m_warningThresholdGB));
    settingsManager.SaveConfig("S3SS.ini", nullptr); // Specify nullptr for error parameter
}

void MemoryMonitor::SetEnabled(bool enabled) {
    m_enabled = enabled;
    
    // Register setting with SettingsManager
    auto& settingsManager = SettingsManager::Get();
    std::wstring settingName = L"QoL:MemoryMonitor:Enabled";
    settingsManager.UpdateConfigValue(settingName, enabled ? L"true" : L"false");
    settingsManager.SaveConfig("S3SS.ini", nullptr); // Specify nullptr for error parameter
}

void MemoryMonitor::SetWarningStyle(WarningStyle style) {
    m_warningStyle = style;
    
    // Register setting with SettingsManager
    auto& settingsManager = SettingsManager::Get();
    std::wstring settingName = L"QoL:MemoryMonitor:WarningStyle";
    settingsManager.UpdateConfigValue(settingName, style == WarningStyle::Modal ? L"modal" : L"overlay");
    settingsManager.SaveConfig("S3SS.ini", nullptr); // Specify nullptr for error parameter
}

void MemoryMonitor::LoadSettings(const std::string& filename) {
    auto& settingsManager = SettingsManager::Get();
    
    // First, register our config values if they don't exist yet
    std::wstring enabledName = L"QoL:MemoryMonitor:Enabled";
    std::wstring thresholdName = L"QoL:MemoryMonitor:WarningThreshold";
    std::wstring styleName = L"QoL:MemoryMonitor:WarningStyle";
    
    auto& configValues = const_cast<std::unordered_map<std::wstring, ConfigValueInfo>&>(
        settingsManager.GetConfigValues());
    
    if (configValues.find(enabledName) == configValues.end()) {
        ConfigValueInfo info;
        info.category = L"QoL";
        info.currentValue = m_enabled ? L"true" : L"false";
        info.bufferSize = 10;
        info.valueType = ConfigValueType::Boolean;
        settingsManager.AddConfigValue(enabledName, info);
    }
    
    if (configValues.find(thresholdName) == configValues.end()) {
        ConfigValueInfo info;
        info.category = L"QoL";
        info.currentValue = std::to_wstring(m_warningThresholdGB);
        info.bufferSize = 10;
        info.valueType = ConfigValueType::Float;
        settingsManager.AddConfigValue(thresholdName, info);
    }
    
    if (configValues.find(styleName) == configValues.end()) {
        ConfigValueInfo info;
        info.category = L"QoL";
        info.currentValue = m_warningStyle == WarningStyle::Modal ? L"modal" : L"overlay";
        info.bufferSize = 10;
        info.valueType = ConfigValueType::String;
        settingsManager.AddConfigValue(styleName, info);
    }
    
    // Load the settings
    settingsManager.LoadConfig(filename, nullptr); // Specify nullptr for error parameter
    
    // Now read the values
    const auto& values = settingsManager.GetConfigValues();
    
    auto it = values.find(enabledName);
    if (it != values.end()) {
        m_enabled = (it->second.currentValue == L"true");
    }
    
    it = values.find(thresholdName);
    if (it != values.end()) {
        try {
            m_warningThresholdGB = std::stof(it->second.currentValue);
        } catch (...) {
            m_warningThresholdGB = 3.5f;
        }
    }
    
    it = values.find(styleName);
    if (it != values.end()) {
        m_warningStyle = (it->second.currentValue == L"modal") ? WarningStyle::Modal : WarningStyle::Overlay;
    }
}

void MemoryMonitor::ResetWarning() {
    m_hasWarned = false;
    m_warningDisplayTime = 0.0f;
    m_warningDismissed = true;  // Mark as dismissed until memory drops below threshold
}

// UISettings implementation
bool UISettings::SaveToINI(const std::string& filename) const {
    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Check if file exists
        std::ifstream checkFile(filename);
        bool fileExists = checkFile.is_open();
        checkFile.close();
        
        if (!fileExists) {
            // Create a new file with just QoL settings
            std::ofstream newFile(filename);
            if (!newFile.is_open()) {
                Utils::Logger::Get().Log("Failed to create " + filename);
                return false;
            }
            
            // Write header
            newFile << "; Sims 3 Settings Setter Configuration\n";
            newFile << "; Auto-generated file\n\n";
            
            // Write QoL section
            newFile << "[QoL]\n";
            newFile << "UIToggleKey=" << m_uiToggleKey << "\n";
            newFile << "\n";
            
            newFile.close();
            Utils::Logger::Get().Log("Created new " + filename + " with QoL settings");
            return true;
        }
        
        // File exists, read it to check for QoL section
        std::ifstream infile(filename);
        std::string fileContent;
        std::string line;
        bool hasQoLSettings = false;
        bool hasOldProgramSettings = false;
        
        while (std::getline(infile, line)) {
            if (line == "[QoL]") {
                hasQoLSettings = true;
            }
            if (line == "[ProgramSettings]") {
                hasOldProgramSettings = true;
            }
            fileContent += line + "\n";
        }
        infile.close();
        
        // If QoL settings already exist, we don't need to do anything
        if (hasQoLSettings) {
            return true;
        }
        
        // Append QoL settings to the end of the file
        std::ofstream file(filename, std::ios::app);
        if (!file.is_open()) {
            Utils::Logger::Get().Log("Failed to open " + filename + " for appending QoL settings");
            return false;
        }
        
        // Add a newline if the file doesn't end with one
        if (!fileContent.empty() && fileContent.back() != '\n') {
            file << "\n";
        }
        
        // Append QoL section
        file << "[QoL]\n";
        file << "UIToggleKey=" << m_uiToggleKey << "\n";
        file << "\n";
        
        file.close();
        Utils::Logger::Get().Log("Added QoL settings to " + filename);
        return true;
    }
    catch (const std::exception& e) {
        Utils::Logger::Get().Log("Error saving QoL settings: " + std::string(e.what()));
        return false;
    }
}

bool UISettings::LoadFromINI(const std::string& filename) {
    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            // File doesn't exist, use defaults and mark as needing save
            m_hasUnsavedChanges = true;
            Utils::Logger::Get().Log("No " + filename + " found, will create with defaults");
            return true;
        }
        
        std::string line;
        bool inQoLSection = false;
        bool inOldProgramSection = false;
        bool foundSettings = false;
        
        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == ';') continue;
            
            // Check for section header
            if (line[0] == '[') {
                inQoLSection = (line == "[QoL]");
                inOldProgramSection = (line == "[ProgramSettings]");
                if (inQoLSection || inOldProgramSection) {
                    foundSettings = true;
                }
                continue;
            }
            
            // Parse key=value pairs in relevant sections
            if (inQoLSection || inOldProgramSection) {
                size_t equalPos = line.find('=');
                if (equalPos != std::string::npos) {
                    std::string key = line.substr(0, equalPos);
                    std::string value = line.substr(equalPos + 1);
                    
                    if (key == "UIToggleKey") {
                        m_uiToggleKey = static_cast<UINT>(std::stoul(value));
                    }
                }
            }
        }
        
        file.close();
        
        // If we didn't find settings, mark as needing save
        if (!foundSettings) {
            m_hasUnsavedChanges = true;
            Utils::Logger::Get().Log("No [QoL] section found in " + filename + ", will add on next save");
        } else {
            m_hasUnsavedChanges = false;
            Utils::Logger::Get().Log("Loaded QoL settings from " + filename);
        }
        
        return true;
    }
    catch (const std::exception& e) {
        Utils::Logger::Get().Log("Error loading QoL settings: " + std::string(e.what()));
        return false;
    }
}

std::string UISettings::GetKeyName(UINT vkCode) {
    static const std::unordered_map<UINT, std::string> keyNames = {
        {VK_INSERT, "Insert"},
        {VK_DELETE, "Delete"},
        {VK_HOME, "Home"},
        {VK_END, "End"},
        {VK_PRIOR, "Page Up"},
        {VK_NEXT, "Page Down"},
        {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"},
        {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"},
        {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
        {VK_NUMPAD0, "Numpad 0"}, {VK_NUMPAD1, "Numpad 1"}, {VK_NUMPAD2, "Numpad 2"},
        {VK_NUMPAD3, "Numpad 3"}, {VK_NUMPAD4, "Numpad 4"}, {VK_NUMPAD5, "Numpad 5"},
        {VK_NUMPAD6, "Numpad 6"}, {VK_NUMPAD7, "Numpad 7"}, {VK_NUMPAD8, "Numpad 8"},
        {VK_NUMPAD9, "Numpad 9"},
        {VK_MULTIPLY, "Numpad *"}, {VK_ADD, "Numpad +"}, 
        {VK_SUBTRACT, "Numpad -"}, {VK_DIVIDE, "Numpad /"},
        {VK_PAUSE, "Pause"}, {VK_SCROLL, "Scroll Lock"},
        {VK_OEM_3, "~"}, {VK_OEM_MINUS, "-"}, {VK_OEM_PLUS, "="},
        {VK_OEM_4, "["}, {VK_OEM_6, "]"}, {VK_OEM_5, "\\"},
        {VK_OEM_1, ";"}, {VK_OEM_7, "'"}, {VK_OEM_COMMA, ","},
        {VK_OEM_PERIOD, "."}, {VK_OEM_2, "/"}
    };
    
    auto it = keyNames.find(vkCode);
    if (it != keyNames.end()) {
        return it->second;
    }
    
    // For letter keys
    if (vkCode >= 'A' && vkCode <= 'Z') {
        return std::string(1, static_cast<char>(vkCode));
    }
    
    // For number keys
    if (vkCode >= '0' && vkCode <= '9') {
        return std::string(1, static_cast<char>(vkCode));
    }
    
    // Default
    return "Key " + std::to_string(vkCode);
}