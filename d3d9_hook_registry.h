#pragma once
#include <d3d9.h>
#include <functional>
#include <string>
#include <map>
#include <vector>

// D3D9 Hook Registry System
namespace D3D9Hooks {

// Hook priorities control execution order (lower numbers run first)
enum class Priority {
    First = 0,   // Run before everything (monitoring, validation)
    Early = 25,  // Run early (parameter modification)
    Normal = 50, // Default priority
    Late = 75,   // Run after most hooks (post-processing)
    Last = 100   // Run last (logging, cleanup)
};

// Hook result controls execution flow
enum class HookResult {
    Continue, // Continue to next hook / original function
    Skip,     // Skip original call, return success (S_OK)
    Block     // Block call, return error (E_FAIL)
};

// Context passed to hooks with common device info
struct DeviceContext {
    LPDIRECT3DDEVICE9 device;
    bool skipOriginal = false;     // Set by hooks to skip original call
    HRESULT overrideResult = S_OK; // Return value if skipOriginal is true
};

// Hook Function Signatures
// DrawIndexedPrimitive - Intercept geometry rendering
using DrawIndexedPrimitiveHook = std::function<HookResult(DeviceContext& ctx, D3DPRIMITIVETYPE type, INT baseVertexIndex, UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primCount)>;

// DrawPrimitive - Intercept non-indexed geometry rendering
using DrawPrimitiveHook = std::function<HookResult(DeviceContext& ctx, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount)>;

// SetRenderTarget - Intercept render target changes
using SetRenderTargetHook = std::function<HookResult(DeviceContext& ctx, DWORD renderTargetIndex, IDirect3DSurface9* renderTarget)>;

// SetPixelShader - Intercept pixel shader changes
using SetPixelShaderHook = std::function<HookResult(DeviceContext& ctx, IDirect3DPixelShader9* shader)>;

// SetVertexShader - Intercept vertex shader changes
using SetVertexShaderHook = std::function<HookResult(DeviceContext& ctx, IDirect3DVertexShader9* shader)>;

// SetTexture - Intercept texture binding
using SetTextureHook = std::function<HookResult(DeviceContext& ctx, DWORD stage, IDirect3DBaseTexture9* texture)>;

// Present - Intercept frame presentation
using PresentHook = std::function<HookResult(DeviceContext& ctx, const RECT* sourceRect, const RECT* destRect, HWND destWindowOverride, const RGNDATA* dirtyRegion)>;

// BeginScene - Intercept scene begin
using BeginSceneHook = std::function<HookResult(DeviceContext& ctx)>;

// CreateTexture - Intercept texture creation
using CreateTextureHook = std::function<HookResult(DeviceContext& ctx, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** texture, HANDLE* sharedHandle)>;

// CreateRenderTarget - Intercept render target surface creation
using CreateRenderTargetHook =
    std::function<HookResult(DeviceContext& ctx, UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL lockable, IDirect3DSurface9** surface, HANDLE* sharedHandle)>;

// SetViewport - Intercept viewport changes
using SetViewportHook = std::function<HookResult(DeviceContext& ctx, const D3DVIEWPORT9* viewport)>;

// CreatePixelShader - Intercept pixel shader creation (for shader replacement/modding)
// pFunction points to the shader bytecode, ppShader receives the created shader
using CreatePixelShaderHook = std::function<HookResult(DeviceContext& ctx, const DWORD* pFunction, IDirect3DPixelShader9** ppShader)>;

// CreateVertexShader - Intercept vertex shader creation
using CreateVertexShaderHook = std::function<HookResult(DeviceContext& ctx, const DWORD* pFunction, IDirect3DVertexShader9** ppShader)>;

// SetPixelShaderConstantF - Intercept pixel shader float constant updates
using SetPixelShaderConstantFHook = std::function<HookResult(DeviceContext& ctx, UINT startRegister, const float* pConstantData, UINT vector4fCount)>;

// SetVertexShaderConstantF - Intercept vertex shader float constant updates
using SetVertexShaderConstantFHook = std::function<HookResult(DeviceContext& ctx, UINT startRegister, const float* pConstantData, UINT vector4fCount)>;

// Register hooks with name and priority
// Name should be unique per patch (use patch name as prefix)
bool RegisterDrawIndexedPrimitive(const std::string& name, DrawIndexedPrimitiveHook hook, Priority priority = Priority::Normal);
bool RegisterDrawPrimitive(const std::string& name, DrawPrimitiveHook hook, Priority priority = Priority::Normal);
bool RegisterSetRenderTarget(const std::string& name, SetRenderTargetHook hook, Priority priority = Priority::Normal);
bool RegisterSetPixelShader(const std::string& name, SetPixelShaderHook hook, Priority priority = Priority::Normal);
bool RegisterSetVertexShader(const std::string& name, SetVertexShaderHook hook, Priority priority = Priority::Normal);
bool RegisterSetTexture(const std::string& name, SetTextureHook hook, Priority priority = Priority::Normal);
bool RegisterPresent(const std::string& name, PresentHook hook, Priority priority = Priority::Normal);
bool RegisterBeginScene(const std::string& name, BeginSceneHook hook, Priority priority = Priority::Normal);
bool RegisterCreateTexture(const std::string& name, CreateTextureHook hook, Priority priority = Priority::Normal);
bool RegisterCreateRenderTarget(const std::string& name, CreateRenderTargetHook hook, Priority priority = Priority::Normal);
bool RegisterSetViewport(const std::string& name, SetViewportHook hook, Priority priority = Priority::Normal);
bool RegisterCreatePixelShader(const std::string& name, CreatePixelShaderHook hook, Priority priority = Priority::Normal);
bool RegisterCreateVertexShader(const std::string& name, CreateVertexShaderHook hook, Priority priority = Priority::Normal);
bool RegisterSetPixelShaderConstantF(const std::string& name, SetPixelShaderConstantFHook hook, Priority priority = Priority::Normal);
bool RegisterSetVertexShaderConstantF(const std::string& name, SetVertexShaderConstantFHook hook, Priority priority = Priority::Normal);

// Unregister all hooks registered with a specific name
void UnregisterAll(const std::string& name);

// Get original function pointers (bypasses all hooks)
// Use this when you need to create resources from within a hook to avoid recursion, don't ask what I was doing
HRESULT CallOriginalCreateRenderTarget(
    LPDIRECT3DDEVICE9 device, UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL lockable, IDirect3DSurface9** surface, HANDLE* sharedHandle);

HRESULT CallOriginalSetRenderTarget(LPDIRECT3DDEVICE9 device, DWORD renderTargetIndex, IDirect3DSurface9* renderTarget);

HRESULT CallOriginalSetViewport(LPDIRECT3DDEVICE9 device, const D3DVIEWPORT9* viewport);

// Internal Hook Executors (called by hooked D3D9 functions)
namespace Internal {
bool ExecuteDrawIndexedPrimitiveHooks(DeviceContext& ctx, D3DPRIMITIVETYPE type, INT baseVertexIndex, UINT minVertexIndex, UINT numVertices, UINT startIndex, UINT primCount);
bool ExecuteDrawPrimitiveHooks(DeviceContext& ctx, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount);
bool ExecuteSetRenderTargetHooks(DeviceContext& ctx, DWORD renderTargetIndex, IDirect3DSurface9* renderTarget);
bool ExecuteSetPixelShaderHooks(DeviceContext& ctx, IDirect3DPixelShader9* shader);
bool ExecuteSetVertexShaderHooks(DeviceContext& ctx, IDirect3DVertexShader9* shader);
bool ExecuteSetTextureHooks(DeviceContext& ctx, DWORD stage, IDirect3DBaseTexture9* texture);
bool ExecutePresentHooks(DeviceContext& ctx, const RECT* sourceRect, const RECT* destRect, HWND destWindowOverride, const RGNDATA* dirtyRegion);
bool ExecuteBeginSceneHooks(DeviceContext& ctx);
bool ExecuteCreateTextureHooks(DeviceContext& ctx, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** texture, HANDLE* sharedHandle);
bool ExecuteCreateRenderTargetHooks(
    DeviceContext& ctx, UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL lockable, IDirect3DSurface9** surface, HANDLE* sharedHandle);
bool ExecuteSetViewportHooks(DeviceContext& ctx, const D3DVIEWPORT9* viewport);
bool ExecuteCreatePixelShaderHooks(DeviceContext& ctx, const DWORD* pFunction, IDirect3DPixelShader9** ppShader);
bool ExecuteCreateVertexShaderHooks(DeviceContext& ctx, const DWORD* pFunction, IDirect3DVertexShader9** ppShader);
bool ExecuteSetPixelShaderConstantFHooks(DeviceContext& ctx, UINT startRegister, const float* pConstantData, UINT vector4fCount);
bool ExecuteSetVertexShaderConstantFHooks(DeviceContext& ctx, UINT startRegister, const float* pConstantData, UINT vector4fCount);

// Initialize hooks (called once at startup)
void Initialize(LPDIRECT3DDEVICE9 device);

// Cleanup hooks (called at shutdown)
void Cleanup();

// Check if hooks are initialized
bool IsInitialized();
} // namespace Internal

} // namespace D3D9Hooks
