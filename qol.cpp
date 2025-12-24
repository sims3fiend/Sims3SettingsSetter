#include "qol.h"
#include "utils.h"
#include "logger.h"
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

    const auto& configValues = settingsManager.GetConfigValues();

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
void UISettings::SetUIToggleKey(UINT key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_uiToggleKey = key;

    // Update SettingsManager
    auto& settingsManager = SettingsManager::Get();
    std::wstring settingName = L"QoL:UIToggleKey";
    settingsManager.UpdateConfigValue(settingName, std::to_wstring(key));
    settingsManager.SaveConfig("S3SS.ini", nullptr);
}

void UISettings::SetDisableHooks(bool disable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_disableHooks = disable;

    // Update SettingsManager
    auto& settingsManager = SettingsManager::Get();
    std::wstring settingName = L"QoL:DisableHooks";
    settingsManager.UpdateConfigValue(settingName, disable ? L"true" : L"false");
    settingsManager.SaveConfig("S3SS.ini", nullptr);
}

bool UISettings::SaveToINI(const std::string& filename) const {
    // No-op: UISettings are saved immediately when changed via SetUIToggleKey/SetDisableHooks
    // This prevents overwriting other sections (like patch settings) that were saved before this call
    return true;
}

bool UISettings::LoadFromINI(const std::string& filename) {
    try {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto& settingsManager = SettingsManager::Get();

        // Register our config values if they don't exist yet
        std::wstring toggleKeyName = L"QoL:UIToggleKey";
        std::wstring disableHooksName = L"QoL:DisableHooks";

        const auto& configValues = settingsManager.GetConfigValues();

        if (configValues.find(toggleKeyName) == configValues.end()) {
            ConfigValueInfo info;
            info.category = L"QoL";
            info.currentValue = std::to_wstring(m_uiToggleKey);
            info.bufferSize = 10;
            info.valueType = ConfigValueType::Integer;
            settingsManager.AddConfigValue(toggleKeyName, info);
        }

        if (configValues.find(disableHooksName) == configValues.end()) {
            ConfigValueInfo info;
            info.category = L"QoL";
            info.currentValue = m_disableHooks ? L"true" : L"false";
            info.bufferSize = 10;
            info.valueType = ConfigValueType::Boolean;
            settingsManager.AddConfigValue(disableHooksName, info);
        }

        // Load the settings
        settingsManager.LoadConfig(filename, nullptr);

        // Now read the values
        const auto& values = settingsManager.GetConfigValues();

        auto it = values.find(toggleKeyName);
        if (it != values.end()) {
            try {
                m_uiToggleKey = static_cast<UINT>(std::stoul(it->second.currentValue));
            } catch (...) {
                m_uiToggleKey = VK_INSERT;
            }
        }

        it = values.find(disableHooksName);
        if (it != values.end()) {
            m_disableHooks = (it->second.currentValue == L"true");
        }

        m_hasUnsavedChanges = false;
        LOG_INFO("Loaded QoL settings from " + filename);

        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Error loading QoL settings: " + std::string(e.what()));
        return false;
    }
}

// BorderlessWindow stuff, I could probably streamline this significantly
void BorderlessWindow::SetWindowHandle(HWND hwnd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hwnd = hwnd;

    // Store original window style and rect when we first get the handle
    if (hwnd && m_originalStyle == 0) {
        m_originalStyle = GetWindowLong(hwnd, GWL_STYLE);
        GetWindowRect(hwnd, &m_originalRect);
    }

    // Apply borderless if it was enabled before we had a window handle
    if (m_mode != BorderlessMode::Disabled && !m_wasApplied) {
        switch (m_mode) {
            case BorderlessMode::DecorationsOnly:
                ApplyDecorationsOnly();
                break;
            case BorderlessMode::Maximized:
                ApplyMaximized();
                break;
            case BorderlessMode::Fullscreen:
                ApplyFullscreen();
                break;
            default:
                break;
        }
    }
}

void BorderlessWindow::SetMode(BorderlessMode mode) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mode == mode) return;
        m_mode = mode;
    }

    // Apply the change
    Apply();

    // Save to config
    auto& settingsManager = SettingsManager::Get();
    std::wstring settingName = L"QoL:BorderlessWindow:Mode";
    std::wstring modeValue;
    switch (mode) {
        case BorderlessMode::Disabled: modeValue = L"disabled"; break;
        case BorderlessMode::DecorationsOnly: modeValue = L"decorations_only"; break;
        case BorderlessMode::Maximized: modeValue = L"maximized"; break;
        case BorderlessMode::Fullscreen: modeValue = L"fullscreen"; break;
    }
    settingsManager.UpdateConfigValue(settingName, modeValue);
    settingsManager.SaveConfig("S3SS.ini", nullptr);
}

