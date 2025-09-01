#pragma once
#include "optimization.h"
#include <windows.h>
#include <vector>

// CPU information structure
struct CPUInfo {
    char vendor[13];
    char brand[49];
    int family;
    int model;
    int logicalProcessors;
    bool isHybrid;
    int recommendedThreadCount;
};

// Thread tracking information
struct ThreadInfo {
    DWORD threadId;
    DWORD originalProcessor;
    DWORD finalProcessor;
    DWORD creationTime;
};

// CPU optimization patch for hybrid architectures
class CPUOptimizationPatch : public OptimizationPatch {
private:
    // Original functions for hooks
    typedef DWORD(WINAPI* SetThreadIdealProcessorFn)(HANDLE hThread, DWORD dwIdealProcessor);
    SetThreadIdealProcessorFn originalSetThreadIdealProcessor;

    // CPU information
    CPUInfo cpuInfo;
    
    // Thread tracking
    static const int MAX_THREADS = 128;
    std::vector<ThreadInfo> threads;
    int threadCount = 0;
    DWORD_PTR coreUsageMask = 0;
    CRITICAL_SECTION threadsLock;
    
    // Topology data
    // Logical processor indices that are considered performance cores (Intel P-cores)
    std::vector<DWORD> pCoreIndices;
    // Logical processor indices grouped by shared L3 cache (approx CCX on AMD)
    std::vector<std::vector<DWORD>> l3Groups;
    bool hasEfficiencyInfo = false;
    
    // Static instance for hook callbacks
    static CPUOptimizationPatch* instance;
    
    // Hook function for thread ideal processor
    static DWORD WINAPI HookedSetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor);
    
    // Helper methods
    void GetCPUInfo();
    bool IsHybridCPU();
    DWORD OptimizeThreadProcessor(DWORD requestedProcessor);
    void BuildTopology();
    void DetectIntelHybridViaCpuSets();
    void DetectAmdL3Groups();
    
public:
    CPUOptimizationPatch();
    virtual ~CPUOptimizationPatch();
    
    bool Install() override;
    bool Uninstall() override;
    
    const CPUInfo& GetCPUInformation() const { return cpuInfo; }
    int GetThreadCount() const { return threadCount; }
    const std::vector<ThreadInfo>& GetThreads() const { return threads; }
}; 