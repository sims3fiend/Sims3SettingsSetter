#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

// CreateFileW Random Access patch - adds FILE_FLAG_RANDOM_ACCESS to main callers of CreateFileW, since sims 3 uses dbpf it makes more sense to hint random than sequential
class CreateFileRandomAccessPatch : public OptimizationPatch {
  private:
    // Address definitions
    static inline const AddressInfo createFileWFlagsInit = {
        .name = "CreateFileW::flagsInit",
        .addresses =
            {
                {GameVersion::Retail, 0x00404563},
                {GameVersion::Steam, 0x00404573},
                {GameVersion::EA, 0x00404563},
            },
        .pattern = "24 08 80 00 00 00 74 ?? C7 44 24 0C 00 00 00 C0 8B C2 83 E0 06 83 E8 02 74 ?? 83 E8 02",
        .patternOffset = 2,                        // Skip "24 08" to get to the 0x80 DWORD
        .expectedBytes = {0x80, 0x00, 0x00, 0x00}, // DWORD 0x80
    };

    static inline const AddressInfo randomAccessFlagBranch = {
        .name = "CreateFileW::randomAccessBranch",
        .addresses =
            {
                {GameVersion::Retail, 0x004db73c},
                {GameVersion::Steam, 0x004db56c},
                {GameVersion::EA, 0x004db51c},
            },
        .pattern = "74 ?? BD 00 00 00 10 6A 00 55 50 6A 00 52 8B 54 24 24 52 8D 46 08 50 FF 15 ?? ?? ?? ?? 83 F8 FF 89 46 04 75 ?? FF 15 ?? ?? ?? ??",
        .expectedBytes = {0x74}, // JZ instruction
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    CreateFileRandomAccessPatch() : OptimizationPatch("CreateFileRandomAccess", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[CreateFileRandomAccessPatch] Installing dual patch...");

        // Resolve addresses
        auto addr1 = createFileWFlagsInit.Resolve();
        auto addr2 = randomAccessFlagBranch.Resolve();

        if (!addr1 || !addr2) { return Fail("Could not resolve required addresses"); }

        // Patch 1: add FILE_FLAG_RANDOM_ACCESS (0x10000000) to flags
        // Change DWORD: 0x80 -> 0x10000080
        DWORD currentValue1 = PatchHelper::ReadDWORD(*addr1);
        LOG_DEBUG(std::format("[CreateFileRandomAccessPatch] Current value at {:#010x}: {:#x}", *addr1, currentValue1));

        DWORD expectedOld1 = 0x80;
        if (!PatchHelper::WriteDWORD(*addr1, 0x10000080, &patchedLocations, &expectedOld1)) { return Fail(std::format("Failed to patch at {:#010x} - expected 0x80, found {:#x}", *addr1, currentValue1)); }
        LOG_DEBUG("[CreateFileRandomAccessPatch] Patch 1 applied: 0x80 -> 0x10000080");

        // Patch 2: NOP out 2 bytes to skip conditional jump
        BYTE currentByte1 = PatchHelper::ReadByte(*addr2);
        BYTE currentByte2 = PatchHelper::ReadByte(*addr2 + 1);
        LOG_DEBUG(std::format("[CreateFileRandomAccessPatch] Current bytes at {:#010x}: {:#x} {:#x}", *addr2, currentByte1, currentByte2));

        if (!PatchHelper::WriteNOP(*addr2, 2, &patchedLocations)) {
            PatchHelper::RestoreAll(patchedLocations);
            isEnabled = false;
            return Fail(std::format("Failed to NOP at {:#010x} - partial install rolled back", *addr2));
        }
        LOG_DEBUG("[CreateFileRandomAccessPatch] Patch 2 applied: NOPed 2 bytes");

        isEnabled = true;
        LOG_INFO("[CreateFileRandomAccessPatch] Successfully installed both patches");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[CreateFileRandomAccessPatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original bytes"); }

        isEnabled = false;
        LOG_INFO("[CreateFileRandomAccessPatch] Successfully uninstalled");
        return true;
    }
};

// Register the patch
REGISTER_PATCH(CreateFileRandomAccessPatch, {.displayName = "CreateFileW Random Access",
                                                .description = "Adds FILE_FLAG_RANDOM_ACCESS to CreateFileW calls for better disk I/O performance",
                                                .category = "Performance",
                                                .experimental = false,
                                                .supportedVersions = VERSION_ALL,
                                                .technicalDetails = {"Adds FILE_FLAG_RANDOM_ACCESS (0x10000000) to CreateFileW dwFlagsAndAttributes",
                                                    "Improves file I/O performance by hinting random access pattern to Windows", "Credits to FoulPlay on discord for figuring this out! :D"}})
