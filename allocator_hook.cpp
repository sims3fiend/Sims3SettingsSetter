#include "allocator_hook.h"
#include <windows.h>
#include <mimalloc.h>
#pragma comment(lib, "mimalloc.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "bcrypt.lib")
#include <detours/detours.h>
#include <string>
#include <vector>
#include <algorithm>
#include <direct.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "dbghelp.lib")
#include "logger.h"

bool g_mimallocActive = false;

// Function pointers for original functions
typedef void* (__cdecl* MallocFunc)(size_t);
typedef void(__cdecl* FreeFunc)(void*);
typedef void* (__cdecl* CallocFunc)(size_t, size_t);
typedef void* (__cdecl* ReallocFunc)(void*, size_t);
typedef void* (__cdecl* AlignedMallocFunc)(size_t, size_t);
typedef void(__cdecl* AlignedFreeFunc)(void*);
typedef void* (__cdecl* AlignedReallocFunc)(void*, size_t, size_t);
typedef size_t(__cdecl* MsizeFunc)(void*);
typedef void* (__cdecl* ExpandFunc)(void*, size_t);
typedef void* (__cdecl* RecallocFunc)(void*, size_t, size_t);

static MallocFunc original_malloc = nullptr;
static FreeFunc original_free = nullptr;
static CallocFunc original_calloc = nullptr;
static ReallocFunc original_realloc = nullptr;
static AlignedMallocFunc original_aligned_malloc = nullptr;
static AlignedFreeFunc original_aligned_free = nullptr;
static AlignedReallocFunc original_aligned_realloc = nullptr;
static MsizeFunc original_msize = nullptr;
static ExpandFunc original_expand = nullptr;
static RecallocFunc original_recalloc = nullptr;

// Safe wrappers
void* __cdecl HookedMalloc(size_t size) {
    return mi_malloc(size);
}

void __cdecl SafeFree(void* p) {
    if (!p) return;
    if (mi_is_in_heap_region(p)) {
        mi_free(p);
    }
    else {
        if (original_free) original_free(p);
    }
}

void* __cdecl HookedCalloc(size_t count, size_t size) {
    return mi_calloc(count, size);
}

void* __cdecl SafeRealloc(void* p, size_t newsize) {
    if (!p) return mi_realloc(p, newsize);
    if (mi_is_in_heap_region(p)) {
        return mi_realloc(p, newsize);
    }
    else {
        // We don't attempt to migrate the block to mimalloc (e.g. via malloc + memcpy + free)
        // because we don't know the size without calling _msize (overhead) and it's risky.
        // Since we hook early, these are rare and will likely be freed... Hopefully....
        if (original_realloc) return original_realloc(p, newsize);
        return nullptr;
    }
}

void* __cdecl HookedAlignedMalloc(size_t size, size_t alignment) {
    return mi_malloc_aligned(size, alignment);
}

void __cdecl SafeAlignedFree(void* p) {
    if (!p) return;
    if (mi_is_in_heap_region(p)) {
        mi_free(p);
    }
    else {
        if (original_aligned_free) original_aligned_free(p);
    }
}

void* __cdecl SafeAlignedRealloc(void* p, size_t size, size_t alignment) {
    if (!p) return mi_malloc_aligned(size, alignment);
    if (mi_is_in_heap_region(p)) {
        return mi_realloc_aligned(p, size, alignment);
    }
    else {
        if (original_aligned_realloc) return original_aligned_realloc(p, size, alignment);
        return nullptr;
    }
}

size_t __cdecl SafeMsize(void* p) {
    if (!p) return 0;
    if (mi_is_in_heap_region(p)) {
        return mi_usable_size(p);
    }
    else {
        if (original_msize) return original_msize(p);
        return 0;
    }
}

void* __cdecl SafeExpand(void* p, size_t size) {
    if (!p) return nullptr;
    if (mi_is_in_heap_region(p)) {
        return mi_expand(p, size);
    }
    else {
        if (original_expand) return original_expand(p, size);
        return nullptr;
    }
}

