#pragma once
#include <Windows.h>
#include <string>
#include <mutex>

enum class WarningStyle {
    Overlay,
    Modal       // Modal dialog that requires user confirmation
};

class MemoryMonitor {
public:
    static MemoryMonitor& Get();

    void Update();
    void SetWarningThreshold(float gigabytes);
    float GetWarningThreshold() const { return m_warningThresholdGB; }
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool enabled);
    float GetCurrentMemoryUsageGB() const { return m_currentMemoryGB; }
    float GetWarningTimeRemaining() const { return m_warningDisplayTime; }
    bool ShouldShowWarning() const { return m_hasWarned && (m_warningStyle == WarningStyle::Modal || m_warningDisplayTime > 0.0f); }
    
    WarningStyle GetWarningStyle() const { return m_warningStyle; }
    void SetWarningStyle(WarningStyle style);

    // Save/load settings
    void SaveSettings(const std::string& filename) const;
    void LoadSettings(const std::string& filename);

    void ResetWarning();  // Just the declaration

private:
    MemoryMonitor() : m_warningThresholdGB(3.5f), m_enabled(false), m_currentMemoryGB(0.0f), 
                      m_hasWarned(false), m_warningDisplayTime(0.0f), m_WARNING_DISPLAY_DURATION(15.0f),
                      m_warningStyle(WarningStyle::Overlay), m_warningDismissed(false) {}
    
    float m_warningThresholdGB;
    bool m_enabled;
    float m_currentMemoryGB;
    bool m_hasWarned;
    float m_warningDisplayTime;
    const float m_WARNING_DISPLAY_DURATION;
    WarningStyle m_warningStyle;
    bool m_warningDismissed;
};

// UI Settings for S3SS itself (not game settings)
class UISettings {
public:
    static UISettings& Get() {
        static UISettings instance;
        return instance;
    }

    // UI Toggle Key
    UINT GetUIToggleKey() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_uiToggleKey;
    }

    void SetUIToggleKey(UINT key);

    // Disable Hooks Setting
    bool GetDisableHooks() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_disableHooks;
    }

    void SetDisableHooks(bool disable);

    // Save/Load
    bool SaveToINI(const std::string& filename) const;
    bool LoadFromINI(const std::string& filename);
    
    // Ensure settings exist in INI (called after main save)
    bool EnsureInINI(const std::string& filename) const { return SaveToINI(filename); }

    bool HasUnsavedChanges() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hasUnsavedChanges;
    }

    void MarkAsSaved() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_hasUnsavedChanges = false;
    }

    // Helper to get key name for display
    static std::string GetKeyName(UINT vkCode);

private:
    UISettings() :
        m_uiToggleKey(VK_INSERT),
        m_disableHooks(false),
        m_hasUnsavedChanges(false) {}

    mutable std::mutex m_mutex;
    UINT m_uiToggleKey;
    bool m_disableHooks;
    bool m_hasUnsavedChanges;
};

// Borderless Window Mode
enum class BorderlessMode {
    Disabled,       // Normal windowed mode with decorations
    DecorationsOnly, // Remove decorations but keep current size/position
    Maximized,      // Remove decorations and maximize to work area (excludes taskbar)
    Fullscreen      // Remove decorations and cover entire monitor (covers taskbar)
};

class BorderlessWindow {
public:
    static BorderlessWindow& Get() {
        static BorderlessWindow instance;
        return instance;
    }

    bool IsEnabled() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mode != BorderlessMode::Disabled;
    }

    BorderlessMode GetMode() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mode;
    }

    void SetMode(BorderlessMode mode);
    void Apply();  // Apply current state to window
    void SetWindowHandle(HWND hwnd);

    // Save/Load
    void LoadSettings(const std::string& filename);

private:
    BorderlessWindow() : m_mode(BorderlessMode::Disabled), m_hwnd(nullptr),
                         m_originalStyle(0), m_originalExStyle(0), m_wasApplied(false) {}

    void RemoveDecorations();  // Helper to remove window chrome
    void ApplyDecorationsOnly();
    void ApplyMaximized();
    void ApplyFullscreen();
    void RestoreWindowed();

    mutable std::mutex m_mutex;
    BorderlessMode m_mode;
    HWND m_hwnd;
    LONG m_originalStyle;
    LONG m_originalExStyle;
    RECT m_originalRect;
    bool m_wasApplied;
}; 