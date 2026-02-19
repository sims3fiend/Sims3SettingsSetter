#include "memory_statistics.h"
#include <intrin.h>
#include <Windows.h>

uint32_t DetailedMemoryReport::FillIn() {
    __stosb(reinterpret_cast<unsigned char*>(this), 0, sizeof(*this));

    MEMORY_BASIC_INFORMATION info;

    // Windows never maps memory at the first or last 64 KB of the address-space.
    uintptr_t address = 64 << 10;

    goto initialIteration;

    for (;;) {
        if (address >= static_cast<uintptr_t>(-(64 << 10))) { break; }
    initialIteration:
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) { return GetLastError(); }

        address = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;

        size_t pageCount = info.RegionSize >> pageSizeLog2;

        if (info.State == MEM_FREE) {
            DWORD regionSizeLog2;
            _BitScanReverse(&regionSizeLog2, info.RegionSize);

            ++freeSpanHistogram[regionSizeLog2 - pageSizeLog2];
            freePageCount += pageCount;
        } else {
            static_assert(offsetof(DetailedMemoryReport, imagePageCount) == offsetof(DetailedMemoryReport, mappedPageCount) + 4);
            static_assert(offsetof(DetailedMemoryReport, privatePageCount) == offsetof(DetailedMemoryReport, mappedPageCount) + 8);

            uint32_t* typePageCount = &mappedPageCount;
            typePageCount += info.Type != MEM_MAPPED;
            typePageCount += info.Type == MEM_PRIVATE;

            *typePageCount += pageCount;

            if (info.State == MEM_RESERVE) {
                reservedPageCount += pageCount;
            } else {
                guardPageCount += (info.Protect & PAGE_GUARD) != 0 ? pageCount : 0;

                static_assert(PAGE_EXECUTE_WRITECOPY == 0x80);

                uint32_t protection = info.Protect & 0x7F;

                committedPageCount += pageCount;

                if (protection != 0) {
                    DWORD protectionBit;
                    _BitScanForward(&protectionBit, protection);
                    pageCountByProtection[protectionBit] += pageCount;
                }
            }
        }
    }

    return 0;
}
