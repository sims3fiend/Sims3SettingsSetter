#include "cpu_optimization.h"
#include <detours.h>
#include <intrin.h>
#include "utils.h"
#include <sstream>

// Initialize static instance
CPUOptimizationPatch* CPUOptimizationPatch::instance = nullptr;

// Constructor
CPUOptimizationPatch::CPUOptimizationPatch() 
    : OptimizationPatch("CPUOptimization", nullptr) {
    instance = this;
    
    // Initialize CPU information
    GetCPUInfo();
    
    // Get the original function address
    originalSetThreadIdealProcessor = 
        (SetThreadIdealProcessorFn)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "SetThreadIdealProcessor");
    
    if (!originalSetThreadIdealProcessor) {
        Utils::Logger::Get().Log("Failed to find SetThreadIdealProcessor function");
    }
}

// Destructor
CPUOptimizationPatch::~CPUOptimizationPatch() {
    instance = nullptr;
}

// Detect CPU information
void CPUOptimizationPatch::GetCPUInfo() {
    // Get basic CPU information
    int regs[4] = { 0 }; // Use a temporary array for __cpuid results
    
    // Get vendor string
    __cpuid(regs, 0);
    *reinterpret_cast<int*>(cpuInfo.vendor) = regs[1];
    *(reinterpret_cast<int*>(cpuInfo.vendor + 4)) = regs[3]; // Correct offset
    *(reinterpret_cast<int*>(cpuInfo.vendor + 8)) = regs[2]; // Correct offset
    cpuInfo.vendor[12] = '\0';
    
    // Get CPU brand string
    cpuInfo.brand[0] = '\0';
    __cpuid(regs, 0x80000000);
    unsigned int maxExtFunc = regs[0]; // Get highest extended function supported
    if (maxExtFunc >= 0x80000004) {
        __cpuid(reinterpret_cast<int*>(cpuInfo.brand), 0x80000002);
        __cpuid(reinterpret_cast<int*>(cpuInfo.brand + 16), 0x80000003);
        __cpuid(reinterpret_cast<int*>(cpuInfo.brand + 32), 0x80000004);
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
    
    // Log CPU information
    std::stringstream ss;
    ss << "CPU Information: " << cpuInfo.brand << "\n"
       << "Vendor: " << cpuInfo.vendor << "\n"
       << "Family: " << cpuInfo.family << ", Model: " << cpuInfo.model << "\n"
       << "Logical Processors: " << cpuInfo.logicalProcessors << "\n"
       << "Hybrid Architecture: " << (cpuInfo.isHybrid ? "Yes" : "No");
    Utils::Logger::Get().Log(ss.str());
}

// Check if this is a hybrid CPU architecture
bool CPUOptimizationPatch::IsHybridCPU() {
    // Intel Alder Lake+ CPU (12th gen+) 
    if (strncmp(cpuInfo.vendor, "GenuineIntel", 12) == 0) {
        return (cpuInfo.family == 6 && cpuInfo.model >= 0x97);
    }
    return false;
}

// Find optimal processor for a thread
DWORD CPUOptimizationPatch::OptimizeThreadProcessor(DWORD requestedProcessor) {
    // Default to the requested processor
    DWORD finalProcessor = requestedProcessor;
    
    if (cpuInfo.logicalProcessors >= 8) {
        // Hybrid CPU optimization (Intel E-cores vs P-cores)
        if (cpuInfo.isHybrid) {
            // On Intel hybrid, first N cores are P-cores (depends on SKU)
            // Approximate heuristic: first half of cores are P-cores
            int p_core_count = cpuInfo.logicalProcessors / 2;
            
            // If requested processor is outside P-core range, use a P-core instead
            if (requestedProcessor >= (DWORD)p_core_count) {
                finalProcessor = requestedProcessor % p_core_count;  // Distribute across P-cores
                Utils::Logger::Get().Log("Hybrid CPU: Redirecting thread from core " + 
                    std::to_string(requestedProcessor) + " to P-core " + std::to_string(finalProcessor));
            }
        }
        // AMD Zen architecture optimization
        else if (strncmp(cpuInfo.vendor, "AuthenticAMD", 12) == 0) {
            // Default to first 4 cores on AMD (typically in same CCX)
            if (requestedProcessor >= 4) {
                // Distribute across first 4 cores
                finalProcessor = requestedProcessor % 4;
                Utils::Logger::Get().Log("AMD CPU: Redistributing thread from core " + 
                    std::to_string(requestedProcessor) + " to core " + std::to_string(finalProcessor));
            }
        }
    }
    
    return finalProcessor;
}

// Hook for SetThreadIdealProcessor
DWORD WINAPI CPUOptimizationPatch::HookedSetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor) {
    if (!instance || !instance->originalSetThreadIdealProcessor) {
        // Use MAXDWORD instead of DWORD_MAX
        return MAXDWORD; // Error case
    }
    
    // Get the thread ID
    DWORD threadId = GetThreadId(hThread);
    
    // Find optimal processor based on CPU architecture
    DWORD finalProcessor = instance->OptimizeThreadProcessor(dwIdealProcessor);
    
    // Track this thread
    bool threadFound = false;
    for (auto& thread : instance->threads) {
        if (thread.threadId == threadId) {
            thread.originalProcessor = dwIdealProcessor;
            thread.finalProcessor = finalProcessor;
            threadFound = true;
            break;
        }
    }
    
    // Add new thread if not found
    if (!threadFound && instance->threads.size() < MAX_THREADS) {
        ThreadInfo info;
        info.threadId = threadId;
        info.originalProcessor = dwIdealProcessor;
        info.finalProcessor = finalProcessor;
        info.creationTime = GetTickCount();
        instance->threads.push_back(info);
        instance->threadCount++;
        
        // Update core usage mask
        instance->coreUsageMask |= (1ULL << finalProcessor);
        
        // Log thread creation
        Utils::Logger::Get().Log("Thread " + std::to_string(threadId) + 
            " assigned to processor " + std::to_string(finalProcessor));
    }
    
    // Call original function with optimized processor
    return instance->originalSetThreadIdealProcessor(hThread, finalProcessor);
}

