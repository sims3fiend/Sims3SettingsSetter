#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <format>

// Hooks the blend-in and blend-out duration resolves inside Sims3::SACS::PlayAnimation, clamping whatever duration the engine ends up using.
// xmm0 holds the candidate blend duration in seconds at each hook site
// xmm0 <= 0 (covers the engine's -1.0 "no override" sentinel, exact-zero, and NaN) passes through unchanged. AnimRequest fields used: +0x50 flags, +0x68 blendIn, +0x6c blendOut.

// Read by the trampolines on every call; Install sets them before writing the JMPs.
static float g_animBlendMin = 0.0f;
static float g_animBlendMax = 0.0f;
static uintptr_t g_returnHere_BlendIn = 0;
static uintptr_t g_returnHere_BlendOut = 0;

// Hook at 0x7F9876 overwriting 5 bytes:  8B 4C 24 20 51   MOV ECX,[ESP+0x20]; PUSH ECX
// There's probabbly an easier way of doing this but oh well
__declspec(naked) static void Trampoline_BlendIn() {
    __asm {
        // Save xmm1/xmm2 to use as scratch.
        sub esp, 8
        movss dword ptr [esp], xmm1
        movss dword ptr [esp + 4], xmm2

        xorps xmm2, xmm2 // xmm2 = 0.0
        ucomiss xmm0, xmm2
        jbe restore // xmm0 <= 0 -> pass-through

        movss xmm1, dword ptr [g_animBlendMin]
        ucomiss xmm1, xmm2
        jbe try_max // min <= 0 -> skip min clamp
        ucomiss xmm0, xmm1
        jae try_max // xmm0 >= min -> leave alone
        movss xmm0, xmm1

    try_max:
        movss xmm1, dword ptr [g_animBlendMax]
        ucomiss xmm1, xmm2
        jbe restore // max <= 0 -> skip max clamp
        ucomiss xmm0, xmm1
        jbe restore // xmm0 <= max -> leave alone
        movss xmm0, xmm1

    restore:
        movss xmm1, dword ptr [esp]
        movss xmm2, dword ptr [esp + 4]
        add esp, 8

        // Re-emit displaced bytes, then resume at the next instruction.
        mov ecx, dword ptr [esp + 0x20]
        push ecx
        jmp dword ptr [g_returnHere_BlendIn]
    }
}

// Hook at 0x7F98B3 overwriting the first 5 of 9 bytes:
//   F3 0F 11 84 24 60 02 00 00   MOVSS [ESP+0x260], XMM0
// The JMP only clobbered the first 5, so the trampoline re-emit the full MOVSS.
__declspec(naked) static void Trampoline_BlendOut() {
    __asm {
        sub esp, 8
        movss dword ptr [esp], xmm1
        movss dword ptr [esp + 4], xmm2

        xorps xmm2, xmm2
        ucomiss xmm0, xmm2
        jbe restore

        movss xmm1, dword ptr [g_animBlendMin]
        ucomiss xmm1, xmm2
        jbe try_max
        ucomiss xmm0, xmm1
        jae try_max
        movss xmm0, xmm1

    try_max:
        movss xmm1, dword ptr [g_animBlendMax]
        ucomiss xmm1, xmm2
        jbe restore
        ucomiss xmm0, xmm1
        jbe restore
        movss xmm0, xmm1

    restore:
        movss xmm1, dword ptr [esp]
        movss xmm2, dword ptr [esp + 4]
        add esp, 8

        movss dword ptr [esp + 0x260], xmm0
        jmp dword ptr [g_returnHere_BlendOut]
    }
}

class AnimationBlendPatch : public OptimizationPatch {
  private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    float minDuration = 0.0f;
    float maxDuration = 0.0f;
    bool forceBlendOut = false;

    // 31-byte signatures centred on the 0.0f -> -1.0f sentinel substitution that precedes each funnel point in Sims3::SACS::PlayAnimation.
    // patternOffset = 26 (0x1A) lands on the displaced bytes we hook.
    static inline const AddressInfo blendInSite = {
        .name = "AnimBlend in-hook",
        .pattern = "F3 0F 10 46 68 0F 2E 05 ?? ?? ?? ?? 9F F6 C4 44 7A 08 F3 0F 10 05 ?? ?? ?? ?? 8B 4C 24 20 51",
        .patternOffset = 26,
        .expectedBytes = {0x8B, 0x4C, 0x24, 0x20, 0x51}, // mov ecx,[esp+0x20]; push ecx
    };

    static inline const AddressInfo blendOutSite = {
        .name = "AnimBlend out-hook",
        .pattern = "F3 0F 10 46 6C 0F 2E 05 ?? ?? ?? ?? 9F F6 C4 44 7A 08 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 84 24 60 02 00 00",
        .patternOffset = 26,
        .expectedBytes = {0xF3, 0x0F, 0x11, 0x84, 0x24}, // first 5 of MOVSS [esp+0x260],xmm0
    };

