#include "optimization.h"
#include <intrin.h>
#include <format>
#include <detours.h>
#include "small_patches.h"
#include "intersection_patch.h"

// Forward declaration
class MemoryPatch;

void OptimizationPatch::MaybeSampleMinimal(LONG currentCalls) {
    auto now = std::chrono::steady_clock::now();
    if (now - currentWindow.start >= SAMPLE_INTERVAL) {
        std::lock_guard<std::mutex> lock(statsMutex);
        if (now - currentWindow.start >= SAMPLE_INTERVAL) {
            lastSampleRate = currentCalls / 
                std::chrono::duration<double>(SAMPLE_INTERVAL).count();

            char buffer[256];
            sprintf_s(buffer, "[%s] Calls/sec: %.0f\n", patchName.c_str(), lastSampleRate);
            OutputDebugStringA(buffer);

            InterlockedExchange(&currentWindow.calls, 0);
            currentWindow.start = now;
        }
    }
}

CPUFeatures::CPUFeatures() {
    int leaves[4] = { 0 };
    __cpuid(leaves, 1);
    hasSSE41 = (leaves[2] & (1 << 19)) != 0;
    hasFMA = (leaves[2] & (1 << 12)) != 0;
}

const CPUFeatures& CPUFeatures::Get() {
    static CPUFeatures instance;
    return instance;
}

OptimizationManager& OptimizationManager::Get() {
    static OptimizationManager instance;
    
    // Register all patches when the manager is first created
    static bool initialized = false;
    if (!initialized) {
        OutputDebugStringA("[OptimizationManager] Initializing patches...\n");
        
        // Register individual patches
        instance.RegisterPatch(std::make_unique<FrameRatePatch>());
        
        // Register other patches
        instance.RegisterPatch(std::make_unique<LotVisibilityPatch>());
        // Register the Intersection Patch
        instance.RegisterPatch(std::make_unique<IntersectionPatch>());
        
        // Mark as initialized
        initialized = true;
        
        // Log all registered patches
        OutputDebugStringA("[OptimizationManager] Registered patches:\n");
        for (const auto& patch : instance.patches) {
            OutputDebugStringA(("  - " + patch->GetName() + "\n").c_str());
        }
        //BLANK STARE
        OutputDebugStringA("[OptimizationManager] All patches registered\n");
    }
    
    return instance;
}

const std::vector<std::unique_ptr<OptimizationPatch>>& OptimizationManager::GetPatches() const {
    return patches;
}

bool OptimizationManager::EnablePatch(const std::string& name) {
    for (auto& patch : patches) {
        if (patch->GetName() == name) {
            return patch->Install();
        }
    }
    return false;
}

bool OptimizationManager::DisablePatch(const std::string& name) {
    for (auto& patch : patches) {
        if (patch->GetName() == name) {
            return patch->Uninstall();
        }
    }
    return false;
}

bool OptimizationManager::SaveState(const std::string& filename) {
    try {
        // First, read entire file content
        std::vector<std::string> existingContent;
        {
            std::ifstream inFile(filename);
            if (inFile.is_open()) {
                std::string line;
                while (std::getline(inFile, line)) {
                    existingContent.push_back(line);
                }
            }
        }

        // Find and remove existing optimization section
        bool inSection = false;
        auto it = existingContent.begin();
        while (it != existingContent.end()) {
            if (*it == "; Optimization Settings") {
                inSection = true;
                it = existingContent.erase(it);
            }
            else if (inSection && it->empty()) {
                // Continue removing in optimization section
                it = existingContent.erase(it);
            }
            else if (inSection && !it->empty() && ((*it)[0] == '[' || (*it)[0] == ';')) {
                inSection = false;
                it++;
            }
            else if (inSection) {
                it = existingContent.erase(it);
            }
            else {
                it++;
            }
        }

        // Write back file with updated content
        std::ofstream outFile(filename);
        if (!outFile.is_open()) {
            OutputDebugStringA("[OptimizationManager] Failed to open file for saving\n");
            return false;
        }

        // Write existing content
        for (const auto& line : existingContent) {
            outFile << line << "\n";
        }

        // Ensure there's a blank line before optimization section
        if (!existingContent.empty() && !existingContent.back().empty()) {
            outFile << "\n";
        }

        // Write optimization settings
        outFile << "; Optimization Settings\n";
        for (const auto& patch : patches) {
            patch->SaveState(outFile);
        }

        return true;
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("[OptimizationManager] Error saving state: " + std::string(e.what()) + "\n").c_str());
        return false;
    }
}

