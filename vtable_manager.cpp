#include "vtable_manager.h"

//need to make this less specific to debug since I'll probably want to use this for other things
bool VTableManager::ValidateVTable(uintptr_t candidate) {
    // Check first 4 entries are executable code
    for(int i = 0; i < 4; i++) {
        uintptr_t entry = *reinterpret_cast<uintptr_t*>(candidate + i*4);
        if(!IsExecutableAddress(entry)) return false;
    }
    
    // Verify specific function contains "Debug/VarMan" string
    uintptr_t stringFunc = *reinterpret_cast<uintptr_t*>(candidate + 0x1C);
    const char* debugPattern = "44 65 62 75 67 2F 56 61 72 4D 61 6E"; // "Debug/VarMan" in hex
    return Pattern::ScanModule(GetModuleHandle(nullptr), debugPattern, nullptr) != 0;
}

bool VTableManager::IsExecutableAddress(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi;
    if(!VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
        return false;
    return (mbi.Protect & (PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE));
}

bool VTableManager::Initialize() {
    // Find constructor using pattern
    uintptr_t ctorAddr = Pattern::Scan(constructorPattern);
    if(!ctorAddr) {
        OutputDebugStringA("VTABLE Manager: Constructor pattern not found\n");
        return false;
    }

    // Extract VTABLE address from constructor MOV instruction
    vtableBase = *reinterpret_cast<uintptr_t*>(ctorAddr + 5);
    if(!vtableBase) {
        OutputDebugStringA("VTABLE Manager: Invalid base address\n");
        return false;
    }

    // Multi-layer validation
    validated = ValidateVTable(vtableBase);
    if(!validated) {
        OutputDebugStringA("VTABLE Manager: Validation failed\n");
        vtableBase = 0;
        return false;
    }

    OutputDebugStringA("VTABLE Manager: Initialized successfully\n");
    return true;
}

void* VTableManager::GetFunctionAddress(const char* debugStr, uintptr_t offset) {
    if(!validated || !vtableBase) {
        OutputDebugStringA("VTABLE Manager: Not initialized\n");
        return nullptr;
    }

    uintptr_t funcAddr = *reinterpret_cast<uintptr_t*>(vtableBase + offset);
    if(!IsExecutableAddress(funcAddr)) {
        OutputDebugStringA(("VTABLE Manager: Invalid function at offset " + 
                          std::to_string(offset) + "\n").c_str());
        return nullptr;
    }

    // Optional: Verify function pattern
    if(!Pattern::Scan(vfuncPattern)) {
        OutputDebugStringA(("VTABLE Manager: Function pattern mismatch at " + 
                          std::to_string(offset) + "\n").c_str());
        return nullptr;
    }

    OutputDebugStringA(("VTABLE Manager: Resolved " + std::string(debugStr) + 
                      " at 0x" + std::to_string(funcAddr) + "\n").c_str());
    return reinterpret_cast<void*>(funcAddr);
} 