    // Gate at 0x7F9897: TEST [ESI+0x50],0x100 ; JZ +0x23 ; MOVSS XMM0,[ESI+0x6C].
    // The JZ skips the entire blend-out resolve when the "explicit blend-out" flag is unset.
    // NOPing the 2-byte JZ forces the resolve to run for every request.
    static inline const AddressInfo blendOutGate = {
        .name = "AnimBlend out-gate",
        .pattern = "F7 46 50 00 01 00 00 74 23 F3 0F 10 46 6C",
        .patternOffset = 7,
        .expectedBytes = {0x74, 0x23}, // jz +0x23
    };

  public:
    AnimationBlendPatch() : OptimizationPatch("AnimationBlendPatch", nullptr) {
        RegisterFloatSetting(&minDuration, "minDuration", SettingUIType::InputBox, 0.0f, 0.0f, 10.0f,
            "Minimum blend duration in seconds. Positive durations shorter than this are clamped up.\n"
            "Lengthens very short blends so animations ease in instead of snapping into place.\n"
            "0 = no clamping. Recommended ~0.27 .");

        RegisterFloatSetting(&maxDuration, "maxDuration", SettingUIType::InputBox, 0.0f, 0.0f, 10.0f,
            "Maximum blend duration in seconds. Positive durations longer than this are clamped down.\n"
            "Shortens drawn-out blends so animations commit to the new pose sooner.\n"
            "0 = no clamping.");

        RegisterBoolSetting(&forceBlendOut, "forceBlendOut", false,
            "Force the engine to resolve a blend-out duration for every animation, regardless of the\n"
            "controller's blend-out flag. Lets asset-specified blend-out times through where the\n"
            "engine would otherwise discard them. May break animations that intentionally snap-end.");
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[AnimationBlendPatch] Installing...");

        auto inAddr = blendInSite.Resolve();
        if (!inAddr) return Fail("Could not resolve blend-in hook address");

        auto outAddr = blendOutSite.Resolve();
        if (!outAddr) return Fail("Could not resolve blend-out hook address");

        std::optional<uintptr_t> gateAddr;
        if (forceBlendOut) {
            gateAddr = blendOutGate.Resolve();
            if (!gateAddr) return Fail("Could not resolve blend-out gate address");
        }

        // Set globals BEFORE writing the JMPs, trampolines read them.
        g_animBlendMin = minDuration;
        g_animBlendMax = maxDuration;
        g_returnHere_BlendIn = *inAddr + 5;   // skip our 5-byte JMP (trampoline re-emits the displaced bytes)
        g_returnHere_BlendOut = *outAddr + 9; // skip past the full 9-byte original MOVSS

        auto tx = PatchHelper::BeginTransaction();
        bool ok = true;
        ok &= PatchHelper::WriteRelativeJump(*inAddr, reinterpret_cast<uintptr_t>(&Trampoline_BlendIn), &tx.locations);
        ok &= PatchHelper::WriteRelativeJump(*outAddr, reinterpret_cast<uintptr_t>(&Trampoline_BlendOut), &tx.locations);
        if (gateAddr) ok &= PatchHelper::WriteNOP(*gateAddr, 2, &tx.locations);

        if (!ok || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install patches");
        }
        patchedLocations = tx.locations;

        LOG_INFO(std::format("[AnimationBlendPatch] blend-in @ {:#010x}, blend-out @ {:#010x}, min={:.3f}s max={:.3f}s forceBlendOut={}", *inAddr, *outAddr, g_animBlendMin, g_animBlendMax, forceBlendOut));
        isEnabled = true;
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();
        LOG_INFO("[AnimationBlendPatch] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) return Fail("Failed to restore original bytes");

        // Zero the trampoline globals so any stale call (shouldn't happen post-restore) becomes a pass-through-into-nothing instead of a jump to a rando address.
        g_animBlendMin = 0.0f;
        g_animBlendMax = 0.0f;
        g_returnHere_BlendIn = 0;
        g_returnHere_BlendOut = 0;

        isEnabled = false;
        LOG_INFO("[AnimationBlendPatch] Successfully uninstalled");
        return true;
    }
};

REGISTER_PATCH(
    AnimationBlendPatch, {.displayName = "Animation Blend Tuning",
                             .description = "Tweak how the engine resolves animation blend (transition) durations.\n",
                             .category = "Animation",
                             .experimental = true,
                             .supportedVersions = VERSION_ALL,
                             .technicalDetails = {"Hooks the blend-in and blend-out duration points inside Sims3::SACS::PlayAnimation.",
                                 "minDuration clamps positive durations up, smoothing out asset-specified blends that are too short and snap.", "maxDuration clamps positive durations down, making slow blends snappier.",
                                 "forceBlendOut NOPs the engine's gate check so blend-out is resolved for every animation, not just those with the controller flag set.",
                                 "Clamps only touch positive durations; the engine's negative -1.0 'no override' sentinel passes through untouched.", "Credits to thepancake1 for the patch!"}})
