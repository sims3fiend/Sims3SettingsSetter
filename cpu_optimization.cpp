/* The game uses a weird hardcoded SetThreadIdealProcessor call in the steam and disk versions, so we use our own implemntation instead xoxo*/
#include "cpu_optimization.h"
#include <detours/detours.h>
#include <intrin.h>
#include "utils.h"
#include "logger.h"
#include <sstream>
#include <cstring>
#include "patch_helpers.h"

// Include ImGui for custom UI rendering
#include "imgui.h"

// Initialize static instance
CPUOptimizationPatch* CPUOptimizationPatch::instance = nullptr;

// Constructor
CPUOptimizationPatch::CPUOptimizationPatch() : OptimizationPatch("CPUOptimization", nullptr), originalSetThreadIdealProcessor(nullptr), threadsLock{0}, threadCount(0), coreUsageMask(0), hasEfficiencyInfo(false) {
    // Don't set instance here - will be set in Install()
    // Don't initialize anything heavy in constructor - defer to Install()
    std::memset(&cpuInfo, 0, sizeof(cpuInfo)); // Zero-initialize cpuInfo
}

// Destructor
CPUOptimizationPatch::~CPUOptimizationPatch() {
    // Ensure hook is uninstalled before destruction to avoid callbacks on a dead instance
    Uninstall();
    if (instance == this) { instance = nullptr; }
}

// Detect CPU information
void CPUOptimizationPatch::GetCPUInfo() {
    // Get basic CPU information
    int regs[4] = {0}; // Use a temporary array for __cpuid results

    // Get vendor string
    __cpuid(regs, 0);
    // Vendor is 12 chars from EBX, EDX, ECX
    std::memcpy(cpuInfo.vendor + 0, &regs[1], 4);
    std::memcpy(cpuInfo.vendor + 4, &regs[3], 4);
    std::memcpy(cpuInfo.vendor + 8, &regs[2], 4);
    cpuInfo.vendor[12] = '\0';

    // Get CPU brand string
    cpuInfo.brand[0] = '\0';
    __cpuid(regs, 0x80000000);
    unsigned int maxExtFunc = regs[0]; // Get highest extended function supported
    if (maxExtFunc >= 0x80000004) {
        int brandRegs[4];
        __cpuid(brandRegs, 0x80000002);
        std::memcpy(cpuInfo.brand + 0, brandRegs, 16);
        __cpuid(brandRegs, 0x80000003);
        std::memcpy(cpuInfo.brand + 16, brandRegs, 16);
        __cpuid(brandRegs, 0x80000004);
        std::memcpy(cpuInfo.brand + 32, brandRegs, 16);
        cpuInfo.brand[48] = '\0'; // Ensure null termination
    }

    // Get CPU family/model
    __cpuid(regs, 1);
    cpuInfo.family = ((regs[0] >> 8) & 0xF) + ((regs[0] >> 20) & 0xFF);
    cpuInfo.model = ((regs[0] >> 4) & 0xF) + ((regs[0] >> 12) & 0xF0);

    // Get logical processor count
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cpuInfo.logicalProcessors = sysInfo.dwNumberOfProcessors;

    // Check if this is a hybrid architecture
    cpuInfo.isHybrid = IsHybridCPU();

    // Determine recommended thread count based on CPU
    if (cpuInfo.logicalProcessors <= 4) {
        cpuInfo.recommendedThreadCount = 2;
    } else if (cpuInfo.logicalProcessors <= 8) {
        cpuInfo.recommendedThreadCount = 4;
    } else {
        cpuInfo.recommendedThreadCount = 8;
    }

    // Don't log during constructor - moved to Install() to avoid static initialization issues
}

// Check if this is a hybrid CPU architecture
bool CPUOptimizationPatch::IsHybridCPU() {
    // Prefer CPUID leaf 0x1A (Hybrid Information) when available
    if (std::strncmp(cpuInfo.vendor, "GenuineIntel", 12) == 0) {
        int regs[4] = {0};
        __cpuid(regs, 0);
        unsigned int maxBasic = (unsigned int)regs[0];
        if (maxBasic >= 0x1A) {
            __cpuid(regs, 0x1A);
            unsigned int coreType = (unsigned int)(regs[0] & 0xFF); // EAX[7:0] core type (1=E-core, 2=P-core)
            // If leaf exists, assume hybrid capable; precise mapping will be built in BuildTopology
            return coreType != 0;
        }
        // Fallback to family/model heuristic for older toolchains
        return (cpuInfo.family == 6 && cpuInfo.model >= 0x97);
    }
    return false;
}

