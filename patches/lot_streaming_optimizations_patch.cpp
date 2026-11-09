#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include "../settings.h"
#include "../utils.h"
#include <cstdint>
#include <format>
#include <mutex>
#include <unordered_map>
#include <vector>

// Consolidates (gone now) the four lot-streaming patches into one, each an independently toggleable sub-patch with a few values exposed for some
// The object streaming throttle spreads a lot's per-object scene-node build over time so a lot loading / popping into detailed view doesn't build every object in one frame...
// Large/flora objects (building/apartment shells, exterior geometry) are exempt and built synchronously in the first window: Lot::SetActiveImpl runs one-shot post-add fixups that need them in the scene immediately (see Detour_AddLotObjectsToScene)
// The map view blocker pauses lot streaming while in map view
// The lot visibility override disables the camera-view distance bias in the lot visibility metric so lots don't load/unload purely on view
// The optimized streaming settings enables "Throttle Lot LoD Transitions", sets its Max Active Lot Threshold to 12 (big = good), and applies the configurable Camera speed threshold value, defaulting lower (makes lots only load if the camera has been still for a period lower = longer)

namespace {

// Object throttle
// Sims3::World::Lot::AddLotObjectsToScene
const AddressInfo addLotObjectsToScene = {
    .name = "Sims3::World::Lot::AddLotObjectsToScene",
    .pattern = "83 EC 08 57 8B F9 80 BF C9 00 00 00 00 74 0E C6 87 C1 00 00 00 00 5F 83 C4 08 C2 08",
    .expectedBytes = {0x83, 0xEC, 0x08, 0x57, 0x8B, 0xF9},
};
// Sims3::World::Lot::UpdateObjectSceneNode (called per object). __thiscall(lot, obj, char, char)
const AddressInfo updateObjectSceneNode = {
    .name = "Sims3::World::Lot::UpdateObjectSceneNode",
    .pattern = "83 EC 0C 83 B9 64 03 00 00 00 89 4C 24 04 0F 84 ?? ?? ?? ?? 83 B9 08 04 00 00 01",
    .expectedBytes = {0x83, 0xEC, 0x0C, 0x83, 0xB9, 0x64},
};
// Sims3::Utility::ScriptMessageScope::ScriptMessageScope (ctor). __thiscall(scope, int beginMsg, int endMsg, lot)
const AddressInfo scriptMessageScopeCtor = {
    .name = "Sims3::Utility::ScriptMessageScope::ScriptMessageScope",
    .pattern = "8B 44 24 08 56 8B F1 8B 4C 24 10 57 8B 7C 24 0C 85 FF 89 06 89 4E 04 74 19",
    .expectedBytes = {0x8B, 0x44, 0x24, 0x08, 0x56},
};
// Sims3::Utility::ScriptMessageScope::~ScriptMessageScope (dtor). __fastcall(scope)
const AddressInfo scriptMessageScopeDtor = {
    .name = "Sims3::Utility::ScriptMessageScope::~ScriptMessageScope",
    .pattern = "56 8B F1 83 3E 00 74 1B E8 ?? ?? ?? ?? 85 C0 74 12 8B 4E 04 8B 10 8B 52 18 6A 00 51",
    .expectedBytes = {0x56, 0x8B, 0xF1, 0x83, 0x3E, 0x00},
};
// Sims3::Utility::PostRemoteMethodCall (x86-merged this into one) Allocates a RemoteMethodCall, bakes in the runtime method pointer + args, and posts it cross-thread
// used here to re-marshal AddLotObjectsToScene onto another thread. __cdecl(thread, lot, func, a4, char, char) can probably be used for other fun things also :)))) hint hint
const AddressInfo postRemoteMethodCall = {
    .name = "SPEC_Sims3::Utility::PostRemoteMethodCall",
    .pattern = "53 56 6A 00 6A 00 6A 00 6A 00 68 ?? ?? ?? ?? 6A 20 E8 ?? ?? ?? ?? 83 C4 18 85 C0 74 ?? C7 00 ?? ?? ?? ?? 33 C9 8D 50 04 87 0A 8B 4C 24 10 8B 54 24 14 89 48 0C 8B 4C 24 18 89 48 14 8A 4C 24 20",
    .expectedBytes = {0x53, 0x56, 0x6A, 0x00, 0x6A, 0x00},
};
// Sims3::World::IsObjectLargeOrFlora (x86-merged IsObjectLarge and IsObjectOutdoorFlora; reads Object+0x18 = mResourceKey then checks the CTProductObject flag bits)
// Large objects here are the building/apartment shells the throttle must build up front. __cdecl(obj) -> int First-instruction operand is an absolute global and the calls are rel32, so both are wildcarded
const AddressInfo isObjectLargeOrFlora = {
    .name = "SPEC_Sims3::World::IsObjectLargeOrFlora",
    .pattern = "8B 0D ?? ?? ?? ?? 56 8B 74 24 08 83 C6 18 56 E8 ?? ?? ?? ?? 85 C0 74 ?? 8B 80 98 00 00 00 8B C8 C1 E9 12 F6 C1 01 75 ?? C1 E8 07 A8 01 75 ??",
    .expectedBytes = {0x8B, 0x0D},
};

// Map view blocker
// Sims3::World::WorldManager::Update (hook target). __thiscall(worldMgr, param2, float param3)
const AddressInfo worldManagerUpdate = {
    .name = "Sims3::World::WorldManager::Update",
    .addresses = {{GameVersion::Retail, 0x00c6d3b0}, {GameVersion::Steam, 0x00c6d570}, {GameVersion::EA, 0x00c6c8f0}},
    .pattern = "55 8B EC 83 E4 F0 83 EC 64 53 56 8B F1 83 BE B4 01 00 00 00 57 75",
    .expectedBytes = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0},
};
// Camera_IsMapViewModeEnabled (spec, no ppc) __cdecl() -> char (reads camera+0x8B9)
const AddressInfo cameraIsMapViewModeEnabled = {
    .name = "Camera_IsMapViewModeEnabled",
    .addresses = {{GameVersion::Steam, 0x0073e060}},
    .pattern = "E8 ?? ?? ?? ?? 85 C0 74 2F 8B C8 E8 ?? ?? ?? ?? 85 C0 74 24 8B C8 E8 ?? ?? ?? ?? 85 C0 74 19 8B 10 8B C8 8B 42 0C 68 89 DD 0F 11 FF D0 85 C0 74 ?? 8A 80 B9 08 00 00",
};

