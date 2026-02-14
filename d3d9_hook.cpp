#include "d3d9_hook.h"
#include "d3d9_hook_registry.h"
#include "gui.h"
#include <detours/detours.h>
#include "imgui.h"
#include "imgui_internal.h" // For ImGuiContext, ImGuiWindow
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include <atomic>
#include <cstdio>
#include "settings_gui.h"
#include "qol.h"
#include "utils.h"
#include "logger.h"

// Mouse coordinate extraction macros (from windowsx.h), see bellow
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

// I HATE IMGUI I HATE IMGUI I HATE IMGUI

LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;

// ImGui scaling state (for resolution spoofing), see comment aove
static constexpr float FONT_OVERSAMPLE = 3.0f;
float g_lastImGuiScale = 0.0f;
bool g_baseStyleSaved = false;
ImGuiStyle g_baseStyle;
EndScene_t original_EndScene = nullptr;
Reset_t original_Reset = nullptr;
WNDPROC original_WndProc = nullptr;
static std::atomic<bool> g_inEndScene(false);
static std::atomic<bool> g_imguiInitializing(false);
static std::atomic<bool> g_imguiInitialized(false);
HWND g_hookedWindow = nullptr;

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!pDevice) { return original_EndScene(pDevice); }
    if (g_inEndScene.exchange(true)) { return original_EndScene(pDevice); }
    HRESULT coop = pDevice->TestCooperativeLevel();
    if (FAILED(coop) && coop != D3DERR_DEVICENOTRESET) {
        char buf[96];
        sprintf_s(buf, "[EndScene] Device not ready. hr=0x%08lX", (unsigned long)coop);
        LOG_DEBUG(buf);
        g_inEndScene.store(false);
        return original_EndScene(pDevice);
    }

    if (!g_imguiInitialized.load() && pDevice) {
        bool expected = false;
        if (!g_imguiInitializing.compare_exchange_strong(expected, true)) {
            g_inEndScene.store(false);
            return original_EndScene(pDevice);
        }
        g_pd3dDevice = pDevice;
        LOG_INFO("[EndScene] Initializing ImGui context");

        // Get the correct window handle
        D3DDEVICE_CREATION_PARAMETERS params;
        if (FAILED(pDevice->GetCreationParameters(&params))) {
            LOG_ERROR("[EndScene] Failed to get device creation parameters");
            g_imguiInitializing.store(false);
            g_inEndScene.store(false);
            return original_EndScene(pDevice);
        }
        HWND gameWindow = params.hFocusWindow;
        // Note: D3D9 creation params do not expose hDeviceWindow. Fallback via swap chain.
        if (!gameWindow) {
            IDirect3DSwapChain9* pSwapChain = nullptr;
            if (SUCCEEDED(pDevice->GetSwapChain(0, &pSwapChain)) && pSwapChain) {
                D3DPRESENT_PARAMETERS pp = {};
                if (SUCCEEDED(pSwapChain->GetPresentParameters(&pp)) && pp.hDeviceWindow) { gameWindow = pp.hDeviceWindow; }
                pSwapChain->Release();
            }
        }
        if (!gameWindow) {
            LOG_ERROR("[EndScene] No valid window handle found");
            g_imguiInitializing.store(false);
            g_inEndScene.store(false);
            return original_EndScene(pDevice);
        }
        g_hookedWindow = gameWindow;

        // Pass window handle to BorderlessWindow for borderless mode support
        BorderlessWindow::Get().SetWindowHandle(gameWindow);

        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        // Load default font at high resolution... Of course of course.. right, right, right......
        ImFontConfig fontConfig;
        fontConfig.SizePixels = 13.0f * FONT_OVERSAMPLE;
        ImGui::GetIO().Fonts->AddFontDefault(&fontConfig);

        // Hook window procedure
        SetLastError(0);
        original_WndProc = (WNDPROC)SetWindowLongPtr(gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        DWORD wndProcErr = GetLastError();
        if (!original_WndProc && wndProcErr != ERROR_SUCCESS) {
            char errBuf[128];
            sprintf_s(errBuf, "[EndScene] SetWindowLongPtr failed. GetLastError=0x%08lX", (unsigned long)wndProcErr);
            LOG_ERROR(errBuf);
        }
        WNDPROC current = (WNDPROC)GetWindowLongPtr(gameWindow, GWLP_WNDPROC);
        if (current != HookedWndProc) { LOG_WARNING("[EndScene] WndProc hook verification failed"); }
        LOG_INFO("[EndScene] Window procedure hooked successfully");

        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX9_Init(pDevice);
        g_imguiInitialized.store(true);
        g_imguiInitializing.store(false);
        LOG_INFO("[EndScene] ImGui initialized");

        // Initialize D3D9 hook registry for patches
        D3D9Hooks::Internal::Initialize(pDevice);
    }

    // Handle deferred borderless reapplication (game may override window pos/size during startup)
    BorderlessWindow::Get().TickReapply();

    if (g_imguiInitialized.load()) {
        try {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();

            // Override display size to match backbuffer when resolution spoofing is active
            // ImGui_ImplWin32_NewFrame uses window client size, but we render at backbuffer size :))))))) HATE
            if (BorderlessWindow::Get().IsEnabled()) {
                IDirect3DSurface9* pBackBuffer = nullptr;
                if (SUCCEEDED(pDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer))) {
                    D3DSURFACE_DESC desc;
                    if (SUCCEEDED(pBackBuffer->GetDesc(&desc))) {
                        ImGuiIO& io = ImGui::GetIO();
                        // Only override if backbuffer differs from what ImGui thinks the display is
                        if (desc.Width != static_cast<UINT>(io.DisplaySize.x) || desc.Height != static_cast<UINT>(io.DisplaySize.y)) {
                            io.DisplaySize = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
                        }

                        // Auto-scale UI based on render resolution
                        {
                            // Use 1080p as the baseline (scale = 1.0)
                            float newScale = static_cast<float>(desc.Height) / 1080.0f;

                            // Clamp scale to reasonable bounds
                            const float minScale = 0.75f;
                            const float maxScale = 3.0f;
                            newScale = (newScale < minScale) ? minScale : (newScale > maxScale) ? maxScale : newScale;

                            // Save the base style once on first run
                            if (!g_baseStyleSaved) {
                                g_baseStyle = ImGuiStyle(ImGui::GetStyle());
                                g_baseStyleSaved = true;
                            }

                            // Only update style sizes if resolution scale changed significantly
                            if (std::abs(newScale - g_lastImGuiScale) > 0.01f) {
                                g_lastImGuiScale = newScale;

                                // Reset style to base, then scale
                                ImGuiStyle& style = ImGui::GetStyle();
                                style = g_baseStyle;
                                style.ScaleAllSizes(newScale);
                            }

                            // Always apply font scale (resolution scale * user font scale / oversample)
                            io.FontGlobalScale = (g_lastImGuiScale * UISettings::Get().GetFontScale()) / FONT_OVERSAMPLE;
                        }
                    }
                    pBackBuffer->Release();
                }
            }

            // Apply user font scale when not in borderless mode (borderless path handles its own scaling above)
            if (!BorderlessWindow::Get().IsEnabled()) { ImGui::GetIO().FontGlobalScale = UISettings::Get().GetFontScale() / FONT_OVERSAMPLE; }

            ImGui::NewFrame();

            SettingsGui::Render();

            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData((ImDrawData*)ImGui::GetDrawData());
        } catch (const std::exception& e) { LOG_ERROR(std::string("[EndScene] Exception: ") + e.what()); } catch (...) {
            LOG_ERROR("[EndScene] Unknown exception");
        }
    }

    HRESULT result = original_EndScene(pDevice);
    g_inEndScene.store(false);
    return result;
}

