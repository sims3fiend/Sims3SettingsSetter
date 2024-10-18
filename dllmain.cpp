#include "hooks.h"
#include "renderer.h"
#include "gui.h"
#include "utils.h"
#include <Windows.h>
#include <Psapi.h>
#include <string>
#include <filesystem>

HANDLE g_ThreadHandle = NULL;
DWORD g_ThreadId = 0;

bool IsCorrectProcess() {
    char processName[MAX_PATH];
    GetModuleBaseNameA(GetCurrentProcess(), NULL, processName, sizeof(processName));
    return (_stricmp(processName, "TS3W.exe") == 0);
}

void SetCorrectWorkingDirectory() {
    char exePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) != 0) {
        std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
        SetCurrentDirectoryA(exeDir.string().c_str());
    }
}

DWORD WINAPI HookThread(LPVOID lpParameter) {
    try {
        SetCorrectWorkingDirectory();
        InitializeLogging();
        Log("Hook thread started");

        if (!IsCorrectProcess()) {
            Log("Not the correct process (TS3W.exe). Exiting hook thread.");
            return 1;
        }

        Log("Current working directory: " + std::filesystem::current_path().string());

        if (!InitializeHooks()) {
            Log("Failed to initialize hooks");
            return 1;
        }

        Log("Hooks initialized successfully");

        MSG msg = {};
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        Log("Hook thread exiting");
        return 0;
    }
    catch (const std::exception& e) {
        Log("Exception in HookThread: " + std::string(e.what()));
        return 1;
    }
    catch (...) {
        Log("Unknown exception in HookThread");
        return 1;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_ThreadHandle = CreateThread(NULL, 0, HookThread, NULL, 0, &g_ThreadId);
        if (g_ThreadHandle == NULL) {
            char errorMsg[256];
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errorMsg, 255, NULL);
            OutputDebugStringA(("Failed to create hook thread: " + std::string(errorMsg)).c_str());
        }
        break;

    case DLL_PROCESS_DETACH:
        if (g_ThreadHandle) {
            if (g_ThreadId != 0) {
                PostThreadMessage(g_ThreadId, WM_QUIT, 0, 0);
            }

            DWORD waitResult = WaitForSingleObject(g_ThreadHandle, 5000);
            switch (waitResult) {
            case WAIT_OBJECT_0:
                Log("Hook thread exited successfully");
                break;
            case WAIT_TIMEOUT:
                Log("WARNING: Hook thread did not exit in time, forcefully terminating");
                TerminateThread(g_ThreadHandle, 1);
                break;
            default:
                Log("Error waiting for hook thread to exit: " + std::to_string(GetLastError()));
                break;
            }

            CloseHandle(g_ThreadHandle);
            g_ThreadHandle = NULL;
            g_ThreadId = 0;
        }

        try {
            CleanupGUI();
            CleanupHooks();
            CleanupLogging();
        }
        catch (const std::exception& e) {
            Log("Exception during cleanup: " + std::string(e.what()));
        }
        catch (...) {
            Log("Unknown exception during cleanup");
        }

        Log("DLL_PROCESS_DETACH completed");
        break;
    }
    return TRUE;
}