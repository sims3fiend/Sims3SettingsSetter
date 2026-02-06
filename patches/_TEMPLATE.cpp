// Template for creating new patches
// Copy this file, rename it, and modify for your patch

#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

// Rename the class to something descriptive
class TemplatePatch : public OptimizationPatch {
  private:
    // Track patched locations for restoration
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    // Add any settings/state you need here, eg:
    // int mySetting = 50;

  public:
    // Change the name to match your class
    TemplatePatch() : OptimizationPatch("TemplatePatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear(); // Clear any previous error

        // Replace with your patch name
        LOG_INFO("[TemplatePatch] Installing...");

        // OPTION 1: Simple hardcoded address patch
        // ----------------------------------------
        /*
        BYTE expectedOld = 0x74;  // What we expect to find (for validation)
        if (!PatchHelper::WriteByte(0x12345678, 0xEB, &patchedLocations, &expectedOld)) {
            return Fail("Failed to patch at 0x12345678");
        }

        isEnabled = true;
        LOG_INFO("[TemplatePatch] Successfully installed");
        return true;
        */

        // OPTION 2: Pattern scanning
        // ---------------------------
        /*
        HMODULE hModule = GetModuleHandle(NULL);
        BYTE* baseAddr;
        size_t imageSize;

        if (!PatchHelper::GetModuleInfo(hModule, &baseAddr, &imageSize)) {
            return Fail("Failed to get module information");
        }

        // Just copy pattern from your disassembler! ? = wildcard
        BYTE* found = baseAddr;
        int patchCount = 0;

        while ((found = (BYTE*)PatchHelper::ScanPattern(found, imageSize - (found - baseAddr), "48 89 5C ? ? 08"))) {
            // Patch at offset from pattern
            if (PatchHelper::WriteByte((uintptr_t)(found + 12), 0xEB, &patchedLocations)) {
                patchCount++;
                LOG_DEBUG("[TemplatePatch] Patched at 0x" +
                         std::to_string((uintptr_t)(found + 12)));
            }
            found += 6;  // Move past this match (pattern length)
        }

        if (patchCount == 0) {
            return Fail("Failed to find any matching patterns");
        }

        isEnabled = true;
        LOG_INFO("[TemplatePatch] Successfully installed (" +
                std::to_string(patchCount) + " locations patched)");
        return true;
        */

        // OPTION 3: Multiple bytes or complex logic
        // ------------------------------------------
        /*
        // Example: NOP out 5 bytes
        if (!PatchHelper::WriteNOP(0x12345678, 5, &patchedLocations)) {
            return Fail("Failed to NOP at 0x12345678");
        }

        // Example: Write multiple bytes
        std::vector<BYTE> newBytes = {0x90, 0x90, 0xEB, 0x05};
        std::vector<BYTE> expectedOld = {0x74, 0x05, 0xE8, 0x12};
        if (!PatchHelper::WriteBytes(0x87654321, newBytes, &patchedLocations, &expectedOld)) {
            return Fail("Failed to write bytes at 0x87654321");
        }

        // Example: Write a DWORD
        DWORD expectedDWord = 0x80;
        if (!PatchHelper::WriteDWORD(0x404573, 0x10000080, &patchedLocations, &expectedDWord)) {
            return Fail("Failed to write DWORD at 0x404573");
        }

        isEnabled = true;
        LOG_INFO("[TemplatePatch] Successfully installed");
        return true;
        */

        // Remove comments and implement your patch logic
        return Fail("Not implemented - please edit this template!");
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear(); // Clear any previous error

        LOG_INFO("[TemplatePatch] Uninstalling...");

        // Restore all patched locations
        if (!PatchHelper::RestoreAll(patchedLocations)) { return Fail("Failed to restore original bytes"); }

        isEnabled = false;
        LOG_INFO("[TemplatePatch] Successfully uninstalled");
        return true;
    }

    // OPTIONAL: Add settings with automatic UI, save/load
    // -----------------------------------------------------
    // Register settings in the constructor - they'll automatically appear in ImGui,
    // get saved/loaded, and you can even bind them to memory addresses for auto-patching.

    // EXAMPLE: Add this to your constructor
    /*
    TemplatePatch() : OptimizationPatch("TemplatePatch", nullptr) {
        // Float setting with presets
        RegisterFloatSetting(&myFloatValue, "myFloatValue", SettingUIType::Slider,
            1.0f, 0.0f, 10.0f,  // default, min, max
            "What this setting does",
            {{"Low", 0.5f}, {"High", 2.0f}}  // optional presets
        );

        // Other setting types: RegisterIntSetting, RegisterBoolSetting, RegisterEnumSetting
        // See smooth_patch_dupe.cpp for a complete example
    }
    */

    // OPTIONAL: Custom ImGui UI
    // -------------------------
    // If you need custom UI beyond the automatic settings, override RenderCustomUI.
    // If you don't override it, registered settings are automatically rendered.
    /*
    void RenderCustomUI() override {
        SAFE_IMGUI_BEGIN();  // Required! Safely checks ImGui context

        // Use full ImGui API (included from "imgui/imgui.h")
        ImGui::Text("My custom UI");
        if (ImGui::Button("Click me")) {
            // do something
        }

        // Optionally render base settings too
        // OptimizationPatch::RenderCustomUI();
    }
    */
};

// Update all of these fields
REGISTER_PATCH(TemplatePatch, {.displayName = "Template Patch",                                 // Display name in GUI
                                  .description = "This is a template for creating new patches", // Short description
                                  .category = "Experimental",                                   // Can be anything
                                  .experimental = true,                                         // Mark as experimental if risky/untested
                                  .targetVersion = GameVersion::Steam,                          // Steam, EA, or All
                                  .technicalDetails = {                                         // Technical info shown in tooltip
                                      "Add technical details about what this patch does", "Mention any risks or compatibility issues, etc."}})