HRESULT __stdcall HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (g_imguiInitialized.load()) { ImGui_ImplDX9_InvalidateDeviceObjects(); }

    // Enforce windowed mode if Borderless Window is active
    // This is critical when using the Resolution Spoofer patch with resolutions larger than the monitor, Exclusive Fullscreen fail/hangs the driver ):
    if (pPresentationParameters && BorderlessWindow::Get().IsEnabled()) {
        if (!pPresentationParameters->Windowed) {
            LOG_INFO("[Reset] Enforcing Windowed Mode for Borderless Window");
            pPresentationParameters->Windowed = TRUE;
            pPresentationParameters->FullScreen_RefreshRateInHz = 0; // need for windowed apparently

            // Enforce DISCARD swap effect to allow backbuffer scaling (resolution spoofing)
            // D3DSWAPEFFECT_COPY requires backbuffer size to match client area, which would fail here
            pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;

            // LOCKABLE_BACKBUFFER is incompatible with D3DSWAPEFFECT_DISCARD
            if (pPresentationParameters->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) {
                pPresentationParameters->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
                LOG_INFO("[Reset] Stripped LOCKABLE_BACKBUFFER flag");
            }
        }
    }

    HRESULT hr = original_Reset(pDevice, pPresentationParameters);
    if (FAILED(hr)) {
        char buf[96];
        sprintf_s(buf, "[Reset] IDirect3DDevice9::Reset failed. hr=0x%08lX", (unsigned long)hr);
        LOG_DEBUG(buf);
    }

    // Reapply borderless settings after Reset, reset usually resizes the window to the backbuffer size
    if (SUCCEEDED(hr) && BorderlessWindow::Get().IsEnabled()) {
        // We don't have direct access to HWND here except via BorderlessWindow's stored handle or getting it from CreationParameters again, but BorderlessWindow should already have it
        // If pPresentationParameters has it, use it to make sure
        if (pPresentationParameters && pPresentationParameters->hDeviceWindow) { BorderlessWindow::Get().SetWindowHandle(pPresentationParameters->hDeviceWindow); }
        BorderlessWindow::Get().Apply();
    }

    if (SUCCEEDED(hr) && g_imguiInitialized.load()) { ImGui_ImplDX9_CreateDeviceObjects(); }
    return hr;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); //for sure, for sure...

