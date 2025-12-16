#include "d3d9_hook_registry.h"
#include "logger.h"
#include <detours/detours.h>
#include <algorithm>
#include <mutex>


//ABSOLUTELY DO NOT LOOK AT THIS AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA, WIP

namespace D3D9Hooks {

    // Thread-safe registry storage
    namespace {
        std::mutex g_registryMutex;
        bool g_initialized = false;
        LPDIRECT3DDEVICE9 g_device = nullptr;

        // Hook storage with priority ordering
        template<typename THook>
        struct HookEntry {
            std::string name;
            THook hook;
            Priority priority;

            bool operator<(const HookEntry& other) const {
                return static_cast<int>(priority) < static_cast<int>(other.priority);
            }
        };

        // Registry for each hook type
        std::vector<HookEntry<DrawIndexedPrimitiveHook>> g_drawIndexedPrimitiveHooks;
        std::vector<HookEntry<DrawPrimitiveHook>> g_drawPrimitiveHooks;
        std::vector<HookEntry<SetRenderTargetHook>> g_setRenderTargetHooks;
        std::vector<HookEntry<SetPixelShaderHook>> g_setPixelShaderHooks;
        std::vector<HookEntry<SetVertexShaderHook>> g_setVertexShaderHooks;
        std::vector<HookEntry<SetTextureHook>> g_setTextureHooks;
        std::vector<HookEntry<PresentHook>> g_presentHooks;
        std::vector<HookEntry<BeginSceneHook>> g_beginSceneHooks;
        std::vector<HookEntry<CreateTextureHook>> g_createTextureHooks;
        std::vector<HookEntry<CreateRenderTargetHook>> g_createRenderTargetHooks;
        std::vector<HookEntry<SetViewportHook>> g_setViewportHooks;
        std::vector<HookEntry<CreatePixelShaderHook>> g_createPixelShaderHooks;
        std::vector<HookEntry<CreateVertexShaderHook>> g_createVertexShaderHooks;
        std::vector<HookEntry<SetPixelShaderConstantFHook>> g_setPixelShaderConstantFHooks;
        std::vector<HookEntry<SetVertexShaderConstantFHook>> g_setVertexShaderConstantFHooks;

        // Original function pointers (for Tier 1 - DetourHelper access)
        typedef HRESULT(__stdcall* DrawIndexedPrimitive_t)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
        typedef HRESULT(__stdcall* DrawPrimitive_t)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, UINT, UINT);
        typedef HRESULT(__stdcall* SetRenderTarget_t)(LPDIRECT3DDEVICE9, DWORD, IDirect3DSurface9*);
        typedef HRESULT(__stdcall* SetPixelShader_t)(LPDIRECT3DDEVICE9, IDirect3DPixelShader9*);
        typedef HRESULT(__stdcall* SetVertexShader_t)(LPDIRECT3DDEVICE9, IDirect3DVertexShader9*);
        typedef HRESULT(__stdcall* SetTexture_t)(LPDIRECT3DDEVICE9, DWORD, IDirect3DBaseTexture9*);
        typedef HRESULT(__stdcall* Present_t)(LPDIRECT3DDEVICE9, const RECT*, const RECT*, HWND, const RGNDATA*);
        typedef HRESULT(__stdcall* BeginScene_t)(LPDIRECT3DDEVICE9);
        typedef HRESULT(__stdcall* CreateTexture_t)(LPDIRECT3DDEVICE9, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
        typedef HRESULT(__stdcall* CreateRenderTarget_t)(LPDIRECT3DDEVICE9, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
        typedef HRESULT(__stdcall* SetViewport_t)(LPDIRECT3DDEVICE9, const D3DVIEWPORT9*);
        typedef HRESULT(__stdcall* CreatePixelShader_t)(LPDIRECT3DDEVICE9, const DWORD*, IDirect3DPixelShader9**);
        typedef HRESULT(__stdcall* CreateVertexShader_t)(LPDIRECT3DDEVICE9, const DWORD*, IDirect3DVertexShader9**);
        typedef HRESULT(__stdcall* SetPixelShaderConstantF_t)(LPDIRECT3DDEVICE9, UINT, const float*, UINT);
        typedef HRESULT(__stdcall* SetVertexShaderConstantF_t)(LPDIRECT3DDEVICE9, UINT, const float*, UINT);

        DrawIndexedPrimitive_t Original_DrawIndexedPrimitive = nullptr;
        DrawPrimitive_t Original_DrawPrimitive = nullptr;
        SetRenderTarget_t Original_SetRenderTarget = nullptr;
        SetPixelShader_t Original_SetPixelShader = nullptr;
        SetVertexShader_t Original_SetVertexShader = nullptr;
        SetTexture_t Original_SetTexture = nullptr;
        Present_t Original_Present = nullptr;
        BeginScene_t Original_BeginScene = nullptr;
        CreateTexture_t Original_CreateTexture = nullptr;
        CreateRenderTarget_t Original_CreateRenderTarget = nullptr;
        SetViewport_t Original_SetViewport = nullptr;
        CreatePixelShader_t Original_CreatePixelShader = nullptr;
        CreateVertexShader_t Original_CreateVertexShader = nullptr;
        SetPixelShaderConstantF_t Original_SetPixelShaderConstantF = nullptr;
        SetVertexShaderConstantF_t Original_SetVertexShaderConstantF = nullptr;

        // Helper to sort hooks by priority
        template<typename THook>
        void SortHooks(std::vector<HookEntry<THook>>& hooks) {
            std::sort(hooks.begin(), hooks.end());
        }
    }

    // Hooked Functions
    HRESULT __stdcall Hooked_DrawIndexedPrimitive(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE type,
        INT baseVertexIndex, UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primCount) {

        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteDrawIndexedPrimitiveHooks(ctx, type, baseVertexIndex, minVertexIndex, numVertices, startIndex, primCount)) {
            return ctx.overrideResult;
        }

        return Original_DrawIndexedPrimitive(device, type, baseVertexIndex, minVertexIndex, numVertices, startIndex, primCount);
    }

