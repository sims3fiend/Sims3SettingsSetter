#pragma once
#include <windows.h>
#include <Psapi.h>  // For MODULEINFO and GetModuleInformation
#include <d3d9.h>
#include <detours/detours.h>
#include <vector>
#include <array>
#include <cstring>
#include <variant>
#include <optional>
#include <fstream>
#include "logger.h"
#include "settings.h"
#include "utils.h"
#include "patch_system.h"  // For GameVersion, g_gameVersion, GAME_VERSION_COUNT

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

    // Global mutex for thread-safe patch location tracking
    inline std::mutex g_patchLocationMutex;

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

            // Thread-safe!!
            std::lock_guard<std::mutex> lock(g_patchLocationMutex);
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

    // Check if memory is writable (for modifying existing structures like CRITICAL_SECTION)
    inline bool IsMemoryWritable(LPVOID address, MEMORY_BASIC_INFORMATION* outMbi = nullptr) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) {
            LOG_ERROR("VirtualQuery failed for address 0x" +
                     std::to_string(reinterpret_cast<uintptr_t>(address)));
            return false;
        }

        if (outMbi) {
            *outMbi = mbi;
        }

        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        // Check for writable protections (including copy-on-write variants)
        constexpr DWORD WRITABLE_PROTECTIONS =
            PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;

        return (mbi.Protect & WRITABLE_PROTECTIONS) != 0;
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
        // Include WRITECOPY protections which are also readable
        constexpr DWORD READABLE_PROTECTIONS =
            PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
            PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;

        if (mbi.State != MEM_COMMIT || (mbi.Protect & READABLE_PROTECTIONS) == 0) {
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

    // Restore all patched locations (thread-safe)
    inline bool RestoreAll(std::vector<PatchLocation>& locations) {
        std::lock_guard<std::mutex> lock(g_patchLocationMutex);

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

    // String-based pattern scanning with nibble-level wildcards, can you believe ts is called a nibble?
    // Supports: "FF" (exact), "??" (full wildcard), "?F" (match low nibble), "F?" (match high nibble) yum!
    // e.g. "0F 82 ?? ?? ?0 8B"  -> matches 0F 82 <any> <any> <any ending in 0> 8B
    inline uintptr_t ScanPattern(BYTE* start, size_t size, const char* pattern) {
        std::vector<BYTE> bytes;
        std::vector<BYTE> masks;  // Per-byte mask, 0xFF = exact, 0xF0 = high nibble, 0x0F = low nibble, 0x00 = wildcard

        // Helper to parse a single hex nibble
        auto parseNibble = [](char c) -> BYTE {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return 0;
        };

        const char* p = pattern;
        while (*p) {
            // Skip whitespace
            while (*p == ' ') p++;
            if (!*p) break;

            char hiChar = *p;
            char loChar = *(p + 1);

            // Need two characters for a byte
            if (!loChar || loChar == ' ') {
                LOG_ERROR("ScanPattern: Invalid pattern format (incomplete byte) at: " + std::string(p));
                return 0;
            }

            bool hiWild = (hiChar == '?');
            bool loWild = (loChar == '?');
            bool hiHex = isxdigit(hiChar);
            bool loHex = isxdigit(loChar);

            // Validate each nibby must be hex or wildcard
            if ((!hiWild && !hiHex) || (!loWild && !loHex)) {
                LOG_ERROR("ScanPattern: Invalid pattern format at: " + std::string(p));
                return 0;
            }

            // Build the byte and mask
            BYTE byteVal = 0;
            BYTE maskVal = 0;

            if (hiHex) {
                byteVal |= (parseNibble(hiChar) << 4);
                maskVal |= 0xF0;
            }
            if (loHex) {
                byteVal |= parseNibble(loChar);
                maskVal |= 0x0F;
            }

            bytes.push_back(byteVal);
            masks.push_back(maskVal);
            p += 2;
        }

        if (bytes.empty()) {
            LOG_ERROR("ScanPattern: Empty pattern");
            return 0;
        }

        // Perform the scan with mask comparison
        for (size_t i = 0; i <= size - bytes.size(); i++) {
            bool found = true;
            for (size_t j = 0; j < bytes.size(); j++) {
                if ((start[i + j] & masks[j]) != (bytes[j] & masks[j])) {
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

} 

// known version address OR pattern scan for unknown versions
// Strategy is:
// If version is KNOWN and we have an address for it -> use that address directly
// If version is UNKNOWN and we have a pattern -> try pattern scan
// Always validate with expectedBytes if provided to prevent patching the wrong area if the users exe is weird idk, should be fine but better safe
struct AddressInfo {
    struct VersionAddress {
        GameVersion version;
        uintptr_t address;
    };

    const char* name;                                    // For logging
    std::vector<VersionAddress> addresses;               // Explicit version → address mapping
    const char* pattern = nullptr;                       // Pattern scan string (nullptr = no pattern)
    int patternOffset = 0;                               // Offset to add to pattern match
    std::vector<uint8_t> expectedBytes = {};             // Bytes to validate (empty = skip)

    // Get address for a specific version (0 if not found)
    uintptr_t GetAddressForVersion(GameVersion version) const {
        for (const auto& va : addresses) {
            if (va.version == version) {
                return va.address;
            }
        }
        return 0;
    }

    // Resolve address for current game version
    std::optional<uintptr_t> Resolve() const {
        // If we know the version and have an address, use it
        if (g_gameVersion != GameVersion::Unknown) {
            uintptr_t addr = GetAddressForVersion(g_gameVersion);
            if (addr != 0) {
                LOG_DEBUG(std::format("[{}] Using address for {}: {:#010x}",
                    name, VERSION_NAMES[static_cast<size_t>(g_gameVersion)], addr));

                if (Validate(addr)) {
                    return addr;
                }
                LOG_ERROR(std::format("[{}] Address {:#010x} failed validation for {}",
                    name, addr, VERSION_NAMES[static_cast<size_t>(g_gameVersion)]));
                return std::nullopt;  // Don't fall through - known version, wrong bytes = bad
            }
            // No address for this known version - fall through to try pattern scan
            LOG_DEBUG(std::format("[{}] No address defined for {}, trying pattern scan...",
                name, VERSION_NAMES[static_cast<size_t>(g_gameVersion)]));
        }

        // or Try pattern scan for unknown versions or known versions without addresses
        if (pattern && pattern[0] != '\0') {
            MODULEINFO modInfo;
            HMODULE hModule = GetModuleHandleW(nullptr);
            if (GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
                if (auto addr = PatchHelper::ScanPattern(
                        static_cast<BYTE*>(modInfo.lpBaseOfDll),
                        modInfo.SizeOfImage,
                        pattern)) {
                    uintptr_t result = addr + patternOffset;
                    LOG_DEBUG(std::format("[{}] Pattern scan found: {:#010x}", name, result));

                    if (Validate(result)) {
                        LOG_INFO(std::format("[{}] Pattern matched on unknown version at {:#010x}", name, result));
                        return result;
                    }
                    LOG_WARNING(std::format("[{}] Pattern match at {:#010x} failed validation", name, result));
                }
            }
            LOG_ERROR(std::format("[{}] Pattern scan failed on unknown version", name));
        } else {
            LOG_ERROR(std::format("[{}] Unknown version and no pattern available", name));
        }

        return std::nullopt;
    }

    // Validate that address contains expected bytes
    bool Validate(uintptr_t addr) const {
        if (expectedBytes.empty()) {
            return true;  // No validation requested
        }

        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) {
            return false;
        }
        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        return std::memcmp(reinterpret_cast<void*>(addr),
                          expectedBytes.data(),
                          expectedBytes.size()) == 0;
    }
};

namespace PatchHelper {

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

namespace PatchHelper {
// IAT Hooking Helper
    namespace IATHookHelper {
        inline bool Hook(HMODULE hModule, const char* dllName, const char* funcName, void* newFunc, void** originalFunc) {
            if (!hModule || !dllName || !funcName || !newFunc) return false;

            PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
            if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) return false;

            PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDosHeader->e_lfanew);
            if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) return false;

            if (pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) return false;

            PIMAGE_IMPORT_DESCRIPTOR pImportDesc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hModule + 
                pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

            for (; pImportDesc->Name; pImportDesc++) {
                const char* moduleName = (const char*)((BYTE*)hModule + pImportDesc->Name);
                if (_stricmp(moduleName, dllName) == 0) {
                    PIMAGE_THUNK_DATA pThunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + pImportDesc->FirstThunk);
                    PIMAGE_THUNK_DATA pOrigThunk = (PIMAGE_THUNK_DATA)((BYTE*)hModule + pImportDesc->OriginalFirstThunk);
                    
                    if (!pOrigThunk) pOrigThunk = pThunk; // Fallback if no OriginalFirstThunk

                    for (; pThunk->u1.Function; pThunk++, pOrigThunk++) {
                        if (IMAGE_SNAP_BY_ORDINAL(pOrigThunk->u1.Ordinal)) continue;

                        PIMAGE_IMPORT_BY_NAME pImport = (PIMAGE_IMPORT_BY_NAME)((BYTE*)hModule + pOrigThunk->u1.AddressOfData);
                        if (strcmp(pImport->Name, funcName) == 0) {
                            if (originalFunc) *originalFunc = (void*)pThunk->u1.Function;

                            DWORD oldProtect;
                            // Try PAGE_READWRITE first, fall back to PAGE_EXECUTE_READWRITE for Win11 compatibility
                            if (!VirtualProtect(&pThunk->u1.Function, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
                                DWORD err = GetLastError();
                                LOG_ERROR("IAT Hook: VirtualProtect(PAGE_READWRITE) failed for " + std::string(funcName) + ", error: " + std::to_string(err));
                                // Try with execute permissions as fallback
                                if (!VirtualProtect(&pThunk->u1.Function, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                                    err = GetLastError();
                                    LOG_ERROR("IAT Hook: VirtualProtect(PAGE_EXECUTE_READWRITE) also failed, error: " + std::to_string(err));
                                    return false;
                                }
                            }

                            pThunk->u1.Function = (uintptr_t)newFunc;

                            // Flush instruction cache for compatibility with certain security configurations
                            FlushInstructionCache(GetCurrentProcess(), &pThunk->u1.Function, sizeof(uintptr_t));

                            VirtualProtect(&pThunk->u1.Function, sizeof(uintptr_t), oldProtect, &oldProtect);
                            return true;
                        }
                    }
                }
            }
            return false;
        }
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

// D3D9 Utilities - should split maybe?
namespace D3D9Helper {

    // Get current backbuffer dimensions
    inline bool GetBackbufferSize(LPDIRECT3DDEVICE9 device, UINT* width, UINT* height) {
        if (!device || !width || !height) return false;

        IDirect3DSurface9* backbuffer = nullptr;
        if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer))) {
            LOG_ERROR("D3D9Helper: Failed to get backbuffer");
            return false;
        }

        D3DSURFACE_DESC desc;
        HRESULT hr = backbuffer->GetDesc(&desc);
        backbuffer->Release();

        if (FAILED(hr)) {
            LOG_ERROR("D3D9Helper: Failed to get backbuffer description");
            return false;
        }

        *width = desc.Width;
        *height = desc.Height;
        return true;
    }

    // Get current viewport
    inline bool GetViewport(LPDIRECT3DDEVICE9 device, D3DVIEWPORT9* viewport) {
        if (!device || !viewport) return false;
        return SUCCEEDED(device->GetViewport(viewport));
    }

    // Get current present parameters
    inline bool GetPresentParameters(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS* params) {
        if (!device || !params) return false;

        IDirect3DSwapChain9* swapChain = nullptr;
        if (FAILED(device->GetSwapChain(0, &swapChain)) || !swapChain) {
            LOG_ERROR("D3D9Helper: Failed to get swap chain");
            return false;
        }

        HRESULT hr = swapChain->GetPresentParameters(params);
        swapChain->Release();

        return SUCCEEDED(hr);
    }

    // Get device capabilities
    inline bool GetDeviceCaps(LPDIRECT3DDEVICE9 device, D3DCAPS9* caps) {
        if (!device || !caps) return false;
        return SUCCEEDED(device->GetDeviceCaps(caps));
    }

    // Comprehensive device info structure
    struct DeviceInfo {
        D3DPRESENT_PARAMETERS presentParams = {};
        D3DVIEWPORT9 viewport = {};
        D3DCAPS9 caps = {};
        UINT backbufferWidth = 0;
        UINT backbufferHeight = 0;
        bool valid = false;
    };

    // Get all current device information at once
    inline DeviceInfo GetDeviceInfo(LPDIRECT3DDEVICE9 device) {
        DeviceInfo info;

        if (!device) {
            LOG_ERROR("D3D9Helper: Invalid device pointer");
            return info;
        }

        info.valid = true;
        info.valid &= GetPresentParameters(device, &info.presentParams);
        info.valid &= GetViewport(device, &info.viewport);
        info.valid &= GetDeviceCaps(device, &info.caps);
        info.valid &= GetBackbufferSize(device, &info.backbufferWidth, &info.backbufferHeight);

        return info;
    }

    // Format helpers
    inline const char* FormatToString(D3DFORMAT format) {
        switch (format) {
            case D3DFMT_A8R8G8B8: return "A8R8G8B8";
            case D3DFMT_X8R8G8B8: return "X8R8G8B8";
            case D3DFMT_R5G6B5: return "R5G6B5";
            case D3DFMT_X1R5G5B5: return "X1R5G5B5";
            case D3DFMT_A1R5G5B5: return "A1R5G5B5";
            case D3DFMT_A4R4G4B4: return "A4R4G4B4";
            case D3DFMT_R8G8B8: return "R8G8B8";
            case D3DFMT_A2B10G10R10: return "A2B10G10R10";
            case D3DFMT_A16B16G16R16: return "A16B16G16R16";
            case D3DFMT_A16B16G16R16F: return "A16B16G16R16F";
            case D3DFMT_A32B32G32R32F: return "A32B32G32R32F";
            case D3DFMT_D16: return "D16";
            case D3DFMT_D24S8: return "D24S8";
            case D3DFMT_D24X8: return "D24X8";
            case D3DFMT_D32: return "D32";
            default: return "Unknown";
        }
    }

    inline const char* PrimitiveTypeToString(D3DPRIMITIVETYPE type) {
        switch (type) {
            case D3DPT_POINTLIST: return "PointList";
            case D3DPT_LINELIST: return "LineList";
            case D3DPT_LINESTRIP: return "LineStrip";
            case D3DPT_TRIANGLELIST: return "TriangleList";
            case D3DPT_TRIANGLESTRIP: return "TriangleStrip";
            case D3DPT_TRIANGLEFAN: return "TriangleFan";
            default: return "Unknown";
        }
    }

    // Shader bytecode helpers -bzzt wrong
    inline bool GetShaderBytecode(IDirect3DPixelShader9* shader, std::vector<BYTE>& bytecode) {
        if (!shader) return false;

        UINT size = 0;
        if (FAILED(shader->GetFunction(nullptr, &size))) {
            LOG_ERROR("D3D9Helper: Failed to get shader size");
            return false;
        }

        bytecode.resize(size);
        if (FAILED(shader->GetFunction(bytecode.data(), &size))) {
            LOG_ERROR("D3D9Helper: Failed to get shader bytecode");
            return false;
        }

        return true;
    }

    inline bool SaveShaderToFile(IDirect3DPixelShader9* shader, const std::string& filename) {
        std::vector<BYTE> bytecode;
        if (!GetShaderBytecode(shader, bytecode)) {
            return false;
        }

        std::ofstream file(filename, std::ios::binary);
        if (!file) {
            LOG_ERROR("D3D9Helper: Failed to open file for writing: " + filename);
            return false;
        }

        file.write(reinterpret_cast<const char*>(bytecode.data()), bytecode.size());
        return file.good();
    }

} // namespace D3D9Helper

