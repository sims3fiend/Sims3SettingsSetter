#pragma once
#include "imgui/imgui.h"
#include "settings.h"

namespace SettingsGui {
    void Initialize();
    void Shutdown();
    void Render();
    void RenderSetting(Setting* setting);
    void RenderMemoryWarning();
    void RenderPresetLoadDialog();
    
    extern bool m_visible;
    extern bool m_showPresetLoadDialog;
    extern std::string m_pendingPresetToLoad;
} 