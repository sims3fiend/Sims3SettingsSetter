# Patches System
The /patches folder contains all game patches organized in a modular way. Each patch automatically registers itself and appears in the GUI, there's also a bunch of helper functions to make life easier. I've made this system to make developing patches for the game easier for me and hopefuly YOU 🫵, if you can think of anything to change or add lmk!

## Adding a new patch
### Simple Byte Patches
For simple byte changes, create a new `.cpp` file in the /patches folder:

```cpp
#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class MySimplePatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    MySimplePatch() : OptimizationPatch("MySimplePatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();  // Clear any previous errors
        LOG_INFO("[MySimplePatch] Installing...");

        // Example: Change a byte at specific address
        BYTE expectedOld = 0x74;  // Validate what we expect to find
        if (!PatchHelper::WriteByte(0x12345678, 0xEB, &patchedLocations, &expectedOld)) {
            return Fail("Failed to patch at 0x12345678");
        }

        isEnabled = true;
        LOG_INFO("[MySimplePatch] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();  // Clear any previous errors
        LOG_INFO("[MySimplePatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        LOG_INFO("[MySimplePatch] Successfully uninstalled");
        return true;
    }
};

// Register with metadata, this is used for the IMGUI portion
REGISTER_PATCH(MySimplePatch, {
    .displayName = "My Simple Patch",
    .description = "Does something cool",
    .category = "Performance",  // or "Graphics", "Experimental", "Quality of Life", etc. Try and use an existing one pls.
    .experimental = false, //adds a [EXPERIMENTAL] tag
    .targetVersion = GameVersion::Steam,  // Steam, EA, or All
    .technicalDetails = { //Just a mini-readme, I should change the naming actually... Shows on hover.
        "Modifies address 0x12345678",
        "Changes JZ to JMP for better performance"
    }
})
```

### Complex Pattern-Based Patches

You can also do patches that need pattern scanning or complex logic:

```cpp
#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <Psapi.h>

class ComplexPatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    ComplexPatch() : OptimizationPatch("ComplexPatch", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[ComplexPatch] Installing...");

        // Get module info
        HMODULE hModule = GetModuleHandle(NULL);
        BYTE* baseAddr;
        size_t imageSize;
        if (!PatchHelper::GetModuleInfo(hModule, &baseAddr, &imageSize)) {
            return Fail("Failed to get module information");
        }

        int patchCount = 0;

        // String-based pattern - just copypaste from the disassembler
        BYTE* found = baseAddr;
        while ((found = (BYTE*)PatchHelper::ScanPattern(found, imageSize - (found - baseAddr),
                                                         "48 89 5C 24 ?"))) {
            // Patch at offset from pattern
            if (PatchHelper::WriteByte((uintptr_t)(found + 12), 0xEB, &patchedLocations)) {
                patchCount++;
                LOG_DEBUG("[ComplexPatch] Patched at 0x" + std::to_string((uintptr_t)(found + 12)));
            }
            found += 5;  // Move past this match
        }

        if (patchCount == 0) {
            return Fail("Failed to find any matching patterns");
        }

        isEnabled = true;
        LOG_INFO("[ComplexPatch] Successfully installed (" + std::to_string(patchCount) + " patches)");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore original bytes");
        }

        isEnabled = false;
        return true;
    }
};

REGISTER_PATCH(ComplexPatch, {
    .displayName = "Complex Pattern Patch",
    .description = "Uses pattern scanning to find and patch multiple locations",
    .category = "Performance",
    .experimental = true,
    .targetVersion = GameVersion::All,
    .technicalDetails = {
        "Scans for pattern: 48 89 5C 24 ?",
        "lalalala"
    }
})
```

### Patches with Configurable Settings

For patches with user-configurable settings, register them in your constructor. Much simpler than DIYing it yourself.

