#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../version.h"
#include "../memory_statistics.h"
#include <bit>
#include <functional>

// We're using `rep movsb` to minimise code size.
#define PascalStringCopy(buffer, string) (__movsb(reinterpret_cast<unsigned char*>(buffer), reinterpret_cast<const unsigned char*>(string) + 1, *(string)), (buffer) += *(string))
#define StringLiteralCopy(buffer, string) (__movsb(reinterpret_cast<unsigned char*>(buffer), reinterpret_cast<const unsigned char*>(string), sizeof(string) - 1), (buffer) += sizeof(string) - 1)

char* __fastcall FormatPageProtection(char buffer[108], uint32_t flags) {
    // The longest possible result of this function is:
    // PAGE_EXECUTE_READWRITE | PAGE_TARGETS_INVALID | PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE | 0x00000000

    char* c = buffer;

    const char* types[9] = {
        // clang-format off
        // we Pascal now
        "\x01""0",
        "\x08""NOACCESS",
        "\x08""READONLY",
        "\x09""READWRITE",
        "\x09""WRITECOPY",
        "\x07""EXECUTE",
        "\x0C""EXECUTE_READ",
        "\x11""EXECUTE_READWRITE",
        "\x11""EXECUTE_WRITECOPY",
        // clang-format on
    };

    static_assert(PAGE_EXECUTE_WRITECOPY == 0x80);

    DWORD typeIndex;
    _BitScanReverse(&typeIndex, ((flags & 0x7F) << 1) | 1);
    const char* type = types[typeIndex];

    StringLiteralCopy(c, "PAGE_");
    PascalStringCopy(c, type);

    flags &= ~0x7F;

    auto appendFlag = [&](uint32_t flag, const char* string) {
        if ((flags & flag) == 0) { return; }
        flags &= ~flag;
        StringLiteralCopy(c, " | ");
        PascalStringCopy(c, string);
    };

    appendFlag(PAGE_TARGETS_INVALID, "\x14PAGE_TARGETS_INVALID");
    appendFlag(PAGE_GUARD, "\x0APAGE_GUARD");
    appendFlag(PAGE_NOCACHE, "\x0CPAGE_NOCACHE");
    appendFlag(PAGE_WRITECOMBINE, "\x11PAGE_WRITECOMBINE");

    if (flags != 0) {
        c = std::format_to(c, " | 0x{:08x}", flags);
    }

    *c++ = '\0';

    return c;
}

