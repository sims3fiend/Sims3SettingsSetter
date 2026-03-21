#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

//TODO: Diffuse/Specular probes, maybe move settings stuff into here, do something with window lighting specifically, etc. idk
// https://www.youtube.com/watch?v=XZeCXoUP4tM file theme

// Hook FinalizePrime to blur floor/ceiling/object locked textures before GPU commit.
// LightingRoom offsets: +0x04 LODParams*, +0xF4 LOD index,
// floor lock at +0x1C4/+0x1C8, ceiling at +0x2B4/+0x2B8, object at +0x3A4/+0x3A8.
typedef void(__fastcall* FinalizePrime_t)(void* thisPtr, void* edx);
static FinalizePrime_t originalFinalizePrime = nullptr;
static int g_floorCeilBlurPasses = 0;

typedef void(__thiscall* GetVisibleRes_t)(void* lodParams, int* outWidth, int* outHeight, char flag);
static GetVisibleRes_t getVisibleResFunc = nullptr;

// Bidirectional box blur on locked ARGB8 texture, 4 sweeps per pass.
static void BlurLockedTexture(BYTE* pixels, int pitch, int width, int height, int passes) {
    if (!pixels || width <= 1 || height <= 1 || passes <= 0) return;

    for (int pass = 0; pass < passes; pass++) {
        // Horizontal left-to-right
        for (int y = 0; y < height; y++) {
            DWORD* row = reinterpret_cast<DWORD*>(pixels + y * pitch);
            for (int x = 0; x < width - 1; x++) {
                DWORD a = row[x], b = row[x + 1];
                row[x] = (a | b) - (((a ^ b) >> 1) & 0x7F7F7F7F);
            }
        }
        // Horizontal right-to-left
        for (int y = 0; y < height; y++) {
            DWORD* row = reinterpret_cast<DWORD*>(pixels + y * pitch);
            for (int x = width - 1; x > 0; x--) {
                DWORD a = row[x], b = row[x - 1];
                row[x] = (a | b) - (((a ^ b) >> 1) & 0x7F7F7F7F);
            }
        }
        // Vertical top-to-bottom
        for (int y = 0; y < height - 1; y++) {
            DWORD* row0 = reinterpret_cast<DWORD*>(pixels + y * pitch);
            DWORD* row1 = reinterpret_cast<DWORD*>(pixels + (y + 1) * pitch);
            for (int x = 0; x < width; x++) {
                DWORD a = row0[x], b = row1[x];
                row0[x] = (a | b) - (((a ^ b) >> 1) & 0x7F7F7F7F);
            }
        }
        // Vertical bottom-to-top
        for (int y = height - 1; y > 0; y--) {
            DWORD* row0 = reinterpret_cast<DWORD*>(pixels + y * pitch);
            DWORD* row1 = reinterpret_cast<DWORD*>(pixels + (y - 1) * pitch);
            for (int x = 0; x < width; x++) {
                DWORD a = row0[x], b = row1[x];
                row0[x] = (a | b) - (((a ^ b) >> 1) & 0x7F7F7F7F);
            }
        }
    }
}

static void __fastcall HookedFinalizePrime(void* thisPtr, void* edx) {
    if (g_floorCeilBlurPasses > 0 && getVisibleResFunc) {
        auto base = reinterpret_cast<uintptr_t>(thisPtr);
        int lod = *reinterpret_cast<int*>(base + 0xF4);
        void* lodParams = reinterpret_cast<void*>(*reinterpret_cast<uintptr_t*>(base + 0x04) + lod * 0x40);

        int visW = 0, visH = 0;
        getVisibleResFunc(lodParams, &visW, &visH, '\0');

        if (visW > 0 && visH > 0) {
            BYTE* floorData = *reinterpret_cast<BYTE**>(base + 0x1C4);
            int floorPitch = *reinterpret_cast<int*>(base + 0x1C8);
            if (floorData && floorPitch > 0) { BlurLockedTexture(floorData, floorPitch, visW, visH, g_floorCeilBlurPasses); }

            BYTE* ceilData = *reinterpret_cast<BYTE**>(base + 0x2B4);
            int ceilPitch = *reinterpret_cast<int*>(base + 0x2B8);
            if (ceilData && ceilPitch > 0) { BlurLockedTexture(ceilData, ceilPitch, visW, visH, g_floorCeilBlurPasses); }

            BYTE* objData = *reinterpret_cast<BYTE**>(base + 0x3A4);
            int objPitch = *reinterpret_cast<int*>(base + 0x3A8);
            if (objData && objPitch > 0) { BlurLockedTexture(objData, objPitch, visW, visH, g_floorCeilBlurPasses); }
        }
    }

    originalFinalizePrime(thisPtr, edx);
}

// Hook LightPointWithAllLights to jitter texel position and average results, this is probably dumb but it does something so whatever
// LightSurfaceData (0x30): +0x00 mWorldPosition float[4], +0x10 mWorldNormal float[4],
// +0x20 mLightmapX ushort, +0x22 mLightmapY ushort.
typedef float*(__thiscall* LightPointWithAllLights_t)(void*, float*, void*, void*, void*, void*);
static LightPointWithAllLights_t originalLPWAL = nullptr;

static int g_shadowSampleCount = 1;
static float g_shadowJitterRadius = 0.5f;
static bool g_adaptiveSampling = false;