**Example usage:**
```cpp
class MyPatch : public OptimizationPatch {
private:
    float myFloat = 0.0f;
    int myInt = 50;

public:
    MyPatch() : OptimizationPatch("MyPatch", nullptr) {
        // Register a float with presets
        RegisterFloatSetting(&myFloat, "floatSetting", SettingUIType::InputBox,
            0.0f, 0.0f, 10.0f, "Description",
            {{"Low", 1.0f}, {"High", 9.0f}});

        RegisterIntSetting(&myInt, "intSetting", 50, 0, 100, "An integer");
    }

    bool Install() override {
        //your patching logic using myFloat and myInt

        // Bind setting to memory so it auto-reapplies (might make this default behaviour...)
        BindSettingToAddress("floatSetting", memoryAddress);
        return true;
    }
};
```

**Available methods:**
- `RegisterFloatSetting(ptr, name, uiType, default, min, max, desc, presets)` - Float with InputBox/Slider/Drag UI
- `RegisterIntSetting(ptr, name, default, min, max, desc, presets)` - Integer with slider
- `RegisterBoolSetting(ptr, name, default, desc)` - Boolean checkbox
- `RegisterEnumSetting(ptr, name, default, desc, choices)` - Dropdown choices
- `BindSettingToAddress(name, address)` - Auto-reapply when setting changes in UI

### Patches with Custom UI (Manual Approach)

If you need complete control over the UI, you can still manually implement custom ImGui controls:

```cpp
#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../imgui/imgui.h"  // Include ImGui for custom UI

class CustomUIPatch : public OptimizationPatch {
private:
    int mySetting = 50;
    float floatSetting = 1.0f;
    bool boolSetting = false;
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    CustomUIPatch() : OptimizationPatch("CustomUIPatch", nullptr) {}

    bool Install() override {
        // ... install logic using mySetting ...
        return true;
    }

    bool Uninstall() override {
        return PatchHelper::RestoreAll(patchedLocations);
    }

    // Override to add custom ImGui controls
    void RenderCustomUI() override {
        #ifdef IMGUI_VERSION
        if (!ImGui::GetCurrentContext()) return;

        // Integer slider
        if (ImGui::SliderInt("My Setting", &mySetting, 0, 100)) {
            // Reapply patch when value changes
            if (isEnabled) {
                Uninstall();
                Install();
            }
        }
        ImGui::TextDisabled("Range: 0-100");

        // Float slider
        if (ImGui::SliderFloat("Float Value", &floatSetting, 0.0f, 10.0f)) {
            if (isEnabled) {
                Uninstall();
                Install();
            }
        }

        // Boolean checkbox
        if (ImGui::Checkbox("Enable Feature", &boolSetting)) {
            if (isEnabled) {
                Uninstall();
                Install();
            }
        }

        ImGui::Separator();
        ImGui::TextDisabled("Current values: Int=%d, Float=%.2f, Bool=%s",
                          mySetting, floatSetting, boolSetting ? "true" : "false");
        #endif
    }

    // OPTIONAL: Save/load state for settings persistence in presets
    void SaveState(std::ofstream& file) const override {
        file << "[Optimization_" << patchName << "]\n";
        file << "Enabled=" << (isEnabled ? "true" : "false") << "\n";
        file << "MySetting=" << mySetting << "\n";
        file << "FloatSetting=" << floatSetting << "\n";
        file << "BoolSetting=" << (boolSetting ? "true" : "false") << "\n\n";
    }

    bool LoadState(const std::string& value) override {
        // Called for "Enabled" key
        if (value == "true") {
            return Install();
        } else if (value == "false") {
            return Uninstall();
        }
        return false;
    }

    // OPTIONAL: Custom loader for your additional settings
    bool LoadCustomSetting(const std::string& key, const std::string& value) {
        if (key == "MySetting") {
            try {
                mySetting = std::stoi(value);
                return true;
            } catch (...) { return false; }
        } else if (key == "FloatSetting") {
            try {
                floatSetting = std::stof(value);
                return true;
            } catch (...) { return false; }
        } else if (key == "BoolSetting") {
            boolSetting = (value == "true");
            return true;
        }
        return false;
    }
};

REGISTER_PATCH(CustomUIPatch, {
    .displayName = "Patch with Settings",
    .description = "A patch with configurable settings",
    .category = "Experimental",
    .experimental = false,
    .targetVersion = GameVersion::Steam,
    .technicalDetails = {
        "Supports custom configuration",
        "Can be adjusted at runtime"
    }
})
```

