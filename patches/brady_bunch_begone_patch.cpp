#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"
#include <cstring>
#include <vector>

class BradyBunchBegonePatch : public OptimizationPatch {
  private:
    std::vector<PatchHelper::PatchLocation> patchedLocations;

    float bbbR = 0.0f;
    float bbbG = 0.0f;
    float bbbB = 0.0f;
    bool disableFillLights = true;

    // The mBradyBunchBlue Vec3 in Sims3::Renderer::GlobalLightSettings.
    //  When a LightingRoom has zero lights, LightingRoom::CaclulateBaseAmbientColor (yes caculate!!!!) copies it straight into mAmbientColor / mSpecularAmbient.
    // GLS has two slots (+0x270 primary, +0x290 sibling added in 1.67) and the reader branches between them at runtime, so we write both.
    // Anchor on the conditional reader: MOV ECX, sibling ; JNZ +5 ; MOV ECX, primary ; CALL.
    static inline const AddressInfo bbbReaderPattern = {
        .name = "BradyBunchBlue reader",
        .pattern = "B9 ?? ?? ?? ?? 75 05 B9 ?? ?? ?? ?? E8",
        .patternOffset = 0,
        .expectedBytes = {},
    };
    static constexpr uintptr_t kSiblingMinusPrimary = 0x20;

    // Sims3::Renderer::LightRig::AddBradyBunchBlueFloor sums real-light weights and if the sum is below mLightRigBBBThreshold (default 0.6) it injects 4 synthetic cardinal fills, washing
    // partially-lit lots flat. Rewriting the threshold-failed JBE as JMP skips that block entirely.
    // Anchor on: MOVSS XMM0,[EAX] ; MOVSS XMM1,[ESP+??] ; COMISS XMM0,XMM1 ; JBE skip.
    static inline const AddressInfo bbbCompareSite = {
        .name = "BBB threshold compare",
        .pattern = "F3 0F 10 00 F3 0F 10 4C 24 ?? 0F 2F C1 0F 86 ?? ?? ?? ??",
        .patternOffset = 0,
        .expectedBytes = {},
    };
    static constexpr int kJbeOffsetInMatch = 13;

  public:
    BradyBunchBegonePatch() : OptimizationPatch("BradyBunchBegone", nullptr) {
        RegisterFloatSetting(&bbbR, "bbbR", SettingUIType::InputBox, 0.0f, 0.0f, 10.0f,
            "Red\n"
            "The engine copies this directly into mAmbientColor for any room that ends up with zero lights.\n"
            "All-zeros makes unlit rooms render with no ambient instead of the blue glow.");
        RegisterFloatSetting(&bbbG, "bbbG", SettingUIType::InputBox, 0.0f, 0.0f, 10.0f, "Green");
        RegisterFloatSetting(&bbbB, "bbbB", SettingUIType::InputBox, 0.0f, 0.0f, 10.0f, "Blue");
        RegisterBoolSetting(&disableFillLights, "disableFillLights", true,
            "Also disable the engine's synthetic fill-light injection in LightRig::AddBradyBunchBlueFloor.\n"
            "Rigs whose real-light weight sums below mLightRigBBBThreshold (0.6) normally get 4 fake fills added but this skips that block entirely.\n");
    }

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();

        LOG_INFO("[BradyBunchBegone] Installing...");

        // Resolve both GLS slot addresses from the reader's immediates.
        auto readerAnchor = bbbReaderPattern.Resolve();
        if (!readerAnchor) return Fail("Could not locate BradyBunchBlue reader pattern");

        uintptr_t siblingAddr = PatchHelper::ReadDWORD(*readerAnchor + 1);
        uintptr_t primaryAddr = PatchHelper::ReadDWORD(*readerAnchor + 8);
        LOG_INFO(std::format("[BradyBunchBegone] reader anchor at {:#010x} -> primary={:#010x}, sibling={:#010x}", *readerAnchor, primaryAddr, siblingAddr));

        if (siblingAddr - primaryAddr != kSiblingMinusPrimary) {
            return Fail(std::format("Unexpected GLS slot layout: primary={:#010x}, sibling={:#010x}, delta={:#x} (expected {:#x})", primaryAddr, siblingAddr, siblingAddr - primaryAddr, kSiblingMinusPrimary));
        }

        auto looksLikeFloat = [](float v) { return v >= -100.0f && v <= 100.0f; };
        for (int i = 0; i < 3; i++) {
            float vPrim = *reinterpret_cast<float*>(primaryAddr + i * 4);
            float vSib = *reinterpret_cast<float*>(siblingAddr + i * 4);
            if (!looksLikeFloat(vPrim) || !looksLikeFloat(vSib)) { return Fail(std::format("BBB slot {} contains non-float-looking values: primary={}, sibling={}", i, vPrim, vSib)); }
        }

        float curPrim[3] = {*reinterpret_cast<float*>(primaryAddr + 0), *reinterpret_cast<float*>(primaryAddr + 4), *reinterpret_cast<float*>(primaryAddr + 8)};
        float curSib[3] = {*reinterpret_cast<float*>(siblingAddr + 0), *reinterpret_cast<float*>(siblingAddr + 4), *reinterpret_cast<float*>(siblingAddr + 8)};
        LOG_INFO(std::format("[BradyBunchBegone] current primary=({},{},{}), sibling=({},{},{})", curPrim[0], curPrim[1], curPrim[2], curSib[0], curSib[1], curSib[2]));

