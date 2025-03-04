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