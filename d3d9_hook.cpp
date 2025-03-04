#include "d3d9_hook.h"
#include "gui.h"
#include <detours.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "settings_gui.h"

// I HATE IMGUI I HATE IMGUI I HATE IMGUI

LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
EndScene_t original_EndScene = nullptr;
Reset_t original_Reset = nullptr;
WNDPROC original_WndProc = nullptr;

HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    static bool in_endscene = false;
    if (in_endscene) {
        return original_EndScene(pDevice);
    }
    
    if (!pDevice || pDevice->TestCooperativeLevel() != D3D_OK) {
        return original_EndScene(pDevice);
    }

    in_endscene = true;
    
    static bool init = false;
    if (!init && pDevice) {
        g_pd3dDevice = pDevice;
        OutputDebugStringA("Initializing ImGui in EndScene\n");

        // Get the correct window handle
        D3DDEVICE_CREATION_PARAMETERS params;
        if (FAILED(pDevice->GetCreationParameters(&params))) {
            OutputDebugStringA("Failed to get device creation parameters\n");
            return original_EndScene(pDevice);
        }
        HWND gameWindow = params.hFocusWindow;
        
        // Initialize ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        // Hook window procedure
        original_WndProc = (WNDPROC)SetWindowLongPtr(gameWindow, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);
        if (!original_WndProc) {
            OutputDebugStringA(("Failed to hook window procedure. Error: " + std::to_string(GetLastError()) + "\n").c_str());
            return original_EndScene(pDevice);
        }
        OutputDebugStringA("Window procedure hooked successfully\n");

        ImGui_ImplWin32_Init(gameWindow);
        ImGui_ImplDX9_Init(pDevice);
        init = true;
        OutputDebugStringA("ImGui initialized in EndScene\n");
    }

    if (init) {
        try {
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            SettingsGui::Render();
            
            ImGui::EndFrame();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
        catch (const std::exception& e) {
            OutputDebugStringA(("Exception in EndScene: " + std::string(e.what()) + "\n").c_str());
        }
        catch (...) {
            OutputDebugStringA("Unknown exception in EndScene\n");
        }
    }

    HRESULT result = original_EndScene(pDevice);
    in_endscene = false;
    return result;
}

HRESULT __stdcall HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = original_Reset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); //for sure, for sure...

LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Pass events to ImGui
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    // Handle Insert key to toggle UI
    if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
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
    OutputDebugStringA("Starting D3D9 hook initialization\n");
    
    try {
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) {
            OutputDebugStringA("Failed to create D3D9\n");
            return false;
        }

        OutputDebugStringA("Created D3D9 interface\n");

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = GetForegroundWindow();

        IDirect3DDevice9* pDevice = nullptr;
        HRESULT hr = pD3D->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
            d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &d3dpp, &pDevice);

        if (FAILED(hr) || !pDevice) {
            OutputDebugStringA("Failed to create D3D device\n");
            pD3D->Release();
            return false;
        }

        OutputDebugStringA("Created D3D device\n");

        void** vTable = *reinterpret_cast<void***>(pDevice);
        original_EndScene = reinterpret_cast<EndScene_t>(vTable[42]);
        original_Reset = reinterpret_cast<Reset_t>(vTable[16]);

        pDevice->Release();
        pD3D->Release();

        OutputDebugStringA("Starting Detours transaction\n");

        if (DetourTransactionBegin() != NO_ERROR ||
            DetourUpdateThread(GetCurrentThread()) != NO_ERROR ||
            DetourAttach(&(PVOID&)original_EndScene, HookedEndScene) != NO_ERROR ||
            DetourAttach(&(PVOID&)original_Reset, HookedReset) != NO_ERROR ||
            DetourTransactionCommit() != NO_ERROR) {
            
            OutputDebugStringA("Failed to attach D3D hooks\n");
            return false;
        }

        OutputDebugStringA("D3D9 hook initialization completed successfully\n");
        return true;
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("D3D9 hook exception: " + std::string(e.what()) + "\n").c_str());
        return false;
    }
    catch (...) {
        OutputDebugStringA("Unknown exception in D3D9 hook initialization\n");
        return false;
    }
}

void CleanupD3D9Hook() {
    // Restore original window procedure if it was hooked
    if (original_WndProc && g_pd3dDevice) {
        D3DDEVICE_CREATION_PARAMETERS params;
        if (SUCCEEDED(g_pd3dDevice->GetCreationParameters(&params))) {
            SetWindowLongPtr(params.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)original_WndProc);
        }
    }

    if (original_EndScene || original_Reset) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (original_EndScene) DetourDetach(&(PVOID&)original_EndScene, HookedEndScene);
        if (original_Reset) DetourDetach(&(PVOID&)original_Reset, HookedReset);
        DetourTransactionCommit();
    }
} 