#include "../logger.h"
#include "../patch_helpers.h"
#include "../patch_system.h"

class SmoothPatchClassic : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    int customTPS = 500;

    int CalculateMSPT() const {
        if (customTPS <= 0) return customTPS; // Special modes: 0 = system, -1 = uncapped
        return 1000 / customTPS;
    }

public:
    SmoothPatchClassic() : OptimizationPatch("SmoothPatchClassic", nullptr) {
        RegisterIntSetting(&customTPS, "customTPS", 500, -1, 2000,
            "TPS (Ticks Per Second, 0 = system default, -1 = uncapped)",
            {}, SettingUIType::InputBox);
    }

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[SmoothPatchClassic] Installing...");

        HMODULE hModule = GetModuleHandle(NULL);
        BYTE* baseAddr;
        size_t imageSize;

        if (!PatchHelper::GetModuleInfo(hModule, &baseAddr, &imageSize)) {
            return Fail("Failed to get module information");
        }

        BYTE* found = baseAddr;
        int patchCount = 0;
        int mspt = CalculateMSPT();

        while ((found = (BYTE*)PatchHelper::ScanPattern(found, imageSize - (found - baseAddr),
                                                         "8B 44 24 04 8B 08 6A 01 51 FF"))) {

            std::vector<BYTE> patch;

            if (mspt == -1) {
                // Uncapped mode: RET immediately (skip sleep entirely)
                patch = {0xC3};
            }
            else if (mspt == 0) {
                // sleep for 0ms
                patch = {0xB9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x6A, 0x00};
            }
            else {
                // Normal capped mode: MOV ECX, [mspt] + NOP
                patch = {0xB9};
                patch.push_back(mspt & 0xFF);
                patch.push_back((mspt >> 8) & 0xFF);
                patch.push_back((mspt >> 16) & 0xFF);
                patch.push_back((mspt >> 24) & 0xFF);
                patch.push_back(0x90);
            }

            if (!PatchHelper::WriteBytes((uintptr_t)found, patch, &patchedLocations)) {
                LOG_ERROR("[SmoothPatchClassic] Failed to patch at 0x" +
                         std::to_string((uintptr_t)found));
                found += 10;
                continue;
            }

            patchCount++;
            LOG_DEBUG("[SmoothPatchClassic] Patched at 0x" + std::to_string((uintptr_t)found));
            found += 10;
        }

        if (patchCount == 0) {
            return Fail("Failed to find any matching patterns");
        }

        isEnabled = true;
        std::string modeDesc;
        if (mspt == -1) {
            modeDesc = "uncapped (no sleep)";
        } else if (mspt == 0) {
            modeDesc = "system default (0ms sleep)";
        } else {
            modeDesc = std::to_string(customTPS) + " TPS, " + std::to_string(mspt) + "ms sleep per tick";
        }
        LOG_INFO("[SmoothPatchClassic] Successfully installed (" +
                std::to_string(patchCount) + " locations patched, " + modeDesc + ")");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[SmoothPatchClassic] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        LOG_INFO("[SmoothPatchClassic] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(SmoothPatchClassic, {
    .displayName = "Smooth Patch (Original Flavour)",
    .description = "It's smooth patch alright",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = allGameVersionsMask,
    .technicalDetails = {
        "Basically 1-1 of smooth patch",
        "Hardcodes sleep duration to whatever the maths says",
        "MSPT = 1000 / TPS (e.g., 500 TPS = 2ms sleep)",
        "Original game runs at 50 'TPS' (20ms sleep)",
        "Takes precedence over Smooth Patch Dupe but wont conflict",
        "Credits: LazyDuchess, Shapes (me), and Foul Play"
    }
})