// Find optimal processor for a thread
DWORD CPUOptimizationPatch::OptimizeThreadProcessor(DWORD requestedProcessor) {
    // Default to the requested processor
    DWORD finalProcessor = requestedProcessor;

    if (cpuInfo.logicalProcessors >= 2) {
        // Intel Hybrid: route to detected P-core set when available
        if (cpuInfo.isHybrid && !pCoreIndices.empty()) {
            bool isRequestedPCore = false;
            for (DWORD idx : pCoreIndices) {
                if (idx == requestedProcessor) {
                    isRequestedPCore = true;
                    break;
                }
            }
            if (!isRequestedPCore) {
                DWORD mapped = pCoreIndices[requestedProcessor % pCoreIndices.size()];
                finalProcessor = mapped;
                LOG_DEBUG("Hybrid CPU: Redirecting thread from core " + std::to_string(requestedProcessor) + " to P-core " + std::to_string(finalProcessor));
            }
        }
        // AMD Zen: keep within a single L3 group (approx CCX)
        else if (std::strncmp(cpuInfo.vendor, "AuthenticAMD", 12) == 0 && !l3Groups.empty()) {
            const std::vector<DWORD>& g0 = l3Groups[0];
            if (!g0.empty()) {
                bool inGroup = false;
                for (DWORD idx : g0) {
                    if (idx == requestedProcessor) {
                        inGroup = true;
                        break;
                    }
                }
                if (!inGroup) {
                    DWORD mapped = g0[requestedProcessor % g0.size()];
                    finalProcessor = mapped;
                    LOG_DEBUG("AMD CPU: Redirecting thread from core " + std::to_string(requestedProcessor) + " to CCX core " + std::to_string(finalProcessor));
                }
            }
        }
    }

    return finalProcessor;
}

// Hook for SetThreadIdealProcessor
DWORD WINAPI CPUOptimizationPatch::HookedSetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor) {
    if (!instance || !instance->originalSetThreadIdealProcessor) {
        HMODULE hKernel = GetModuleHandle(L"kernel32.dll");
        auto pfnOrig = hKernel ? (SetThreadIdealProcessorFn)GetProcAddress(hKernel, "SetThreadIdealProcessor") : nullptr;
        if (pfnOrig) { return pfnOrig(hThread, dwIdealProcessor); }
        return MAXDWORD; // last resort
    }

    // Get the thread ID
    DWORD threadId = GetThreadId(hThread);

    // Find optimal processor based on CPU architecture
    DWORD finalProcessor = instance->OptimizeThreadProcessor(dwIdealProcessor);

    // Debug before/after mapping, don't forget to remove this :)))! Should just add a ini for this actually.... HMMMMM
    LOG_DEBUG("SetThreadIdealProcessor: thread " + std::to_string(threadId) + " requested=" + std::to_string(dwIdealProcessor) + " final=" + std::to_string(finalProcessor));

    // Track this thread
    EnterCriticalSection(&instance->threadsLock);
    bool threadFound = false;
    for (auto& thread : instance->threads) {
        if (thread.threadId == threadId) {
            thread.originalProcessor = dwIdealProcessor;
            thread.finalProcessor = finalProcessor;
            threadFound = true;
            LOG_DEBUG("Thread " + std::to_string(threadId) + " ideal updated: requested=" + std::to_string(dwIdealProcessor) + " final=" + std::to_string(finalProcessor));
            break;
        }
    }
    if (!threadFound && instance->threads.size() < MAX_THREADS) {
        ThreadInfo info;
        info.threadId = threadId;
        info.originalProcessor = dwIdealProcessor;
        info.finalProcessor = finalProcessor;
        info.creationTime = GetTickCount();
        instance->threads.push_back(info);
        instance->threadCount++;
        instance->coreUsageMask |= (static_cast<DWORD_PTR>(1) << finalProcessor);
        LOG_DEBUG("Thread " + std::to_string(threadId) + " ideal assigned: requested=" + std::to_string(dwIdealProcessor) + " final=" + std::to_string(finalProcessor));
    }
    LeaveCriticalSection(&instance->threadsLock);

    // Call original function with optimized processor
    DWORD previousIdeal = instance->originalSetThreadIdealProcessor(hThread, finalProcessor);
    LOG_DEBUG("SetThreadIdealProcessor result: thread " + std::to_string(threadId) + " previous=" + std::to_string(previousIdeal));
    return previousIdeal;
}