char* __fastcall FormatDetailedMemoryReport(char buffer[2046], const DetailedMemoryReport& report) {
    char* c = buffer;

    constexpr uint8_t pageTypeColumnLength = 19;

    auto pageRow = [&](const char* label, uint32_t pageCount) __declspec(noinline) {
        *c++ = ' ';
        PascalStringCopy(c, label);
        uint8_t padding = (pageTypeColumnLength - 1) - *label;
        __stosb(reinterpret_cast<unsigned char*>(c), ' ', padding);
        c += padding;
        c = std::format_to(c, "| {: >7}", pageCount);
        float bytes = static_cast<float>(pageCount << pageSizeLog2);
        c = std::format_to(c, " |  {: >8.3f} MB", bytes / 1048576.0f);
        c = std::format_to(c, " |  {: >6.3f}%\r\n", bytes / addressSpace * 100.0f);
    };

    // clang-format off
    StringLiteralCopy(c, "     Page Type     |  Count  |  Total Size  |  Proportion of Address Space\r\n"
                         "-------------------+---------+--------------+------------------------------\r\n");
    pageRow("\x04""Free", report.freePageCount);
    pageRow("\x09""Committed", report.committedPageCount);
    pageRow("\x08""Reserved", report.reservedPageCount);
    pageRow("\x0E""Writeable Data", report.writeableDataPageCount);
    pageRow("\x0E""Read-only Data", report.readOnlyDataPageCount);
    pageRow("\x0F""Write-copy Data", report.writeCopyDataPageCount);
    pageRow("\x11""Inaccessible Data", report.inaccessibleDataPageCount);
    pageRow("\x0E""Writeable Code", report.writeableCodePageCount);
    pageRow("\x0E""Read-only Code", report.readOnlyCodePageCount);
    pageRow("\x0F""Write-copy Code", report.writeCopyCodePageCount);
    pageRow("\x11""Inaccessible Code", report.inaccessibleCodePageCount);
    pageRow("\x05""Guard", report.guardPageCount);
    pageRow("\x05""Image", report.imagePageCount);
    pageRow("\x06""Mapped", report.mappedPageCount);
    pageRow("\x07""Private", report.privatePageCount);
    // clang-format on

    StringLiteralCopy(c, "\r\nFree Span Histogram\r\n");

    uint32_t basedLevel = DetailedMemoryReport::freeSpanHistogramBaseLog2;

    for (uint32_t level = 0; level < DetailedMemoryReport::freeSpanHistogramLevels; ++level, ++basedLevel) {
        uint32_t freeSpanCount = report.freeSpanHistogram[level];
        uint32_t possibleFreeSpanCount = addressSpace >> basedLevel;
        uint32_t cascadingFreeSpanCount = freeSpanCount;

        for (uint32_t upperLevel = level + 1; upperLevel < DetailedMemoryReport::freeSpanHistogramLevels; ++upperLevel) { cascadingFreeSpanCount += report.freeSpanHistogram[upperLevel] << (upperLevel - level); }

        char prefix = basedLevel < 20 ? 'K' : (basedLevel < 30 ? 'M' : 'G');
        uint8_t shift = basedLevel < 20 ? 10 : (basedLevel < 30 ? 20 : 30);
        uint32_t size = 1U << (basedLevel - shift);

        c = std::format_to(c, "{: >3} {}B | {: >7} out of {: <7}  ^ {: >7}\r\n", size, prefix, freeSpanCount, possibleFreeSpanCount, cascadingFreeSpanCount);
    }

    *c++ = '\0';

    return c;
}

class ExpandedCrashLogs : public OptimizationPatch {
  private:
    // This is the code responsible for formatting access violations in a crash log.
    static inline const AddressInfo accessViolationFormattingAddressInfo = {.name = "ExpandedCrashLogs::accessViolationFormatting",
        .addresses =
            {
                {GameVersion::Retail, 0x004d5aa0},
                {GameVersion::Steam, 0x004d58b0},
                {GameVersion::EA, 0x004d5780},
            },
        .pattern = "8B 30 81 FE 05 00 00 C0 75 41 83 78 14 00",
        .patternOffset = 10};

    static constexpr uintptr_t offsetOfAccessViolationFormattingEpilogue = 61;

    // This is the call responsible for writing the command-line in a crash log.
    static inline const AddressInfo commandLineInCrashLogCallAddressInfo = {.name = "ExpandedCrashLogs::commandLineInCrashLogCall",
        .addresses =
            {
                {GameVersion::Retail, 0x0058e16e},
                {GameVersion::Steam, 0x0058dd5e},
                {GameVersion::EA, 0x0058d66e},
            },
        .pattern = "8B 00 50 68 ?? ?? ?? ?? 56 E8 ?? ?? ?? ?? 8B 0E 83 C4 0C 51 8B CF E8",
        .patternOffset = 9};

    // This is a function used to append a printf-style formatted string to some kind of fancy string.
    static inline const AddressInfo appendFormattedStringAddressInfo = {.name = "ExpandedCrashLogs::appendFormattedString",
        .addresses =
            {
                {GameVersion::Retail, 0x00567870},
                {GameVersion::Steam, 0x00567460},
                {GameVersion::EA, 0x00567000},
            },
        .pattern = "8B 4C 24 08 56 8B 74 24 08 8D 44 24 10 50 51 8B CE E8 ?? ?? ?? ?? 8B C6 5E C3 CC CC CC CC CC CC 53 8B 5C 24 08"};

