#include "vtable_manager.h"
#include <Psapi.h>
#include "utils.h"
#include "logger.h"

//need to make this less specific to debug since I'll probably want to use this for other things
static bool IsWithinModule(uintptr_t address, HMODULE module) {
    MODULEINFO moduleInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo))) { return false; }

    uintptr_t base = reinterpret_cast<uintptr_t>(moduleInfo.lpBaseOfDll);
    uintptr_t size = static_cast<uintptr_t>(moduleInfo.SizeOfImage);
    return address >= base && address < (base + size);
}

// Find an ASCII string's address within the main module
static uintptr_t FindAsciiStringAddress(const char* literal) {
    HMODULE module = GetModuleHandle(nullptr);
    MODULEINFO moduleInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo))) { return 0; }

    const char* base = reinterpret_cast<const char*>(moduleInfo.lpBaseOfDll);
    const size_t size = static_cast<size_t>(moduleInfo.SizeOfImage);
    const size_t len = strlen(literal);
    if (len == 0 || len > size) return 0;

    for (size_t i = 0; i + len <= size; ++i) {
        if (memcmp(base + i, literal, len) == 0) { return reinterpret_cast<uintptr_t>(base + i); }
    }
    return 0;
}

// Check if the function's first bytes contain an immediate pointer to target
static bool FunctionReferencesPointer(uintptr_t funcAddr, size_t searchLen, uintptr_t targetPtr) {
    // Clamp searchLen to a reasonable window
    const size_t kMaxSearch = 0x400;
    if (searchLen == 0 || searchLen > kMaxSearch) searchLen = kMaxSearch;

    for (size_t i = 0; i + sizeof(uintptr_t) <= searchLen; ++i) {
        if (*reinterpret_cast<const uintptr_t*>(funcAddr + i) == targetPtr) { return true; }
    }
    return false;
}

bool VTableManager::ValidateVTable(uintptr_t candidate) {
    // Ensure the vtable itself is in readable memory (typically .rdata/.data)
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(candidate), &mbi, sizeof(mbi))) { return false; }

    // Validate the first few entries point to executable code within the main module
    HMODULE mainModule = GetModuleHandle(nullptr);
    const int kEntriesToCheck = 8;
    for (int i = 0; i < kEntriesToCheck; ++i) {
        uintptr_t entry = *reinterpret_cast<uintptr_t*>(candidate + i * sizeof(uintptr_t));
        if (!IsExecutableAddress(entry)) return false;
        if (!IsWithinModule(entry, mainModule)) return false;
    }

    return true;
}

bool VTableManager::IsExecutableAddress(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) return false;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}

bool VTableManager::Initialize() {
    // Find constructor using pattern
    uintptr_t ctorAddr = Pattern::Scan(constructorPattern);
    if (!ctorAddr) {
        LOG_ERROR("[VTableManager] Constructor pattern not found");
        return false;
    }

    // Extract VTABLE address from constructor MOV instruction
    vtableBase = *reinterpret_cast<uintptr_t*>(ctorAddr + 5);
    if (!vtableBase) {
        LOG_ERROR("[VTableManager] Invalid base address");
        return false;
    }

    // Multi-layer validation
    validated = ValidateVTable(vtableBase);
    if (!validated) {
        LOG_ERROR("[VTableManager] Validation failed");
        vtableBase = 0;
        return false;
    }

    LOG_INFO("[VTableManager] Initialized successfully");
    return true;
}

void* VTableManager::GetFunctionAddress(const char* debugStr, uintptr_t offset) {
    if (!validated || !vtableBase) {
        LOG_ERROR("[VTableManager] Not initialized");
        return nullptr;
    }

    uintptr_t funcAddr = *reinterpret_cast<uintptr_t*>(vtableBase + offset);
    if (!IsExecutableAddress(funcAddr)) {
        LOG_ERROR("[VTableManager] Invalid function at offset " + std::to_string(offset));
        return nullptr;
    }

    if (!IsWithinModule(funcAddr, GetModuleHandle(nullptr))) {
        LOG_ERROR("[VTableManager] Function outside main module at offset " + std::to_string(offset));
        return nullptr;
    }

    // For VariableRegistry, verify local reference to expected debug literal
    // VTBL offset 0x3C is used for VariableRegistry in tha code
    if (offset == 0x3C) {
        // Prefer the longer literal if present, otherwise fall back to VarMan
        uintptr_t varLiteral = FindAsciiStringAddress("Debug/VariableRegistry/Variable");
        if (!varLiteral) { varLiteral = FindAsciiStringAddress("Debug/VarMan"); }
        if (varLiteral && !FunctionReferencesPointer(funcAddr, 0x200, varLiteral)) { LOG_WARNING("[VTableManager] VariableRegistry slot did not reference expected literal near prologue."); }
    }

    LOG_DEBUG(std::string("[VTableManager] Resolved ") + debugStr + " at 0x" + std::to_string(funcAddr));
    return reinterpret_cast<void*>(funcAddr);
}