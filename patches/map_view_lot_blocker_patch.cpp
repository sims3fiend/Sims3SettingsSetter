#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <chrono>
#include <thread>

class MapViewLotBlockerPatch : public OptimizationPatch {
private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    std::vector<DetourHelper::Hook> hooks;
    std::thread delayThread;
    volatile bool shouldExit = false;
    volatile bool isCurrentlyPatched = false;

    // Function addresses
    static const uintptr_t WORLD_MANAGER_UPDATE_LOT_STREAMING = 0x00C6C290;
    static const uintptr_t CAMERA_ENABLE_MAP_VIEW_MODE = 0x0073DFB0;
    static const uintptr_t CAMERA_DISABLE_MAP_VIEW_MODE = 0x0073E000;

    // Original bytes at WORLD_MANAGER_UPDATE_LOT_STREAMING
    // 55 8B EC = push ebp; mov ebp, esp
    inline static const BYTE ORIGINAL_BYTES[3] = { 0x55, 0x8B, 0xEC }; //could make this cleaner

    // Patch bytes: ret 10h (C2 10 00) - return and clean 16 bytes from stack
    inline static const BYTE PATCH_BYTES[3] = { 0xC2, 0x10, 0x00 };

    // Function pointers for hooks
    typedef void (__cdecl* Camera_EnableMapViewMode_t)(float);
    typedef void (__cdecl* Camera_DisableMapViewMode_t)(float, float, float);

    Camera_EnableMapViewMode_t originalEnableMapView = nullptr;
    Camera_DisableMapViewMode_t originalDisableMapView = nullptr;

    // Static instance pointer for hooks
    static MapViewLotBlockerPatch* instance;

    bool ApplyFunctionBlock() {
        // Check if already patched
        if (isCurrentlyPatched) {
            return true;
        }

        LOG_DEBUG("[MapViewLotBlocker] Applying function block (patching to ret)");

        // Validate current bytes match what we expect
        if (!PatchHelper::ValidateBytes((LPVOID)WORLD_MANAGER_UPDATE_LOT_STREAMING, ORIGINAL_BYTES, sizeof(ORIGINAL_BYTES))) {
            // Might already be patched or wrong version
            BYTE current[3];
            for (int i = 0; i < 3; i++) {
                current[i] = PatchHelper::ReadByte(WORLD_MANAGER_UPDATE_LOT_STREAMING + i);
            }

            // Check if it's already our patch
            if (current[0] == PATCH_BYTES[0] && current[1] == PATCH_BYTES[1] && current[2] == PATCH_BYTES[2]) {
                LOG_DEBUG("[MapViewLotBlocker] Already patched");
                isCurrentlyPatched = true;
                return true;
            }

            LOG_WARNING("[MapViewLotBlocker] Unexpected bytes at target address");
            return false;
        }

        // Apply the patch
        std::vector<BYTE> patchBytes(PATCH_BYTES, PATCH_BYTES + sizeof(PATCH_BYTES));
        if (!PatchHelper::WriteBytes(WORLD_MANAGER_UPDATE_LOT_STREAMING,
                                     patchBytes,
                                     &patchedLocations)) {
            LOG_ERROR("[MapViewLotBlocker] Failed to write patch bytes");
            return false;
        }

        isCurrentlyPatched = true;
        LOG_INFO("[MapViewLotBlocker] Lot streaming BLOCKED");
        return true;
    }

    bool RemoveFunctionBlock() {
        // Check if not patched
        if (!isCurrentlyPatched) {
            return true;
        }

        LOG_DEBUG("[MapViewLotBlocker] Removing function block (restoring original)");

        // Restore original bytes
        if (!PatchHelper::RestoreAll(patchedLocations)) {
            LOG_ERROR("[MapViewLotBlocker] Failed to restore original bytes");
            return false;
        }

        isCurrentlyPatched = false;
        LOG_INFO("[MapViewLotBlocker] Lot streaming RESTORED");
        return true;
    }

    void DelayedRestoreThreadFunc() {
        LOG_DEBUG("[MapViewLotBlocker] Starting 1-second delay before restore");

        // Wait 1 second
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        if (!shouldExit) {
            RemoveFunctionBlock();
        }
    }

    // Hooked function: called when entering map view
    static void __cdecl HookedEnableMapView(float param) {
        if (instance) {
            LOG_INFO("[MapViewLotBlocker] Map view ENABLED - blocking lot streaming NOW");
            instance->ApplyFunctionBlock();
        }

        // Call original function
        if (instance && instance->originalEnableMapView) {
            instance->originalEnableMapView(param);
        }
    }

