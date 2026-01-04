#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../allocator_hook.h"
#include "imgui.h"

class MimallocPatch : public OptimizationPatch {
public:
    MimallocPatch() : OptimizationPatch("Mimalloc", nullptr) {}

    bool Install() override {
        // We don't actually install hooks here because they must be installed at DLL load
        // This function just updates the state so it can be saved :D
        isEnabled = true;
        return true;
    }

    bool Uninstall() override {
        // Same as Install, we just update state.
        isEnabled = false;
        return true;
    }

    void RenderCustomUI() override {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "REQUIRES RESTART TO APPLY CHANGES");
        ImGui::TextWrapped("This patch replaces the game's memory allocator with mimalloc and requires a restart to take effect");
        
        // Show actual status
        if (g_mimallocActive) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Current Status: ACTIVE");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Current Status: INACTIVE");
        }

        // Show pending status
        if (isEnabled != g_mimallocActive) {
            ImGui::TextDisabled("(Will be %s on next restart)", isEnabled ? "Active" : "Inactive");
        }
    }
};

REGISTER_PATCH(MimallocPatch, {
    .displayName = "Mimalloc Allocator",
    .description = "Replaces the default MSVCR80 allocator with mimalloc. Improves performance and reduces memory fragmentation.",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = allGameVersionsMask,
    .technicalDetails = {
        "Hooks MSVCR80 malloc/free/realloc/etc.",
        "Redirects memory allocations to mimalloc, a more performant library for better performance and memory management",
        "Game must be restarted to use."
    }
})