// Patched into the FLD in RayCheckOccluders2D (default 0.5).
static float g_fuzzyEdgeWidth = 0.5f;

// Per-texel rotation hash to "decorrelate" (new word) jitter pattern between adjacent texels.
static inline float TexelRotationAngle(unsigned short lmX, unsigned short lmY) {
    unsigned int h = static_cast<unsigned int>(lmX) * 0x45d9f3b + static_cast<unsigned int>(lmY) * 0x119de1f3;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = ((h >> 16) ^ h) * 0x45d9f3b;
    h = (h >> 16) ^ h;
    constexpr float kTwoPiOver65536 = 6.2831853f / 65536.0f;
    return static_cast<float>(h & 0xFFFF) * kTwoPiOver65536;
}

// 4-sample: rotated grid
static constexpr float kJitterPattern4[][2] = {
    {-0.375f, -0.125f},
    {0.125f, -0.375f},
    {0.375f, 0.125f},
    {-0.125f, 0.375f},
};
// 8-sample: two interleaved rotated grids
static constexpr float kJitterPattern8[][2] = {
    {-0.375f, -0.125f},
    {0.125f, -0.375f},
    {0.375f, 0.125f},
    {-0.125f, 0.375f},
    {-0.250f, -0.375f},
    {0.375f, -0.250f},
    {0.250f, 0.375f},
    {-0.375f, 0.250f},
};
// 16-sample: Poisson disc (Bridson's algorithm, min_dist=0.42)
static constexpr float kJitterPattern16[][2] = {
    {-0.853f, -0.370f},
    {-0.420f, -0.444f},
    {-0.797f, -0.876f},
    {-0.311f, -1.000f},
    {-0.038f, -0.648f},
    {+0.569f, -0.846f},
    {+0.756f, -0.338f},
    {+0.000f, +0.000f},
    {+0.529f, +0.084f},
    {+0.989f, +0.321f},
    {+0.602f, +0.753f},
    {+0.110f, +0.610f},
    {-0.357f, +0.967f},
    {-0.401f, +0.499f},
    {-0.903f, +0.943f},
    {-0.902f, +0.151f},
};
// 32-sample: Poisson disc (Bridson's algorithm, min_dist=0.29)
static constexpr float kJitterPattern32[][2] = {
    {+0.000f, +0.000f},
    {-0.024f, +0.528f},
    {-0.512f, +0.650f},
    {+0.218f, +0.964f},
    {+0.785f, +0.929f},
    {+0.821f, +0.521f},
    {+0.493f, +0.857f},
    {+0.662f, +0.273f},
    {+0.253f, +0.392f},
    {-0.263f, +0.848f},
    {-0.424f, +0.350f},
    {+0.513f, +0.535f},
    {+0.743f, -0.113f},
    {-0.886f, +0.041f},
    {-0.832f, +0.886f},
    {+0.446f, -0.064f},
    {-0.878f, +0.405f},
    {+0.979f, -0.639f},
    {-0.537f, +0.998f},
    {+0.557f, -0.399f},
    {-0.129f, -0.476f},
    {-0.375f, +0.006f},
    {-0.698f, -0.364f},
    {+0.133f, -0.282f},
    {-0.583f, -0.659f},
    {+0.947f, +0.192f},
    {+0.218f, -0.589f},
    {+0.960f, -0.318f},
    {+0.698f, -0.806f},
    {+0.924f, -0.992f},
    {+0.346f, -0.874f},
    {-0.880f, -0.626f},
};

static float* __fastcall HookedLPWAL(void* thisPtr, void* /*edx*/, float* outColor, void* occSet2D, void* occSet3D, void* occTestData, void* surfaceData) {
    int samples = g_shadowSampleCount;
    if (samples <= 1) { return originalLPWAL(thisPtr, outColor, occSet2D, occSet3D, occTestData, surfaceData); }

    float* worldPos = reinterpret_cast<float*>(surfaceData);
    alignas(16) float savedPos[4];
    std::memcpy(savedPos, worldPos, 16);

    const float (*pattern)[2];
    int sampleCount;
    if (samples <= 4) {
        pattern = kJitterPattern4;
        sampleCount = 4;
    } else if (samples <= 8) {
        pattern = kJitterPattern8;
        sampleCount = 8;
    } else if (samples <= 16) {
        pattern = kJitterPattern16;
        sampleCount = 16;
    } else {
        pattern = kJitterPattern32;
        sampleCount = 32;
    }

    float radius = g_shadowJitterRadius;
    float accumR = 0.0f, accumG = 0.0f, accumB = 0.0f, accumA = 0.0f;
    float firstMag = -1.0f;
    int actualSamples = sampleCount;

    // Per-texel rotation to decorrelate jitter pattern between adjacent texels
    unsigned short lmX = *reinterpret_cast<unsigned short*>(reinterpret_cast<uintptr_t>(surfaceData) + 0x20);
    unsigned short lmY = *reinterpret_cast<unsigned short*>(reinterpret_cast<uintptr_t>(surfaceData) + 0x22);
    float theta = TexelRotationAngle(lmX, lmY);
    float cosT = std::cos(theta);
    float sinT = std::sin(theta);

    for (int i = 0; i < sampleCount; i++) {
        float pu = pattern[i][0] * radius;
        float pv = pattern[i][1] * radius;
        float ju = pu * cosT - pv * sinT;
        float jv = pu * sinT + pv * cosT;

        // XZ plane only,  vertical jitter pushes samples through floors->ceiling which looks insane... Probably happens with this also but oh well
        worldPos[0] = savedPos[0] + ju;
        worldPos[1] = savedPos[1];
        worldPos[2] = savedPos[2] + jv;
        worldPos[3] = savedPos[3];

        originalLPWAL(thisPtr, outColor, occSet2D, occSet3D, occTestData, surfaceData);

        accumR += outColor[0];
        accumG += outColor[1];
        accumB += outColor[2];
        accumA += outColor[3];

        // Adaptive early-out: if first 2 samples agree, skip the rest
        float mag = outColor[0] + outColor[1] + outColor[2];
        if (i == 0) {
            firstMag = mag;
        } else if (i == 1 && g_adaptiveSampling && std::abs(mag - firstMag) < 0.02f) {
            actualSamples = 2;
            break;
        }
    }

    std::memcpy(worldPos, savedPos, 16);

    float invN = 1.0f / static_cast<float>(actualSamples);
    outColor[0] = accumR * invN;
    outColor[1] = accumG * invN;
    outColor[2] = accumB * invN;
    outColor[3] = accumA * invN;

    return outColor;
}
// me when I'm mentally healthy and very of sane mind
class LightingQualityPatch : public OptimizationPatch {
  private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    std::vector<DetourHelper::Hook> hooks;
    
