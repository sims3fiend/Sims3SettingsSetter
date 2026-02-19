#include "../patch_system.h"
#include "../patch_helpers.h"
#include "../logger.h"

class OversizedThreadStackFix : public OptimizationPatch {
  private:
    // This is a call of _beginthreadex that doesn't bother to supply a reasonable stack-size.
    static inline const AddressInfo _beginthreadexCallAddressInfo = {.name = "OversizedThreadStackFix::_beginthreadexCall",
        .addresses =
            {
                {GameVersion::Retail, 0x004db525},
                {GameVersion::Steam, 0x004db355},
                {GameVersion::EA, 0x004db305},
            },
        .pattern = "6A 00 6A 00 C6 86 40 02 00 00 01 FF ?? ?? ?? ?? 00 83 C4 18 85 C0 89 86 24 02 00 00 74 0D",
        .patternOffset = 11};

    static inline uintptr_t(__cdecl* originalBeginThreadEx)(void* security, unsigned stackSize, unsigned(__stdcall* threadProcedure)(void*), void* arguments, unsigned flags, unsigned* threadID) = nullptr;
    static inline uintptr_t(__cdecl* hookedBeginThreadEx)(void* security, unsigned stackSize, unsigned(__stdcall* threadProcedure)(void*), void* arguments, unsigned flags, unsigned* threadID) = nullptr;

    static constexpr uint32_t sensibleStackReservation = 64 << 10;

    static inline std::atomic<uint32_t> addressSpaceSaved = 0;
    static inline std::atomic<uint16_t> adjustedStackCount = 0;

    static uintptr_t __cdecl HookedBeginThreadEx(void* security, unsigned stackSize, unsigned(__stdcall* threadProcedure)(void*), void* arguments, unsigned flags, unsigned* threadID) {
        if (stackSize == 0) {
            const auto exe = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
            const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);

            uint32_t defaultStackReserve = pe->OptionalHeader.SizeOfStackReserve;
            uint32_t spaceSaved = defaultStackReserve >= sensibleStackReservation ? defaultStackReserve - sensibleStackReservation : 0;

            addressSpaceSaved.fetch_add(spaceSaved, std::memory_order_relaxed);
            adjustedStackCount.fetch_add(1, std::memory_order_relaxed);

            stackSize = sensibleStackReservation;
            flags |= STACK_SIZE_PARAM_IS_A_RESERVATION;
        }

        return originalBeginThreadEx(security, stackSize, threadProcedure, arguments, flags, threadID);
    }

    std::vector<PatchHelper::PatchLocation> patchedLocations;

  public:
    OversizedThreadStackFix() : OptimizationPatch("OversizedThreadStackFix", nullptr) {}

    bool Install() override {
        if (isEnabled) return true;
        lastError.clear();
        LOG_INFO("[OversizedThreadStackFix] Installing...");

        auto _beginthreadexCallAddress = _beginthreadexCallAddressInfo.Resolve();
        if (!_beginthreadexCallAddress) { return Fail("Could not resolve _beginthreadexCall address"); }
        uintptr_t _beginthreadexCall = *_beginthreadexCallAddress;

        hookedBeginThreadEx = &HookedBeginThreadEx;

        bool successful = true;
        auto tx = PatchHelper::BeginTransaction();

        uint32_t* address;
        std::memcpy(&address, reinterpret_cast<const uint8_t*>(_beginthreadexCall + 2), 4);
        originalBeginThreadEx = reinterpret_cast<decltype(originalBeginThreadEx)>(*address);

        successful &= PatchHelper::WriteDWORD(_beginthreadexCall + 2, reinterpret_cast<uintptr_t>(&hookedBeginThreadEx), &tx.locations);

        if (!successful || !PatchHelper::CommitTransaction(tx)) {
            PatchHelper::RollbackTransaction(tx);
            return Fail("Failed to install");
        }

        patchedLocations = tx.locations;

        isEnabled = true;
        LOG_INFO("[OversizedThreadStackFix] Successfully installed");
        return true;
    }

    bool Uninstall() override {
        if (!isEnabled) return true;

        lastError.clear();
        LOG_INFO("[OversizedThreadStackFix] Uninstalling...");

        if (!PatchHelper::RestoreAll(patchedLocations)) {
            isEnabled = true;
            return Fail("Failed to restore original code");
        }

        isEnabled = false;
        LOG_INFO("[OversizedThreadStackFix] Successfully uninstalled");
        return true;
    }

    void RenderCustomUI() override {
        uint32_t spaceSaved = addressSpaceSaved.load(std::memory_order_relaxed);

        if (spaceSaved == 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Address-space saved: 0 MB\nPlease restart your game to benefit from this patch.");
        } else {
            uint16_t count = adjustedStackCount.load(std::memory_order_relaxed);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Address-space saved: %g MB across %u thread%c", static_cast<float>(spaceSaved) / 1048576.0f, count, count == 1 ? ' ' : 's');
        }

        OptimizationPatch::RenderCustomUI();
    }
};

REGISTER_PATCH(OversizedThreadStackFix,
    {.displayName = "Oversized Thread Stack Fix",
        .description = "Reduces the memory used by background threads that the game creates several-dozen of for watching file changes.",
        .category = "Performance",
        .experimental = false,
        .supportedVersions = VERSION_ALL,
        .technicalDetails = {
            "This patch was authored by \"Just Harry\".",
            "Rather negligently, the game's code fails to specify how large the stack should be for each of these created threads, resulting in the default stack-size being used (which is usually 1 MB).",
            "This patch makes the threads be created with a stack-reservation of only 64 KB, with the stack being lazily committed.",
        }})
