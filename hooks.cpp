#include <windows.h>
#include <d3d9.h>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "settings_gui.h"

// Forward declare message handler from imgui_impl_win32.cpp :) :) :) :) :) :):)!
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

typedef HRESULT(WINAPI* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
Present_t Original_Present = nullptr;

HRESULT WINAPI Present_Hook(IDirect3DDevice9* device, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) {

    static bool in_present = false;
    if (in_present) { return Original_Present(device, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion); }

    in_present = true;
    HRESULT result = Original_Present(device, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    in_present = false;
    return result;
}