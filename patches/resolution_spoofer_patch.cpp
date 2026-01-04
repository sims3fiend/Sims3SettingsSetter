#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../qol.h"
#include <d3d9.h>
#include <vector>

// IDirect3D9 vtable indices:
// Index 6 (0x18): GetAdapterModeCount
// Index 7 (0x1C): EnumAdapterModes
// Index 8 (0x20): GetAdapterDisplayMode
// other idk

class ResolutionSpooferPatch : public OptimizationPatch {
private:
    // Function typedefs for IDirect3D9 methods
    typedef UINT(STDMETHODCALLTYPE* GetAdapterModeCount_t)(IDirect3D9*, UINT Adapter, D3DFORMAT Format);
    typedef HRESULT(STDMETHODCALLTYPE* EnumAdapterModes_t)(IDirect3D9*, UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode);
    typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

    static GetAdapterModeCount_t originalGetAdapterModeCount;
    static EnumAdapterModes_t originalEnumAdapterModes;
    static CreateDevice_t originalCreateDevice;

    // Synthetic resolutions to inject
    struct SyntheticRes {
        UINT width;
        UINT height;
        UINT refreshRate;
    };

    static std::vector<SyntheticRes> syntheticResolutions;

    // Per-format cache to handle multiple format queries correctly
    static UINT cachedRealModeCount_X8R8G8B8;
    static UINT cachedRealModeCount_R5G6B5;

    static void BuildSyntheticResolutions() {
        syntheticResolutions.clear();

        syntheticResolutions.push_back({2560, 1440, 60});   // 1440p
        syntheticResolutions.push_back({3840, 2160, 60});   // 4K UHD
        syntheticResolutions.push_back({4096, 2160, 60});   // 4K DCI
        syntheticResolutions.push_back({5120, 2880, 60});   // 5K
        syntheticResolutions.push_back({6144, 3456, 60});   // 6K
        //syntheticResolutions.push_back({7680, 4320, 60});   // 8K UHD // crashes for me ;_;, only when moving the camera fast tho idk
    }

    static UINT STDMETHODCALLTYPE HookedGetAdapterModeCount(IDirect3D9* pThis, UINT Adapter, D3DFORMAT Format) {
        UINT realCount = originalGetAdapterModeCount(pThis, Adapter, Format);

        // Only inject for common display formats
        if (Format == D3DFMT_X8R8G8B8) {
            cachedRealModeCount_X8R8G8B8 = realCount;
            return realCount + static_cast<UINT>(syntheticResolutions.size());
        }
        else if (Format == D3DFMT_R5G6B5) {
            cachedRealModeCount_R5G6B5 = realCount;
            return realCount + static_cast<UINT>(syntheticResolutions.size());
        }

        return realCount;
    }

    static HRESULT STDMETHODCALLTYPE HookedEnumAdapterModes(IDirect3D9* pThis, UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) {
        if (!pMode) return D3DERR_INVALIDCALL;

        // Get the correct cached count for this format
        UINT cachedRealModeCount = 0;
        if (Format == D3DFMT_X8R8G8B8) {
            cachedRealModeCount = cachedRealModeCount_X8R8G8B8;
        }
        else if (Format == D3DFMT_R5G6B5) {
            cachedRealModeCount = cachedRealModeCount_R5G6B5;
        }
        else {
            return originalEnumAdapterModes(pThis, Adapter, Format, Mode, pMode);
        }

        // Check if this is a synthetic mode index
        if (Mode >= cachedRealModeCount) {
            UINT syntheticIndex = Mode - cachedRealModeCount;
            if (syntheticIndex < syntheticResolutions.size()) {
                const auto& res = syntheticResolutions[syntheticIndex];
                pMode->Width = res.width;
                pMode->Height = res.height;
                pMode->RefreshRate = res.refreshRate;
                pMode->Format = Format;
                return D3D_OK;
            }
            return D3DERR_INVALIDCALL;
        }

        return originalEnumAdapterModes(pThis, Adapter, Format, Mode, pMode);
    }