// Visibility override
// JZ in the lot visibility/distance metric JZ(0x74) -> JMP(0xEB).
const AddressInfo lotVisibilityCameraBiasJZ = {
    .name = "LotVisibility::cameraBias JZ",
    .addresses = {{GameVersion::Retail, 0x00c62ae5}, {GameVersion::Steam, 0x00c63015}, {GameVersion::EA, 0x00c623a5}},
    .pattern = "74 ?? F3 0F 10 44 24 08 F3 0F 5C 87 E0 00 00 00 F3 0F 11 44 24 08 D9 44 24 08 5F 5E 8B E5 5D C2 0C 00",
    .expectedBytes = {0x74},
};

// Object Streaming Throttle internals
// in short, the engine marshal runs inline when posted from its own service thread, so we process x objects then let the DLL hook thread post the continuation cross-thread, which actually enqueues = real per-frame spread.
constexpr size_t kOffObjBegin = 0x14;
constexpr size_t kOffObjEnd = 0x18;
constexpr size_t kOffDetailedViewRequested = 0xC1;
constexpr size_t kOffBulldozing = 0xC9;
constexpr int kScopeMsgBegin = 0x4c55e8c;
constexpr int kScopeMsgEnd = 0x4c55efa;

using UpdateObjectSceneNode_t = void(__thiscall*)(void* lot, void* obj, char initialLoad, char alwaysVisibleOnly);
using ScriptMessageScopeCtor_t = void*(__thiscall*)(void* scope, int beginMsg, int endMsg, void* lot);
using ScriptMessageScopeDtor_t = void(__fastcall*)(void* scope);
using PostRemoteMethodCall_t = char(__cdecl*)(int thread, void* lot, void* func, int a4, char initialLoad, char alwaysVisibleOnly);
using IsObjectLargeOrFlora_t = int(__cdecl*)(void* obj);

// Settings (bound directly so GUI edits are live in the detour).
int g_objectsPerLot = 2;
int g_delayMs = 16;

