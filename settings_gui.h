#pragma once
#include "imgui.h"
#include "settings.h"

namespace SettingsGui {
    void Initialize();
    void Shutdown();
    void Render();
    void RenderSetting(Setting* setting);
    void RenderMemoryWarning();

    extern bool m_visible;
} 