        float newRGB[3] = {bbbR, bbbG, bbbB};

        auto tx = PatchHelper::BeginTransaction();
        bool ok = true;
        for (int i = 0; i < 3; i++) {
            DWORD newBits, oldPrimBits, oldSibBits;
            std::memcpy(&newBits, &newRGB[i], 4);
            std::memcpy(&oldPrimBits, &curPrim[i], 4);
            std::memcpy(&oldSibBits, &curSib[i], 4);
            ok &= PatchHelper::WriteDWORD(primaryAddr + i * 4, newBits, &tx.locations, &oldPrimBits);
            ok &= PatchHelper::WriteDWORD(siblingAddr + i * 4, newBits, &tx.locations, &oldSibBits);
        }
        if (!ok) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to write BradyBunchBlue values");
        }
        LOG_INFO(std::format("[BradyBunchBegone] BBB color set to ({},{},{}) on both slots", bbbR, bbbG, bbbB));

        // Optional: JBE -> JMP+NOP so the fill-light inject block is always skipped.
        if (disableFillLights) {
            auto cmpAnchor = bbbCompareSite.Resolve();
            if (!cmpAnchor) {
                PatchHelper::RollbackTransaction(tx);
                return Fail("Could not locate BBB threshold compare site");
            }
            uintptr_t jbeAddr = *cmpAnchor + kJbeOffsetInMatch;
            BYTE op0 = PatchHelper::ReadByte(jbeAddr);
            BYTE op1 = PatchHelper::ReadByte(jbeAddr + 1);
            if (op0 != 0x0F || op1 != 0x86) {
                PatchHelper::RollbackTransaction(tx);
                return Fail(std::format("Unexpected opcode at JBE site {:#010x}: {:#04x} {:#04x} (want 0F 86)", jbeAddr, op0, op1));
            }
            DWORD oldRel = PatchHelper::ReadDWORD(jbeAddr + 2);
            DWORD newRel = oldRel + 1; // JMP is 5 bytes (vs JBE's 6), so rel32 is +1
            uintptr_t target = jbeAddr + 6 + oldRel;
            LOG_INFO(std::format("[BradyBunchBegone] fill-light JBE at {:#010x} -> {:#010x}, rewriting to JMP+NOP", jbeAddr, target));

            std::vector<BYTE> newBytes = {
                0xE9, // JMP rel32
                static_cast<BYTE>(newRel & 0xFF),
                static_cast<BYTE>((newRel >> 8) & 0xFF),
                static_cast<BYTE>((newRel >> 16) & 0xFF),
                static_cast<BYTE>((newRel >> 24) & 0xFF),
                0x90,
            };
            std::vector<BYTE> oldBytes = {
                0x0F,
                0x86,
                static_cast<BYTE>(oldRel & 0xFF),
                static_cast<BYTE>((oldRel >> 8) & 0xFF),
                static_cast<BYTE>((oldRel >> 16) & 0xFF),
                static_cast<BYTE>((oldRel >> 24) & 0xFF),
            };
            if (!PatchHelper::WriteBytes(jbeAddr, newBytes, &tx.locations, &oldBytes)) {
                PatchHelper::RollbackTransaction(tx);
                return Fail("Failed to rewrite fill-light JBE -> JMP");
            }
        }

        if (!PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to commit transaction");
        }
        patchedLocations = tx.locations;

        isEnabled = true;
        LOG_INFO("[BradyBunchBegone] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;
        lastError.clear();

        LOG_INFO("[BradyBunchBegone] Uninstalling...");
        if (!PatchHelper::RestoreAll(patchedLocations)) return Fail("Failed to restore original bytes");

        isEnabled = false;
        return true;
    }

    void RenderCustomUI() override {
        SAFE_IMGUI_BEGIN();

        ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Game defaults: R=0.15  G=0.15  B=0.30");
        ImGui::Spacing();

        OptimizationPatch::RenderCustomUI();
    }
};

REGISTER_PATCH(BradyBunchBegonePatch, {.displayName = "Brady Bunch BEGONE",
                                          .description = "Stops the engine from filling unlit/dim rooms with a fake blue ambient.",
                                          .category = "Graphics",
                                          .experimental = true,
                                          .supportedVersions = VERSION_ALL,
                                          .technicalDetails = {"Sets the BBB RGB defaults to (0,0,0) which makes empty lightless rooms render with no ambient (instead of the weird blue glow)",
                                              "Reload the lot or toggle lights to refresh lighting.", "Writes the mBradyBunchBlue DebugVar at GLS+0x270 (primary) AND a bonus +0x290 one",
                                              "FillLight option rewrites the threshold-failed JBE in LightRig::AddBradyBunchBlueFloor so the 4-direction fill block is always skipped. This does nothing basically.",
                                              "Requires the lots/rooms lighting to be refreshed to display.", "Credits to Arro on Discord / Tumblr for the patch idea!"}})
