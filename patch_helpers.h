#pragma once
#include <windows.h>
#include <Psapi.h>  // For MODULEINFO and GetModuleInformation
#include <detours/detours.h>
#include <vector>
#include <cstring>
#include <variant>
#include "logger.h"
#include "settings.h"
#include "utils.h"

// Forward declarations for ImGui
struct ImGuiContext;
namespace ImGui {
    ImGuiContext* GetCurrentContext();
}

#pragma comment(lib, "Psapi.lib")

// Helper utilities for common patching operations
namespace PatchHelper {

    // Structure to track a patched location
    struct PatchLocation {
        uintptr_t address;
        std::vector<BYTE> originalBytes;
        size_t size;
    };

    // Safely change memory protection and write data
    inline bool WriteProtectedMemory(LPVOID address, LPCVOID data, SIZE_T size, 
                                     std::vector<PatchLocation>* tracker = nullptr) {
        // Validate memory is accessible before attempting to patch
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) {
            LOG_ERROR("VirtualQuery failed for address 0x" + 
                     std::to_string(reinterpret_cast<uintptr_t>(address)));
            return false;
        }
        
        if (mbi.State != MEM_COMMIT) {
            LOG_ERROR("Memory at 0x" + std::to_string(reinterpret_cast<uintptr_t>(address)) + 
                     " is not committed (State: " + std::to_string(mbi.State) + ")");
            return false;
        }
        
        // Store original bytes if tracking is enabled
        if (tracker) {
            PatchLocation loc;
            loc.address = reinterpret_cast<uintptr_t>(address);
            loc.size = size;
            loc.originalBytes.resize(size);
            std::memcpy(loc.originalBytes.data(), address, size);
            tracker->push_back(loc);
        }

