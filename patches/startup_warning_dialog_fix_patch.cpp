#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class StartupWarningDialogFix : public OptimizationPatch {
  private:
    static inline const AddressInfo getTopWindowCallAddressInfo = {.name = "StartupWarningDialogFix::getTopWindowCall",
        .addresses =
            {
                {GameVersion::Retail, 0x00eccb61},
                {GameVersion::Steam, 0x00ecc171},
                {GameVersion::EA, 0x00ecc161},
            },
        .pattern = "FF 15 ?? ?? ?? ?? 50 FF 15 ?? ?? ?? ?? 8B F0 56 FF 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 80 B8 A8 02 00 00 00",
        .patternOffset = 7};

    static constexpr uintptr_t offsetOfMessageBoxWCall = 34;
    static constexpr uintptr_t offsetOfPushDialogProcedureAddress = 81;

    static inline HWND(__stdcall* hijackedGetTopWindow)(HWND window) = nullptr;

    static inline INT_PTR(__stdcall* originalDialogProcedure)(HWND window, uint32_t message, WPARAM wParam, LPARAM lParam) = nullptr;

    static inline int(__stdcall* originalMessageBoxW)(HWND window, const WCHAR* text, const WCHAR* caption, uint32_t flags) = nullptr;
    static inline int(__stdcall* hookedMessageBoxW)(HWND window, const WCHAR* text, const WCHAR* caption, uint32_t flags) = nullptr;

    std::vector<PatchHelper::PatchLocation> patchedLocations;

    static HWND __stdcall HijackedGetTopWindow(HWND ignoredDesktopWindow) {
        HWND parentWindowForDialog = nullptr;

        // Ideally, we'd have hooked the creation of the game's window,
        // or we'd know the address of the window's HWND, but the window is created
        // via a bewildering morass of dynamic-dispatch, and I don't want to puzzle it out.
        // And the game isn't far enough along at this point for `g_hookedWindow` to be set,
        // so instead we'll just search for a window with a class-name of "Canvas" that's associated with this thread.
        // This'll work unless someone is trying to break it.
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM context) -> BOOL {
                WCHAR c[8];
                if (GetClassNameW(window, c, 8) == 6) {
                    if (c[0] == 'C' && c[1] == 'a' && c[2] == 'n' && c[3] == 'v' && c[4] == 'a' && c[5] == 's') {
                        *reinterpret_cast<HWND*>(context) = window;
                        return false;
                    }
                }
                return true;
            },
            reinterpret_cast<LPARAM>(&parentWindowForDialog));

        return parentWindowForDialog;
    }

    static INT_PTR __stdcall HookedDialogProcedure(HWND window, uint32_t message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_SHOWWINDOW) {
            // Make sure the user can actually see the window by making it topmost.
            SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetForegroundWindow(window);
        }
        return originalDialogProcedure(window, message, wParam, lParam);
    }

    static int __stdcall HookedMessageBoxW(HWND window, const WCHAR* text, const WCHAR* caption, uint32_t flags) {
        // Again, make sure the user can actually see the window by making it topmost.
        return originalMessageBoxW(window, text, caption, flags | MB_TOPMOST);
    }

  public:
    StartupWarningDialogFix() : OptimizationPatch("StartupWarningDialogFix", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[StartupWarningDialogFix] Installing...");

        auto getTopWindowCallAddress = getTopWindowCallAddressInfo.Resolve();
        if (!getTopWindowCallAddress) { return Fail("Could not resolve getTopWindowCall address"); }
        uintptr_t getTopWindowCall = *getTopWindowCallAddress;
        const uint8_t* c = reinterpret_cast<const uint8_t*>(getTopWindowCall);

        // As this patch is enabled by default, we'll be careful and ensure the patch-sites are as expected.
        if (c[0] != 0xFF || c[1] != 0025 || c[offsetOfMessageBoxWCall + 0] != 0xFF || c[offsetOfMessageBoxWCall + 1] != 0025 || c[offsetOfPushDialogProcedureAddress] != 0x68) {
            return Fail("Failed to install as the game's code is not as expected");
        }

        hijackedGetTopWindow = &HijackedGetTopWindow;
        hookedMessageBoxW = &HookedMessageBoxW;

        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();
        uint32_t* address;

        std::memcpy(&address, reinterpret_cast<const uint8_t*>(getTopWindowCall + offsetOfMessageBoxWCall + 2), 4);
        originalMessageBoxW = reinterpret_cast<decltype(originalMessageBoxW)>(*address);

        std::memcpy(&address, reinterpret_cast<const uint8_t*>(getTopWindowCall + offsetOfPushDialogProcedureAddress + 1), 4);
        originalDialogProcedure = reinterpret_cast<decltype(originalDialogProcedure)>(address);

        successful &= PatchHelper::WriteDWORD(getTopWindowCall + 2, reinterpret_cast<uintptr_t>(&hijackedGetTopWindow), &tx.locations);
        successful &= PatchHelper::WriteDWORD(getTopWindowCall + offsetOfMessageBoxWCall + 2, reinterpret_cast<uintptr_t>(&hookedMessageBoxW), &tx.locations);
        successful &= PatchHelper::WriteDWORD(getTopWindowCall + offsetOfPushDialogProcedureAddress + 1, reinterpret_cast<uintptr_t>(&HookedDialogProcedure), &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

        isEnabled = true;
        LOG_INFO("[StartupWarningDialogFix] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[StartupWarningDialogFix] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            isEnabled = true;
            return Fail("Failed to restore original code");
        }

        isEnabled = false;
        LOG_INFO("[StartupWarningDialogFix] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(StartupWarningDialogFix, {.displayName = "Startup Warning Dialog Fix",
                                            .description = "Fixes a dialog that either fails to be created or fails to appear.",
                                            .category = "Bug Fix",
                                            .experimental = false,
                                            .enabledByDefault = true,
                                            .supportedVersions = VERSION_ALL,
                                            .technicalDetails = {
                                                "This patch was authored by \"Just Harry\".",
                                                "On some occasions the game will attempt to show a dialog during startup, usually to alert the presence of \"unofficial game modifications\".\n"
                                                "This dialog is created with a parent window, and the logic used to select the window to be the parent often chooses a window that the game lacks permission to use, causing "
                                                "the creation of the dialog to fail.\n"
                                                "And then even if the dialog is successfully created it may not be visible, or it may be initially hidden behind the game's window.",
                                                "This patch overrides the logic for selecting the parent window such that either the game's window is the parent, or there is no parent.",
                                                "Additionally, when the dialog is shown it is made topmost, to ensure that it is visible.",
                                            }})