void BorderlessWindow::Apply() {
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (m_mode) {
        case BorderlessMode::Disabled:
            RestoreWindowed();
            break;
        case BorderlessMode::DecorationsOnly:
            ApplyDecorationsOnly();
            break;
        case BorderlessMode::Maximized:
            ApplyMaximized();
            break;
        case BorderlessMode::Fullscreen:
            ApplyFullscreen();
            break;
    }
}

void BorderlessWindow::RemoveDecorations() {
    // Store original styles if we haven't already
    if (m_originalStyle == 0) {
        m_originalStyle = GetWindowLong(m_hwnd, GWL_STYLE);
        m_originalExStyle = GetWindowLong(m_hwnd, GWL_EXSTYLE);
        GetWindowRect(m_hwnd, &m_originalRect);
    }

    // Remove window decorations (border, title bar, etc.)
    LONG style = m_originalStyle;
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
    style |= WS_POPUP;
    SetWindowLong(m_hwnd, GWL_STYLE, style);

    // Remove extended styles that might cause borders
    LONG exStyle = m_originalExStyle;
    exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    SetWindowLong(m_hwnd, GWL_EXSTYLE, exStyle);
}

void BorderlessWindow::ApplyDecorationsOnly() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    RemoveDecorations();

    // Just update the frame, keep current size/position
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

    m_wasApplied = true;
    LOG_INFO("Borderless (decorations only) mode applied");
}

void BorderlessWindow::ApplyMaximized() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    RemoveDecorations();

    // Get the monitor work area (excludes taskbar)
    HMONITOR hMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFO);

    if (GetMonitorInfo(hMonitor, &monitorInfo)) {
        int x = monitorInfo.rcWork.left;
        int y = monitorInfo.rcWork.top;
        int width = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        int height = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

        SetWindowPos(m_hwnd, nullptr, x, y, width, height,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
    } else {
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    m_wasApplied = true;
    LOG_INFO("Borderless maximized mode applied");
}

void BorderlessWindow::ApplyFullscreen() {
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    RemoveDecorations();

    // Get the monitor the window is currently on
    HMONITOR hMonitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(MONITORINFO);

    if (GetMonitorInfo(hMonitor, &monitorInfo)) {
        // Use rcMonitor (full monitor area) instead of rcWork (excludes taskbar)
        int x = monitorInfo.rcMonitor.left;
        int y = monitorInfo.rcMonitor.top;
        int width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        int height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

        // Move window to top-left of monitor and resize to cover full screen. This changes the window size, which may cause issues if the game
        // is using a spoofed resolution via ResolutionSpoofer. In that case, D3D backbuffer won't match the window size.
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
        // Remove TOPMOST immediately after to avoid staying always-on-top
        SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        // Fallback: just update the frame without resizing
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    m_wasApplied = true;
    LOG_INFO("Borderless fullscreen mode applied");
}

void BorderlessWindow::RestoreWindowed() {
    if (!m_hwnd || !IsWindow(m_hwnd) || !m_wasApplied) return;

    // Restore original style but keep current size/position
    // Restoring the original window size can cause driver timeouts when large backbuffer (e.g., from resolution spoofing) is active
    if (m_originalStyle != 0) {
        SetWindowLong(m_hwnd, GWL_STYLE, m_originalStyle);
        SetWindowLong(m_hwnd, GWL_EXSTYLE, m_originalExStyle);

        // Just update the frame, don't change size/position
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    m_wasApplied = false;
    LOG_INFO("Restored windowed mode");
}

void BorderlessWindow::LoadSettings(const std::string& filename) {
    auto& settingsManager = SettingsManager::Get();

    std::wstring modeName = L"QoL:BorderlessWindow:Mode";

    const auto& configValues = settingsManager.GetConfigValues();

    if (configValues.find(modeName) == configValues.end()) {
        ConfigValueInfo info;
        info.category = L"QoL";
        info.currentValue = L"disabled";
        info.bufferSize = 20;
        info.valueType = ConfigValueType::String;
        settingsManager.AddConfigValue(modeName, info);
    }

    // Load the settings
    settingsManager.LoadConfig(filename, nullptr);

    // Read the value
    const auto& values = settingsManager.GetConfigValues();

    auto it = values.find(modeName);
    if (it != values.end()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto& val = it->second.currentValue;
        if (val == L"decorations_only") {
            m_mode = BorderlessMode::DecorationsOnly;
        } else if (val == L"maximized") {
            m_mode = BorderlessMode::Maximized;
        } else if (val == L"fullscreen") {
            m_mode = BorderlessMode::Fullscreen;
        } else {
            m_mode = BorderlessMode::Disabled;
        }
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