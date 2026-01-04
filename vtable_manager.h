#pragma once
#include <Windows.h>
#include <string>
#include "addresses.h"

class VTableManager {
private:
    uintptr_t vtableBase = 0;

    bool ValidateVTable(uintptr_t candidate);
    bool IsExecutableAddress(uintptr_t addr);

public:
    bool Initialize();
    void* GetFunctionAddress(const char* debugStr, uintptr_t offset);
}; 