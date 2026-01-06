#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../optimization.h"
#include <windows.h>
#include <cstdint>
#include <vector>
#include <immintrin.h>
// my magnum opus I fear

// There are probably other optimisations (PGO??? idk what that is but sure) but for now I am never touching this again ever
class RefPackDecompressorPatch : public OptimizationPatch {
private:
    static inline const AddressInfo refPackDecompressor = {
        .name = "RefPackDecompressor::hookAddr",
        .addresses = {
            {GameVersion::Retail, 0x004eb900},
            {GameVersion::Steam,  0x004eb3b0},
            {GameVersion::EA,     0x004eb4f0},
        },
        .pattern = "83 EC 10 8B 4C 24 1C 85 C9 53 55 56 8B 74 24 20 57 C7 44 24 1C 00 00 00 00 0F 84",
        .expectedBytes = {0x83, 0xEC, 0x10, 0x8B, 0x4C, 0x24, 0x1C},
    };

    std::vector<PatchHelper::PatchLocation> patchedLocations;
    static RefPackDecompressorPatch* instance;
    static bool cpuHasAVX2;

    // Strategy structs, compiler inlines these directly into template instantiations instead of causing indirect jump if we did function pointer h-haha... Totally worth it...
    // We use these because some people can't use AVX2 :( BOOOOOOOOOOOOOOOOO
    struct StrategySSE2 {
        static __forceinline void Copy(uint8_t* dst, const uint8_t* src, uint32_t len) {
            uint8_t* d = dst;
            const uint8_t* s = src;
            while (len >= 16) {
                _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s));
                d += 16; s += 16; len -= 16;
            }
            while (len--) *d++ = *s++;
        }
    };

    struct StrategyAVX2 {
        static __forceinline void Copy(uint8_t* dst, const uint8_t* src, uint32_t len) {
            uint8_t* d = dst;
            const uint8_t* s = src;
            // 32-byte chunks (AVX2)
            while (len >= 32) {
                _mm256_storeu_si256((__m256i*)d, _mm256_loadu_si256((const __m256i*)s));
                d += 32; s += 32; len -= 32;
            }
            // 16-byte chunks (SSE2)
            while (len >= 16) {
                _mm_storeu_si128((__m128i*)d, _mm_loadu_si128((const __m128i*)s));
                d += 16; s += 16; len -= 16;
            }
            // Tail
            while (len--) *d++ = *s++;
        }
    };

    template <typename Strategy>
    static __forceinline void CopyOverlapping(uint8_t* dst, const uint8_t* src, uint32_t len) {
        ptrdiff_t offset = dst - src;

        // For RLE (offset < len), src is 'history' relative to dst
        // As long as offset >= 32, the 32-byte load reads data that was written at least 32 bytes ago
        // Since we write 32 bytes at a time, we never overwrite data we are about to read in the SAME iteration... we pray
        if (offset >= 32) {
             Strategy::Copy(dst, src, len);
        }
        else if (offset >= 16) {
            while (len >= 16) {
                _mm_storeu_si128((__m128i*)dst, _mm_loadu_si128((const __m128i*)src));
                dst += 16; src += 16; len -= 16;
            }
            while (len--) *dst++ = *src++;
        }
        else {
            // Tight byte-copy loop for small overlaps
            while (len--) *dst++ = *src++;
        }
    }

    template <typename Strategy>
    static int DecompressImpl(
        uint8_t* __restrict dst, uint32_t dstSize,
        uint8_t* __restrict src, uint32_t srcSize
    ) {
        // Basic sanity checks
        if (!dst || !src || srcSize < 2) return 0;

        uint8_t* dstStart = dst;
        uint8_t* dstEnd = dst + dstSize;
        uint8_t* srcEnd = src + srcSize;

        // Header parsing + validation
        // We MUST validate the header to ensure we don't overrun input or output
        uint16_t header = (src[0] << 8) | src[1];
        src += 2;
        // srcSize tracked implicitly by pointer comparison now, but we need to check initial header read
        // (Checked by srcSize < 2 above)

        uint32_t skipBytes = 0;
        uint32_t sizeBytes = 3;

        if (header & 0x8000) {
            skipBytes = (header & 0x100) ? 4 : 0;
            sizeBytes = 4;
        } else {
            skipBytes = (header & 0x100) ? 3 : 0;
            sizeBytes = 3;
        }

        // Validate header read bounds
        if (src + skipBytes + sizeBytes > srcEnd) return 0;

        src += skipBytes;

        // Read expected size to validate against buffer size
        uint32_t expectedSize = 0;
        if (sizeBytes == 4) {
            expectedSize = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];
            src += 4;
        } else {
            expectedSize = (src[0] << 16) | (src[1] << 8) | src[2];
            src += 3;
        }

        // Validate output buffer size
        // If the caller provided a buffer smaller than the decompressed size, fail asap
        if (expectedSize > dstSize) {
            // Log error? For now just return 0 to match original behavior on error, really doubt this ever gets hit anyway
            return 0;
        }

        // main loop
        while (src < srcEnd) {
            // Ensure we can read at least the command byte
            if (src >= srcEnd) break;

            uint8_t cmd = *src++;

            // Type 1: Short Backref (0x00-0x7F)
            if (cmd < 0x80) {
                if (src >= srcEnd) goto error_truncated;
                uint8_t offset_low = *src++;

                uint32_t literal_len = cmd & 0x3;
                uint32_t match_len = ((cmd >> 2) & 0x7) + 3;
                uint32_t offset = (((cmd & 0x60) << 3) | offset_low) + 1;

                // Literal Copy
                if (literal_len) {
                    // Bounds check: input and output
                    if (src + literal_len > srcEnd || dst + literal_len > dstEnd) goto error_overflow;

                    dst[0] = src[0];
                    if (literal_len > 1) dst[1] = src[1];
                    if (literal_len > 2) dst[2] = src[2];

                    dst += literal_len; src += literal_len;
                }

                // Backref Copy
                // Bounds check: output and backref source validity
                // Check (dst - offset < dstStart) to prevent reading before the buffer, could potentially skip but risky...
                if (dst - offset < dstStart || dst + match_len > dstEnd) goto error_overflow;

                CopyOverlapping<Strategy>(dst, dst - offset, match_len);
                dst += match_len;
            }
            // Type 2: Medium Backref (0x80-0xBF)
            else if (!(cmd & 0x40)) {
                if (src + 2 > srcEnd) goto error_truncated;
                uint8_t offset_high = *src++;
                uint8_t offset_low = *src++;

                uint32_t literal_len = offset_high >> 6;
                uint32_t match_len = (cmd & 0x3F) + 4;
                uint32_t offset = (((offset_high & 0x3F) << 8) | offset_low) + 1;

                if (literal_len) {
                    if (src + literal_len > srcEnd || dst + literal_len > dstEnd) goto error_overflow;
                    dst[0] = src[0];
                    if (literal_len > 1) dst[1] = src[1];
                    if (literal_len > 2) dst[2] = src[2];
                    dst += literal_len; src += literal_len;
                }

                if (dst - offset < dstStart || dst + match_len > dstEnd) goto error_overflow;
                CopyOverlapping<Strategy>(dst, dst - offset, match_len);
                dst += match_len;
            }
            // Type 3: Long Backref (0xC0-0xDF)
            else if (!(cmd & 0x20)) {
                if (src + 3 > srcEnd) goto error_truncated;
                uint8_t b2 = *src++;
                uint8_t b3 = *src++;
                uint8_t b4 = *src++;

                uint32_t literal_len = cmd & 0x3;
                uint32_t match_len = (((cmd & 0xC) << 6) + b4) + 5;
                uint32_t offset = (((cmd & 0x10) << 12) | (b2 << 8) | b3) + 1;

                if (literal_len) {
                    if (src + literal_len > srcEnd || dst + literal_len > dstEnd) goto error_overflow;
                    dst[0] = src[0];
                    if (literal_len > 1) dst[1] = src[1];
                    if (literal_len > 2) dst[2] = src[2];
                    dst += literal_len; src += literal_len;
                }

                if (dst - offset < dstStart || dst + match_len > dstEnd) goto error_overflow;
                CopyOverlapping<Strategy>(dst, dst - offset, match_len);
                dst += match_len;
            }
            // Type 4: Literal Run / Stop (0xE0-0xFF)
            else {
                uint32_t len = ((cmd & 0x1F) << 2) + 4;

                if (len > 112) {
                    len = cmd & 0x3; // Stop code
                    // Handle tail bytes
                    if (len) {
                        if (src + len > srcEnd || dst + len > dstEnd) goto error_overflow;
                        dst[0] = src[0];
                        if (len > 1) dst[1] = src[1];
                        if (len > 2) dst[2] = src[2];
                        dst += len;
                    }
                    break;
                }

                if (src + len > srcEnd || dst + len > dstEnd) goto error_overflow;
                Strategy::Copy(dst, src, len);
                dst += len; src += len;
            }
        }
        return (int)(dst - dstStart); //original returns expectedSize from header, we return actual bytes written, IMO this is better and shouldn't cause issues... h-haha...

    error_truncated:
    error_overflow:
        return 0;
    }

    static int __cdecl Dispatch(
        uint8_t* dst, uint32_t dstSize,
        uint8_t* src, uint32_t srcSize
    ) {
        if (cpuHasAVX2) {
            return DecompressImpl<StrategyAVX2>(dst, dstSize, src, srcSize);
        } else {
            return DecompressImpl<StrategySSE2>(dst, dstSize, src, srcSize);
        }
    }

