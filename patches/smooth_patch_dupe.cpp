#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class SmoothPatchDupe : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    float frameLimitValue = 0.0f;  // 0 = no limit, 0.02 = original (50fps), etc.
    void* frameLimitAddress = nullptr;

public:
    SmoothPatchDupe() : OptimizationPatch("SmoothPatchDupe", nullptr) {
        // Register the frame limit setting with presets
        RegisterFloatSetting(&frameLimitValue, "frameLimitValue", SettingUIType::InputBox,
            0.0f, 0.0f, 0.1f,  // default, min, max
            "Frame rate limiter (0.0 = unlimited, 0.02 = ~50 fps)",
            {                  // presets
                {"Unlimited", 0.0f},
                {"60 FPS", 0.01667f},
                {"Original 50 FPS", 0.02f},
                {"30 FPS", 0.03333f}
            }
        );
    }

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[SmoothPatchDupe] Installing...");

        // Get module info
        HMODULE hModule = GetModuleHandle(NULL);
        BYTE* baseAddr;
        size_t imageSize;
        if (!PatchHelper::GetModuleInfo(hModule, &baseAddr, &imageSize)) {
            return Fail("Failed to get module information");
        }

        // Find pattern: 80 7e 60 00 0f 57 c0 f3 0f 11 44 24 10
        BYTE* patternAddr = (BYTE*)PatchHelper::ScanPattern(
            baseAddr,
            imageSize,
            "80 7e 60 00 0f 57 c0 f3 0f 11 44 24 10"
        );

        if (!patternAddr) {
            return Fail("Failed to find pattern");
        }

        LOG_DEBUG("[SmoothPatchDupe] Found pattern at 0x" +
                 std::to_string((uintptr_t)patternAddr));

        // Add 0x25 (37 bytes) to get to the FLD instruction
        BYTE* fldAddr = patternAddr + 0x25;

        // Verify this is an FLD instruction: d9 05 ?? ?? ?? ??
        if (fldAddr[0] != 0xD9 || fldAddr[1] != 0x05) {
            return Fail("Expected FLD instruction not found at calculated offset");
        }

        // Extract the 4-byte address from FLD instruction (little-endian)
        // The address is at bytes 2-5 of the instruction
        uint32_t floatAddress;
        std::memcpy(&floatAddress, fldAddr + 2, 4);
        frameLimitAddress = (void*)(uintptr_t)floatAddress;

        LOG_INFO("[SmoothPatchDupe] Frame limit address: 0x" +
                std::to_string((uintptr_t)frameLimitAddress));

        // Read current value for logging
        float currentValue = *reinterpret_cast<float*>(frameLimitAddress);
        LOG_INFO("[SmoothPatchDupe] Current frame limit: " + std::to_string(currentValue));

        // Apply the patch with the current frameLimitValue
        if (!PatchHelper::WriteProtectedMemory(frameLimitAddress, &frameLimitValue,
                                               sizeof(float), &patchedLocations)) {
            return Fail("Failed to write frame limit value");
        }

        //Bind the setting to the memory address
        BindSettingToAddress("frameLimitValue", frameLimitAddress);

        isEnabled = true;
        LOG_INFO("[SmoothPatchDupe] Successfully installed (Frame limit: " +
                std::to_string(frameLimitValue) + ")");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[SmoothPatchDupe] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        frameLimitAddress = nullptr;
        isEnabled = false;
        LOG_INFO("[SmoothPatchDupe] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(SmoothPatchDupe, {
    .displayName = "Smooth Patch (Frame Limit)",
    .description = "Removes or adjusts the frame rate limiter without touching sleep functions",
    .category = "Performance",
    .experimental = false,
    .targetVersion = GameVersion::All,
    .technicalDetails = {
        "Pattern: 80 7e 60 00 0f 57 c0 f3 0f 11 44 24 10",
        "Offset: +0x25 to FLD instruction (d9 05 ?? ?? ?? ??)",
        "Extracts address from FLD instruction",
        "Modifies float at address (default: 0.02 = ~50 fps)",
        "Set to 0.0 to remove limit, or custom values for different fps caps",
        "The value is seconds per frame (e.g., 0.02 = 50 fps, 0.01667 = 60~ fps)",
        "This does not cap FPS, you'll still most likely want something else for that.",
        "Credits to LazyDutchess for the original SmoothPatch concept"
    }
})
