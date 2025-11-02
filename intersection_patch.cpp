#include "intersection_patch.h"
#include <detours/detours.h>
#include "logger.h"

IntersectionPatch* IntersectionPatch::instance = nullptr;

// Calculate offsets at compile time
static const size_t CALLS_OFFSET = OptimizationPatch::GetCurrentWindowOffset() + OptimizationPatch::GetCallsOffset();

IntersectionPatch::IntersectionPatch() 
    : OptimizationPatch("Intersection", (void*)0x67afb0) // Steam version address, need to do a pattern or rather, everyone else needs to use steam
{
    instance = this;
}

bool IntersectionPatch::Install() {
    if (isEnabled) return true;
    
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG error = DetourAttach(&originalFunc, OptimizedHook);
    if (DetourTransactionCommit() == NO_ERROR) {
        isEnabled = true;
        LOG_INFO("[IntersectionPatch] Successfully installed");
        return true;
    }
    
    LOG_ERROR("[IntersectionPatch] Failed to install");
    return false;
}

bool IntersectionPatch::Uninstall() {
    if (!isEnabled) return true;
    
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&originalFunc, OptimizedHook);
    if (DetourTransactionCommit() == NO_ERROR) {
        isEnabled = false;
        return true;
    }
    return false;
}
// this is like, idk I feel vulnerable about this code so please don't make fun of it ldsahfhsajdas
__declspec(naked) bool __cdecl IntersectionPatch::OptimizedHook() {
    __asm {
        // Prologue
        push ebp
        mov ebp, esp
        and esp, 0xfffffff0    // 16-byte alignment for SSE
        sub esp, 0x40          // Space for XMM operations

        // Load data immediately
        mov eax, [ebp + 8]     // param1
        mov ecx, [eax]         // array start
        mov edx, [eax + 4]     // array end
        sub edx, ecx           // length in bytes

        // Strategic prefetch
        prefetchnta[ecx]
        prefetchnta[ecx + 64]

        // Quick size check (common early-out)
        sar edx, 3              // Convert to element count
        cmp edx, 3
        jbe return_true

        // Check alignment for optimal path
        test ecx, 0xF
        jz aligned_path

        // Unaligned path with LDDQU for better performance (?)
        lddqu xmm0, [ecx]      // Better unaligned load on modern CPUs apparently idfk
        lddqu xmm1, [ecx + 16]
        jmp process_data

        aligned_path:
        movaps xmm0, [ecx]
        movaps xmm1, [ecx + 16]

        process_data:
        // Efficient SIMD processing
        movaps xmm2, xmm0
        movaps xmm3, xmm1

        // Optimized shuffling
        shufps xmm2, xmm2, 0x88
        shufps xmm3, xmm3, 0x88

        // calc differences
        subps xmm2, xmm3

        // Square results (using SSE multiply)
        mulps xmm2, xmm2

        // Fast comparison
        movaps xmm4, [esp + 16]
        cmpps xmm2, xmm4, 1

        // Quick result extraction
        movmskps eax, xmm2
        test eax, eax
        setnz al

        // Fast return
        mov esp, ebp
        pop ebp
        ret 4

        align 16
        return_true:
        mov al, 1
        mov esp, ebp
        pop ebp
        ret 4
    }
} 