// you guessed it, resolution scaling!
static LPARAM ScaleMouseCoords(HWND hWnd, LPARAM lParam) {
    if (!g_pd3dDevice || !BorderlessWindow::Get().IsEnabled()) { return lParam; }

    // Get backbuffer dimensions
    IDirect3DSurface9* pBackBuffer = nullptr;
    if (FAILED(g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer))) { return lParam; }

    D3DSURFACE_DESC desc;
    HRESULT hr = pBackBuffer->GetDesc(&desc);
    pBackBuffer->Release();

    if (FAILED(hr)) { return lParam; }

    // Get window client size
    RECT clientRect;
    if (!GetClientRect(hWnd, &clientRect)) { return lParam; }

    int windowWidth = clientRect.right - clientRect.left;
    int windowHeight = clientRect.bottom - clientRect.top;

    // If sizes match, no scaling needed
    if (windowWidth == 0 || windowHeight == 0 || (desc.Width == static_cast<UINT>(windowWidth) && desc.Height == static_cast<UINT>(windowHeight))) { return lParam; }

    // Scale mouse coordinates from window space to backbuffer space
    int mouseX = GET_X_LPARAM(lParam);
    int mouseY = GET_Y_LPARAM(lParam);

    float scaleX = static_cast<float>(desc.Width) / static_cast<float>(windowWidth);
    float scaleY = static_cast<float>(desc.Height) / static_cast<float>(windowHeight);

    int scaledX = static_cast<int>(mouseX * scaleX);
    int scaledY = static_cast<int>(mouseY * scaleY);

    return MAKELPARAM(scaledX, scaledY);
}

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Scale mouse coordinates for resolution spoofing before passing to ImGui
    LPARAM scaledLParam = lParam;
    switch (uMsg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDBLCLK:
        scaledLParam = ScaleMouseCoords(hWnd, lParam);
        break;
    }

    // Pass events to ImGui with scaled coordinates
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, scaledLParam)) return true;

    // Handle configurable key to toggle UI
    if (uMsg == WM_KEYDOWN && wParam == UISettings::Get().GetUIToggleKey()) {
        SettingsGui::m_visible = !SettingsGui::m_visible;
        return 0;
    }

    // Block input when UI is visible, this doesn't fully work idfk why like??????
    if (SettingsGui::m_visible) {
        switch (uMsg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_MOUSEMOVE:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_CHAR:
            return 0;
        }
    }

    return CallWindowProc(original_WndProc, hWnd, uMsg, wParam, lParam);
}