    int subdivisionMultiplier = 0;
    int wallLightmapMultiplier = 0;
    int softShadows = 0;
    int shadowSamples = 0;
    int shadowRadius = 1;
    int lightmapTextureCap = 0;
    int diagonalWallOcclusion = 0;
    int adaptiveSampling = 1;
    int wallBlur = 0;
    int lightmapBlur = 0;
    float softShadowWidth = 0.5f;
    static constexpr int kMultiplierValues[] = {1, 2, 4, 8};
    static constexpr int kWallMultiplierValues[] = {1, 2, 4};

    uintptr_t kBaseSubdivisionAddr = 0;
    uintptr_t kSeparateDiagonalsAddr = 0; // byte array at kBaseSubdivision - 0x0C
    uintptr_t wallWidthAddr = 0;          // DWORD[3] at kBaseSubdivision + 0x30
    uintptr_t wallHeightAddr = 0;         // DWORD[3] at kBaseSubdivision + 0x3C
    uintptr_t cacheBypassJzAddr = 0;
    uintptr_t cacheLightingParamsFuncAddr = 0;
    DWORD currentSub[3] = {};
    DWORD currentWallW[3] = {};
    DWORD currentWallH[3] = {};

    // Original wall lightmap dimensions for validation
    static constexpr DWORD kOrigWallWidths[] = {256, 512, 1024};
    static constexpr DWORD kOrigWallHeights[] = {128, 256, 512};

    static constexpr const char* kGetSubdivisionPattern = "8B 41 08 8B 04 85 ?? ?? ?? ?? C3";
    static constexpr int kSubdivisionAddrOffset = 6;

    static constexpr const char* kCacheLightingParamsPattern = "53 8B 5C 24 08 56 8B F1 3B 9E CC 03 00 00 74";
    static constexpr int kCacheJzOffset = 14;

    // kSoftWallShadows byte[3] at kBaseSubdivision - 0x08
    uintptr_t kSoftWallShadowsAddr = 0;

    static constexpr const char* kNextHigherPow2Pattern = "B9 00 04 00 00 3B C8 1B C0 23 C1 03 C1 C3";
    uintptr_t nextHigherPow2Addr = 0;

    static constexpr const char* kLightPointWithAllLightsPattern = "55 8B EC 83 E4 F0 83 EC ?? 0F 57 C0 8B 45 08 53 8B D9 8B 8B CC 00 00 00 2B 8B C8 00 00 00";
    uintptr_t lightPointWithAllLightsAddr = 0;

    static constexpr const char* kFinalizePrimePattern = "56 8B F1 57 8B BE F4 00 00 00 C1 E7 06 03 7E 04 8B CF E8";
    uintptr_t finalizePrimeAddr = 0;

    static constexpr const char* kGetVisibleResPattern = "80 7C 24 0C 00 8B D1 74 ?? 8B 42 08 8B 0C 85";
    uintptr_t getVisibleResAddr = 0;

    static constexpr const char* kBlurWallLightmapsPattern = "B8 44 40 00 00 E8 ?? ?? ?? ?? 56 8B F1 83 BE F4 00 00 00 02";
    static constexpr int kNumBlursAddrOffset = 0x20;
    uintptr_t numBlursAddr = 0;

    // Diagonal wall 3D occlusion flags: at kBaseSubdivision + 0x2A (surface type 2, LOD 1 & 2), this is probably wrong
    static constexpr uintptr_t kDiag3DOcclusionOffset = 0x2A;

    static constexpr const char* kFuzzyEdgeFldPattern = "80 7D 14 00 74 ?? D9 05 ?? ?? ?? ?? 83 EC 08";
    static constexpr int kFuzzyEdgeAddrOffset = 8;
    uintptr_t fuzzyEdgeFldAddr = 0;

