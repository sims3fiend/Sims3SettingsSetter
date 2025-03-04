#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <memory>
#include <fstream>

// Base class for optimization patches
class OptimizationPatch {
public:
    struct SampleWindow {
        volatile LONG calls{ 0 };
        std::chrono::steady_clock::time_point start;
    };

protected:
    std::mutex statsMutex;
    SampleWindow currentWindow;
    static constexpr auto SAMPLE_INTERVAL = std::chrono::seconds(5);

    void* originalFunc;
    std::string patchName;
    bool isEnabled = false;
    double lastSampleRate = 0.0;

    void MaybeSampleMinimal(LONG currentCalls);

public:
    OptimizationPatch(const std::string& name, void* original) 
        : patchName(name), originalFunc(original) {
        currentWindow.start = std::chrono::steady_clock::now();
        currentWindow.calls = 0;
    }
    
    virtual ~OptimizationPatch() = default;

    virtual bool Install() = 0;
    virtual bool Uninstall() = 0;
    
    const std::string& GetName() const { return patchName; }
    bool IsEnabled() const { return isEnabled; }
    double GetLastSampleRate() const { return lastSampleRate; }

    static constexpr size_t GetCurrentWindowOffset() { 
        return offsetof(OptimizationPatch, currentWindow); 
    }
    
    static constexpr size_t GetCallsOffset() { 
        return offsetof(SampleWindow, calls); 
    }

    // Add methods for serialization
    virtual void SaveState(std::ofstream& file) const {
        file << "[Optimization_" << patchName << "]\n";
        file << "Enabled=" << (isEnabled ? "true" : "false") << "\n\n";
    }
    
    virtual bool LoadState(const std::string& value) {
        if (value == "true" && !isEnabled) {
            return Install();
        }
        else if (value == "false" && isEnabled) {
            return Uninstall();
        }
        return true;
    }
};

// CPU feature detection
struct CPUFeatures {
    bool hasSSE41{ false };
    bool hasFMA{ false };

    CPUFeatures();
    static const CPUFeatures& Get();
};

// Manager for all optimization patches
class OptimizationManager {
    std::vector<std::unique_ptr<OptimizationPatch>> patches;

public:
    static OptimizationManager& Get();
    
    void RegisterPatch(std::unique_ptr<OptimizationPatch> patch);
    const std::vector<std::unique_ptr<OptimizationPatch>>& GetPatches() const;
    
    bool EnablePatch(const std::string& name);
    bool DisablePatch(const std::string& name);


    bool SaveState(const std::string& filename);
    bool LoadState(const std::string& filename);
}; 