bool InitializeD3D9Hook() {
    LOG_INFO("[Init] Starting D3D9 hook initialization");

    try {
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) {
            LOG_ERROR("[Init] Failed to create D3D9 interface");
            return false;
        }

        LOG_INFO("[Init] Created D3D9 interface");

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        // Create a minimal hidden window instead of relying on foreground window
        WNDCLASS wc = {};
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"S3SS_DummyWndClass";
        if (!RegisterClass(&wc)) {
            DWORD regErr = GetLastError();
            if (regErr != ERROR_CLASS_ALREADY_EXISTS) {
                char errBuf[128];
                sprintf_s(errBuf, "[Init] RegisterClass failed. GetLastError=0x%08lX", (unsigned long)regErr);
                LOG_ERROR(errBuf);
                pD3D->Release();
                return false;
            }
        }
        HWND dummyWnd = CreateWindowEx(0, wc.lpszClassName, L"S3SS Dummy", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, nullptr);
        d3dpp.hDeviceWindow = dummyWnd;

        IDirect3DDevice9* pDevice = nullptr;
        HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);

        if (FAILED(hr) || !pDevice) {
            char errBuf[128];
            sprintf_s(errBuf, "[Init] Failed to create D3D device. hr=0x%08lX", (unsigned long)hr);
            LOG_ERROR(errBuf);
            if (dummyWnd) DestroyWindow(dummyWnd);
            pD3D->Release();
            return false;
        }

        LOG_INFO("[Init] Created D3D device");

        void** vTable = *reinterpret_cast<void***>(pDevice);
        original_EndScene = reinterpret_cast<EndScene_t>(vTable[42]);
        original_Reset = reinterpret_cast<Reset_t>(vTable[16]);

        pDevice->Release();
        pD3D->Release();
        if (dummyWnd) DestroyWindow(dummyWnd);

        LOG_DEBUG("[Init] Starting Detours transaction");

        if (DetourTransactionBegin() != NO_ERROR || DetourUpdateThread(GetCurrentThread()) != NO_ERROR || DetourAttach(&(PVOID&)original_EndScene, HookedEndScene) != NO_ERROR ||
            DetourAttach(&(PVOID&)original_Reset, HookedReset) != NO_ERROR || DetourTransactionCommit() != NO_ERROR) {

            LOG_ERROR("[Init] Failed to attach D3D hooks");
            return false;
        }

        LOG_INFO("[Init] D3D9 hook initialization completed successfully");
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("[Init] Exception during D3D9 hook: ") + e.what());
        return false;
    } catch (...) {
        LOG_ERROR("[Init] Unknown exception during D3D9 hook initialization");
        return false;
    }
}

void CleanupD3D9Hook() {
    // Cleanup D3D9 hook registry
    D3D9Hooks::Internal::Cleanup();

    // Restore original window procedure if it was hooked
    if (original_WndProc && g_hookedWindow) {
        LOG_INFO("[Cleanup] Restoring original window procedure");
        SetWindowLongPtr(g_hookedWindow, GWLP_WNDPROC, (LONG_PTR)original_WndProc);
    }

    if (original_EndScene || original_Reset) {
        LOG_INFO("[Cleanup] Detaching D3D hooks");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (original_EndScene) DetourDetach(&(PVOID&)original_EndScene, HookedEndScene);
        if (original_Reset) DetourDetach(&(PVOID&)original_Reset, HookedReset);
        DetourTransactionCommit();
    }
}