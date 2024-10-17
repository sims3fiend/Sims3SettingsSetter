#pragma once

#include <Windows.h>
#include <string>
#include <d3d9.h>
#include <map>
#include <mutex>
#include <array>
#include <variant>
#include <vector>
#include <unordered_map>
#include "script_settings.h"
#include "utils.h"

// Macro
#ifdef CONFIG_HOOK_EXPORTS
#define CONFIG_HOOK_API __declspec(dllexport)
#else
#define CONFIG_HOOK_API __declspec(dllimport)
#endif

// Function prototypes for the original functions
typedef unsigned int(__fastcall* RetrieveConfigValue_t)(int param_1_00, int dummy, short* param_2, int* param_3, void** param_4);
typedef unsigned char(__cdecl* GetConfigBoolWithKeyConstruction_t)(int* param_1, unsigned char param_2);
typedef HRESULT(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9 pDevice);
typedef HRESULT(__stdcall* Reset_t)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
// for later :) typedef HRESULT(__stdcall* Present_t)(LPDIRECT3DDEVICE9 pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion);



typedef void(__fastcall* FUN_006e7b60_t)(int param_1);
extern FUN_006e7b60_t original_FUN_006e7b60;
void __fastcall HookedFUN_006e7b60(int param_1);

typedef void(__fastcall* FUN_00c128c0_t)(int param_1);
extern FUN_00c128c0_t original_FUN_00c128c0;
void __fastcall HookedFUN_00c128c0(int param_1);

typedef void(__thiscall* FUN_006c35e0_t)(void* param_1_00, unsigned int param_2);
void __fastcall HookedFUN_006c35e0(void* param_1_00, void* dummy, unsigned int param_2);
extern FUN_006c35e0_t original_FUN_006c35e0;

//this one for undef4 *param_1
typedef uint32_t* (__fastcall* FUN_00d6cc30_t)(uint32_t* param_1);
extern FUN_00d6cc30_t original_FUN_00d6cc30;
uint32_t* __fastcall HookedFUN_00d6cc30(uint32_t* param_1);

//FUN_0082f910
typedef uint32_t* (__fastcall* FUN_0082f910_t)(uint32_t* param_1);
extern FUN_0082f910_t original_FUN_0082f910;
uint32_t* __fastcall HookedFUN_0082f910(uint32_t* param_1);

//FUN_0096dc30
typedef uint32_t* (__fastcall* FUN_0096dc30_t)(uint32_t* param_1);
extern FUN_0096dc30_t original_FUN_0096dc30;
uint32_t* __fastcall HookedFUN_0096dc30(uint32_t* param_1);

//FUN_00816610
typedef uint32_t* (__fastcall* FUN_00816610_t)(uint32_t* param_1);
extern FUN_00816610_t original_FUN_00816610;
uint32_t* __fastcall HookedFUN_00816610(uint32_t* param_1);

typedef uint32_t* (__fastcall* FUN_00c671e0_t)(uint32_t* param_1);
extern FUN_00c671e0_t original_FUN_00c671e0;
uint32_t* __fastcall HookedFUN_00c671e0(uint32_t* param_1);

typedef uint32_t* (__fastcall* FUN_009172b0_t)(uint32_t* param_1);
extern FUN_009172b0_t original_FUN_009172b0;
uint32_t* __fastcall HookedFUN_009172b0(uint32_t* param_1);

typedef void (*DumpCachedTexturesCallback_t)(void*);
extern DumpCachedTexturesCallback_t g_DumpCachedTexturesCallback;
extern void* g_DumpCachedTexturesContext;

// Exported functions for initializing and cleaning up hooks
extern "C" {
    CONFIG_HOOK_API bool InitializeHooks();
    CONFIG_HOOK_API void CleanupHooks();
}

// Utility function declarations
std::wstring NarrowToWide(const std::string& narrow);
std::string WideToNarrow(const std::wstring& wide);
bool IsSafeToRead(const void* ptr, size_t size);
std::string DumpMemory(const void* ptr, size_t size);

// Hooked function declarations
unsigned int __fastcall HookedRetrieveConfigValue(int param_1_00, int dummy, short* param_2, int* param_3, void** param_4);
unsigned char __cdecl HookedGetConfigBoolWithKeyConstruction(int* param_1, unsigned char param_2);
HRESULT __stdcall HookedEndScene(LPDIRECT3DDEVICE9 pDevice);
HRESULT __stdcall HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);



// Function to initialize the hook (using Detours)
bool InitializeDetours();

// Function to clean up the hook (using Detours)
void CleanupDetours();

// Original function pointers
extern RetrieveConfigValue_t original_RetrieveConfigValue;
extern GetConfigBoolWithKeyConstruction_t original_GetConfigBoolWithKeyConstruction;
extern EndScene_t original_EndScene;
extern Reset_t original_Reset;
//extern Present_t original_Present;


typedef void(__cdecl* FUN_007235b0_t)(int);
extern FUN_007235b0_t original_FUN_007235b0;
void __cdecl HookedFUN_007235b0(int param_1);

typedef void(__fastcall* FUN_006e9270_t)(int param_1);
extern FUN_006e9270_t original_FUN_006e9270;
void __fastcall HookedFUN_006e9270(int param_1);

typedef uint32_t(__fastcall* FUN_006ef780_t)(uint32_t param_1);
extern FUN_006ef780_t original_FUN_006ef780;
uint32_t __fastcall HookedFUN_006ef780(uint32_t param_1);

typedef int(__fastcall* FUN_00572490_t)(int param_1);
extern FUN_00572490_t original_FUN_00572490;
int __fastcall HookedFUN_00572490(int param_1);


// Thread safety for config stuff
extern std::map<std::string, std::pair<std::string, uintptr_t>> uniqueConfigs;
extern std::mutex configMutex;

extern std::variant<bool, int, float, std::string> ParseSettingValue(SettingType type, const std::string& value);