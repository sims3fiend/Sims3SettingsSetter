#include "qol.h"
#include "utils.h"
#include "logger.h"
#include "settings.h"
#include "config/config_store.h"
#include <Psapi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <toml++/toml.hpp>
#include <winternl.h>

MemoryMonitor& MemoryMonitor::Get() {
    static MemoryMonitor instance;
    return instance;
}

enum { ProcessVmCounters = 3 };

struct VM_COUNTERS {
    size_t PeakVirtualSize;
    size_t VirtualSize;
    uint32_t PageFaultCount;
    size_t PeakWorkingSetSize;
    size_t WorkingSetSize;
    size_t QuotaPeakPagedPoolUsage;
    size_t QuotaPagedPoolUsage;
    size_t QuotaPeakNonPagedPoolUsage;
    size_t QuotaNonPagedPoolUsage;
    size_t PagefileUsage;
    size_t PeakPagefileUsage;
};

void MemoryMonitor::Update() {
    if (!m_enabled) {
        m_hasWarned = false;
        m_warningDismissed = false;
        return;
    }

    VM_COUNTERS memory;
    if (!NtQueryInformationProcess(GetCurrentProcess(), static_cast<PROCESSINFOCLASS>(ProcessVmCounters), &memory, sizeof(memory), nullptr)) {
        // Convert to GB
        m_currentMemoryGB = memory.VirtualSize / (1024.0f * 1024.0f * 1024.0f);

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
        if (m_warningDisplayTime > 0.0f) { m_warningDisplayTime -= 1.0f / 60.0f; }
    }
}

void MemoryMonitor::SetWarningThreshold(float gigabytes) {
    m_warningThresholdGB = gigabytes;
    m_hasWarned = false;
    ConfigStore::Get().SaveAll();
}

void MemoryMonitor::SetEnabled(bool enabled) {
    m_enabled = enabled;
    ConfigStore::Get().SaveAll();
}

void MemoryMonitor::SetWarningStyle(WarningStyle style) {
    m_warningStyle = style;
    ConfigStore::Get().SaveAll();
}

void MemoryMonitor::SaveToToml(toml::table& qolTable) const {
    toml::table memTable;
    memTable.insert("enabled", m_enabled);
    memTable.insert("warning_threshold", static_cast<double>(m_warningThresholdGB));
    memTable.insert("warning_style", std::string(m_warningStyle == WarningStyle::Modal ? "modal" : "overlay"));
    qolTable.insert("memory_monitor", std::move(memTable));
}

void MemoryMonitor::LoadFromToml(const toml::table& qolTable) {
    auto memNode = qolTable["memory_monitor"].as_table();
    if (!memNode) return;

    m_enabled = (*memNode)["enabled"].value_or(false);
    m_warningThresholdGB = static_cast<float>((*memNode)["warning_threshold"].value_or(3.5));
    std::string style = (*memNode)["warning_style"].value_or(std::string("overlay"));
    m_warningStyle = (style == "modal") ? WarningStyle::Modal : WarningStyle::Overlay;
}

void MemoryMonitor::ResetWarning() {
    m_hasWarned = false;
    m_warningDisplayTime = 0.0f;
    m_warningDismissed = true; // Mark as dismissed until memory drops below threshold
}

// UISettings implementation
void UISettings::SetUIToggleKey(UINT key) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_uiToggleKey = key;
    }
    ConfigStore::Get().SaveAll();
}

void UISettings::SetDisableHooks(bool disable) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_disableHooks = disable;
    }
    ConfigStore::Get().SaveAll();
}

void UISettings::SetFontScale(float scale) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fontScale = scale;
    }
    ConfigStore::Get().SaveAll();
}

void UISettings::SaveToToml(toml::table& qolTable) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    toml::table uiTable;
    uiTable.insert("toggle_key", static_cast<int64_t>(m_uiToggleKey));
    uiTable.insert("disable_hooks", m_disableHooks);
    uiTable.insert("font_scale", static_cast<double>(m_fontScale));
    qolTable.insert("ui", std::move(uiTable));
}