// Install the patch
bool CPUOptimizationPatch::Install() {
    if (isEnabled) return true;
    
    Utils::Logger::Get().Log("Installing CPU optimization patch...");
    
    if (!originalSetThreadIdealProcessor) {
        Utils::Logger::Get().Log("Cannot install CPU optimization - missing SetThreadIdealProcessor function");
        return false;
    }
    
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    
    LONG result = DetourAttach(&(PVOID&)originalSetThreadIdealProcessor, HookedSetThreadIdealProcessor);
    if (result != NO_ERROR) {
        Utils::Logger::Get().Log("Failed to attach SetThreadIdealProcessor hook: " + std::to_string(result));
        DetourTransactionAbort();
        return false;
    }
    
    result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        Utils::Logger::Get().Log("Failed to commit CPU optimization hook: " + std::to_string(result));
        return false;
    }
    
    isEnabled = true;
    Utils::Logger::Get().Log("CPU optimization patch installed successfully");
    return true;
}

// Uninstall the patch
bool CPUOptimizationPatch::Uninstall() {
    if (!isEnabled) return true;
    
    Utils::Logger::Get().Log("Removing CPU optimization patch...");
    
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    
    LONG result = DetourDetach(&(PVOID&)originalSetThreadIdealProcessor, HookedSetThreadIdealProcessor);
    if (result != NO_ERROR) {
        Utils::Logger::Get().Log("Failed to detach SetThreadIdealProcessor hook: " + std::to_string(result));
        DetourTransactionAbort();
        return false;
    }
    
    result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        Utils::Logger::Get().Log("Failed to commit CPU optimization hook removal: " + std::to_string(result));
        return false;
    }
    
    isEnabled = false;
    Utils::Logger::Get().Log("CPU optimization patch removed successfully");
    return true;
} 