    bool ResolveAddresses() {
        HMODULE hModule = GetModuleHandle(NULL);
        BYTE* baseAddr;
        size_t imageSize;

        if (!PatchHelper::GetModuleInfo(hModule, &baseAddr, &imageSize)) { return Fail("Failed to get module information"); }

        // Find GetFloorCeilingSubdivision to locate kBaseSubdivision array
        auto getSubdivisionFuncAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kGetSubdivisionPattern);
        if (!getSubdivisionFuncAddr) { return Fail("Could not find GetFloorCeilingSubdivision pattern"); }
        LOG_INFO(std::format("[LightingQualityPatch] GetFloorCeilingSubdivision at {:#010x}", getSubdivisionFuncAddr));

        // Read the embedded kBaseSubdivision array address
        kBaseSubdivisionAddr = *reinterpret_cast<uintptr_t*>(getSubdivisionFuncAddr + kSubdivisionAddrOffset);
        LOG_INFO(std::format("[LightingQualityPatch] kBaseSubdivision array at {:#010x}", kBaseSubdivisionAddr));

        // kSeparateDiagonals is a byte[3] array 0x0C before kBaseSubdivision
        kSeparateDiagonalsAddr = kBaseSubdivisionAddr - 0x0C;
        LOG_INFO(std::format("[LightingQualityPatch] kSeparateDiagonals array at {:#010x}", kSeparateDiagonalsAddr));

        // Validate kBaseSubdivision values
        currentSub[0] = PatchHelper::ReadDWORD(kBaseSubdivisionAddr);
        currentSub[1] = PatchHelper::ReadDWORD(kBaseSubdivisionAddr + 4);
        currentSub[2] = PatchHelper::ReadDWORD(kBaseSubdivisionAddr + 8);
        LOG_INFO(std::format("[LightingQualityPatch] Current kBaseSubdivision: {{{}, {}, {}}}", currentSub[0], currentSub[1], currentSub[2]));

        bool isOriginal = (currentSub[0] == 1 && currentSub[1] == 2 && currentSub[2] == 4);
        bool isValidMultiple = (currentSub[1] == currentSub[0] * 2 && currentSub[2] == currentSub[0] * 4);
        bool isAllSame = (currentSub[0] == currentSub[1] && currentSub[1] == currentSub[2] && currentSub[0] > 0);
        if (!isOriginal && !isValidMultiple && !isAllSame) { return Fail(std::format("kBaseSubdivision validation failed: got {{{}, {}, {}}}", currentSub[0], currentSub[1], currentSub[2])); }

        // Validate kSeparateDiagonals[2] (should be 0 or 1)
        BYTE diagLod2 = *reinterpret_cast<BYTE*>(kSeparateDiagonalsAddr + 2);
        if (diagLod2 > 1) { return Fail(std::format("kSeparateDiagonals[2] validation failed: got {}", diagLod2)); }
        LOG_INFO(std::format("[LightingQualityPatch] kSeparateDiagonals[2] = {}", diagLod2));

        // kSoftWallShadows byte[3] at kBaseSubdivision - 0x08
        kSoftWallShadowsAddr = kBaseSubdivisionAddr - 0x08;
        BYTE softWall0 = *reinterpret_cast<BYTE*>(kSoftWallShadowsAddr);
        BYTE softWall1 = *reinterpret_cast<BYTE*>(kSoftWallShadowsAddr + 1);
        BYTE softWall2 = *reinterpret_cast<BYTE*>(kSoftWallShadowsAddr + 2);
        if (softWall0 > 1 || softWall1 > 1 || softWall2 > 1) { return Fail(std::format("kSoftWallShadows validation failed: got {{{}, {}, {}}}", softWall0, softWall1, softWall2)); }
        LOG_INFO(std::format("[LightingQualityPatch] kSoftWallShadows: {{{}, {}, {}}}", softWall0, softWall1, softWall2));

        // Wall lightmap dimension arrays are at known offsets from kBaseSubdivision
        wallWidthAddr = kBaseSubdivisionAddr + 0x30;
        wallHeightAddr = kBaseSubdivisionAddr + 0x3C;

        for (int i = 0; i < 3; i++) {
            currentWallW[i] = PatchHelper::ReadDWORD(wallWidthAddr + i * 4);
            currentWallH[i] = PatchHelper::ReadDWORD(wallHeightAddr + i * 4);
        }
        LOG_INFO(std::format("[LightingQualityPatch] Wall widths:  {{{}, {}, {}}}", currentWallW[0], currentWallW[1], currentWallW[2]));
        LOG_INFO(std::format("[LightingQualityPatch] Wall heights: {{{}, {}, {}}}", currentWallH[0], currentWallH[1], currentWallH[2]));

        // Validate wall dimensions
        auto isValidWallDims = [](const DWORD cur[3], const DWORD orig[3]) {
            bool isOrig = (cur[0] == orig[0] && cur[1] == orig[1] && cur[2] == orig[2]);
            bool isAllSame = (cur[0] == cur[1] && cur[1] == cur[2] && cur[0] > 0);
            return isOrig || isAllSame;
        };
        if (!isValidWallDims(currentWallW, kOrigWallWidths)) { return Fail(std::format("Wall width validation failed: got {{{}, {}, {}}}", currentWallW[0], currentWallW[1], currentWallW[2])); }
        if (!isValidWallDims(currentWallH, kOrigWallHeights)) { return Fail(std::format("Wall height validation failed: got {{{}, {}, {}}}", currentWallH[0], currentWallH[1], currentWallH[2])); }