    HRESULT __stdcall Hooked_DrawPrimitive(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE primitiveType,
        UINT startVertex, UINT primitiveCount) {

        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteDrawPrimitiveHooks(ctx, primitiveType, startVertex, primitiveCount)) {
            return ctx.overrideResult;
        }

        return Original_DrawPrimitive(device, primitiveType, startVertex, primitiveCount);
    }

    HRESULT __stdcall Hooked_SetRenderTarget(LPDIRECT3DDEVICE9 device, DWORD renderTargetIndex,
        IDirect3DSurface9* renderTarget) {

        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetRenderTargetHooks(ctx, renderTargetIndex, renderTarget)) {
            return ctx.overrideResult;
        }

        return Original_SetRenderTarget(device, renderTargetIndex, renderTarget);
    }

    HRESULT __stdcall Hooked_SetPixelShader(LPDIRECT3DDEVICE9 device, IDirect3DPixelShader9* shader) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetPixelShaderHooks(ctx, shader)) {
            return ctx.overrideResult;
        }

        return Original_SetPixelShader(device, shader);
    }

    HRESULT __stdcall Hooked_SetVertexShader(LPDIRECT3DDEVICE9 device, IDirect3DVertexShader9* shader) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetVertexShaderHooks(ctx, shader)) {
            return ctx.overrideResult;
        }

        return Original_SetVertexShader(device, shader);
    }

    HRESULT __stdcall Hooked_SetTexture(LPDIRECT3DDEVICE9 device, DWORD stage, IDirect3DBaseTexture9* texture) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetTextureHooks(ctx, stage, texture)) {
            return ctx.overrideResult;
        }

        return Original_SetTexture(device, stage, texture);
    }

    HRESULT __stdcall Hooked_Present(LPDIRECT3DDEVICE9 device, const RECT* sourceRect,
        const RECT* destRect, HWND destWindowOverride, const RGNDATA* dirtyRegion) {

        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecutePresentHooks(ctx, sourceRect, destRect, destWindowOverride, dirtyRegion)) {
            return ctx.overrideResult;
        }

        return Original_Present(device, sourceRect, destRect, destWindowOverride, dirtyRegion);
    }

    HRESULT __stdcall Hooked_BeginScene(LPDIRECT3DDEVICE9 device) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteBeginSceneHooks(ctx)) {
            return ctx.overrideResult;
        }

        return Original_BeginScene(device);
    }

    HRESULT __stdcall Hooked_CreateTexture(LPDIRECT3DDEVICE9 device, UINT width, UINT height,
        UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DTexture9** texture, HANDLE* sharedHandle) {

        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteCreateTextureHooks(ctx, width, height, levels, usage, format, pool, texture, sharedHandle)) {
            return ctx.overrideResult;
        }

        return Original_CreateTexture(device, width, height, levels, usage, format, pool, texture, sharedHandle);
    }

    HRESULT __stdcall Hooked_CreateRenderTarget(LPDIRECT3DDEVICE9 device, UINT width, UINT height,
        D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality,
        BOOL lockable, IDirect3DSurface9** surface, HANDLE* sharedHandle) {

        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteCreateRenderTargetHooks(ctx, width, height, format, multiSample, multisampleQuality, lockable, surface, sharedHandle)) {
            return ctx.overrideResult;
        }

        return Original_CreateRenderTarget(device, width, height, format, multiSample, multisampleQuality, lockable, surface, sharedHandle);
    }

    HRESULT __stdcall Hooked_SetViewport(LPDIRECT3DDEVICE9 device, const D3DVIEWPORT9* viewport) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetViewportHooks(ctx, viewport)) {
            return ctx.overrideResult;
        }

        return Original_SetViewport(device, viewport);
    }

    HRESULT __stdcall Hooked_CreatePixelShader(LPDIRECT3DDEVICE9 device, const DWORD* pFunction, IDirect3DPixelShader9** ppShader) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteCreatePixelShaderHooks(ctx, pFunction, ppShader)) {
            return ctx.overrideResult;
        }

        return Original_CreatePixelShader(device, pFunction, ppShader);
    }

    HRESULT __stdcall Hooked_CreateVertexShader(LPDIRECT3DDEVICE9 device, const DWORD* pFunction, IDirect3DVertexShader9** ppShader) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteCreateVertexShaderHooks(ctx, pFunction, ppShader)) {
            return ctx.overrideResult;
        }

        return Original_CreateVertexShader(device, pFunction, ppShader);
    }

    HRESULT __stdcall Hooked_SetPixelShaderConstantF(LPDIRECT3DDEVICE9 device, UINT startRegister, const float* pConstantData, UINT vector4fCount) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetPixelShaderConstantFHooks(ctx, startRegister, pConstantData, vector4fCount)) {
            return ctx.overrideResult;
        }

        return Original_SetPixelShaderConstantF(device, startRegister, pConstantData, vector4fCount);
    }

    HRESULT __stdcall Hooked_SetVertexShaderConstantF(LPDIRECT3DDEVICE9 device, UINT startRegister, const float* pConstantData, UINT vector4fCount) {
        DeviceContext ctx{ device, false, S_OK };

        if (!Internal::ExecuteSetVertexShaderConstantFHooks(ctx, startRegister, pConstantData, vector4fCount)) {
            return ctx.overrideResult;
        }

        return Original_SetVertexShaderConstantF(device, startRegister, pConstantData, vector4fCount);
    }

    // Registration Functions
    bool RegisterDrawIndexedPrimitive(const std::string& name, DrawIndexedPrimitiveHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_drawIndexedPrimitiveHooks.push_back({ name, hook, priority });
        SortHooks(g_drawIndexedPrimitiveHooks);
        LOG_DEBUG("[D3D9Hooks] Registered DrawIndexedPrimitive hook: " + name);
        return true;
    }

    bool RegisterDrawPrimitive(const std::string& name, DrawPrimitiveHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_drawPrimitiveHooks.push_back({ name, hook, priority });
        SortHooks(g_drawPrimitiveHooks);
        LOG_DEBUG("[D3D9Hooks] Registered DrawPrimitive hook: " + name);
        return true;
    }

    bool RegisterSetRenderTarget(const std::string& name, SetRenderTargetHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setRenderTargetHooks.push_back({ name, hook, priority });
        SortHooks(g_setRenderTargetHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetRenderTarget hook: " + name);
        return true;
    }

    bool RegisterSetPixelShader(const std::string& name, SetPixelShaderHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setPixelShaderHooks.push_back({ name, hook, priority });
        SortHooks(g_setPixelShaderHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetPixelShader hook: " + name);
        return true;
    }

    bool RegisterSetVertexShader(const std::string& name, SetVertexShaderHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setVertexShaderHooks.push_back({ name, hook, priority });
        SortHooks(g_setVertexShaderHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetVertexShader hook: " + name);
        return true;
    }

    bool RegisterSetTexture(const std::string& name, SetTextureHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setTextureHooks.push_back({ name, hook, priority });
        SortHooks(g_setTextureHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetTexture hook: " + name);
        return true;
    }

    bool RegisterPresent(const std::string& name, PresentHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_presentHooks.push_back({ name, hook, priority });
        SortHooks(g_presentHooks);
        LOG_DEBUG("[D3D9Hooks] Registered Present hook: " + name);
        return true;
    }

    bool RegisterBeginScene(const std::string& name, BeginSceneHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_beginSceneHooks.push_back({ name, hook, priority });
        SortHooks(g_beginSceneHooks);
        LOG_DEBUG("[D3D9Hooks] Registered BeginScene hook: " + name);
        return true;
    }

    bool RegisterCreateTexture(const std::string& name, CreateTextureHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_createTextureHooks.push_back({ name, hook, priority });
        SortHooks(g_createTextureHooks);
        LOG_DEBUG("[D3D9Hooks] Registered CreateTexture hook: " + name);
        return true;
    }

    bool RegisterCreateRenderTarget(const std::string& name, CreateRenderTargetHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_createRenderTargetHooks.push_back({ name, hook, priority });
        SortHooks(g_createRenderTargetHooks);
        LOG_DEBUG("[D3D9Hooks] Registered CreateRenderTarget hook: " + name);
        return true;
    }

    bool RegisterSetViewport(const std::string& name, SetViewportHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setViewportHooks.push_back({ name, hook, priority });
        SortHooks(g_setViewportHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetViewport hook: " + name);
        return true;
    }

    bool RegisterCreatePixelShader(const std::string& name, CreatePixelShaderHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_createPixelShaderHooks.push_back({ name, hook, priority });
        SortHooks(g_createPixelShaderHooks);
        LOG_DEBUG("[D3D9Hooks] Registered CreatePixelShader hook: " + name);
        return true;
    }

    bool RegisterCreateVertexShader(const std::string& name, CreateVertexShaderHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_createVertexShaderHooks.push_back({ name, hook, priority });
        SortHooks(g_createVertexShaderHooks);
        LOG_DEBUG("[D3D9Hooks] Registered CreateVertexShader hook: " + name);
        return true;
    }

    bool RegisterSetPixelShaderConstantF(const std::string& name, SetPixelShaderConstantFHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setPixelShaderConstantFHooks.push_back({ name, hook, priority });
        SortHooks(g_setPixelShaderConstantFHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetPixelShaderConstantF hook: " + name);
        return true;
    }

    bool RegisterSetVertexShaderConstantF(const std::string& name, SetVertexShaderConstantFHook hook, Priority priority) {
        std::lock_guard<std::mutex> lock(g_registryMutex);
        g_setVertexShaderConstantFHooks.push_back({ name, hook, priority });
        SortHooks(g_setVertexShaderConstantFHooks);
        LOG_DEBUG("[D3D9Hooks] Registered SetVertexShaderConstantF hook: " + name);
        return true;
    }

    // Direct Original Function Calls
    HRESULT CallOriginalCreateRenderTarget(
        LPDIRECT3DDEVICE9 device,
        UINT width,
        UINT height,
        D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multiSample,
        DWORD multisampleQuality,
        BOOL lockable,
        IDirect3DSurface9** surface,
        HANDLE* sharedHandle
    ) {
        using namespace Internal;
        if (!Original_CreateRenderTarget) {
            LOG_ERROR("[D3D9Hooks] Original_CreateRenderTarget not initialized!");
            return E_FAIL;
        }
        return Original_CreateRenderTarget(device, width, height, format, multiSample, multisampleQuality, lockable, surface, sharedHandle);
    }

    HRESULT CallOriginalSetRenderTarget(
        LPDIRECT3DDEVICE9 device,
        DWORD renderTargetIndex,
        IDirect3DSurface9* renderTarget
    ) {
        using namespace Internal;
        if (!Original_SetRenderTarget) {
            LOG_ERROR("[D3D9Hooks] Original_SetRenderTarget not initialized!");
            return E_FAIL;
        }
        return Original_SetRenderTarget(device, renderTargetIndex, renderTarget);
    }

    HRESULT CallOriginalSetViewport(
        LPDIRECT3DDEVICE9 device,
        const D3DVIEWPORT9* viewport
    ) {
        using namespace Internal;
        if (!Original_SetViewport) {
            LOG_ERROR("[D3D9Hooks] Original_SetViewport not initialized!");
            return E_FAIL;
        }
        return Original_SetViewport(device, viewport);
    }

    void UnregisterAll(const std::string& name) {
        std::lock_guard<std::mutex> lock(g_registryMutex);

        auto removeByName = [&name](auto& container) {
            container.erase(
                std::remove_if(container.begin(), container.end(),
                    [&name](const auto& entry) { return entry.name == name; }),
                container.end()
            );
        };

        removeByName(g_drawIndexedPrimitiveHooks);
        removeByName(g_drawPrimitiveHooks);
        removeByName(g_setRenderTargetHooks);
        removeByName(g_setPixelShaderHooks);
        removeByName(g_setVertexShaderHooks);
        removeByName(g_setTextureHooks);
        removeByName(g_presentHooks);
        removeByName(g_beginSceneHooks);
        removeByName(g_createTextureHooks);
        removeByName(g_createRenderTargetHooks);
        removeByName(g_setViewportHooks);
        removeByName(g_createPixelShaderHooks);
        removeByName(g_createVertexShaderHooks);
        removeByName(g_setPixelShaderConstantFHooks);
        removeByName(g_setVertexShaderConstantFHooks);

        LOG_DEBUG("[D3D9Hooks] Unregistered all hooks for: " + name);
    }

    // Hook Executors
    namespace Internal {

        bool ExecuteDrawIndexedPrimitiveHooks(DeviceContext& ctx, D3DPRIMITIVETYPE type, INT baseVertexIndex,
                                              UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primCount) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_drawIndexedPrimitiveHooks) {
                HookResult result = entry.hook(ctx, type, baseVertexIndex, minVertexIndex, numVertices, startIndex, primCount);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteDrawPrimitiveHooks(DeviceContext& ctx, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_drawPrimitiveHooks) {
                HookResult result = entry.hook(ctx, primitiveType, startVertex, primitiveCount);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetRenderTargetHooks(DeviceContext& ctx, DWORD renderTargetIndex, IDirect3DSurface9* renderTarget) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setRenderTargetHooks) {
                HookResult result = entry.hook(ctx, renderTargetIndex, renderTarget);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetPixelShaderHooks(DeviceContext& ctx, IDirect3DPixelShader9* shader) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setPixelShaderHooks) {
                HookResult result = entry.hook(ctx, shader);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetVertexShaderHooks(DeviceContext& ctx, IDirect3DVertexShader9* shader) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setVertexShaderHooks) {
                HookResult result = entry.hook(ctx, shader);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetTextureHooks(DeviceContext& ctx, DWORD stage, IDirect3DBaseTexture9* texture) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setTextureHooks) {
                HookResult result = entry.hook(ctx, stage, texture);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecutePresentHooks(DeviceContext& ctx, const RECT* sourceRect, const RECT* destRect,
                                HWND destWindowOverride, const RGNDATA* dirtyRegion) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_presentHooks) {
                HookResult result = entry.hook(ctx, sourceRect, destRect, destWindowOverride, dirtyRegion);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteBeginSceneHooks(DeviceContext& ctx) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_beginSceneHooks) {
                HookResult result = entry.hook(ctx);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteCreateTextureHooks(DeviceContext& ctx, UINT width, UINT height, UINT levels, DWORD usage,
                                       D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** texture, HANDLE* sharedHandle) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_createTextureHooks) {
                HookResult result = entry.hook(ctx, width, height, levels, usage, format, pool, texture, sharedHandle);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteCreateRenderTargetHooks(DeviceContext& ctx, UINT width, UINT height, D3DFORMAT format,
                                            D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL lockable,
                                            IDirect3DSurface9** surface, HANDLE* sharedHandle) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_createRenderTargetHooks) {
                HookResult result = entry.hook(ctx, width, height, format, multiSample, multisampleQuality, lockable, surface, sharedHandle);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetViewportHooks(DeviceContext& ctx, const D3DVIEWPORT9* viewport) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setViewportHooks) {
                HookResult result = entry.hook(ctx, viewport);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteCreatePixelShaderHooks(DeviceContext& ctx, const DWORD* pFunction, IDirect3DPixelShader9** ppShader) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_createPixelShaderHooks) {
                HookResult result = entry.hook(ctx, pFunction, ppShader);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteCreateVertexShaderHooks(DeviceContext& ctx, const DWORD* pFunction, IDirect3DVertexShader9** ppShader) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_createVertexShaderHooks) {
                HookResult result = entry.hook(ctx, pFunction, ppShader);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetPixelShaderConstantFHooks(DeviceContext& ctx, UINT startRegister, const float* pConstantData, UINT vector4fCount) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setPixelShaderConstantFHooks) {
                HookResult result = entry.hook(ctx, startRegister, pConstantData, vector4fCount);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        bool ExecuteSetVertexShaderConstantFHooks(DeviceContext& ctx, UINT startRegister, const float* pConstantData, UINT vector4fCount) {
            std::lock_guard<std::mutex> lock(g_registryMutex);

            for (auto& entry : g_setVertexShaderConstantFHooks) {
                HookResult result = entry.hook(ctx, startRegister, pConstantData, vector4fCount);

                if (result == HookResult::Skip) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = S_OK;
                    return false;
                }
                else if (result == HookResult::Block) {
                    ctx.skipOriginal = true;
                    ctx.overrideResult = E_FAIL;
                    return false;
                }
            }

            return !ctx.skipOriginal;
        }

        void Initialize(LPDIRECT3DDEVICE9 device) {
            if (g_initialized) {
                LOG_WARNING("[D3D9Hooks] Already initialized");
                return;
            }

            g_device = device;

            // Get vtable pointers
            void** vTable = *reinterpret_cast<void***>(device);

            Original_DrawIndexedPrimitive = reinterpret_cast<DrawIndexedPrimitive_t>(vTable[82]);
            Original_DrawPrimitive = reinterpret_cast<DrawPrimitive_t>(vTable[81]);
            Original_SetRenderTarget = reinterpret_cast<SetRenderTarget_t>(vTable[37]);
            Original_SetPixelShader = reinterpret_cast<SetPixelShader_t>(vTable[107]);
            Original_SetVertexShader = reinterpret_cast<SetVertexShader_t>(vTable[92]);
            Original_SetTexture = reinterpret_cast<SetTexture_t>(vTable[65]);
            Original_Present = reinterpret_cast<Present_t>(vTable[17]);
            Original_BeginScene = reinterpret_cast<BeginScene_t>(vTable[41]);
            Original_CreateTexture = reinterpret_cast<CreateTexture_t>(vTable[23]);
            Original_CreateRenderTarget = reinterpret_cast<CreateRenderTarget_t>(vTable[28]);
            Original_SetViewport = reinterpret_cast<SetViewport_t>(vTable[47]);
            Original_CreatePixelShader = reinterpret_cast<CreatePixelShader_t>(vTable[106]);
            Original_CreateVertexShader = reinterpret_cast<CreateVertexShader_t>(vTable[91]);
            Original_SetPixelShaderConstantF = reinterpret_cast<SetPixelShaderConstantF_t>(vTable[109]);
            Original_SetVertexShaderConstantF = reinterpret_cast<SetVertexShaderConstantF_t>(vTable[94]);

            // Install hooks using Detours
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&)Original_DrawIndexedPrimitive, Hooked_DrawIndexedPrimitive);
            DetourAttach(&(PVOID&)Original_DrawPrimitive, Hooked_DrawPrimitive);
            DetourAttach(&(PVOID&)Original_SetRenderTarget, Hooked_SetRenderTarget);
            DetourAttach(&(PVOID&)Original_SetPixelShader, Hooked_SetPixelShader);
            DetourAttach(&(PVOID&)Original_SetVertexShader, Hooked_SetVertexShader);
            DetourAttach(&(PVOID&)Original_SetTexture, Hooked_SetTexture);
            DetourAttach(&(PVOID&)Original_Present, Hooked_Present);
            DetourAttach(&(PVOID&)Original_BeginScene, Hooked_BeginScene);
            DetourAttach(&(PVOID&)Original_CreateTexture, Hooked_CreateTexture);
            DetourAttach(&(PVOID&)Original_CreateRenderTarget, Hooked_CreateRenderTarget);
            DetourAttach(&(PVOID&)Original_SetViewport, Hooked_SetViewport);
            DetourAttach(&(PVOID&)Original_CreatePixelShader, Hooked_CreatePixelShader);
            DetourAttach(&(PVOID&)Original_CreateVertexShader, Hooked_CreateVertexShader);
            DetourAttach(&(PVOID&)Original_SetPixelShaderConstantF, Hooked_SetPixelShaderConstantF);
            DetourAttach(&(PVOID&)Original_SetVertexShaderConstantF, Hooked_SetVertexShaderConstantF);

            if (DetourTransactionCommit() == NO_ERROR) {
                g_initialized = true;
                LOG_INFO("[D3D9Hooks] Hook registry initialized successfully");
            } else {
                LOG_ERROR("[D3D9Hooks] Failed to initialize hook registry");
            }
        }

        void Cleanup() {
            if (!g_initialized) return;

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            if (Original_DrawIndexedPrimitive) DetourDetach(&(PVOID&)Original_DrawIndexedPrimitive, Hooked_DrawIndexedPrimitive);
            if (Original_DrawPrimitive) DetourDetach(&(PVOID&)Original_DrawPrimitive, Hooked_DrawPrimitive);
            if (Original_SetRenderTarget) DetourDetach(&(PVOID&)Original_SetRenderTarget, Hooked_SetRenderTarget);
            if (Original_SetPixelShader) DetourDetach(&(PVOID&)Original_SetPixelShader, Hooked_SetPixelShader);
            if (Original_SetVertexShader) DetourDetach(&(PVOID&)Original_SetVertexShader, Hooked_SetVertexShader);
            if (Original_SetTexture) DetourDetach(&(PVOID&)Original_SetTexture, Hooked_SetTexture);
            if (Original_Present) DetourDetach(&(PVOID&)Original_Present, Hooked_Present);
            if (Original_BeginScene) DetourDetach(&(PVOID&)Original_BeginScene, Hooked_BeginScene);
            if (Original_CreateTexture) DetourDetach(&(PVOID&)Original_CreateTexture, Hooked_CreateTexture);
            if (Original_CreateRenderTarget) DetourDetach(&(PVOID&)Original_CreateRenderTarget, Hooked_CreateRenderTarget);
            if (Original_SetViewport) DetourDetach(&(PVOID&)Original_SetViewport, Hooked_SetViewport);
            if (Original_CreatePixelShader) DetourDetach(&(PVOID&)Original_CreatePixelShader, Hooked_CreatePixelShader);
            if (Original_CreateVertexShader) DetourDetach(&(PVOID&)Original_CreateVertexShader, Hooked_CreateVertexShader);
            if (Original_SetPixelShaderConstantF) DetourDetach(&(PVOID&)Original_SetPixelShaderConstantF, Hooked_SetPixelShaderConstantF);
            if (Original_SetVertexShaderConstantF) DetourDetach(&(PVOID&)Original_SetVertexShaderConstantF, Hooked_SetVertexShaderConstantF);

            DetourTransactionCommit();

            g_initialized = false;
            LOG_INFO("[D3D9Hooks] Hook registry cleaned up");
        }

        bool IsInitialized() {
            return g_initialized;
        }

    } 

} 