// Restore original process affinity after init delay
DWORD WINAPI CPUOptimizationPatch::RestoreAffinityThread(LPVOID param) {
    Sleep(5000);
    auto* self = static_cast<CPUOptimizationPatch*>(param);
    if (self && self->originalProcessAffinity) {
        SetProcessAffinityMask(GetCurrentProcess(), self->originalProcessAffinity);
        LOG_INFO("Hybrid CPU: Restored original process affinity");
    }
    return 0;
}

// Build topology info for P-cores and AMD L3 groups
void CPUOptimizationPatch::BuildTopology() {
    pCoreIndices.clear();
    l3Groups.clear();
    hasEfficiencyInfo = false;

    DetectIntelHybridViaCpuSets();
    DetectAmdL3Groups();
}

void CPUOptimizationPatch::DetectIntelHybridViaCpuSets() {
    if (std::strncmp(cpuInfo.vendor, "GenuineIntel", 12) != 0) return;

    // Try CPUID 0x1A by pinning this thread to each logical processor (x86 limited to 32)
    int regs[4] = {0};
    __cpuid(regs, 0);
    unsigned int maxBasic = (unsigned int)regs[0];
    if (maxBasic < 0x1A) return;

    HANDLE hThread = GetCurrentThread();
    DWORD_PTR prevMask = SetThreadAffinityMask(hThread, (DWORD_PTR)-1);
    if (prevMask == 0) {
        // If cannot query affinity, bail
        return;
    }
    // Restore immediately; we'll set per-index
    SetThreadAffinityMask(hThread, prevMask);

    for (DWORD i = 0; i < (DWORD)cpuInfo.logicalProcessors && i < (DWORD)(sizeof(DWORD_PTR) * 8); ++i) {
        DWORD_PTR mask = (static_cast<DWORD_PTR>(1) << i);
        DWORD_PTR old = SetThreadAffinityMask(hThread, mask);
        if (old == 0) continue;
        __cpuid(regs, 0x1A);
        unsigned int coreType = (unsigned int)(regs[0] & 0xFF); // 1=E-core, 2=P-core (per Intel docs)
        if (coreType == 2) { pCoreIndices.push_back(i); }
        // restore previous
        SetThreadAffinityMask(hThread, old);
    }
    if (!pCoreIndices.empty() && pCoreIndices.size() < (size_t)cpuInfo.logicalProcessors) { hasEfficiencyInfo = true; }
}

void CPUOptimizationPatch::DetectAmdL3Groups() {
    if (std::strncmp(cpuInfo.vendor, "AuthenticAMD", 12) != 0) return;

    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &length);
    DWORD lastError = ::GetLastError(); // Use global Windows API function
    if (lastError != ERROR_INSUFFICIENT_BUFFER || length == 0) return;

    std::vector<unsigned char> buffer(length);
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX ptr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationCache, ptr, &length)) return;

    unsigned char* end = buffer.data() + length;
    while (reinterpret_cast<unsigned char*>(ptr) < end) {
        if (ptr->Relationship == RelationCache && ptr->Cache.Level == 3) {
            std::vector<DWORD> indices;
            KAFFINITY mask = ptr->Cache.GroupMask.Mask;
            for (DWORD i = 0; i < (DWORD)(sizeof(KAFFINITY) * 8); ++i) {
                if (mask & (static_cast<KAFFINITY>(1) << i)) { indices.push_back(i); }
            }
            if (!indices.empty()) l3Groups.push_back(indices);
        }
        ptr = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(reinterpret_cast<unsigned char*>(ptr) + ptr->Size);
    }
}