### Registering Existing Classes

If you have an existing patch class in another file (like my old patches I cba porting):

```cpp
#include "../patch_system.h"
#include "../my_existing_patch.h"

REGISTER_PATCH(MyExistingPatchClass, {
    .displayName = "My Existing Patch",
    .description = "Registers an existing patch class",
    .category = "Performance",
    .experimental = false,
    .targetVersion = GameVersion::All,
    .technicalDetails = {
        "Implementation in my_existing_patch.h/cpp"
    }
})
```

## Available Helper Functions

### DetourHelper Namespace

For function hooking with Microsoft Detours:

- `InstallHooks(hooks)` - Install multiple function hooks in a single transaction
- `RemoveHooks(hooks)` - Remove multiple function hooks in a single transaction

Example usage:
```cpp
#include "../patch_helpers.h"

class MyHookPatch : public OptimizationPatch {
private:
    typedef void (WINAPI* SomeFunction_t)(int param);
    SomeFunction_t originalFunction;
    std::vector<DetourHelper::Hook> hooks;

    static MyHookPatch* instance;

    static void WINAPI HookedFunction(int param) {
        // Do something before
        instance->originalFunction(param);
        // Do something after
    }

public:
    bool Install() override {
        originalFunction = (SomeFunction_t)GetProcAddress(
            GetModuleHandle(L"kernel32.dll"), "SomeFunction");

        hooks = {
            {(void**)&originalFunction, (void*)HookedFunction}
        };

        if (!DetourHelper::InstallHooks(hooks)) {
            return Fail("Failed to install hooks");
        }

        isEnabled = true;
        return true;
    }

    bool Uninstall() override {
        if (!DetourHelper::RemoveHooks(hooks)) {
            return Fail("Failed to remove hooks");
        }
        isEnabled = false;
        return true;
    }
};
```

### LiveSetting Namespace

Useful for accessing the live settings values, not really sure why you'd use em vs a preset but hey

- `GetAddress(name)` - Get memory address of a setting by name
- `GetValue(name, outValue)` - Read current value of a setting
- `Patch(name, newValue, tracker)` - Modify a setting with tracking for restore
- `Exists(name)` - Check if a setting exists

Example usage:
```cpp
#include "../patch_helpers.h"

class StreamingSettingsPatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

public:
    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[StreamingSettings] Installing...");

        // Check if setting exists (optional)
        if (!LiveSetting::Exists(L"Camera speed threshold")) {
            return Fail("Camera speed threshold setting not found????");
        }

        // Patch a live setting by name with automatic tracking
        if (!LiveSetting::Patch(L"Camera speed threshold", 5.0f, &patchedLocations)) {
            return Fail("Failed to set camera speed threshold");
        }

        // Or get the address for manual patching
        void* addr = LiveSetting::GetAddress(L"Some Setting");
        if (addr) {
            // Do custom patching...
        }

        // Or just read a value without modifying
        float currentValue;
        if (LiveSetting::GetValue(L"Camera speed threshold", currentValue)) {
            LOG_INFO("Current camera speed: " + std::to_string(currentValue));
        }

        isEnabled = true;
        LOG_INFO("[StreamingSettings] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        // RestoreAll automatically restores tracked changes
        if (!PatchHelper::RestoreAll(patchedLocations)) {
            return Fail("Failed to restore settings");
        }

        isEnabled = false;
        return true;
    }
};
```

### PatchHelper Namespace