    static HRESULT STDMETHODCALLTYPE HookedCreateDevice(IDirect3D9* pThis, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) {
        // Capture the window handle (prefer hDeviceWindow if set)
        HWND hTargetWindow = hFocusWindow;
        if (pPresentationParameters && pPresentationParameters->hDeviceWindow) {
            hTargetWindow = pPresentationParameters->hDeviceWindow;
        }

        // Enforce windowed mode if Borderless Window is active
        if (pPresentationParameters && BorderlessWindow::Get().IsEnabled()) {
            if (!pPresentationParameters->Windowed) {
                LOG_INFO("[ResolutionSpoofer] Enforcing Windowed Mode in CreateDevice (Borderless Enabled)");
                pPresentationParameters->Windowed = TRUE;
                pPresentationParameters->FullScreen_RefreshRateInHz = 0;

                // Enforce DISCARD swap effect to allow backbuffer scaling
                pPresentationParameters->SwapEffect = D3DSWAPEFFECT_DISCARD;

                if (pPresentationParameters->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) {
                    pPresentationParameters->Flags &= ~D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
                }
            }
        }
        
        HRESULT hr = originalCreateDevice(pThis, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

        // If successful and borderless is enabled, force the window update immediately
        // This defeats the game's attempt to resize the window to the backbuffer size (e.g 4K)a nd ensures it stays at monitor size (e.g 1080p) for downsampling.
        if (SUCCEEDED(hr) && BorderlessWindow::Get().IsEnabled() && hTargetWindow) {
            LOG_INFO("[ResolutionSpoofer] Force-applying borderless window state after CreateDevice");
            BorderlessWindow::Get().SetWindowHandle(hTargetWindow);
            BorderlessWindow::Get().Apply(); 
        }

        return hr;
    }

public:
    ResolutionSpooferPatch() : OptimizationPatch("ResolutionSpoofer", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[ResolutionSpoofer] Installing...");

        // Create a temporary D3D9 interface to get vtable
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) {
            return Fail("Failed to create D3D9 interface");
        }

        // Get vtable pointer
        void** vtable = *reinterpret_cast<void***>(pD3D);

        // Store original function pointers (vtable indices 6 and 7)
        originalGetAdapterModeCount = reinterpret_cast<GetAdapterModeCount_t>(vtable[6]);
        originalEnumAdapterModes = reinterpret_cast<EnumAdapterModes_t>(vtable[7]);
        originalCreateDevice = reinterpret_cast<CreateDevice_t>(vtable[16]);

        LOG_DEBUG("[ResolutionSpoofer] Original GetAdapterModeCount: 0x" +
                 std::to_string(reinterpret_cast<uintptr_t>(originalGetAdapterModeCount)));
        LOG_DEBUG("[ResolutionSpoofer] Original EnumAdapterModes: 0x" +
                 std::to_string(reinterpret_cast<uintptr_t>(originalEnumAdapterModes)));
        LOG_DEBUG("[ResolutionSpoofer] Original CreateDevice: 0x" +
                 std::to_string(reinterpret_cast<uintptr_t>(originalCreateDevice)));

        pD3D->Release();

        // Build synthetic resolution list
        BuildSyntheticResolutions();

        // Install hooks using Detours
        std::vector<DetourHelper::Hook> hooks = {
            {reinterpret_cast<void**>(&originalGetAdapterModeCount), reinterpret_cast<void*>(HookedGetAdapterModeCount)},
            {reinterpret_cast<void**>(&originalEnumAdapterModes), reinterpret_cast<void*>(HookedEnumAdapterModes)},
            {reinterpret_cast<void**>(&originalCreateDevice), reinterpret_cast<void*>(HookedCreateDevice)}
        };

        if (!DetourHelper::InstallHooks(hooks)) {
            return Fail("Failed to install D3D9 hooks");
        }

        isEnabled = true;
        LOG_INFO("[ResolutionSpoofer] Successfully installed - " +
                std::to_string(syntheticResolutions.size()) + " synthetic resolutions available");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[ResolutionSpoofer] Uninstalling...");

        std::vector<DetourHelper::Hook> hooks = {
            {reinterpret_cast<void**>(&originalGetAdapterModeCount), reinterpret_cast<void*>(HookedGetAdapterModeCount)},
            {reinterpret_cast<void**>(&originalEnumAdapterModes), reinterpret_cast<void*>(HookedEnumAdapterModes)},
            {reinterpret_cast<void**>(&originalCreateDevice), reinterpret_cast<void*>(HookedCreateDevice)}
        };

        if (!DetourHelper::RemoveHooks(hooks)) {
            return Fail("Failed to remove D3D9 hooks");
        }

        syntheticResolutions.clear();
        cachedRealModeCount_X8R8G8B8 = 0;
        cachedRealModeCount_R5G6B5 = 0;

        isEnabled = false;
        LOG_INFO("[ResolutionSpoofer] Successfully uninstalled");
        return true;
    }

};

// Static member initialization, lazy
ResolutionSpooferPatch::GetAdapterModeCount_t ResolutionSpooferPatch::originalGetAdapterModeCount = nullptr;
ResolutionSpooferPatch::EnumAdapterModes_t ResolutionSpooferPatch::originalEnumAdapterModes = nullptr;
ResolutionSpooferPatch::CreateDevice_t ResolutionSpooferPatch::originalCreateDevice = nullptr;
std::vector<ResolutionSpooferPatch::SyntheticRes> ResolutionSpooferPatch::syntheticResolutions;
UINT ResolutionSpooferPatch::cachedRealModeCount_X8R8G8B8 = 0;
UINT ResolutionSpooferPatch::cachedRealModeCount_R5G6B5 = 0;

REGISTER_PATCH(ResolutionSpooferPatch, {
    .displayName = "Resolution Spoofer",
    .description = "Injects spoofed high resolutions for downsampling goodness",
    .category = "Graphics",
    .experimental = true,
    .supportedVersions = allGameVersionsMask, //add something about like, restart the game after setting, etc., you'll need the UI patch, etc.
    .technicalDetails = { 
        "Tricks the game into thinking other larger resolutions are available",
        "Great for AA/carity purposes, you will need a resolution patch for the UI as it gets very tiny (check readme)",
        "If people ask I will add a custom setting for adding your own resolutions"
    }
})