bool OptimizationManager::LoadState(const std::string& filename) {
    try {
        OutputDebugStringA(("[OptimizationManager] Attempting to load from: " + filename + "\n").c_str());
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            OutputDebugStringA("[OptimizationManager] Failed to open file for loading\n");
            return false;
        }

        //young widower seeking optimization settings section enquire within
        std::string line;
        bool foundOptSection = false;
        while (std::getline(file, line)) {
            if (line == "; Optimization Settings") {
                foundOptSection = true;
                OutputDebugStringA("[OptimizationManager] Found optimization settings section\n");
                break;
            }
        }

        if (!foundOptSection) {
            OutputDebugStringA("[OptimizationManager] No optimization section found in file\n");
            return true; // Not an error, just no settings
        }

        std::string currentPatch;
        std::string currentKey;
        bool foundAnyOptimizations = false;

        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == ';') {
                OutputDebugStringA(("[OptimizationManager] Skipping line: " + line + "\n").c_str());
                continue;
            }

            // Check for optimization section header
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    // Get the full section name including "Optimization_"
                    //BZT WRONG REDO TODO: CHANGE TO  OPTIMIZATION:PATCHNAME LIKE CONFIG DUNCE DUNCE DUNCE FOOL
                    currentPatch = line.substr(1, end - 1);
                    // If it starts with "Optimization_", remove that prefix to get just the patch name
                    const std::string prefix = "Optimization_";
                    if (currentPatch.compare(0, prefix.length(), prefix) == 0) {
                        currentPatch = currentPatch.substr(prefix.length());
                        OutputDebugStringA(("[OptimizationManager] Found patch section: " + currentPatch + "\n").c_str());
                        foundAnyOptimizations = true;
                    }
                }
                continue;
            }

            // Parse key=value pairs
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos && !currentPatch.empty()) {
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                OutputDebugStringA(("[OptimizationManager] Found setting - Key: " + key + ", Value: " + value + "\n").c_str());

                // Find the patch by name
                bool found = false;
                for (const auto& patch : patches) {
                    if (patch->GetName() == currentPatch) {
                        OutputDebugStringA(("[OptimizationManager] Found matching patch: " + currentPatch + "\n").c_str());
                        
                        // For memory patches call their LoadState with both key and value
                        if (MemoryPatch* memPatch = dynamic_cast<MemoryPatch*>(patch.get())) {
                            bool success = false;
                            
                            // Call the specific LoadState method for patches
                            if (FrameRatePatch* framePatch = dynamic_cast<FrameRatePatch*>(patch.get())) {
                                success = framePatch->LoadState(key, value);
                            }
                            else if (key == "Enabled") {
                                // For other patches, just handle the Enabled state
                                success = patch->LoadState(value);
                            }
                            
                            OutputDebugStringA(("[OptimizationManager] State application " + 
                                std::string(success ? "succeeded" : "failed") + "\n").c_str());
                        }
                        else if (key == "Enabled") {
                            // For non-memory patches, just handle the Enabled state
                            bool success = patch->LoadState(value);
                            OutputDebugStringA(("[OptimizationManager] State application " + 
                                std::string(success ? "succeeded" : "failed") + "\n").c_str());
                        }
                        
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    OutputDebugStringA(("[OptimizationManager] No matching patch found for: " + 
                        currentPatch + "\n").c_str());
                }
            }
        }

        if (!foundAnyOptimizations) {
            OutputDebugStringA("[OptimizationManager] No optimization sections found in file\n");
        }

        return true;
    }
    catch (const std::exception& e) {
        OutputDebugStringA(("[OptimizationManager] Error loading state: " + std::string(e.what()) + "\n").c_str());
        return false;
    }
}

// Update the RegisterPatch method to remove references to removed patches
void OptimizationManager::RegisterPatch(std::unique_ptr<OptimizationPatch> patch) {
    // Check if a patch with the same name already exists
    std::string patchName = patch->GetName();
    for (const auto& existingPatch : patches) {
        if (existingPatch->GetName() == patchName) {
            OutputDebugStringA(("[OptimizationManager] Patch already registered: " + patchName + "\n").c_str());
            return; // Skip this patch since it's already registered
        }
    }
    
    // If we get here, this is a new patch
    patches.push_back(std::move(patch));
    OutputDebugStringA(("[OptimizationManager] Registered patch: " + patchName + "\n").c_str());
} 