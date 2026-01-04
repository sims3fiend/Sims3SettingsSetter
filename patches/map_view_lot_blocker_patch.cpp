#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>

class MapViewLotBlockerPatch : public OptimizationPatch {
private:
    std::vector<DetourHelper::Hook> hooks;
    std::thread delayThread;
    std::condition_variable threadCV;
    std::mutex threadMutex;
    std::atomic<bool> shouldExit{ false };
    std::atomic<bool> blockLotStreaming{ false };  // Changed from isCurrentlyPatched

    // Function addresses
    static const uintptr_t SPEC_WORLD_MANAGER_UPDATE = 0x00C6D570;
    static const uintptr_t CAMERA_ENABLE_MAP_VIEW_MODE = 0x0073DFB0;
    static const uintptr_t CAMERA_DISABLE_MAP_VIEW_MODE = 0x0073E000;

    // WorldManager offset for lot streaming skip flag
    static const uintptr_t WORLD_MANAGER_LOT_SKIP_OFFSET = 0x258;

    // Function pointers for hooks
    typedef void (__cdecl* Camera_EnableMapViewMode_t)(float);
    typedef void (__cdecl* Camera_DisableMapViewMode_t)(float, float, float);
    typedef int (__thiscall* WorldManager_Update_t)(void* worldMgr, float param2, float param3);

    Camera_EnableMapViewMode_t originalEnableMapView = nullptr;
    Camera_DisableMapViewMode_t originalDisableMapView = nullptr;
    WorldManager_Update_t originalWorldManagerUpdate = nullptr;

    // Static instance pointer for hooks
    static MapViewLotBlockerPatch* instance;

    void EnableLotBlocking() {
        blockLotStreaming.store(true);
        LOG_INFO("[MapViewLotBlocker] Lot streaming BLOCKED");
    }

    void DisableLotBlocking() {
        blockLotStreaming.store(false);
        LOG_INFO("[MapViewLotBlocker] Lot streaming RESTORED");
    }

    void DelayedRestoreThreadFunc() {
        LOG_DEBUG("[MapViewLotBlocker] Starting 1-second delay before restore");

        // Wait 1 second with interruptible sleep
        std::unique_lock<std::mutex> lock(threadMutex);
        if (threadCV.wait_for(lock, std::chrono::milliseconds(1000), [this] { return shouldExit.load(); })) {
            // shouldExit became true, exit early
            LOG_DEBUG("[MapViewLotBlocker] Delay thread interrupted");
            return;
        }

        // Timeout expired and shouldExit is still false, proceed with restore
        if (!shouldExit.load()) {
            DisableLotBlocking();
        }
    }

    // Hooked WorldManager_Update - manipulates the skip flag
    static int __fastcall HookedWorldManagerUpdate(void* worldMgr, void* unused, float param2, float param3) {
        if (instance && instance->blockLotStreaming.load()) {
            // Temporarily set the skip flag to prevent lot processing
            char* skipFlag = (char*)worldMgr + WORLD_MANAGER_LOT_SKIP_OFFSET;
            char originalValue = *skipFlag;
            *skipFlag = 1;  // Tell the game to skip lot processing

            // Call original function
            int result = instance->originalWorldManagerUpdate(worldMgr, param2, param3);

            // Restore original flag value
            *skipFlag = originalValue;
            return result;
        }

        // Not blocking, call original
        return instance->originalWorldManagerUpdate(worldMgr, param2, param3);
    }

    // Hooked function: called when entering map view
    static void __cdecl HookedEnableMapView(float param) {
        if (instance) {
            LOG_INFO("[MapViewLotBlocker] Map view ENABLED - blocking lot streaming NOW");
            instance->EnableLotBlocking();
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
            instance->shouldExit.store(true);
            instance->threadCV.notify_all();  // Wake up waiting thread
            if (instance->delayThread.joinable()) {
                instance->delayThread.join();
            }

            // Start new delay thread
            instance->shouldExit.store(false);
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
        shouldExit.store(true);
        threadCV.notify_all();
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
        if (VirtualQuery((LPVOID)SPEC_WORLD_MANAGER_UPDATE, &mbi, sizeof(mbi)) == 0) {
            return Fail("Target function address not accessible");
        }

        if (mbi.State != MEM_COMMIT) {
            return Fail("Target function memory not committed");
        }

        // Set up function pointers
        originalWorldManagerUpdate = reinterpret_cast<WorldManager_Update_t>(SPEC_WORLD_MANAGER_UPDATE);
        originalEnableMapView = reinterpret_cast<Camera_EnableMapViewMode_t>(CAMERA_ENABLE_MAP_VIEW_MODE);
        originalDisableMapView = reinterpret_cast<Camera_DisableMapViewMode_t>(CAMERA_DISABLE_MAP_VIEW_MODE);

        // Set up hooks
        hooks = {
            {(void**)&originalWorldManagerUpdate, (void*)HookedWorldManagerUpdate},
            {(void**)&originalEnableMapView, (void*)HookedEnableMapView},
            {(void**)&originalDisableMapView, (void*)HookedDisableMapView}
        };

        // Install hooks
        if (!DetourHelper::InstallHooks(hooks)) {
            return Fail("Failed to install hooks");
        }

        isEnabled = true;
        LOG_INFO("[MapViewLotBlocker] Successfully installed with function hooks");
        LOG_INFO("[MapViewLotBlocker] Hooked SPEC_WorldManager_Update at 0x" + std::to_string(SPEC_WORLD_MANAGER_UPDATE));
        LOG_INFO("[MapViewLotBlocker] Hooked Camera_EnableMapViewMode at 0x" + std::to_string(CAMERA_ENABLE_MAP_VIEW_MODE));
        LOG_INFO("[MapViewLotBlocker] Hooked Camera_DisableMapViewMode at 0x" + std::to_string(CAMERA_DISABLE_MAP_VIEW_MODE));
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[MapViewLotBlocker] Uninstalling...");

        // Signal thread to exit
        shouldExit.store(true);
        threadCV.notify_all();

        // Wait for delay thread to finish if running
        if (delayThread.joinable()) {
            delayThread.join();
        }

        // Remove hooks
        if (!DetourHelper::RemoveHooks(hooks)) {
            LOG_WARNING("[MapViewLotBlocker] Failed to remove hooks (may be okay if game is closing)");
        }

        // Ensure blocking is disabled
        blockLotStreaming.store(false);

        isEnabled = false;
        LOG_INFO("[MapViewLotBlocker] Successfully uninstalled");
        return true;
    }
};

// Static instance pointer initialization
MapViewLotBlockerPatch* MapViewLotBlockerPatch::instance = nullptr;

// Auto-register the patch
REGISTER_PATCH(MapViewLotBlockerPatch, {
    .displayName = "Map View Lot Streaming Blocker",
    .description = "Prevents lot loading while in map view mode, reduces stutter/slowdown when exiting/entering map view.",
    .category = "Performance",
    .experimental = true,
    .supportedVersions = 1 << GameVersion::Steam_1_67_2_024037,
    .technicalDetails = {
        "Hooks SPEC_WorldManager_Update at 0x00C6D570",
        "Hooks Camera_EnableMapViewMode at 0x0073DFB0",
        "Hooks Camera_DisableMapViewMode at 0x0073E000",
        "Sets WorldManager skip flag at offset +0x258 to block lot processing",
        "Blocks lot streaming when entering map view",
        "Restores lot streaming 1 second after exiting map view (for zoom animation)",
        "Thread-safe implementation with atomic flags and condition variables"
    }
})