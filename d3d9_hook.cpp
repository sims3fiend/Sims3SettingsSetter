#include "d3d9_hook.h"
#include "gui.h"
#include <detours.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include <atomic>
#include <cstdio>
#include "settings_gui.h"
#include "qol.h"
#include "utils.h"

// I HATE IMGUI I HATE IMGUI I HATE IMGUI

LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
EndScene_t original_EndScene = nullptr;
Reset_t original_Reset = nullptr;
WNDPROC original_WndProc = nullptr;
static std::atomic<bool> g_inEndScene(false);
static std::atomic<bool> g_imguiInitializing(false);
static std::atomic<bool> g_imguiInitialized(false);
static HWND g_hookedWindow = nullptr;

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!pDevice) {
        return original_EndScene(pDevice);
    }
    if (g_inEndScene.exchange(true)) {
        return original_EndScene(pDevice);
    }
    HRESULT coop = pDevice->TestCooperativeLevel();
    if (FAILED(coop) && coop != D3DERR_DEVICENOTRESET) {
        char buf[96];
        sprintf_s(buf, "[EndScene] Device not ready. hr=0x%08lX", (unsigned long)coop);
        Utils::Logger::Get().Log(buf);
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
        Utils::Logger::Get().Log("[EndScene] Initializing ImGui context");

        // Get the correct window handle
        D3DDEVICE_CREATION_PARAMETERS params;
        if (FAILED(pDevice->GetCreationParameters(&params))) {
            Utils::Logger::Get().Log("[EndScene] Failed to get device creation parameters");
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
                if (SUCCEEDED(pSwapChain->GetPresentParameters(&pp)) && pp.hDeviceWindow) {
                    gameWindow = pp.hDeviceWindow;
                }
                pSwapChain->Release();
            }
        }
        if (!gameWindow) {
            Utils::Logger::Get().Log("[EndScene] No valid window handle found");
            g_imguiInitializing.store(false);
            g_inEndScene.store(false);
            return original_EndScene(pDevice);
        }
        g_hookedWindow = gameWindow;
        
        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        // Hook window procedure
        SetLastError(0);
        original_WndProc = (WNDPROC)SetWindowLongPtr(gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        DWORD wndProcErr = GetLastError();
        if (!original_WndProc && wndProcErr != ERROR_SUCCESS) {
            char errBuf[128];
            sprintf_s(errBuf, "[EndScene] SetWindowLongPtr failed. GetLastError=0x%08lX", (unsigned long)wndProcErr);
            Utils::Logger::Get().Log(errBuf);
        }
        WNDPROC current = (WNDPROC)GetWindowLongPtr(gameWindow, GWLP_WNDPROC);
        if (current != HookedWndProc) {
            Utils::Logger::Get().Log("[EndScene] WndProc hook verification failed");
        }
        Utils::Logger::Get().Log("[EndScene] Window procedure hooked successfully");

        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX9_Init(pDevice);
        g_imguiInitialized.store(true);
        g_imguiInitializing.store(false);
        Utils::Logger::Get().Log("[EndScene] ImGui initialized");
    }

    if (g_imguiInitialized.load()) {
        try {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            SettingsGui::Render();
            
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData((ImDrawData*)ImGui::GetDrawData());
        }
        catch (const std::exception& e) {
            Utils::Logger::Get().Log(std::string("[EndScene] Exception: ") + e.what());
        }
        catch (...) {
            Utils::Logger::Get().Log("[EndScene] Unknown exception");
        }
    }

    HRESULT result = original_EndScene(pDevice);
    g_inEndScene.store(false);
    return result;
}

HRESULT __stdcall HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (g_imguiInitialized.load()) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    HRESULT hr = original_Reset(pDevice, pPresentationParameters);
    if (FAILED(hr)) {
        char buf[96];
        sprintf_s(buf, "[Reset] IDirect3DDevice9::Reset failed. hr=0x%08lX", (unsigned long)hr);
        Utils::Logger::Get().Log(buf);
    }
    if (SUCCEEDED(hr) && g_imguiInitialized.load()) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); //for sure, for sure...

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Pass events to ImGui
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

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
    Utils::Logger::Get().Log("[Init] Starting D3D9 hook initialization");
    
    try {
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) {
            Utils::Logger::Get().Log("[Init] Failed to create D3D9 interface");
            return false;
        }

        Utils::Logger::Get().Log("[Init] Created D3D9 interface");

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
                Utils::Logger::Get().Log(errBuf);
                pD3D->Release();
                return false;
            }
        }
        HWND dummyWnd = CreateWindowEx(0, wc.lpszClassName, L"S3SS Dummy", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, nullptr);
        d3dpp.hDeviceWindow = dummyWnd;

        IDirect3DDevice9* pDevice = nullptr;
        HRESULT hr = pD3D->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &d3dpp, &pDevice);

        if (FAILED(hr) || !pDevice) {
            char errBuf[128];
            sprintf_s(errBuf, "[Init] Failed to create D3D device. hr=0x%08lX", (unsigned long)hr);
            Utils::Logger::Get().Log(errBuf);
            if (dummyWnd) DestroyWindow(dummyWnd);
            pD3D->Release();
            return false;
        }

        Utils::Logger::Get().Log("[Init] Created D3D device");

        void** vTable = *reinterpret_cast<void***>(pDevice);
        original_EndScene = reinterpret_cast<EndScene_t>(vTable[42]);
        original_Reset = reinterpret_cast<Reset_t>(vTable[16]);

        pDevice->Release();
        pD3D->Release();
        if (dummyWnd) DestroyWindow(dummyWnd);

        Utils::Logger::Get().Log("[Init] Starting Detours transaction");

        if (DetourTransactionBegin() != NO_ERROR ||
            DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
            DetourAttach(&(PVOID&)original_EndScene, HookedEndScene) != NO_ERROR ||
            DetourAttach(&(PVOID&)original_Reset, HookedReset) != NO_ERROR ||
            DetourTransactionCommit() != NO_ERROR) {
            
            Utils::Logger::Get().Log("[Init] Failed to attach D3D hooks");
            return false;
        }

        Utils::Logger::Get().Log("[Init] D3D9 hook initialization completed successfully");
        return true;
    }
    catch (const std::exception& e) {
        Utils::Logger::Get().Log(std::string("[Init] Exception during D3D9 hook: ") + e.what());
        return false;
    }
    catch (...) {
        Utils::Logger::Get().Log("[Init] Unknown exception during D3D9 hook initialization");
        return false;
    }
}

void CleanupD3D9Hook() {
    // Restore original window procedure if it was hooked
    if (original_WndProc && g_hookedWindow) {
        Utils::Logger::Get().Log("[Cleanup] Restoring original window procedure");
        SetWindowLongPtr(g_hookedWindow, GWLP_WNDPROC, (LONG_PTR)original_WndProc);
    }

    if (original_EndScene || original_Reset) {
        Utils::Logger::Get().Log("[Cleanup] Detaching D3D hooks");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (original_EndScene) DetourDetach(&(PVOID&)original_EndScene, HookedEndScene);
        if (original_Reset) DetourDetach(&(PVOID&)original_Reset, HookedReset);
        DetourTransactionCommit();
    }
} 