#### Writing to Memory
- `WriteByte(address, value, tracker, expectedOld)` - Write a single byte
- `WriteBytes(address, bytes, tracker, expectedOld)` - Write multiple bytes
- `WriteDWORD(address, value, tracker, expectedOld)` - Write a 4-byte value
- `WriteWORD(address, value, tracker, expectedOld)` - Write a 2-byte value
- `WriteNOP(address, count, tracker)` - NOP out bytes (0x90)
- `WriteRelativeJump(address, destination, tracker)` - Write E9 JMP instruction
- `WriteRelativeCall(address, destination, tracker)` - Write E8 CALL instruction

All write functions automatically:
- Track original bytes for restoration
- Handle memory protection
- Verify the write succeeded
- Optionally validate expected old value

#### Reading from Memory
- `ReadByte(address)` - Read a single byte
- `ReadWORD(address)` - Read a 2-byte value
- `ReadDWORD(address)` - Read a 4-byte value

#### Pattern Scanning
- `ScanPattern(start, size, "48 89 5C ? ? 08")` - String-based pattern scanning with wildcards

#### Utilities
- `RestoreAll(locations)` - Restore all patched locations
- `ValidateBytes(address, expected, size)` - Check if bytes match
- `GetModuleInfo(hModule, &baseAddr, &imageSize)` - Get module info
- `CalculateRelativeOffset(from, to, instructionSize)` - Calculate relative jump/call offset

## Categories

Currently arbitrary, defaults to "General"

## Version Targeting

Patches can specify which game version they support using the `targetVersion` field:

- **GameVersion::Steam** - Works only on ts3w.exe (Steam version).
- **GameVersion::EA** - Works only on ts3.exe (EA/Origin version). 
- **GameVersion::All** - Works on all versions, for when you've got a banger pattern/using IAT stuff

Patches incompatible with the current version will be shown in the GUI but greyed out and unselectable, mostly to further add to EA users misery.

## Helper Examples

### Reading Memory Before Patching

```cpp
// Read and log current values for debugging
DWORD currentFlags = PatchHelper::ReadDWORD(0x404573);
LOG_DEBUG("Current flags: 0x" + std::to_string(currentFlags));

// Check before modifying
BYTE currentByte = PatchHelper::ReadByte(0x12345678);
if (currentByte != 0x74) {
    return Fail("Ruhroh, Unexpected byte found!");
}
```

### Pattern Scanning (wildcards yipee!)

```cpp
// Chuck pattern in from dissasembler
BYTE* addr = (BYTE*)PatchHelper::ScanPattern(baseAddr, imageSize, "48 89 5C ? ? 08");
if (addr) {
    // Patch at offset
    PatchHelper::WriteByte((uintptr_t)(addr + 5), 0xEB, &patchedLocations);
}

// Multiple matches
BYTE* found = baseAddr;
while ((found = (BYTE*)PatchHelper::ScanPattern(found, imageSize - (found - baseAddr),
                                                 "E8 ? ? ? ?"))) {
    // Process each match
    found += 5;  // Move past this match, etc.
}
```

### Hooking Functions
Might change this later to be less bad/closer to detours
```cpp
// Redirect function call to your hook
uintptr_t originalFuncAddr = 0x12345678;
uintptr_t myHookAddr = (uintptr_t)&MyHookFunction;

// Write JMP to hook
PatchHelper::WriteRelativeJump(originalFuncAddr, myHookAddr, &patchedLocations);

// Or redirect a CALL instruction
PatchHelper::WriteRelativeCall(0x87654321, myHookAddr, &patchedLocations);
```

## Error Handling

The base class provides a `Fail()` helper that:
- Sets the error message (shown in GUI tooltip)
- Logs the error to the log file
- Returns false

```cpp
// Instead of this:
if (!someOperation()) {
    LOG_ERROR("[MyPatch] Something failed");
    return false;
}

// do this:
if (!someOperation()) {
    return Fail("Something failed");
}
```
Errors appear in the GUI as red text with tooltips showing the message, as well as in the log.

## Tips
(mostly for myself...)
- idk