    // Hooked function: called when exiting map view
    static void __cdecl HookedDisableMapView(float p1, float p2, float p3) {
        // Call original function first
        if (instance && instance->originalDisableMapView) {
            instance->originalDisableMapView(p1, p2, p3);
        }

        if (instance) {
            LOG_INFO("[MapViewLotBlocker] Map view DISABLED - starting delay before restore");

            // Stop any existing delay thread
            instance->shouldExit = true;
            if (instance->delayThread.joinable()) {
                instance->delayThread.join();
            }

            // Start new delay thread
            instance->shouldExit = false;
            instance->delayThread = std::thread(&MapViewLotBlockerPatch::DelayedRestoreThreadFunc, instance);
        }
    }

public:
    MapViewLotBlockerPatch() : OptimizationPatch("MapViewLotBlockerPatch", nullptr) {
        instance = this;
    }

    ~MapViewLotBlockerPatch() {
        instance = nullptr;

        // Ensure thread is stopped
        shouldExit = true;
        if (delayThread.joinable()) {
            delayThread.join();
        }
    }

    bool Install() override {
        if (isEnabled) return true;

        lastError.clear();
        LOG_INFO("[MapViewLotBlocker] Installing...");

        // Validate that target addresses are accessible
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPVOID)WORLD_MANAGER_UPDATE_LOT_STREAMING, &mbi, sizeof(mbi)) == 0) {
            return Fail("Target function address not accessible");
        }

        if (mbi.State != MEM_COMMIT) {
            return Fail("Target function memory not committed");
        }

        // Validate original bytes
        if (!PatchHelper::ValidateBytes((LPVOID)WORLD_MANAGER_UPDATE_LOT_STREAMING, ORIGINAL_BYTES, sizeof(ORIGINAL_BYTES))) {
            return Fail("Unexpected bytes at target address 0x" + std::to_string(WORLD_MANAGER_UPDATE_LOT_STREAMING) + ". Wrong game version?");
        }

        // Set up function pointers
        originalEnableMapView = reinterpret_cast<Camera_EnableMapViewMode_t>(CAMERA_ENABLE_MAP_VIEW_MODE);
        originalDisableMapView = reinterpret_cast<Camera_DisableMapViewMode_t>(CAMERA_DISABLE_MAP_VIEW_MODE);

        // Set up hooks
        hooks = {
            {(void**)&originalEnableMapView, (void*)HookedEnableMapView},
            {(void**)&originalDisableMapView, (void*)HookedDisableMapView}
        };

        // Install hooks
        if (!DetourHelper::InstallHooks(hooks)) {
            return Fail("Failed to install camera function hooks");
        }

        isEnabled = true;
        LOG_INFO("[MapViewLotBlocker] Successfully installed with function hooks");
        LOG_INFO("[MapViewLotBlocker] Hooked Camera_EnableMapViewMode at 0x" + std::to_string(CAMERA_ENABLE_MAP_VIEW_MODE));
        LOG_INFO("[MapViewLotBlocker] Hooked Camera_DisableMapViewMode at 0x" + std::to_string(CAMERA_DISABLE_MAP_VIEW_MODE));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[MapViewLotBlocker] Uninstalling...");

        // Signal thread to exit
        shouldExit = true;

        // Wait for delay thread to finish if running
        if (delayThread.joinable()) {
            delayThread.join();
        }

        // Remove hooks
        if (!DetourHelper::RemoveHooks(hooks)) {
            LOG_WARNING("[MapViewLotBlocker] Failed to remove hooks (may be okay if game is closing)");
        }

        // Restore any active patches
        if (isCurrentlyPatched) {
            if (!RemoveFunctionBlock()) {
                return Fail("Failed to restore original function during uninstall");
            }
        }

        // Clean up
        patchedLocations.clear();
        isCurrentlyPatched = false;

        isEnabled = false;
        LOG_INFO("[MapViewLotBlocker] Successfully uninstalled");
        return true;
    }
};

// Static instance pointer initialization
MapViewLotBlockerPatch* MapViewLotBlockerPatch::instance = nullptr;

// Register the patch
REGISTER_PATCH(MapViewLotBlockerPatch, {
    .displayName = "Map View Lot Streaming Blocker",
    .description = "Prevents lot loading while in map view mode, reduces stutter/slowdown when exiting/entering map view",
    .category = "Performance",
    .experimental = false,
    .targetVersion = GameVersion::Steam,
    .technicalDetails = {
        "Hooks Camera_EnableMapViewMode at 0x0073DFB0",
        "Hooks Camera_DisableMapViewMode at 0x0073E000",
        "Patches WorldManager_UpdateLotStreaming(spec name) at 0x00C6C290 to 'ret 10h'",
        "Blocks lot streaming when entering map view",
        "Restores lot streaming 1 second after exiting map view (for zoom animation)"
    }
})
