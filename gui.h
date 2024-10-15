#pragma once
#include <d3d9.h>
#include "renderer.h"
#include <unordered_map>
#include <vector>
#include <string>
#include "utils.h"

bool IsGUIInitialized();
void InitializeGUI(LPDIRECT3DDEVICE9 pDevice);
void RenderGUI(LPDIRECT3DDEVICE9 pDevice);
void CleanupGUI();
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern std::unordered_map<std::string, std::vector<bool>> functionSettingsAvailability;
const std::vector<std::pair<std::string, const std::unordered_map<std::string, SettingInfo>*>> GetSettingsMapsForFunction(const std::string& funcName);
void DrawImGuiInterface();
std::variant<bool, std::string> ConvertToScriptSettingValue(const std::variant<bool, int, float, std::string>& value);