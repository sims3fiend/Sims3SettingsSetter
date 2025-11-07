#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

// Optimized lot streaming settings patch xoxo
class LotStreamingSettingsPatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    // Track which live settings we need to apply (New! Defered!)
    struct PendingLiveSetting {
        std::wstring name;
        Setting::ValueType value;
    };
    std::vector<PendingLiveSetting> pendingLiveSettings;

    // Helper to apply or defer a live setting
    template<typename T>
    bool ApplyOrDeferLiveSetting(const wchar_t* name, T value) {
        if (LiveSetting::Exists(name)) {
            // Setting exists, apply immediately
            if (!LiveSetting::Patch(name, value, &patchedLocations)) {
                return false;
            }
            LOG_INFO("[LotStreamingSettingsPatch] + Applied '" + Utils::WideToUtf8(name) + "'");
            return true;
        } else {
            // Setting doesn't exist yet, store for later
            LOG_WARNING("[LotStreamingSettingsPatch] '" + Utils::WideToUtf8(name) + "' not found - will retry when available");
            pendingLiveSettings.push_back({name, value});

            // Also store in SettingsManager for when it gets registered
            SettingsManager::Get().StorePendingSavedValue(name, value);
            return true;
        }
    }

public:
    LotStreamingSettingsPatch() : OptimizationPatch("LotStreamingSettings", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotStreamingSettingsPatch] Installing...");

        // 1. Enable "Throttle Lot LoD Transitions" setting
        if (!ApplyOrDeferLiveSetting(L"Throttle Lot LoD Transitions", true)) {
            return Fail("Failed to enable 'Throttle Lot LoD Transitions'");
        }

        // 2. Set "Camera speed threshold" to 5.0 to slow loading down a bit - need to recheck this tho
        if (!ApplyOrDeferLiveSetting(L"Camera speed threshold", 5.0f)) {
            return Fail("Failed to set 'Camera speed threshold'");
        }

        isEnabled = true;
        LOG_INFO("[LotStreamingSettingsPatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[LotStreamingSettingsPatch] Uninstalling...");

        // RestoreAll automatically restores all tracked patches
        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original values");
        }

        // Clear pending settings list
        pendingLiveSettings.clear();

        isEnabled = false;
        LOG_INFO("[LotStreamingSettingsPatch] Successfully uninstalled");
        return true;
    }

    // Override RenderCustomUI to retry pending settings periodically
    void RenderCustomUI() override {
        // Try to apply any pending settings that might now be available
        if (!pendingLiveSettings.empty()) {
            auto it = pendingLiveSettings.begin();
            while (it != pendingLiveSettings.end()) {
                const auto& pending = *it;

                // Check if setting is now available
                if (LiveSetting::Exists(pending.name.c_str())) {
                    // Try to apply it
                    bool applied = std::visit([&](auto&& value) -> bool {
                        using T = std::decay_t<decltype(value)>;
                        if (LiveSetting::Patch(pending.name.c_str(), value, &patchedLocations)) {
                            LOG_INFO("[LotStreamingSettingsPatch] Successfully applied deferred setting: " +
                                   Utils::WideToUtf8(pending.name));
                            return true;
                        }
                        return false;
                    }, pending.value);

                    if (applied) {
                        // Remove from pending list
                        it = pendingLiveSettings.erase(it);
                        continue;
                    }
                }
                ++it;
            }
        }
    }
};

// Auto-register the patch
REGISTER_PATCH(LotStreamingSettingsPatch, {
    .displayName = "Optimized Lot Streaming Settings",
    .description = "Optimizes lot streaming behavior by enabling throttle LoD transitions and adjusting camera speed threshold.",
    .category = "Performance",
    .experimental = false,
    .targetVersion = GameVersion::All,
    .technicalDetails = {
        "Enables 'Throttle Lot LoD Transitions' setting",
        "Sets 'Camera speed threshold' to 5.0 (lower = more time between stopping camera movement and lot loading)",
        "Reduces stutter when playing with lot counts >1",
        "Cross-platform compatible, just sets the live settings"
    }
})