    // This is the setup for the first call after the block responsible for writing the "Extra" section in a crash log.
    static inline const AddressInfo callSetupAfterExtraSectionInCrashLogAddressInfo = {.name = "ExpandedCrashLogs::callSetupAfterExtraSectionInCrashLog",
        .addresses =
            {
                {GameVersion::Retail, 0x004d722c},
                {GameVersion::Steam, 0x004d6ffc},
                {GameVersion::EA, 0x004d6ebc},
            },
        .pattern = "8B 65 E8 8B 75 E4 81 C6 10 05 00 00 8B 0E 8B 01 6A 01 68 ?? ?? ?? ?? 8B 50 20 FF D2 C7 45 FC FF FF FF FF",
        .patternOffset = 39};

    // This signature is fine for our needs.
    static inline uint32_t (*resolvedAppendFormattedString)(uintptr_t object, const char* format, uintptr_t argument);

    static inline uintptr_t endOfExceptionReportSectionsChain = 0;

    std::vector<PatchHelper::PatchLocation> patchedLocations;

    static uint32_t WriteCommandLineHook(uintptr_t object, const char* format, uintptr_t argument) {
        resolvedAppendFormattedString(object, format, argument);
        return resolvedAppendFormattedString(object, "S3SSVersion: %hs\r\n", reinterpret_cast<uintptr_t>(S3SS_VERSION_STRING));
    }

    static uint32_t __fastcall FormatAccessViolation(uint32_t eax, uint32_t esp) {
        const EXCEPTION_RECORD* exceptionRecord = reinterpret_cast<const EXCEPTION_RECORD*>(eax);

        char* buffer = *reinterpret_cast<char* const*>(esp + 8);
        uint32_t size = *reinterpret_cast<const uint32_t*>(esp + 12);

        char* c = buffer;

        // This won't happen as the callee supplies a 256-byte buffer, but let's not tempt fate.
        if (size < 213) {
            if (size != 0) { *c = '\0'; }
            return 0;
        }

        StringLiteralCopy(c, "ACCESS_VIOLATION ");

        uint32_t access = exceptionRecord->ExceptionInformation[0];
        const char* accessViolationType = access == 0 ? "\x0Creading from"
                                                      : (access == 1 ? "\x0Awriting to"
                                                                     : (access == 8 ? "\x17"
                                                                                      "from a DEP violation at"
                                                                                    : "\x15of an unknown type at"));
        PascalStringCopy(c, accessViolationType);

        uint32_t violatedAddress = exceptionRecord->ExceptionInformation[1];

        c = std::format_to(c, " address 0x{:08x}", violatedAddress);

        // Windows never maps memory at the first or last 64 KB of the address-space.
        if ((violatedAddress < (64 << 10)) | (violatedAddress >= static_cast<uint32_t>(-(64 << 10)))) {
            const char* description = violatedAddress == 0 ? "\x07 (NULL)" : "\x15 (Unmappable address)";
            PascalStringCopy(c, description);
        } else {
            MEMORY_BASIC_INFORMATION memoryInfo;
            VirtualQuery(reinterpret_cast<const void*>(violatedAddress), &memoryInfo, sizeof(memoryInfo));

            StringLiteralCopy(c, "; State: ");

            uint32_t state = memoryInfo.State;
            const char* memoryState = state == MEM_COMMIT ? "\x0AMEM_COMMIT" : (state == MEM_RESERVE ? "\x0BMEM_RESERVE" : "\x08MEM_FREE");
            PascalStringCopy(c, memoryState);

            if ((state == MEM_COMMIT) | (state == MEM_RESERVE)) {
                StringLiteralCopy(c, "; Type: ");

                uint32_t type = memoryInfo.Type;
                const char* memoryType = type == MEM_PRIVATE ? "\x0BMEM_PRIVATE" : (type == MEM_MAPPED ? "\x0AMEM_MAPPED" : "\x09MEM_IMAGE");
                PascalStringCopy(c, memoryType);

                if (state == MEM_COMMIT) {
                    StringLiteralCopy(c, "; Protection: ");
                    c = FormatPageProtection(c, memoryInfo.Protect);
                }
            }
        }

        *c = '\0';

        return c - buffer;
    }

