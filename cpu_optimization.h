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
    
    // Static instance for hook callbacks
    static CPUOptimizationPatch* instance;
    
    // Hook function for thread ideal processor
    static DWORD WINAPI HookedSetThreadIdealProcessor(HANDLE hThread, DWORD dwIdealProcessor);
    
    // Helper methods
    void GetCPUInfo();
    bool IsHybridCPU();
    DWORD OptimizeThreadProcessor(DWORD requestedProcessor);
    
public:
    CPUOptimizationPatch();
    virtual ~CPUOptimizationPatch();
    
    bool Install() override;
    bool Uninstall() override;
    
    const CPUInfo& GetCPUInformation() const { return cpuInfo; }
    int GetThreadCount() const { return threadCount; }
    const std::vector<ThreadInfo>& GetThreads() const { return threads; }
}; 