void UISettings::LoadFromToml(const toml::table& qolTable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto uiNode = qolTable["ui"].as_table();
    if (!uiNode) return;

    m_uiToggleKey = static_cast<UINT>((*uiNode)["toggle_key"].value_or(int64_t(VK_INSERT)));
    m_disableHooks = (*uiNode)["disable_hooks"].value_or(false);
    m_fontScale = static_cast<float>((*uiNode)["font_scale"].value_or(1.0));
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
        // jank alert
        // Schedule a deferred reapply — the game may override our window position/size during its own startup sequence after we apply here
        m_reapplyCountdown = 60;
    }
}

void BorderlessWindow::TickReapply() {
    bool shouldApply = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_reapplyCountdown > 0 && --m_reapplyCountdown == 0) { shouldApply = true; }
    }
    if (shouldApply) { Apply(); }
}

void BorderlessWindow::SetMode(BorderlessMode mode) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_mode == mode) return;
        m_mode = mode;
    }

    Apply();
    ConfigStore::Get().SaveAll();
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
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);

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

        SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
    } else {
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
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
        SetWindowPos(m_hwnd, nullptr, x, y, width, height, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
    } else {
        // Fallback: just update the frame without resizing
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
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
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }

    m_wasApplied = false;
    LOG_INFO("Restored windowed mode");
}

void BorderlessWindow::SaveToToml(toml::table& qolTable) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    toml::table bwTable;
    std::string modeStr;
    switch (m_mode) {
    case BorderlessMode::Disabled:
        modeStr = "disabled";
        break;
    case BorderlessMode::DecorationsOnly:
        modeStr = "decorations_only";
        break;
    case BorderlessMode::Maximized:
        modeStr = "maximized";
        break;
    case BorderlessMode::Fullscreen:
        modeStr = "fullscreen";
        break;
    }
    bwTable.insert("mode", modeStr);
    qolTable.insert("borderless_window", std::move(bwTable));
}

void BorderlessWindow::LoadFromToml(const toml::table& qolTable) {
    auto bwNode = qolTable["borderless_window"].as_table();
    if (!bwNode) return;

    std::string mode = (*bwNode)["mode"].value_or(std::string("disabled"));
    std::lock_guard<std::mutex> lock(m_mutex);
    if (mode == "decorations_only") {
        m_mode = BorderlessMode::DecorationsOnly;
    } else if (mode == "maximized") {
        m_mode = BorderlessMode::Maximized;
    } else if (mode == "fullscreen") {
        m_mode = BorderlessMode::Fullscreen;
    } else {
        m_mode = BorderlessMode::Disabled;
    }
}

std::string UISettings::GetKeyName(UINT vkCode) {
    static const std::unordered_map<UINT, std::string> keyNames = {{VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"}, {VK_PRIOR, "Page Up"}, {VK_NEXT, "Page Down"}, {VK_F1, "F1"},
        {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"}, {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"}, {VK_NUMPAD0, "Numpad 0"},
        {VK_NUMPAD1, "Numpad 1"}, {VK_NUMPAD2, "Numpad 2"}, {VK_NUMPAD3, "Numpad 3"}, {VK_NUMPAD4, "Numpad 4"}, {VK_NUMPAD5, "Numpad 5"}, {VK_NUMPAD6, "Numpad 6"}, {VK_NUMPAD7, "Numpad 7"}, {VK_NUMPAD8, "Numpad 8"},
        {VK_NUMPAD9, "Numpad 9"}, {VK_MULTIPLY, "Numpad *"}, {VK_ADD, "Numpad +"}, {VK_SUBTRACT, "Numpad -"}, {VK_DIVIDE, "Numpad /"}, {VK_PAUSE, "Pause"}, {VK_SCROLL, "Scroll Lock"}, {VK_OEM_3, "~"}, {VK_OEM_MINUS, "-"},
        {VK_OEM_PLUS, "="}, {VK_OEM_4, "["}, {VK_OEM_6, "]"}, {VK_OEM_5, "\\"}, {VK_OEM_1, ";"}, {VK_OEM_7, "'"}, {VK_OEM_COMMA, ","}, {VK_OEM_PERIOD, "."}, {VK_OEM_2, "/"}};

    auto it = keyNames.find(vkCode);
    if (it != keyNames.end()) { return it->second; }

    // For letter keys
    if (vkCode >= 'A' && vkCode <= 'Z') { return std::string(1, static_cast<char>(vkCode)); }

    // For number keys
    if (vkCode >= '0' && vkCode <= '9') { return std::string(1, static_cast<char>(vkCode)); }

    // Default
    return "Key " + std::to_string(vkCode);
}