        DWORD oldProtect;
        if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LOG_ERROR("Failed to change memory protection at 0x" + 
                     std::to_string(reinterpret_cast<uintptr_t>(address)));
            return false;
        }

        std::memcpy(address, data, size);

        VirtualProtect(address, size, oldProtect, &oldProtect);

        // Verify write
        if (std::memcmp(address, data, size) != 0) {
            LOG_ERROR("Failed to verify memory write at 0x" + 
                     std::to_string(reinterpret_cast<uintptr_t>(address)));
            return false;
        }

        return true;
    }

    // Validate that memory contains expected bytes before patching
    inline bool ValidateBytes(LPVOID address, LPCVOID expected, SIZE_T size) {
        // Use VirtualQuery for safer memory validation
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) {
            LOG_ERROR("VirtualQuery failed for address 0x" + 
                     std::to_string(reinterpret_cast<uintptr_t>(address)));
            return false;
        }
        
        // Check if memory is readable
        if (mbi.State != MEM_COMMIT || 
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) {
            LOG_ERROR("Memory at 0x" + std::to_string(reinterpret_cast<uintptr_t>(address)) + 
                     " is not readable (State: " + std::to_string(mbi.State) + 
                     ", Protect: " + std::to_string(mbi.Protect) + ")");
            return false;
        }
        
        // Now safe to compare
        return std::memcmp(address, expected, size) == 0;
    }

    // Write a single byte
    inline bool WriteByte(uintptr_t address, BYTE value, 
                         std::vector<PatchLocation>* tracker = nullptr,
                         BYTE* expectedOld = nullptr) {
        if (expectedOld && !ValidateBytes((LPVOID)address, expectedOld, 1)) {
            LOG_ERROR("Byte validation failed at 0x" + std::to_string(address));
            return false;
        }
        return WriteProtectedMemory((LPVOID)address, &value, 1, tracker);
    }

    // Write multiple bytes
    inline bool WriteBytes(uintptr_t address, const std::vector<BYTE>& bytes,
                          std::vector<PatchLocation>* tracker = nullptr,
                          const std::vector<BYTE>* expectedOld = nullptr) {
        if (expectedOld && !ValidateBytes((LPVOID)address, expectedOld->data(), expectedOld->size())) {
            LOG_ERROR("Bytes validation failed at 0x" + std::to_string(address));
            return false;
        }
        return WriteProtectedMemory((LPVOID)address, bytes.data(), bytes.size(), tracker);
    }

    // Write a DWORD (4 bytes)
    inline bool WriteDWORD(uintptr_t address, DWORD value,
                          std::vector<PatchLocation>* tracker = nullptr,
                          DWORD* expectedOld = nullptr) {
        if (expectedOld && !ValidateBytes((LPVOID)address, expectedOld, sizeof(DWORD))) {
            LOG_ERROR("DWORD validation failed at 0x" + std::to_string(address));
            return false;
        }
        return WriteProtectedMemory((LPVOID)address, &value, sizeof(DWORD), tracker);
    }

    // Write a WORD (2 bytes)
    inline bool WriteWORD(uintptr_t address, WORD value,
                         std::vector<PatchLocation>* tracker = nullptr,
                         WORD* expectedOld = nullptr) {
        if (expectedOld && !ValidateBytes((LPVOID)address, expectedOld, sizeof(WORD))) {
            LOG_ERROR("WORD validation failed at 0x" + std::to_string(address));
            return false;
        }
        return WriteProtectedMemory((LPVOID)address, &value, sizeof(WORD), tracker);
    }

    // NOP out bytes (replace with 0x90)
    inline bool WriteNOP(uintptr_t address, size_t count,
                        std::vector<PatchLocation>* tracker = nullptr) {
        std::vector<BYTE> nops(count, 0x90);
        return WriteProtectedMemory((LPVOID)address, nops.data(), count, tracker);
    }

    // Restore all patched locations
    inline bool RestoreAll(std::vector<PatchLocation>& locations) {
        bool success = true;
        for (auto it = locations.rbegin(); it != locations.rend(); ++it) {
            if (!WriteProtectedMemory((LPVOID)it->address, 
                                     it->originalBytes.data(), 
                                     it->size, 
                                     nullptr)) {
                LOG_ERROR("Failed to restore patch at 0x" + std::to_string(it->address));
                success = false;
            }
        }
        if (success) {
            locations.clear();
        }
        return success;
    }

    // Get module information
    inline bool GetModuleInfo(HMODULE hModule, BYTE** baseAddr, size_t* imageSize) {
        MODULEINFO modInfo;
        if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(MODULEINFO))) {
            LOG_ERROR("Failed to get module information");
            return false;
        }
        *baseAddr = (BYTE*)modInfo.lpBaseOfDll;
        *imageSize = modInfo.SizeOfImage;
        return true;
    }

    // String-based pattern scanning: "48 89 5C ? ? 08" where ? is wildcard
    // Just copy/paste patterns from your disassembler!
    inline uintptr_t ScanPattern(BYTE* start, size_t size, const char* pattern) {
        std::vector<BYTE> bytes;
        std::string mask;

        const char* p = pattern;
        while (*p) {
            // Skip whitespace
            while (*p == ' ') p++;
            if (!*p) break;

            // Check for wildcard
            if (*p == '?') {
                bytes.push_back(0x00);  // Dummy byte
                mask += '?';
                p++;
            }
            // Parse hex byte
            else if (isxdigit(*p) && isxdigit(*(p + 1))) {
                char byteStr[3] = { *p, *(p + 1), '\0' };
                bytes.push_back((BYTE)strtol(byteStr, nullptr, 16));
                mask += 'x';
                p += 2;
            }
            else {
                // Invalid format
                LOG_ERROR("ScanPattern: Invalid pattern format at: " + std::string(p));
                return 0;
            }
        }

        if (bytes.empty()) {
            LOG_ERROR("ScanPattern: Empty pattern");
            return 0;
        }

        // Perform the scan
        for (size_t i = 0; i <= size - bytes.size(); i++) {
            bool found = true;
            for (size_t j = 0; j < bytes.size(); j++) {
                if (mask[j] == '?') continue;  // Wildcard
                if (start[i + j] != bytes[j]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return reinterpret_cast<uintptr_t>(start + i);
            }
        }
        return 0;
    }

    // Read helpers for inspecting memory before patching
    inline BYTE ReadByte(uintptr_t address) {
        return *reinterpret_cast<BYTE*>(address);
    }

    inline WORD ReadWORD(uintptr_t address) {
        return *reinterpret_cast<WORD*>(address);
    }

    inline DWORD ReadDWORD(uintptr_t address) {
        return *reinterpret_cast<DWORD*>(address);
    }

    // Calculate relative jump/call offset for x86/x64
    // nextInstrAddr = address of the instruction AFTER the jump/call (usually jumpAddr + 5)
    inline int32_t CalculateRelativeOffset(uintptr_t from, uintptr_t to, size_t instructionSize = 5) {
        return static_cast<int32_t>(to - (from + instructionSize));
    }

    // Write a relative jump (E9 xx xx xx xx)
    inline bool WriteRelativeJump(uintptr_t address, uintptr_t destination,
                                  std::vector<PatchLocation>* tracker = nullptr) {
        std::vector<BYTE> jumpBytes(5);
        jumpBytes[0] = 0xE9;  // JMP rel32
        int32_t offset = CalculateRelativeOffset(address, destination, 5);
        std::memcpy(&jumpBytes[1], &offset, 4);
        return WriteProtectedMemory((LPVOID)address, jumpBytes.data(), 5, tracker);
    }

    // Write a relative call (E8 xx xx xx xx)
    inline bool WriteRelativeCall(uintptr_t address, uintptr_t destination,
                                  std::vector<PatchLocation>* tracker = nullptr) {
        std::vector<BYTE> callBytes(5);
        callBytes[0] = 0xE8;  // CALL rel32
        int32_t offset = CalculateRelativeOffset(address, destination, 5);
        std::memcpy(&callBytes[1], &offset, 4);
        return WriteProtectedMemory((LPVOID)address, callBytes.data(), 5, tracker);
    }

    // Multi-patch transaction: all-or-nothing helper
    // Usage: auto tx = BeginTransaction(); WriteByte(..., &tx.locations); ... CommitTransaction(tx);
    struct PatchTransaction {
        std::vector<PatchLocation> locations;
        bool committed = false;
    };

    inline PatchTransaction BeginTransaction() {
        return PatchTransaction();
    }

    inline bool CommitTransaction(PatchTransaction& tx) {
        tx.committed = true;
        return true;
    }

    inline bool RollbackTransaction(PatchTransaction& tx) {
        if (!tx.committed && !tx.locations.empty()) {
            return RestoreAll(tx.locations);
        }
        return true;
    }

    // ImGui Safety Helpers for patch UI rendering
    namespace ImGuiSafety {
        // Simple safety check - just verify context exists
        inline bool IsSafeToRender() {
            return ImGui::GetCurrentContext() != nullptr;
        }
    } // namespace ImGuiSafety

} // namespace PatchHelper