// Install the patch
bool CPUOptimizationPatch::Install() {
    if (isEnabled) return true;

    // Set instance pointer
    instance = this;

    // Initialize critical section
    InitializeCriticalSection(&threadsLock);
    threads.reserve(MAX_THREADS);

    // Initialize CPU information
    GetCPUInfo();

    // Build CPU topology (P-cores / L3 groups)
    BuildTopology();

    // Alder Lake fix: restrict process to P-cores during init, restore after 5s
    if (cpuInfo.isHybrid && !pCoreIndices.empty()) {
        DWORD_PTR processAffinity = 0, systemAffinity = 0;
        if (GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity)) {
            originalProcessAffinity = processAffinity;
            DWORD_PTR pCoreMask = 0;
            for (DWORD idx : pCoreIndices) { pCoreMask |= (static_cast<DWORD_PTR>(1) << idx); }
            SetProcessAffinityMask(GetCurrentProcess(), pCoreMask);
            LOG_INFO("Hybrid CPU: Restricting to P-cores during init (mask=0x" + std::to_string(pCoreMask) + ")");
            HANDLE hThread = CreateThread(nullptr, 0, RestoreAffinityThread, this, 0, nullptr);
            if (hThread) CloseHandle(hThread);
        }
    }

    // Get the original function address
    originalSetThreadIdealProcessor = (SetThreadIdealProcessorFn)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "SetThreadIdealProcessor");

    if (!originalSetThreadIdealProcessor) {
        LOG_ERROR("Cannot install CPU optimization - missing SetThreadIdealProcessor function");
        DeleteCriticalSection(&threadsLock);
        instance = nullptr;
        return false;
    }

    // Log CPU information now (safe to do so after static initialization)
    std::stringstream ss;
    ss << "CPU Information: " << cpuInfo.brand << "\n"
       << "Vendor: " << cpuInfo.vendor << "\n"
       << "Family: " << cpuInfo.family << ", Model: " << cpuInfo.model << "\n"
       << "Logical Processors: " << cpuInfo.logicalProcessors << "\n"
       << "Hybrid Architecture: " << (cpuInfo.isHybrid ? "Yes" : "No");
    LOG_INFO(ss.str());

    LOG_INFO("Installing CPU optimization patch...");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    LONG result = DetourAttach(&(PVOID&)originalSetThreadIdealProcessor, HookedSetThreadIdealProcessor);
    if (result != NO_ERROR) {
        LOG_ERROR("Failed to attach SetThreadIdealProcessor hook: " + std::to_string(result));
        DetourTransactionAbort();
        return false;
    }

    result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        LOG_ERROR("Failed to commit CPU optimization hook: " + std::to_string(result));
        return false;
    }

    isEnabled = true;
    LOG_INFO("CPU optimization patch installed successfully");
    return true;
}

// Uninstall the patch
bool CPUOptimizationPatch::Uninstall() {
    if (!isEnabled) return true;

    LOG_INFO("Removing CPU optimization patch...");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    LONG result = DetourDetach(&(PVOID&)originalSetThreadIdealProcessor, HookedSetThreadIdealProcessor);
    if (result != NO_ERROR) {
        LOG_ERROR("Failed to detach SetThreadIdealProcessor hook: " + std::to_string(result));
        DetourTransactionAbort();
        return false;
    }

    result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        LOG_ERROR("Failed to commit CPU optimization hook removal: " + std::to_string(result));
        return false;
    }

    // Clean up
    DeleteCriticalSection(&threadsLock);
    if (instance == this) { instance = nullptr; }

    isEnabled = false;
    LOG_INFO("CPU optimization patch removed successfully");
    return true;
}

// Render custom UI for the patch (requires imgui.h)
void CPUOptimizationPatch::RenderCustomUI() {
#ifdef IMGUI_VERSION
    // Only render if we have a valid ImGui context
    if (!ImGui::GetCurrentContext()) { return; }

    // Display CPU information
    ImGui::TextDisabled("CPU: %s", cpuInfo.brand);
    ImGui::TextDisabled("Cores: %d, Hybrid: %s", cpuInfo.logicalProcessors, cpuInfo.isHybrid ? "Yes" : "No");
    ImGui::TextDisabled("Threads optimized: %d", threadCount);
#endif
}