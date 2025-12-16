#pragma once
#include <d3d9.h>
#include <memory>

// Function typedefs
typedef HRESULT(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
typedef HRESULT(__stdcall* Reset_t)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);

// Global variables
extern LPDIRECT3DDEVICE9 g_pd3dDevice;
extern EndScene_t original_EndScene;
extern Reset_t original_Reset;
extern WNDPROC original_WndProc;
extern HWND g_hookedWindow;

bool InitializeD3D9Hook();
void CleanupD3D9Hook();

// Hook functions
HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice);
HRESULT __stdcall HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam); 