    struct CrashLogObject {
        struct VTable {
            uintptr_t unknown0;
            uintptr_t unknown1;
            uintptr_t unknown2;
            uintptr_t unknown3;
            uintptr_t unknown4;
            uintptr_t unknown5;
            uintptr_t openSection;
            uintptr_t closeSection;
            uintptr_t writeLine;
        };

        const VTable* vtable;

        void AcceptString(const char*) {}

        void HookedEndOfExceptionReportSections() {
            WriteS3SSSectionsInCrashLog();

            uintptr_t nextInChain;

            if (endOfExceptionReportSectionsChain == 0) {
                nextInChain = this->vtable->unknown5;
            } else {
                nextInChain = endOfExceptionReportSectionsChain;
            }

            // I hate __thiscall and it hates me!
            std::mem_fn(std::bit_cast<decltype(&CrashLogObject::HookedEndOfExceptionReportSections)>(nextInChain))(this);
        }

        void WriteS3SSSectionsInCrashLog() {
            __try {
                const VTable* vtable = this->vtable;

                std::mem_fn(std::bit_cast<decltype(&CrashLogObject::AcceptString)>(vtable->writeLine))(this, "");
                std::mem_fn(std::bit_cast<decltype(&CrashLogObject::AcceptString)>(vtable->openSection))(this, "S3SS memory statistics");

                char buffer[2048];
                DetailedMemoryReport memoryReport;
                memoryReport.FillIn();

                buffer[0] = '\r';
                buffer[1] = '\n';
                FormatDetailedMemoryReport(buffer + 2, memoryReport);
                std::mem_fn(std::bit_cast<decltype(&CrashLogObject::AcceptString)>(vtable->writeLine))(this, buffer);

                std::mem_fn(std::bit_cast<decltype(&CrashLogObject::AcceptString)>(vtable->closeSection))(this, "S3SS memory statistics");
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                __try {
                    std::mem_fn(std::bit_cast<decltype(&CrashLogObject::AcceptString)>(this->vtable->writeLine))(this, "<An exception was encountered while writing this section.>");
                } __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    };

    struct BigExceptionReportStructure {
        const uintptr_t* vtable;
    };

  public:
    ExpandedCrashLogs() : OptimizationPatch("ExpandedCrashLogs", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[ExpandedCrashLogs] Installing...");

        auto accessViolationFormattingAddress = accessViolationFormattingAddressInfo.Resolve();
        if (!accessViolationFormattingAddress) { return Fail("Could not resolve accessViolationFormatting address"); }
        uintptr_t accessViolationFormatting = *accessViolationFormattingAddress;

        auto callSetupAfterExtraSectionInCrashLogAddress = callSetupAfterExtraSectionInCrashLogAddressInfo.Resolve();
        if (!callSetupAfterExtraSectionInCrashLogAddress) { return Fail("Could not resolve callSetupAfterExtraSectionInCrashLog address"); }
        uintptr_t callSetupAfterExtraSectionInCrashLog = *callSetupAfterExtraSectionInCrashLogAddress;

        auto commandLineInCrashLogCallAddress = commandLineInCrashLogCallAddressInfo.Resolve();
        if (!commandLineInCrashLogCallAddress) { return Fail("Could not resolve commandLineInCrashLogCall address"); }
        uintptr_t commandLineInCrashLogCall = *commandLineInCrashLogCallAddress;

        auto appendFormattedStringAddress = appendFormattedStringAddressInfo.Resolve();
        if (!appendFormattedStringAddress) { return Fail("Could not resolve appendFormattedString address"); }
        uintptr_t appendFormattedString = *appendFormattedStringAddress;

        resolvedAppendFormattedString = reinterpret_cast<decltype(resolvedAppendFormattedString)>(appendFormattedString);

        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();

        // clang-format off
        uint8_t formatAccessViolationCall[16] = {
            0x3E, 0x8B, 0310,             // ds: mov ecx, eax
            0x3E, 0x8B, 0324,             // ds: mov edx, esp
        //6:
            0x68, 0x77, 0x77, 0x77, 0x77, // push returnAddress
        //11:
            0xE9, 0x77, 0x77, 0x77, 0x77, // jmp FormatAccessViolation
        };
        // clang-format on

        uintptr_t returnAddress = accessViolationFormatting + offsetOfAccessViolationFormattingEpilogue;
        std::memcpy(formatAccessViolationCall + 6 + 1, &returnAddress, 4);

        uintptr_t formatAccessViolationCallDisplacement = PatchHelper::CalculateRelativeOffset(accessViolationFormatting + 11, reinterpret_cast<uintptr_t>(&FormatAccessViolation));
        std::memcpy(formatAccessViolationCall + 11 + 1, &formatAccessViolationCallDisplacement, 4);

        uintptr_t writeCommandLineHookCallDisplacement = PatchHelper::CalculateRelativeOffset(commandLineInCrashLogCall, reinterpret_cast<uintptr_t>(&WriteCommandLineHook));

        // There isn't a nice place to hijack the end of the exception-report's sections,
        // and it's plausible that other patches may want to hook this as well (S3MM, maybe).
        // To accommodate compatibility, we'll check to see if this has already been patched
        // to call something: if it has, we'll make note of the address being called,
        // and we'll thus call it ourselves in our hook.
        // Otherwise, if it isn't a call we'll call the virtual-function that the game's original code
        // was going to call.
        // If other patches hooking this spot do the same thing, then they can all hold hands and sing Kumbaya.
        if (*reinterpret_cast<const uint8_t*>(callSetupAfterExtraSectionInCrashLog) == 0xE8) {
            int32_t displacement;
            std::memcpy(&displacement, reinterpret_cast<const uint8_t*>(callSetupAfterExtraSectionInCrashLog + 1), 4);
            endOfExceptionReportSectionsChain = callSetupAfterExtraSectionInCrashLog + 5 + displacement;
        } else {
            endOfExceptionReportSectionsChain = 0;
        }

        successful &= PatchHelper::WriteProtectedMemory(reinterpret_cast<void*>(accessViolationFormatting), formatAccessViolationCall, 16, &tx.locations);
        successful &= PatchHelper::WriteDWORD(commandLineInCrashLogCall + 1, writeCommandLineHookCallDisplacement, &tx.locations);
        successful &= PatchHelper::WriteRelativeCall(callSetupAfterExtraSectionInCrashLog, std::bit_cast<uintptr_t>(&CrashLogObject::HookedEndOfExceptionReportSections), &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

        isEnabled = true;
        LOG_INFO("[ExpandedCrashLogs] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[ExpandedCrashLogs] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            isEnabled = true;
            return Fail("Failed to restore original code");
        }

        isEnabled = false;
        LOG_INFO("[ExpandedCrashLogs] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(
    ExpandedCrashLogs, {.displayName = "Expanded Crash Logs",
                           .description = "Expands the game's crash log writer to include more information.",
                           .category = "Diagnostic",
                           .experimental = false,
                           .supportedVersions = VERSION_ALL,
                           .technicalDetails = {
                               "This patch was authored by \"Just Harry\".",
                               "S3SS's version is logged in the [Build info] section.",
                               "Access violations are logged with more detail: the state of the memory at the faulting address is logged; DEP violations are handled properly and will be mentioned as such if they occur.",
                               "Detailed statistics about the state of the process's virtual-memory are logged in a new [S3SS memory statistics] section after the [Extra] section.",
                           }})
