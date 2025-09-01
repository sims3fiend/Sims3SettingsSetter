#include "optimization.h"
#include <intrin.h>
#include <format>
#include <detours.h>
#include "small_patches.h"
#include "intersection_patch.h"
#include "cpu_optimization.h"
#include "utils.h"
#include "logger.h"

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
            LOG_DEBUG(buffer);

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
        LOG_INFO("[OptimizationManager] Initializing patches...");
        
        // Register individual patches
        instance.RegisterPatch(std::make_unique<FrameRatePatch>());
        
        // Register other patches
        instance.RegisterPatch(std::make_unique<LotVisibilityPatch>());
        // Register the Intersection Patch
        instance.RegisterPatch(std::make_unique<IntersectionPatch>());
        
        // Register the CPU optimization patch
        instance.RegisterPatch(std::make_unique<CPUOptimizationPatch>());
        
        // Mark as initialized
        initialized = true;
        
        // Log all registered patches
        LOG_INFO("[OptimizationManager] Registered patches:");
        for (const auto& patch : instance.patches) {
            LOG_INFO("  - " + patch->GetName());
        }
        //BLANK STARE
        LOG_INFO("[OptimizationManager] All patches registered");
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
            LOG_ERROR("[OptimizationManager] Failed to open file for saving: " + filename);
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
        LOG_ERROR(std::string("[OptimizationManager] Error saving state: ") + e.what());
        return false;
    }
}

bool OptimizationManager::LoadState(const std::string& filename) {
    try {
        LOG_DEBUG("[OptimizationManager] Attempting to load from: " + filename);
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            LOG_ERROR("[OptimizationManager] Failed to open file for loading: " + filename);
            return false;
        }

        //young widower seeking optimization settings section enquire within
        std::string line;
        bool foundOptSection = false;
        while (std::getline(file, line)) {
            if (line == "; Optimization Settings") {
                foundOptSection = true;
                LOG_DEBUG("[OptimizationManager] Found optimization settings section");
                break;
            }
        }

        if (!foundOptSection) {
            LOG_DEBUG("[OptimizationManager] No optimization section found in file");
            return true; // Not an error, just no settings
        }

        std::string currentPatchName;
        bool foundAnyOptimizations = false;

        while (std::getline(file, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == ';') {
                // OutputDebugStringA(("[OptimizationManager] Skipping line: " + line + "\n").c_str()); // uncomment to see what's being skipped
                continue;
            }

            // Check for optimization section header like [Optimization_FrameRate]
            if (line[0] == '[' && line.back() == ']') {
                size_t start = line.find('_'); // Find the underscore after "Optimization"
                if (start != std::string::npos && start + 1 < line.length() - 1) {
                    currentPatchName = line.substr(start + 1, line.length() - start - 2); // Extract patch name
                    LOG_DEBUG("[OptimizationManager] Found patch section: " + currentPatchName);
                    foundAnyOptimizations = true;
                } else {
                    currentPatchName.clear(); // Invalid section header
                }
                continue;
            }


            // Parse key=value pairs within a valid section
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos && !currentPatchName.empty()) {
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                LOG_DEBUG("[OptimizationManager] Found setting for [" + currentPatchName + "] - Key: " + key + ", Value: " + value);

                // Find the patch by name and apply the setting
                bool foundPatch = false;
                for (auto& patch : patches) { // Iterate directly through the patches vector
                    if (patch->GetName() == currentPatchName) {
                        LOG_DEBUG("[OptimizationManager] Applying setting to patch: " + currentPatchName);

                        bool success = false;
                        // Special handling for FrameRatePatch which has custom LoadState
                        if (FrameRatePatch* framePatch = dynamic_cast<FrameRatePatch*>(patch.get())) {
                             // FrameRatePatch expects key and value
                            success = framePatch->LoadState(key, value);
                        }
                        // Generic handling for "Enabled" key for all other patches
                        else if (key == "Enabled") {
                            // Other patches expect only the value for LoadState
                            success = patch->LoadState(value);
                        } else {
                             // Log if a key other than "Enabled" is found for a non-FrameRate patch
                             LOG_WARNING("[OptimizationManager] Ignoring unknown key '" + key + "' for patch '" + currentPatchName + "'");
                             success = true; // Treat as success to continue parsing
                        }

                        LOG_DEBUG("[OptimizationManager] State application " +
                            std::string(success ? "succeeded" : "failed"));

                        foundPatch = true;
                        break; // Found the patch, move to the next line
                    }
                }

                if (!foundPatch) {
                    LOG_WARNING("[OptimizationManager] No matching patch found for section: " +
                        currentPatchName);
                }
            }
        }

        if (!foundAnyOptimizations) {
            LOG_DEBUG("[OptimizationManager] No optimization sections found in file");
        }

        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR(std::string("[OptimizationManager] Error loading state: ") + e.what());
        return false;
    }
}

// Update the RegisterPatch method to remove references to removed patches
void OptimizationManager::RegisterPatch(std::unique_ptr<OptimizationPatch> patch) {
    // Check if a patch with the same name already exists
    std::string patchName = patch->GetName();
    for (const auto& existingPatch : patches) {
        if (existingPatch->GetName() == patchName) {
            LOG_WARNING("[OptimizationManager] Patch already registered: " + patchName);
            return; // Skip this patch since it's already registered
        }
    }
    
    // If we get here, this is a new patch
    patches.push_back(std::move(patch));
    LOG_DEBUG("[OptimizationManager] Registered patch: " + patchName);
} 