        // Find CacheLightingParams
        cacheLightingParamsFuncAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kCacheLightingParamsPattern);
        if (!cacheLightingParamsFuncAddr) { return Fail("Could not find CacheLightingParams pattern"); }
        LOG_INFO(std::format("[LightingQualityPatch] CacheLightingParams at {:#010x}", cacheLightingParamsFuncAddr));

        cacheBypassJzAddr = cacheLightingParamsFuncAddr + kCacheJzOffset;

        // Validate JZ byte
        BYTE jzByte = *reinterpret_cast<BYTE*>(cacheBypassJzAddr);
        if (jzByte != 0x74 && jzByte != 0x90) { return Fail(std::format("CacheLightingParams JZ validation failed: got {:#04x}", jzByte)); }

        // Find NextHigherPow2 final SBB block (optional)
        if (lightmapTextureCap > 0) {
            nextHigherPow2Addr = PatchHelper::ScanPattern(baseAddr, imageSize, kNextHigherPow2Pattern);
            if (!nextHigherPow2Addr) { return Fail("Could not find NextHigherPow2 SBB pattern"); }
            LOG_INFO(std::format("[LightingQualityPatch] NextHigherPow2 SBB block at {:#010x}", nextHigherPow2Addr));
        }

        // Find LightPointWithAllLights (for multi-sample hook)
        if (shadowSamples > 0) {
            lightPointWithAllLightsAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kLightPointWithAllLightsPattern);
            if (!lightPointWithAllLightsAddr) { return Fail("Could not find LightPointWithAllLights pattern"); }
            LOG_INFO(std::format("[LightingQualityPatch] LightPointWithAllLights at {:#010x}", lightPointWithAllLightsAddr));
        }

        // Find FinalizePrime and GetVisibleRes (for floor/ceiling blur)
        if (lightmapBlur > 0) {
            finalizePrimeAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kFinalizePrimePattern);
            if (!finalizePrimeAddr) { return Fail("Could not find FinalizePrime pattern"); }
            LOG_INFO(std::format("[LightingQualityPatch] FinalizePrime at {:#010x}", finalizePrimeAddr));

            getVisibleResAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kGetVisibleResPattern);
            if (!getVisibleResAddr) { return Fail("Could not find GetFloorCeilingVisibleResolution pattern"); }
            LOG_INFO(std::format("[LightingQualityPatch] GetFloorCeilingVisibleResolution at {:#010x}", getVisibleResAddr));
        }

        // Resolve diagonal wall 3D occlusion flags (optional)
        if (diagonalWallOcclusion > 0) {
            uintptr_t diagAddr = kBaseSubdivisionAddr + kDiag3DOcclusionOffset;
            BYTE lod1 = *reinterpret_cast<BYTE*>(diagAddr + 1);
            BYTE lod2 = *reinterpret_cast<BYTE*>(diagAddr + 2);
            if (lod1 > 1 || lod2 > 1) { return Fail(std::format("Diagonal 3D occlusion flag validation failed: LOD1={}, LOD2={}", lod1, lod2)); }
            LOG_INFO(std::format("[LightingQualityPatch] Diagonal 3D occlusion flags at {:#010x}: LOD1={}, LOD2={}", diagAddr, lod1, lod2));
        }

        // Find BlurWallLightmaps to extract numBlurs address
        if (wallBlur > 0) {
            auto blurFuncAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kBlurWallLightmapsPattern);
            if (!blurFuncAddr) { return Fail("Could not find BlurWallLightmaps pattern"); }
            LOG_INFO(std::format("[LightingQualityPatch] BlurWallLightmaps at {:#010x}", blurFuncAddr));

            numBlursAddr = *reinterpret_cast<uintptr_t*>(blurFuncAddr + kNumBlursAddrOffset);

            int currentNumBlurs = *reinterpret_cast<int*>(numBlursAddr);
            if (currentNumBlurs < 1 || currentNumBlurs > 30) { return Fail(std::format("numBlurs validation failed: got {}", currentNumBlurs)); }
            LOG_INFO(std::format("[LightingQualityPatch] numBlurs at {:#010x} = {}", numBlursAddr, currentNumBlurs));
        }

        // Find fuzzyEdge FLD in RayCheckOccluders2D (for soft shadow width)
        if (softShadowWidth > 0.5f) {
            fuzzyEdgeFldAddr = PatchHelper::ScanPattern(baseAddr, imageSize, kFuzzyEdgeFldPattern);
            if (!fuzzyEdgeFldAddr) { return Fail("Could not find fuzzyEdge FLD pattern in RayCheckOccluders2D"); }
            uintptr_t currentAddrOperand = *reinterpret_cast<uintptr_t*>(fuzzyEdgeFldAddr + kFuzzyEdgeAddrOffset);
            float currentVal = *reinterpret_cast<float*>(currentAddrOperand);
            if (std::abs(currentVal - 0.5f) > 0.01f) { return Fail(std::format("fuzzyEdge validation failed: expected 0.5, got {}", currentVal)); }
            LOG_INFO(std::format("[LightingQualityPatch] fuzzyEdge FLD at {:#010x}, addr operand -> {:#010x} ({})", fuzzyEdgeFldAddr + 6, currentAddrOperand, currentVal));
        }

        return true;
    }

  public:
    LightingQualityPatch() : OptimizationPatch("LightingQualityPatch", nullptr) {
        RegisterEnumSetting(&subdivisionMultiplier, "subdivisionMultiplier", 0,
            "Floor lightmap resolution multiplier.\n"
            "Higher values = more gooderer but will take longer for lights to generate and use more memory.\n"
            "NOTE: higher resolution reduces the visual effect of blur settings,since blur operates on a fixed pixel radius.\nGame may also crash when changing this ingame, you need to restart also for the changes to "
            "show!\n\n"
            "2-4x is probably the best spot",
            {"1x (default)", "2x", "4x", "8x"});

        RegisterEnumSetting(&wallLightmapMultiplier, "wallLightmapMultiplier", 0,
            "Wall lightmap resolution multiplier.\n"
            "Separate from floor lightmaps. Original: 1024x512.\n"
            "Higher values reduce effectiveness of wall blur but looks slightly better",
            {"Off (default)", "2x", "4x"});

        RegisterEnumSetting(&softShadows, "softShadows", 0,
            "Enables soft wall shadow edges at all LOD levels.\n"
            "Default is only enabled at LOD 2. I have no idea what this does but might as well include it",
            {"Off", "On"});

        RegisterEnumSetting(&shadowSamples, "shadowSamples", 0,
            "Multi-sample lighting for softer shadow edges. Recommended to set at 4x, diminishing returns\n"
            "Evaluates lighting at multiple jittered positions per texel and averages the results. Softens both wall and object shadows.\n"
            "Cost scales linearly with sample count and subdivision multipliers, advisable to use with adaptive sampling.",
            {"Off", "4x", "8x", "16x", "32x"});

        RegisterEnumSetting(&shadowRadius, "shadowRadius", 1,
            "Jitter radius for multi-sample lighting (world units). Higher values can look overly smoothed/incorrect\n"
            "Larger = softer shadow edges. Each tile is 1.0 units.",
            {"0.125", "0.25", "0.5", "1.0"});

        RegisterEnumSetting(&lightmapTextureCap, "lightmapTextureCap", 0,
            "Maximum lightmap texture dimension.\n"
            "Uses more VRAM but probably not a whole lot, may be necessary for higher subdivisions, idk.",
            {"2048 (default)", "4096"});

        RegisterEnumSetting(&diagonalWallOcclusion, "diagonalWallOcclusion", 0,
            "Enables object shadow casting on diagonal walls.\n"
            "The engine disables this by default, so furniture never casts shadows onto diagonal walls",
            {"Off", "On"});

        RegisterEnumSetting(&adaptiveSampling, "adaptiveSampling", 1,
            "Adaptive early-out for multi-sample lighting.\n"
            "Skips remaining samples when the first 2 agree (~80%% of texels).\n"
            "Negligible quality loss for the performance gained",
            {"Off", "On"});

        RegisterIntSetting(&wallBlur, "wallBlur", 0, 0, 16,
            "Additional blur passes for wall lightmaps (added to engine's base of 2).\n"
            "Less effective at higher wall lightmap resolution.");

        RegisterIntSetting(&lightmapBlur, "lightmapBlur", 0, 0, 16,
            "Blur passes for floor/ceiling lightmaps.\n"
            "Less effective at higher floor subdivision.");

        RegisterFloatSetting(&softShadowWidth, "softShadowWidth", SettingUIType::Slider, 0.5f, 0.5f, 4.0f,
            "Soft shadow gradient width at wall edges (world units).\n"
            "Wider = softer edges. Requires soft shadows enabled.");
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[LightingQualityPatch] Installing...");
        int multiplier = kMultiplierValues[std::clamp(subdivisionMultiplier, 0, 3)];
        LOG_INFO(std::format("[LightingQualityPatch] subdivisionMultiplier: {}x", multiplier));

        if (!ResolveAddresses()) { return false; }

        auto tx = PatchHelper::BeginTransaction();
        bool ok = true;

        // Patch kBaseSubdivision data array
        {
            DWORD maxSub = static_cast<DWORD>(4 * multiplier);

            DWORD oldSub0 = currentSub[0], oldSub1 = currentSub[1], oldSub2 = currentSub[2];
            ok &= PatchHelper::WriteDWORD(kBaseSubdivisionAddr, maxSub, &tx.locations, &oldSub0);
            ok &= PatchHelper::WriteDWORD(kBaseSubdivisionAddr + 4, maxSub, &tx.locations, &oldSub1);
            ok &= PatchHelper::WriteDWORD(kBaseSubdivisionAddr + 8, maxSub, &tx.locations, &oldSub2);
            LOG_INFO(std::format("[LightingQualityPatch] kBaseSubdivision: {{{},{},{}}} -> {{{},{},{}}}", oldSub0, oldSub1, oldSub2, maxSub, maxSub, maxSub));
        }

        // Patch kSeparateDiagonals[2] to 0 (only when subdivision is changed)
        if (multiplier > 1) {
            BYTE oldDiag = *reinterpret_cast<BYTE*>(kSeparateDiagonalsAddr + 2);
            if (oldDiag != 0) {
                BYTE zero = 0;
                ok &= PatchHelper::WriteByte(kSeparateDiagonalsAddr + 2, zero, &tx.locations, &oldDiag);
                LOG_INFO(std::format("[LightingQualityPatch] kSeparateDiagonals[2]: {} -> 0", oldDiag));
            }
        }

        // Patch wall lightmap dimensions
        {
            int wallMult = kWallMultiplierValues[std::clamp(wallLightmapMultiplier, 0, 2)];
            if (wallMult > 1) {
                DWORD newW = kOrigWallWidths[2] * static_cast<DWORD>(wallMult);
                DWORD newH = kOrigWallHeights[2] * static_cast<DWORD>(wallMult);
                DWORD maxDim = (lightmapTextureCap > 0) ? 4096 : 2048;
                if (newW > maxDim) newW = maxDim;
                if (newH > maxDim) newH = maxDim;

                for (int i = 0; i < 3; i++) {
                    DWORD oldW = currentWallW[i], oldH = currentWallH[i];
                    ok &= PatchHelper::WriteDWORD(wallWidthAddr + i * 4, newW, &tx.locations, &oldW);
                    ok &= PatchHelper::WriteDWORD(wallHeightAddr + i * 4, newH, &tx.locations, &oldH);
                }
                LOG_INFO(
                    std::format("[LightingQualityPatch] Wall dims: {{{},{},{}}}x{{{},{},{}}} -> {}x{}", currentWallW[0], currentWallW[1], currentWallW[2], currentWallH[0], currentWallH[1], currentWallH[2], newW, newH));
            } else {
                LOG_INFO("[LightingQualityPatch] Wall lightmap multiplier off, skipping");
            }
        }

        // NOP the cache bypass JZ in CacheLightingParams
        ok &= PatchHelper::WriteNOP(cacheBypassJzAddr, 2, &tx.locations);

        // Patch kSoftWallShadows to enable soft shadow edges at all LODs
        if (softShadows > 0 && kSoftWallShadowsAddr != 0) {
            BYTE one = 1;
            for (int i = 0; i < 3; i++) {
                BYTE oldVal = *reinterpret_cast<BYTE*>(kSoftWallShadowsAddr + i);
                if (oldVal != 1) { ok &= PatchHelper::WriteByte(kSoftWallShadowsAddr + i, one, &tx.locations, &oldVal); }
            }
            LOG_INFO("[LightingQualityPatch] kSoftWallShadows: -> {1, 1, 1}");
        }

        // Raise NextHigherPow2 cap from 2048 to 4096
        if (lightmapTextureCap > 0 && nextHigherPow2Addr != 0) {
            std::vector<BYTE> doubleSbb = {
                0x52, // PUSH EDX
                0xB9,
                0x00,
                0x04,
                0x00,
                0x00, // MOV ECX, 0x400
                0x3B,
                0xC8, // CMP ECX, EAX
                0x1B,
                0xD2, // SBB EDX, EDX
                0x23,
                0xD1, // AND EDX, ECX
                0x03,
                0xCA, // ADD ECX, EDX
                0x3B,
                0xC8, // CMP ECX, EAX
                0x1B,
                0xC0, // SBB EAX, EAX
                0x23,
                0xC1, // AND EAX, ECX
                0x03,
                0xC1, // ADD EAX, ECX
                0x5A, // POP EDX
                0xC3, // RET
            };
            std::vector<BYTE> oldBytes(reinterpret_cast<BYTE*>(nextHigherPow2Addr), reinterpret_cast<BYTE*>(nextHigherPow2Addr) + doubleSbb.size());
            ok &= PatchHelper::WriteBytes(nextHigherPow2Addr, doubleSbb, &tx.locations, &oldBytes);
            LOG_INFO(std::format("[LightingQualityPatch] NextHigherPow2 cap raised to 4096 at {:#010x}", nextHigherPow2Addr));
        }

        // Enable 3D object occlusion on diagonal walls
        if (diagonalWallOcclusion > 0) {
            uintptr_t diagAddr = kBaseSubdivisionAddr + kDiag3DOcclusionOffset;
            BYTE oldLod1 = *reinterpret_cast<BYTE*>(diagAddr + 1);
            BYTE oldLod2 = *reinterpret_cast<BYTE*>(diagAddr + 2);
            BYTE one = 1;
            ok &= PatchHelper::WriteByte(diagAddr + 1, one, &tx.locations, &oldLod1);
            ok &= PatchHelper::WriteByte(diagAddr + 2, one, &tx.locations, &oldLod2);
            LOG_INFO(std::format("[LightingQualityPatch] Diagonal 3D occlusion: LOD1 {}->1, LOD2 {}->1", oldLod1, oldLod2));
        }

        // Patch engine's built-in wall blur parameters
        if (wallBlur > 0 && numBlursAddr != 0) {
            int newNumBlurs = 2 + wallBlur; // base 2 + user value
            DWORD oldVal = *reinterpret_cast<DWORD*>(numBlursAddr);
            ok &= PatchHelper::WriteDWORD(numBlursAddr, static_cast<DWORD>(newNumBlurs), &tx.locations, &oldVal);
            LOG_INFO(std::format("[LightingQualityPatch] numBlurs: {} -> {}", oldVal, newNumBlurs));
        }

        // Redirect fuzzyEdge FLD to our static float
        if (softShadowWidth > 0.5f && fuzzyEdgeFldAddr != 0) {
            g_fuzzyEdgeWidth = softShadowWidth;
            uintptr_t patchAddr = fuzzyEdgeFldAddr + kFuzzyEdgeAddrOffset;
            DWORD oldAddrVal = *reinterpret_cast<DWORD*>(patchAddr);
            DWORD newAddrVal = reinterpret_cast<DWORD>(&g_fuzzyEdgeWidth);
            ok &= PatchHelper::WriteDWORD(patchAddr, newAddrVal, &tx.locations, &oldAddrVal);
            LOG_INFO(std::format("[LightingQualityPatch] fuzzyEdge redirected to {:#010x} = {:.2f}", newAddrVal, g_fuzzyEdgeWidth));
        }

        if (!ok || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to write patches");
        }
        patchedLocations = tx.locations;

        // Set adaptive sampling flag for hook
        g_adaptiveSampling = (adaptiveSampling > 0);

        // Hook: multi-sample LightPointWithAllLights
        if (shadowSamples > 0 && lightPointWithAllLightsAddr != 0) {
            static constexpr int kSampleValues[] = {4, 8, 16, 32};
            g_shadowSampleCount = kSampleValues[std::clamp(shadowSamples - 1, 0, 3)];
            static constexpr float kRadiusValues[] = {0.125f, 0.25f, 0.5f, 1.0f};
            g_shadowJitterRadius = kRadiusValues[std::clamp(shadowRadius, 0, 3)];

            originalLPWAL = reinterpret_cast<LightPointWithAllLights_t>(lightPointWithAllLightsAddr);
            hooks.push_back({reinterpret_cast<void**>(&originalLPWAL), reinterpret_cast<void*>(&HookedLPWAL)});
            LOG_INFO(std::format("[LightingQualityPatch] Shadow multi-sampling: {}x samples, radius {:.3f}", g_shadowSampleCount, g_shadowJitterRadius));
        }

        // Hook: floor/ceiling blur via FinalizePrime
        if (lightmapBlur > 0 && finalizePrimeAddr != 0 && getVisibleResAddr != 0) {
            g_floorCeilBlurPasses = lightmapBlur;
            getVisibleResFunc = reinterpret_cast<GetVisibleRes_t>(getVisibleResAddr);

            originalFinalizePrime = reinterpret_cast<FinalizePrime_t>(finalizePrimeAddr);
            hooks.push_back({reinterpret_cast<void**>(&originalFinalizePrime), reinterpret_cast<void*>(&HookedFinalizePrime)});
            LOG_INFO(std::format("[LightingQualityPatch] Floor/ceiling blur: {} passes", g_floorCeilBlurPasses));
        }

        // Install all Detours hooks in one transaction
        if (!hooks.empty()) {
            if (!DetourHelper::InstallHooks(hooks)) {
                PatchHelper::RestoreAll(patchedLocations);
                return Fail("Failed to install multi-sample hook");
            }
        }

        isEnabled = true;
        LOG_INFO("[LightingQualityPatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[LightingQualityPatch] Uninstalling...");

        // Remove Detours hooks first
        if (!hooks.empty()) {
            DetourHelper::RemoveHooks(hooks);
            hooks.clear();
            originalLPWAL = nullptr;
            g_shadowSampleCount = 1;
            g_adaptiveSampling = false;
            g_fuzzyEdgeWidth = 0.5f;
            originalFinalizePrime = nullptr;
            g_floorCeilBlurPasses = 0;
            getVisibleResFunc = nullptr;
        }

        // Restore all data patches
        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original bytes"); }

        isEnabled = false;
        LOG_INFO("[LightingQualityPatch] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(LightingQualityPatch,
    {.displayName = "Lighting Quality",
        .description = "Improves interior lighting: higher lightmap resolution, softer shadows, and blur.\nHighly recommend using the various lighting settings in the settings tab, e.g. turning drop shadows off, playing "
                       "around with the interior lighting settings, etc.\n"
                       "You will need to update the lighting to see the changes, this can be done by turning lights on/off or moving objects in build/buy."
                       "nTo change the subdivisionMultiplier you will need to restart",
        .category = "Graphics",
        .experimental = true,
        .supportedVersions = VERSION_ALL,
        .technicalDetails = {"Modifies kBaseSubdivision array to increase floor texels-per-tile at all LODs", "Increases wall lightmap dimensions (at kBaseSubdivision+0x30/+0x3C)",
            "Zeroes kSeparateDiagonals[2] to fix UV scale mismatch", "NOPs JZ in CacheLightingParams to force UV cache refresh", "Patches kSoftWallShadows {0,0,1} -> {1,1,1} for soft shadows at all LODs",
            "Hooks LightPointWithAllLights to add multi-sample jittered lighting with per-texel rotation", "NextHigherPow2 patch raises max texture dim to 4096",
            "Wall blur patches engine's BlurWallLightmaps numBlurs global", "Floor/ceiling blur hooks FinalizePrime for box blur before texture unlock", "Soft shadow width redirects FLD operand in RayCheckOccluders2D"}})
