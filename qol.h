#pragma once
#include <Windows.h>
#include <string>

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