public:
    RefPackDecompressorPatch() : OptimizationPatch("RefPackDecompressor", nullptr) {
        instance = this;
    }

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();

        auto addr = refPackDecompressor.Resolve();
        if (!addr) {
            return Fail("Could not resolve RefPack decompressor address");
        }

        const auto& cpuFeatures = CPUFeatures::Get();
        cpuHasAVX2 = cpuFeatures.hasAVX2;

        if (cpuHasAVX2) {
            LOG_INFO("[RefPackDecompressor] Installing optimized decompressor (AVX2 + Safety Checks)...");
        } else {
            LOG_INFO("[RefPackDecompressor] Installing optimized decompressor (SSE2 + Safety Checks)...");
        }

        uintptr_t targetAddr = (uintptr_t)&Dispatch;

        if (!PatchHelper::WriteRelativeJump(*addr, targetAddr, &patchedLocations)) {
            return Fail(std::format("Failed to install decompressor hook at {:#010x}", *addr));
        }

        isEnabled = true;
        LOG_INFO("[RefPackDecompressor] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[RefPackDecompressor] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original decompressor");
        }

        isEnabled = false;
        LOG_INFO("[RefPackDecompressor] Successfully uninstalled");
        return true;
    }
};

// Static member init
RefPackDecompressorPatch* RefPackDecompressorPatch::instance = nullptr;
bool RefPackDecompressorPatch::cpuHasAVX2 = false;

REGISTER_PATCH(RefPackDecompressorPatch, {
    .displayName = "RefPack Decompressor Optimization",
    .description = "Highly optimized RefPack decompression using AVX2/SSE2 intrinsics with safety checks. Auto-detects CPU capabilities.",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = VERSION_ALL,
    .technicalDetails = {
        "Replaces original RefPack decompressor entirely",
        "AVX2 path for modern CPUs, SSE2 fallback for older ones",
        "Optimizes quite a significant amount, probably one of the most impactful patches",
        "Essentially decompression/reading .package files big faster now yes :D yipeee"
    }
})
