#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../addresses.h"
#include "../logger.h"

class SmoothPatchDupe : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    float frameLimitValue = 0.0f;  // 0 = no limit, 0.02 = original (50fps), etc.
    void* frameLimitAddress = nullptr;
    float* customFrameLimitPtr = nullptr;  // Allocate our own float, the one the target uses is actually used by a bunch of random functions, fml
    static constexpr float MIN_FRAME_LIMIT = 0.000001f;  // Very small value to prevent division by zero, doesn't actually matter though! Might change this back to how it was

public:
    SmoothPatchDupe() : OptimizationPatch("SmoothPatchDupe", nullptr) {
        // Register the frame limit setting with presets
        RegisterFloatSetting(&frameLimitValue, "frameLimitValue", SettingUIType::InputBox,
            MIN_FRAME_LIMIT, MIN_FRAME_LIMIT, 0.1f,  // default, min, max
            "Frame rate limiter (0.000001 = ~unlimited, 0.02 = ~50 fps)",
            {                  // presets
                {"Unlimited", MIN_FRAME_LIMIT},
                {"60 FPS", 0.01667f},
                {"Original 50 FPS", 0.02f},
                {"30 FPS", 0.03333f}
            }
        );
    }

    ~SmoothPatchDupe() override {
        // Free allocated memory if it exists
        if (customFrameLimitPtr) {
            VirtualFree(customFrameLimitPtr, 0, MEM_RELEASE);
            customFrameLimitPtr = nullptr;
        }
    }

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[SmoothPatchDupe] Installing...");

        // Allocate memory for our custom variable
        customFrameLimitPtr = (float*)VirtualAlloc(
            nullptr,
            sizeof(float),
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE
        );

        if (!customFrameLimitPtr) {
            return Fail("Failed to allocate memory for custom frame limit variable");
        }

        *customFrameLimitPtr = frameLimitValue;

        BYTE* fldAddr = (BYTE*)gameAddresses->smoothPatchDupe;

        // Verify this is an FLD instruction: d9 05 ?? ?? ?? ??
        if (fldAddr[0] != 0xD9 || fldAddr[1] != 0x05) {
            return Fail(std::format("Expected FLD instruction not found at {:#010x}", (uintptr_t)fldAddr));
        }

        // Extract the original shared constant address for validation
        uint32_t originalAddress;
        std::memcpy(&originalAddress, fldAddr + 2, 4);
        frameLimitAddress = (void*)(uintptr_t)originalAddress;

        // Validate we found the expected default constant (~0.02f)
        float currentValue = *reinterpret_cast<float*>(frameLimitAddress);
        const float expectedDefault = 0.02f;
        float diff = currentValue - expectedDefault;
        if (diff < 0.0f) diff = -diff;
        const float epsilon = 0.0008f;
        if (diff > epsilon) {
            return Fail("Unexpected value at shared constant address (expected ~0.02f); refusing to patch.");
        }

        // Patch the FLD instruction to point to our custom variable instead of the shared constant
        uint32_t customAddress = (uint32_t)(uintptr_t)customFrameLimitPtr;

        if (!PatchHelper::WriteProtectedMemory(fldAddr + 2, &customAddress,
                                               sizeof(uint32_t), &patchedLocations)) {
            return Fail("Failed to patch FLD instruction address");
        }

        // Bind the setting to our custom variable address
        BindSettingToAddress("frameLimitValue", customFrameLimitPtr);

        isEnabled = true;
        LOG_INFO("[SmoothPatchDupe] Successfully installed (Frame limit: " +
                std::to_string(frameLimitValue) + ")");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[SmoothPatchDupe] Uninstalling...");

        // Restore the original FLD instruction (points back to shared constant)
        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original FLD instruction");
        }

        // Free our custom allocated memory
        if (customFrameLimitPtr) {
            VirtualFree(customFrameLimitPtr, 0, MEM_RELEASE);
            customFrameLimitPtr = nullptr;
        }

        frameLimitAddress = nullptr;
        isEnabled = false;
        LOG_INFO("[SmoothPatchDupe] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(SmoothPatchDupe, {
    .displayName = "Smooth Patch Dupe (Frame Limit)",
    .description = "Removes or adjusts the frame rate limiter without touching sleep functions",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = allGameVersionsMask,
    .technicalDetails = {
        "Pattern: 80 7e 60 00 0f 57 c0 f3 0f 11 44 24 10",
        "Offset: +0x25 to the FLD instruction (d9 05 ?? ?? ?? ??)",
        "Allocates custom memory for frame limit value",
        "Patches FLD instruction to load from custom memory instead of shared constant",
        "Value is seconds per frame (0.02 = 50fps, 0.01667 = 60fps)",
        "Does not cap FPS - you may want a separate FPS limiter",
        "Credits to LazyDutchess for the original SmoothPatch concept"
    }
})
