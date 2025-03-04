#pragma once
#include <Windows.h>
#include <string>
#include "pattern_scan.h"

class VTableManager {
private:
    uintptr_t vtableBase = 0;
    bool validated = false;

    // Pattern for constructor: MOV [ESI],vtable; XOR ECX,ECX; MOV [ESI+0C],ECX
    const char* constructorPattern = "56 8B F1 C7 06 ?? ?? ?? ?? 33 C9 89 4E 0C";
    
    //would not believe...
    const char* vfuncPattern = 
        "83 EC 18 53 56 8B F1 8D ?? 38 68 ?? ?? ?? ?? 89 4C 24 ?? E8 ?? ?? ?? ?? "
        "C6 44 24 ?? 00 8B 44 24 ??";

    bool ValidateVTable(uintptr_t candidate);
    bool IsExecutableAddress(uintptr_t addr);

public:
    bool Initialize();
    void* GetFunctionAddress(const char* debugStr, uintptr_t offset);
}; 