// Macro for safe ImGui rendering in patches
// The macro checks if ImGui context exists before rendering
#define SAFE_IMGUI_BEGIN() \
    if (!PatchHelper::ImGuiSafety::IsSafeToRender()) { \
        LOG_DEBUG("Skipping patch UI render - ImGui context not available"); \
        return; \
    }

// Simple patch description for REGISTER_SIMPLE_PATCH macro
namespace SimplePatch {
    struct PatchDesc {
        uintptr_t address;
        std::vector<BYTE> newBytes;
        std::vector<BYTE> oldBytes;
    };

    inline PatchDesc Byte(uintptr_t addr, BYTE newVal, BYTE oldVal = 0) {
        return PatchDesc{ addr, {newVal}, {oldVal} };
    }

    inline PatchDesc Bytes(uintptr_t addr, std::vector<BYTE> newVals, std::vector<BYTE> oldVals = {}) {
        return PatchDesc{ addr, newVals, oldVals };
    }

    inline PatchDesc DWord(uintptr_t addr, DWORD newVal, DWORD oldVal = 0) {
        std::vector<BYTE> newBytes(4), oldBytes(4);
        std::memcpy(newBytes.data(), &newVal, 4);
        std::memcpy(oldBytes.data(), &oldVal, 4);
        return PatchDesc{ addr, newBytes, oldBytes };
    }

