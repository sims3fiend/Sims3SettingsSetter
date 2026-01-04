#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../addresses.h"
#include "../logger.h"

// CreateFileW Random Access patch - adds FILE_FLAG_RANDOM_ACCESS to main callers of CreateFileW, since sims 3 uses dbpf it makes more sense to hint random than sequential
class CreateFileRandomAccessPatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    
public:
    CreateFileRandomAccessPatch() : OptimizationPatch("CreateFileRandomAccess", nullptr) {}
    
    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();  // Clear any previous error
        LOG_INFO("[CreateFileRandomAccessPatch] Installing dual patch...");

        // Patch 1: sub_404550 - add FILE_FLAG_RANDOM_ACCESS (0x10000000) to flags
        // Change DWORD at `createFileWFlagsInit`: 0x80 -> 0x10000080 (adds random access flag)

        uintptr_t flagsInit = gameAddresses->createFileWFlagsInit;

        // Read and log current value for debugging
        DWORD currentValue1 = PatchHelper::ReadDWORD(flagsInit);
        LOG_DEBUG(std::format("[CreateFileRandomAccessPatch] Current value at {:#010x}: {:#010x}", flagsInit, currentValue1));

        DWORD expectedOld1 = 0x80;
        if (!PatchHelper::WriteDWORD(flagsInit, 0x10000080, &patchedLocations, &expectedOld1)) {
            return Fail(std::format("Failed to patch sub_404550 at {:#010x} - expected 0x80, found {:#010x}", flagsInit, currentValue1));
        }
        LOG_DEBUG("[CreateFileRandomAccessPatch] Patch 1 applied: 0x80 -> 0x10000080");

        uintptr_t branch = gameAddresses->randomAccessFlagBranch;

        // Patch 2: sub_4DB4C0 - force fallthrough to ensure random access flag is set
        // NOP out 2 bytes at `randomAccessFlagBranch` to skip conditional jump
        // Read and log current bytes for debugging
        BYTE currentByte1 = PatchHelper::ReadByte(branch);
        BYTE currentByte2 = PatchHelper::ReadByte(branch + 1);
        LOG_DEBUG(std::format("[CreateFileRandomAccessPatch] Current bytes at {:#010x}: {:#04x}; {:#04x}", branch, currentByte1, currentByte2));

        if (!PatchHelper::WriteNOP(branch, 2, &patchedLocations)) {
            // Restore first patch on failure to maintain clean state
            PatchHelper::RestoreAll(patchedLocations);
            isEnabled = false;  // Ensure we're in clean state
            return Fail(std::format("Failed to NOP sub_4DB4C0 at {:#010x} - partial install rolled back", branch));
        }
        LOG_DEBUG("[CreateFileRandomAccessPatch] Patch 2 applied: NOPed 2 bytes");

        isEnabled = true;
        LOG_INFO("[CreateFileRandomAccessPatch] Successfully installed both patches");
        return true;
    }
    
    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();  // Clear any previous error
        LOG_INFO("[CreateFileRandomAccessPatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        LOG_INFO("[CreateFileRandomAccessPatch] Successfully uninstalled");
        return true;
    }
};

// Register the patch
REGISTER_PATCH(CreateFileRandomAccessPatch, {
    .displayName = "CreateFileW Random Access",
    .description = "Adds FILE_FLAG_RANDOM_ACCESS to CreateFileW calls for better disk I/O performance",
    .category = "Performance",
    .experimental = false,
    .technicalDetails = {
        "Patch 1: Modifies sub_404550 at 0x404573 (DWORD: 0x80 -> 0x10000080)",
        "Patch 2: NOPs sub_4DB4C0 at 0x4DB56C (2 bytes) to force fallthrough",
        "Adds FILE_FLAG_RANDOM_ACCESS (0x10000000) to CreateFileW dwFlagsAndAttributes",
        "Improves file I/O performance by hinting random access pattern to Windows",
        "Credits to FoulPlay on discord for figuring this out! :D"
    }
})