// Resolved engine pointers (set when the throttle sub-feature installs).
uintptr_t g_addLotObjectsToScene = 0;
PostRemoteMethodCall_t g_postRemoteMethodCall = nullptr;
UpdateObjectSceneNode_t g_updateObjectSceneNode = nullptr;
ScriptMessageScopeCtor_t g_scriptMessageScopeCtor = nullptr;
ScriptMessageScopeDtor_t g_scriptMessageScopeDtor = nullptr;
IsObjectLargeOrFlora_t g_isObjectLargeOrFlora = nullptr;
bool g_throttleActive = false; // gates DrainPending

int ClampPerLot(int k) {
    return k < 1 ? 1 : (k > 256 ? 256 : k);
}

struct LotState {
    size_t next = 0;
    char startedDetailed = 0;
    char initialLoad = 0;
    char alwaysVisibleOnly = 0;
    bool needsPost = false;
    uint64_t lastWorkTick = 0;
    void* objBegin = nullptr;
};
std::unordered_map<void*, LotState> g_state;
std::mutex g_stateMtx;

void EraseState(void* lot) {
    std::lock_guard<std::mutex> lk(g_stateMtx);
    g_state.erase(lot);
}

#pragma warning(push)
#pragma warning(disable : 4733)
bool ProbeLot(void* lot) {
    if (!lot) return false;
    __try {
        volatile uint8_t a = *(reinterpret_cast<volatile uint8_t*>(lot) + kOffBulldozing);
        volatile uint8_t b = *(reinterpret_cast<volatile uint8_t*>(lot) + kOffDetailedViewRequested);
        (void)a;
        (void)b;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#pragma warning(pop)

// __fastcall(lot, edx, char, char) is ABI-identical to the original __thiscall(lot,char,char) RET 8.
void __fastcall Detour_AddLotObjectsToScene(void* lot, void* /*edx*/, char initialLoad, char alwaysVisibleOnly) {
    uint8_t* L = static_cast<uint8_t*>(lot);

    if (L[kOffBulldozing] != 0) { // bulldozing early-out, matches original
        L[kOffDetailedViewRequested] = 0;
        EraseState(lot);
        return;
    }

    void** begin = *reinterpret_cast<void***>(L + kOffObjBegin);
    void** end = *reinterpret_cast<void***>(L + kOffObjEnd);
    size_t count = (begin && end && end >= begin) ? static_cast<size_t>(end - begin) : 0;

    char detailed = static_cast<char>(L[kOffDetailedViewRequested]);

    size_t idx = 0;
    char startedDetailed = detailed;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        auto it = g_state.find(lot);
        // Resume only on the same object list; a changed mObjects begin means reload/reuse -> restart.
        if (it != g_state.end() && it->second.objBegin == reinterpret_cast<void*>(begin)) {
            idx = it->second.next;
            startedDetailed = it->second.startedDetailed;
        }
    }

    if (idx != 0 && detailed != startedDetailed) { // demoted/promoted mid-stream -> stop
        EraseState(lot);
        return;
    }
    if (idx >= count) {
        EraseState(lot);
        return;
    }

    const int quota = ClampPerLot(g_objectsPerLot);

    void* scope[2] = {nullptr, nullptr};
    g_scriptMessageScopeCtor(scope, kScopeMsgBegin, kScopeMsgEnd, lot);
    // First window: build every large/flora object (apartment/building shells, exterior geometry) up front.
    // Lot::SetActiveImpl calls Lot::LoadLotObjects (which reaches this synchronously) and then runs one-shot fixups right after it returns
    // e.g. the apartment shell re-bind, gated on lot +0x408==1 / +0x40c=='P' that silently no-op and never retry if the shell has no scene presence yet -> shells left disjointed from their lots.
    // Only regular objects are throttled; continuation windows skip the shells already built here.
    if (idx == 0) {
        for (size_t i = 0; i < count; ++i) {
            if (begin[i] && g_isObjectLargeOrFlora(begin[i]) != 0) { g_updateObjectSceneNode(lot, begin[i], initialLoad, alwaysVisibleOnly); }
        }
    }
    size_t pos = idx;
    for (int built = 0; pos < count && built < quota; ++pos) {
        if (begin[pos] && g_isObjectLargeOrFlora(begin[pos]) != 0) continue; // built in the first window
        g_updateObjectSceneNode(lot, begin[pos], initialLoad, alwaysVisibleOnly);
        ++built;
    }
    while (pos < count && begin[pos] && g_isObjectLargeOrFlora(begin[pos]) != 0) ++pos; // don't post a continuation for a shell-only tail
    g_scriptMessageScopeDtor(scope);

    if (pos < count) {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        auto& st = g_state[lot];
        st.next = pos;
        st.startedDetailed = startedDetailed;
        st.initialLoad = initialLoad;
        st.alwaysVisibleOnly = alwaysVisibleOnly;
        st.needsPost = true;
        st.lastWorkTick = GetTickCount64();
        st.objBegin = reinterpret_cast<void*>(begin);
    } else {
        EraseState(lot);
    }
}

// Hook-thread pump: posts one continuation per pending lot whose delay has elapsed, cross-thread.
void DrainPending() {
    if (!g_throttleActive || !g_addLotObjectsToScene || !g_postRemoteMethodCall) return;

    struct Post {
        void* lot;
        char il;
        char av;
    };
    std::vector<Post> posts;
    {
        const uint64_t now = GetTickCount64();
        const uint64_t delay = (g_delayMs < 0) ? 0 : static_cast<uint64_t>(g_delayMs);
        std::lock_guard<std::mutex> lk(g_stateMtx);
        posts.reserve(g_state.size());
        for (auto& [lot, st] : g_state) {
            if (st.needsPost && (now - st.lastWorkTick) >= delay) {
                st.needsPost = false;
                posts.push_back({lot, st.initialLoad, st.alwaysVisibleOnly});
            }
        }
    }
    for (const auto& p : posts) {
        if (!ProbeLot(p.lot)) {
            EraseState(p.lot);
            continue;
        }
        g_postRemoteMethodCall(1, p.lot, reinterpret_cast<void*>(g_addLotObjectsToScene), 0, p.il, p.av);
    }
}

// Map View Lot Blocker
constexpr size_t kWorldMgrSkipOffset = 0x258; // "skip lot streaming" gate in WorldManager::Update
constexpr uint64_t kMapViewGraceMs = 1000;    // keep blocking briefly after exit (zoom-out anim)

using WorldManagerUpdate_t = int(__thiscall*)(void* worldMgr, unsigned param2, float param3);
using CameraIsMapViewModeEnabled_t = char(__cdecl*)();

WorldManagerUpdate_t g_worldManagerUpdateOrig = nullptr; // Detours trampoline
CameraIsMapViewModeEnabled_t g_cameraIsMapViewModeEnabled = nullptr;
uint64_t g_lastMapViewTick = 0;

bool QueryMapView() {
    if (!g_cameraIsMapViewModeEnabled) return false;
    __try {
        return g_cameraIsMapViewModeEnabled() != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// __fastcall(self, edx, ...) emulates __thiscall(self, param2, param3) RET 8.
int __fastcall Hooked_WorldManagerUpdate(void* worldMgr, void* /*edx*/, unsigned param2, float param3) {
    if (!g_worldManagerUpdateOrig) return 0;

    bool inMap = QueryMapView();
    uint64_t now = GetTickCount64();
    if (inMap) g_lastMapViewTick = now;

    // Block while in map view, plus a bounded grace window after exit. Can never stick: blocking dies at most kMapViewGraceMs after the last frame map view was actually active.
    bool block = inMap || (g_lastMapViewTick != 0 && (now - g_lastMapViewTick) < kMapViewGraceMs);

    if (block) {
        char* skip = reinterpret_cast<char*>(worldMgr) + kWorldMgrSkipOffset;
        char old = *skip;
        *skip = 1;
        int r = g_worldManagerUpdateOrig(worldMgr, param2, param3);
        *skip = old;
        return r;
    }
    return g_worldManagerUpdateOrig(worldMgr, param2, param3);
}

} // namespace

// Patch
class LotStreamingOptimizationsPatch : public OptimizationPatch {
  private:
    // Sub-feature toggles
    bool subThrottle = true;
    bool subMapView = true;
    bool subVisibility = true;
    bool subStreamingSettings = true;

    // Exposed value (the LoD-transition Max Active Lot Threshold is fixed at 12, not exposed).
    float cameraSpeedThreshold = 5.0f;

    // Tracked patches (throttle JMP, visibility byte, live-setting writes) + the map-view detour.
    std::vector<PatchHelper::PatchLocation> patchedLocations;
    std::vector<DetourHelper::Hook> mapViewHooks;

    bool throttleInstalled = false;
    bool mapViewInstalled = false;

    // Live-setting application progress (settings may not exist until the game is further along).
    bool lsThrottleLodDone = false;
    bool lsThresholdDone = false;
    bool lsCameraDone = false;

    // Each maintained live setting with the address we last wrote it at (guarded by maintainedWritesMutex).
    // The game can re-register a setting at a new address: "Camera speed threshold" is a WorldManager member, and the
    // WorldManager is deleted (unregistering it) and re-created between game sessions, which frees the old address.
    std::vector<std::pair<const wchar_t*, uintptr_t>> liveSettingAddresses;

    bool InstallThrottle() {
        auto entry = addLotObjectsToScene.Resolve();
        auto node = updateObjectSceneNode.Resolve();
        auto ctor = scriptMessageScopeCtor.Resolve();
        auto dtor = scriptMessageScopeDtor.Resolve();
        auto post = postRemoteMethodCall.Resolve();
        auto visPred = isObjectLargeOrFlora.Resolve();
        if (!entry || !node || !ctor || !dtor || !post || !visPred) {
            LOG_WARNING("[LotStreamingOpt] Object throttle: could not resolve all addresses");
            return false;
        }

        g_addLotObjectsToScene = *entry;
        g_updateObjectSceneNode = reinterpret_cast<UpdateObjectSceneNode_t>(*node);
        g_scriptMessageScopeCtor = reinterpret_cast<ScriptMessageScopeCtor_t>(*ctor);
        g_scriptMessageScopeDtor = reinterpret_cast<ScriptMessageScopeDtor_t>(*dtor);
        g_postRemoteMethodCall = reinterpret_cast<PostRemoteMethodCall_t>(*post);
        g_isObjectLargeOrFlora = reinterpret_cast<IsObjectLargeOrFlora_t>(*visPred);
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            g_state.clear();
        }

        if (!PatchHelper::WriteRelativeJump(*entry, reinterpret_cast<uintptr_t>(&Detour_AddLotObjectsToScene), &patchedLocations)) {
            LOG_WARNING("[LotStreamingOpt] Object throttle: failed to write entry hook");
            return false;
        }
        g_throttleActive = true;
        LOG_INFO(std::format("[LotStreamingOpt] Object throttle installed (entry @ {:#010x})", *entry));
        return true;
    }

    bool InstallMapView() {
        auto wm = worldManagerUpdate.Resolve();
        auto isMap = cameraIsMapViewModeEnabled.Resolve();
        if (!wm || !isMap) {
            LOG_WARNING("[LotStreamingOpt] Map view blocker: could not resolve addresses (skipping)");
            return false;
        }
        g_worldManagerUpdateOrig = reinterpret_cast<WorldManagerUpdate_t>(*wm);
        g_cameraIsMapViewModeEnabled = reinterpret_cast<CameraIsMapViewModeEnabled_t>(*isMap);
        g_lastMapViewTick = 0;

        mapViewHooks = {{reinterpret_cast<void**>(&g_worldManagerUpdateOrig), reinterpret_cast<void*>(&Hooked_WorldManagerUpdate)}};
        if (!DetourHelper::InstallHooks(mapViewHooks)) {
            LOG_WARNING("[LotStreamingOpt] Map view blocker: failed to install hook");
            mapViewHooks.clear();
            return false;
        }
        LOG_INFO(std::format("[LotStreamingOpt] Map view blocker installed (WorldManager::Update @ {:#010x})", *wm));
        return true;
    }

    bool InstallVisibility() {
        auto addr = lotVisibilityCameraBiasJZ.Resolve();
        if (!addr) {
            LOG_WARNING("[LotStreamingOpt] Visibility override: could not resolve condition (skipping)");
            return false;
        }
        BYTE expectedOld = 0x74;
        if (!PatchHelper::WriteByte(*addr, 0xEB, &patchedLocations, &expectedOld)) { // JZ -> JMP
            LOG_WARNING("[LotStreamingOpt] Visibility override: failed to patch");
            return false;
        }
        LOG_INFO(std::format("[LotStreamingOpt] Visibility override installed (JZ->JMP @ {:#010x})", *addr));
        return true;
    }

    // Apply the streaming live settings; safe to call repeatedly (per-setting done-flags). Settings that don't exist yet are retried from Update() until they register.
    void TryApplyStreamingSettings() {
        if (!subStreamingSettings) return;

        if (!lsThrottleLodDone && LiveSetting::Exists(L"Throttle Lot LoD Transitions")) {
            lsThrottleLodDone = LiveSetting::Patch<bool>(L"Throttle Lot LoD Transitions", true, &patchedLocations);
            if (lsThrottleLodDone) {
                LOG_INFO("[LotStreamingOpt] Enabled 'Throttle Lot LoD Transitions'");
                MaintainLiveSetting(L"Throttle Lot LoD Transitions", std::vector<BYTE>{0x01});
            }
        }
        if (!lsThresholdDone && LiveSetting::Exists(L"Throttle Lot LoD Transitions Max Active Lot Threshold")) {
            // Counts register as int; fall back to unsigned if the registry typed it that way.
            lsThresholdDone = LiveSetting::Patch<int>(L"Throttle Lot LoD Transitions Max Active Lot Threshold", 12, &patchedLocations) ||
                              LiveSetting::Patch<unsigned int>(L"Throttle Lot LoD Transitions Max Active Lot Threshold", 12u, &patchedLocations);
            if (lsThresholdDone) {
                LOG_INFO("[LotStreamingOpt] Set 'Max Active Lot Threshold' = 12");
                int v = 12; // int and unsigned share the same 4-byte little-endian representation for 12
                MaintainLiveSetting(L"Throttle Lot LoD Transitions Max Active Lot Threshold", ToBytes(&v, sizeof(v)));
            }
        }
        if (!lsCameraDone && LiveSetting::Exists(L"Camera speed threshold")) {
            lsCameraDone = LiveSetting::Patch<float>(L"Camera speed threshold", cameraSpeedThreshold, &patchedLocations);
            if (lsCameraDone) {
                LOG_INFO(std::format("[LotStreamingOpt] Set 'Camera speed threshold' = {:.2f}", cameraSpeedThreshold));
                MaintainLiveSetting(L"Camera speed threshold", ToBytes(&cameraSpeedThreshold, sizeof(cameraSpeedThreshold)));
            }
        }
    }

    static std::vector<BYTE> ToBytes(const void* p, size_t n) {
        const BYTE* b = static_cast<const BYTE*>(p);
        return std::vector<BYTE>(b, b + n);
    }
    void MaintainLiveSetting(const wchar_t* name, std::vector<BYTE> desiredBytes) {
        if (void* addr = LiveSetting::GetAddress(name)) {
            AddMaintainedWrite(reinterpret_cast<uintptr_t>(addr), std::move(desiredBytes));
            std::lock_guard<std::mutex> lk(maintainedWritesMutex);
            liveSettingAddresses.push_back({name, reinterpret_cast<uintptr_t>(addr)});
        }
    }

    // Follow re-registered live settings to their current address before re-asserting, so we never write to (or later
    // restore into) the address a setting had when we first applied it.
    void OnSettingsRefired() override {
        if (!isEnabled.load()) return;
        {
            std::scoped_lock lk(maintainedWritesMutex, PatchHelper::g_patchLocationMutex);
            for (auto& [name, oldAddr] : liveSettingAddresses) {
                const uintptr_t newAddr = reinterpret_cast<uintptr_t>(LiveSetting::GetAddress(name));
                if (newAddr == oldAddr) continue;
                LOG_INFO(std::format("[LotStreamingOpt] '{}' moved {:#010x} -> {:#010x}", Utils::WideToUtf8(name), oldAddr, newAddr));

                for (auto it = maintainedWrites.begin(); it != maintainedWrites.end();) {
                    if (it->address != oldAddr) { ++it; continue; }
                    if (!newAddr) { it = maintainedWrites.erase(it); continue; } // setting gone: stop maintaining it
                    it->address = newAddr;
                    ++it;
                }
                for (auto it = patchedLocations.begin(); it != patchedLocations.end();) {
                    if (it->address != oldAddr) { ++it; continue; }
                    if (!newAddr) { it = patchedLocations.erase(it); continue; }
                    // Uninstall restores the new object's own value, not the one read from the old object.
                    it->address = newAddr;
                    std::memcpy(it->originalBytes.data(), reinterpret_cast<const void*>(newAddr), it->size);
                    ++it;
                }
                oldAddr = newAddr;
            }
        }
        OptimizationPatch::OnSettingsRefired();
    }

  public:
    LotStreamingOptimizationsPatch() : OptimizationPatch("LotStreamingOptimizations", nullptr) {
        RegisterBoolSetting(&subThrottle, "objectThrottle", true,
            "Spread each lot's object/scene-node building across frames instead of one burst.\n"
            "Building shells (apartments etc.) are always built immediately so they stay attached to their lot.");
        RegisterBoolSetting(&subMapView, "mapViewBlocker", true, "Pause lot streaming while in map view, so entering/leaving map view doesn't hitch.");
        RegisterBoolSetting(&subVisibility, "visibilityOverride", true, "Disable the camera-view distance bias so lots don't load/unload purely on view angle.");
        RegisterBoolSetting(&subStreamingSettings, "streamingSettings", true,
            "Enable 'Throttle Lot LoD Transitions' (+ Max Active Lot Threshold 12) and apply the\n"
            "camera speed threshold below.");

        RegisterIntSetting(&g_objectsPerLot, "objectsPerLot", 2, 1, 64, "Object throttle: Objects built per window. Lower = smoother", {}, SettingUIType::Slider);
        RegisterIntSetting(&g_delayMs, "delayMs", 16, 0, 500, "Object throttle: Minimum ms between a lot's windows. Higher = more spread / smoother.", {}, SettingUIType::Slider);
        RegisterFloatSetting(&cameraSpeedThreshold, "cameraSpeedThreshold", SettingUIType::InputBox, 5.0f, 0.0f, 50.0f,
            "Streaming settings: Camera speed threshold the engine uses decide how soon after camera movement stops to start loading lots");
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[LotStreamingOpt] Installing...");

        // Each sub-feature is independent: a resolve/install failure logs and is skipped rather than failing the whole patch.
        throttleInstalled = subThrottle && InstallThrottle();
        mapViewInstalled = subMapView && InstallMapView();
        if (subVisibility) InstallVisibility();

        lsThrottleLodDone = lsThresholdDone = lsCameraDone = false;
        TryApplyStreamingSettings();

        isEnabled = true;
        LOG_INFO("[LotStreamingOpt] Installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();
        LOG_INFO("[LotStreamingOpt] Uninstalling...");

        ClearMaintainedWrites(); // stop re-asserting live settings before we restore their originals
        {
            std::lock_guard<std::mutex> lk(maintainedWritesMutex);
            liveSettingAddresses.clear();
        }

        if (!mapViewHooks.empty()) {
            DetourHelper::RemoveHooks(mapViewHooks);
            mapViewHooks.clear();
        }
        mapViewInstalled = false;

        // Restores throttle JMP, visibility byte, and live-setting writes (all tracked).
        if (!PatchHelper::RestoreAll(patchedLocations)) { LOG_WARNING("[LotStreamingOpt] Some tracked locations failed to restore"); }
        patchedLocations.clear();

        g_throttleActive = false;
        throttleInstalled = false;
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            g_state.clear();
        }

        isEnabled = false;
        LOG_INFO("[LotStreamingOpt] Uninstalled");
        return true;
    }

    void Update() override {
        // Base handles debounced reinstall when any setting/toggle changes (which re-applies the camera threshold and re-evaluates sub-toggles).
        OptimizationPatch::Update();
        if (!isEnabled.load()) return;

        if (throttleInstalled) DrainPending();

        // Keep retrying live settings that weren't registered yet at install time.
        if (subStreamingSettings && (!lsThrottleLodDone || !lsThresholdDone || !lsCameraDone)) { TryApplyStreamingSettings(); }
    }
};

REGISTER_PATCH(LotStreamingOptimizationsPatch,
    {.displayName = "Lot Streaming Optimizations",
        .description =
            "Reduces stutter from lot streaming: throttles per-object loading, pauses streaming in map view, overrides view-based lot visibility, and applies optimized streaming settings. Each part toggleable below.",
        .category = "Performance",
        .experimental = true,
        .supportedVersions = VERSION_ALL,
        .technicalDetails = {"Object throttle: replaces Lot::AddLotObjectsToScene, building x objects/window and re-posting the rest cross-thread. Large/flora objects (shells) build synchronously in the first window so "
                             "SetActiveImpl's one-shot fixups (apartment shell re-bind) find them.",
            "Map view blocker: hooks WorldManager::Update and gates the +0x258 skip flag on the live Camera_IsMapViewModeEnabled state.",
            "Visibility override: JZ->JMP on the camera-view distance bias in the lot visibility metric.",
            "Streaming settings: 'Throttle Lot LoD Transitions' on, its Max Active Lot Threshold = 12, configurable Camera speed threshold."}})