    inline PatchDesc Word(uintptr_t addr, WORD newVal, WORD oldVal = 0) {
        std::vector<BYTE> newBytes(2), oldBytes(2);
        std::memcpy(newBytes.data(), &newVal, 2);
        std::memcpy(oldBytes.data(), &oldVal, 2);
        return PatchDesc{ addr, newBytes, oldBytes };
    }

    inline PatchDesc NOP(uintptr_t addr, size_t count) {
        return PatchDesc{ addr, std::vector<BYTE>(count, 0x90), {} };
    }
}

// Detours helper for function hooking
namespace DetourHelper {
    struct Hook {
        void** originalPtr;
        void* hookFunc;
    };

    inline bool InstallHooks(const std::vector<Hook>& hooks) {
        if (hooks.empty()) return true;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        for (const auto& hook : hooks) {
            LONG result = DetourAttach(hook.originalPtr, hook.hookFunc);
            if (result != NO_ERROR) {
                LOG_ERROR("Failed to attach hook: " + std::to_string(result));
                DetourTransactionAbort();
                return false;
            }
        }

        LONG result = DetourTransactionCommit();
        if (result != NO_ERROR) {
            LOG_ERROR("Failed to commit hooks: " + std::to_string(result));
            return false;
        }

        return true;
    }

    inline bool RemoveHooks(const std::vector<Hook>& hooks) {
        if (hooks.empty()) return true;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        for (const auto& hook : hooks) {
            LONG result = DetourDetach(hook.originalPtr, hook.hookFunc);
            if (result != NO_ERROR) {
                LOG_ERROR("Failed to detach hook: " + std::to_string(result));
                DetourTransactionAbort();
                return false;
            }
        }

        LONG result = DetourTransactionCommit();
        if (result != NO_ERROR) {
            LOG_ERROR("Failed to commit hook removal: " + std::to_string(result));
            return false;
        }

        return true;
    }
} // namespace DetourHelper

// Live Settings Integration - Access game settings dynamically through SettingsManager
namespace LiveSetting {

    // Get the memory address of a live setting by name
    // Returns nullptr if setting not found
    inline void* GetAddress(const wchar_t* name) {
        auto* setting = SettingsManager::Get().GetSetting(name);
        return setting ? setting->GetAddress() : nullptr;
    }

    // Get a live setting value by name
    // Returns false if setting not found or type mismatch
    template<typename T>
    inline bool GetValue(const wchar_t* name, T& outValue) {
        auto* setting = SettingsManager::Get().GetSetting(name);
        if (!setting) {
            LOG_ERROR("LiveSetting: Setting not found: " + Utils::WideToUtf8(name));
            return false;
        }

        try {
            outValue = std::get<T>(setting->GetValue());
            return true;
        }
        catch (const std::bad_variant_access&) {
            LOG_ERROR("LiveSetting: Type mismatch for setting: " + Utils::WideToUtf8(name));
            return false;
        }
    }

    // Patch a live setting value with automatic memory tracking
    // Perfect for patches that need to modify dynamic settings
    template<typename T>
    inline bool Patch(const wchar_t* name, T newValue,
                     std::vector<PatchHelper::PatchLocation>* tracker = nullptr) {
        auto* setting = SettingsManager::Get().GetSetting(name);
        if (!setting) {
            LOG_ERROR("LiveSetting: Setting not found: " + Utils::WideToUtf8(name));
            return false;
        }

        void* address = setting->GetAddress();
        if (!address) {
            LOG_ERROR("LiveSetting: Setting has no address: " + Utils::WideToUtf8(name));
            return false;
        }

        // Verify type matches before patching
        try {
            std::get<T>(setting->GetValue());
        }
        catch (const std::bad_variant_access&) {
            LOG_ERROR("LiveSetting: Type mismatch for setting: " + Utils::WideToUtf8(name));
            return false;
        }

        // Use existing WriteProtectedMemory with tracking for restore
        return PatchHelper::WriteProtectedMemory(address, &newValue, sizeof(T), tracker);
    }

    // Check if a setting exists in the live settings system
    inline bool Exists(const wchar_t* name) {
        return SettingsManager::Get().GetSetting(name) != nullptr;
    }

} // namespace LiveSetting

