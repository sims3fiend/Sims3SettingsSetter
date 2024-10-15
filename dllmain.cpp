#include "hooks.h"
#include "renderer.h"
#include "gui.h"
#include "utils.h"
#include <Windows.h>

HANDLE g_ThreadHandle = NULL;

#include <eh.h>


DWORD WINAPI HookThread(LPVOID lpParameter) {
    try {
        InitializeLogging();
        Log("Hook thread started");

        if (!InitializeHooks()) {
            Log("Failed to initialize hooks");
            return 1;
        }

        Log("Hooks initialized successfully");

        // Message loop to keep the thread alive
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
        g_ThreadHandle = CreateThread(NULL, 0, HookThread, NULL, 0, NULL);
        if (g_ThreadHandle == NULL) {
            // Log error if thread creation fails
            char errorMsg[256];
            FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errorMsg, 255, NULL);
            Log("Failed to create hook thread: " + std::string(errorMsg));
        }
        break;
    case DLL_PROCESS_DETACH:
        Log("DLL_PROCESS_DETACH started");
        if (g_ThreadHandle) {
            // Signal the thread to exit
            PostThreadMessage(GetThreadId(g_ThreadHandle), WM_QUIT, 0, 0);

            // Wait for the thread to exit with a timeout
            DWORD waitResult = WaitForSingleObject(g_ThreadHandle, 5000);
            if (waitResult == WAIT_TIMEOUT) {
                Log("WARNING: Hook thread did not exit in time, forcefully terminating");
                TerminateThread(g_ThreadHandle, 1);
            }
            else if (waitResult == WAIT_OBJECT_0) {
                Log("Hook thread exited successfully");
            }
            else {
                Log("Error waiting for hook thread to exit");
            }

            CloseHandle(g_ThreadHandle);
            g_ThreadHandle = NULL;
        }

        // Ensure all cleanup functions are called
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