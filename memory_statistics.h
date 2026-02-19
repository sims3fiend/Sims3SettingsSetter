#pragma once
#include <stdint.h>

// Let me know when Microsoft decides to break all x86 Windows software.
constexpr uint32_t pageSizeLog2 = 12;
constexpr uint32_t addressSpace = static_cast<uint32_t>(-(128 << 10));

struct DetailedMemoryReport {
    static constexpr uint32_t freeSpanHistogramBaseLog2 = 12;
    static constexpr uint32_t freeSpanHistogramLevels = 20;

    uint32_t freePageCount;
    uint32_t reservedPageCount;
    uint32_t committedPageCount;

    union {
        struct {
            uint32_t inaccessibleDataPageCount; // PAGE_NOACCESS
            uint32_t readOnlyDataPageCount;     // PAGE_READONLY
            uint32_t writeableDataPageCount;    // PAGE_READWRITE
            uint32_t writeCopyDataPageCount;    // PAGE_WRITECOPY
            uint32_t inaccessibleCodePageCount; // PAGE_EXECUTE
            uint32_t readOnlyCodePageCount;     // PAGE_EXECUTE_READ
            uint32_t writeableCodePageCount;    // PAGE_EXECUTE_READWRITE
            uint32_t writeCopyCodePageCount;    // PAGE_EXECUTE_WRITECOPY
        };
        uint32_t pageCountByProtection[8];
    };

    uint32_t guardPageCount;
    uint32_t mappedPageCount;
    uint32_t imagePageCount;
    uint32_t privatePageCount;
    uint32_t freeSpanHistogram[freeSpanHistogramLevels];

    uint32_t FillIn();
};