void* __cdecl SafeRecalloc(void* p, size_t count, size_t size) {
    if (!p) return mi_calloc(count, size);
    if (mi_is_in_heap_region(p)) {
        return mi_recalloc(p, count, size);
    }
    else {
        if (original_recalloc) return original_recalloc(p, count, size);
        return nullptr;
    }
}

// Helper to check if hooks should be enabled from config
bool ShouldEnableAllocatorHooks() {
    char iniPath[MAX_PATH];
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
        strcat_s(iniPath, "S3SS.ini");
    }
    else {
        strcpy_s(iniPath, "S3SS.ini");
    }

    // Check if file exists
    DWORD attr = GetFileAttributesA(iniPath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        // File doesn't exist, default to FALSE (user must explicitly enable)
        return false;
    }

    // Read value - default to false if key doesn't exist
    char value[32];
    GetPrivateProfileStringA("Optimization_Mimalloc", "Enabled", "false", value, 32, iniPath);
    return _stricmp(value, "true") == 0;
}

void InitializeAllocatorHooks() {
    if (!ShouldEnableAllocatorHooks()) {
        LOG_INFO("Allocator hooks disabled via config.");
        return;
    }

    LOG_INFO("Initializing Allocator Hooks (mimalloc via Detours)...");

    HMODULE hMsvcr80 = GetModuleHandleA("MSVCR80.dll");
    if (!hMsvcr80) {
        LOG_ERROR("MSVCR80.dll not loaded! Cannot install hooks.");
        return;
    }

    // Helper to get address and log
    auto GetProc = [&](const char* name) -> void* {
        void* addr = (void*)GetProcAddress(hMsvcr80, name);
        if (!addr) LOG_WARNING(std::string("Could not find export: ") + name);
        return addr;
    };

    // init original pointers
    original_malloc = (MallocFunc)GetProc("malloc");
    original_free = (FreeFunc)GetProc("free");
    original_calloc = (CallocFunc)GetProc("calloc");
    original_realloc = (ReallocFunc)GetProc("realloc");
    original_aligned_malloc = (AlignedMallocFunc)GetProc("_aligned_malloc");
    original_aligned_free = (AlignedFreeFunc)GetProc("_aligned_free");
    original_aligned_realloc = (AlignedReallocFunc)GetProc("_aligned_realloc");
    original_msize = (MsizeFunc)GetProc("_msize");
    original_expand = (ExpandFunc)GetProc("_expand");
    original_recalloc = (RecallocFunc)GetProc("_recalloc");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    int hookCount = 0;

    if (original_malloc) { DetourAttach(&(PVOID&)original_malloc, HookedMalloc); hookCount++; }
    if (original_free) { DetourAttach(&(PVOID&)original_free, SafeFree); hookCount++; }
    if (original_calloc) { DetourAttach(&(PVOID&)original_calloc, HookedCalloc); hookCount++; }
    if (original_realloc) { DetourAttach(&(PVOID&)original_realloc, SafeRealloc); hookCount++; }
    if (original_aligned_malloc) { DetourAttach(&(PVOID&)original_aligned_malloc, HookedAlignedMalloc); hookCount++; }
    if (original_aligned_free) { DetourAttach(&(PVOID&)original_aligned_free, SafeAlignedFree); hookCount++; }
    if (original_aligned_realloc) { DetourAttach(&(PVOID&)original_aligned_realloc, SafeAlignedRealloc); hookCount++; }
    if (original_msize) { DetourAttach(&(PVOID&)original_msize, SafeMsize); hookCount++; }
    if (original_expand) { DetourAttach(&(PVOID&)original_expand, SafeExpand); hookCount++; }
    if (original_recalloc) { DetourAttach(&(PVOID&)original_recalloc, SafeRecalloc); hookCount++; }

    LONG error = DetourTransactionCommit();
    if (error == NO_ERROR) {
        LOG_INFO("Successfully hooked " + std::to_string(hookCount) + " allocator functions.");
        g_mimallocActive = true;
    }
    else {
        LOG_ERROR("Failed to commit Detours transaction: " + std::to_string(error));
    }
}
