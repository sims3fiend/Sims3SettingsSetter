#include "optimization.h"
#include "patch_system.h"
#include <intrin.h>
#include <format>
#include <detours.h>
#include "intersection_patch.h"
#include "cpu_optimization.h"
#include "utils.h"
#include "logger.h"

// Metadata storage for patches (use unique_ptr to avoid pointer invalidation on reallocation)
static std::vector<std::unique_ptr<PatchMetadata>>& GetMetadataStorage() {
    static std::vector<std::unique_ptr<PatchMetadata>> storage;
    return storage;
}

void OptimizationPatch::SetMetadata(const PatchMetadata& meta) {
    // Store in static storage to ensure lifetime
    // Using unique_ptr so pointers remain valid even if vector grows
    GetMetadataStorage().push_back(std::make_unique<PatchMetadata>(meta));
    metadata = GetMetadataStorage().back().get();
}

bool OptimizationPatch::IsCompatibleWithCurrentVersion() const {
    if (!metadata) {
        return true; // No metadata = assume compatible
    }

    GameVersion current = DetectGameVersion();
    GameVersion target = metadata->targetVersion;

    // All means it works on any version
    if (target == GameVersion::All) {
        return true;
    }

    // Otherwise must match exactly
    return current == target;
}

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
    static bool initializing = false;
    
    if (!initialized && !initializing) {
        initializing = true;  // Prevent re-entry
        
        LOG_INFO("[OptimizationManager] Initializing patches...");
        
        try {
            // Use PatchRegistry to auto-register all patches
            PatchRegistry::InstantiateAll();
            
            // Log all registered patches
            LOG_INFO("[OptimizationManager] Registered patches:");
            for (const auto& patch : instance.patches) {
                LOG_INFO("  - " + patch->GetName());
            }
            LOG_INFO("[OptimizationManager] All patches registered");
        }
        catch (const std::exception& e) {
            LOG_ERROR("[OptimizationManager] Exception during patch initialization: " + std::string(e.what()));
        }
        catch (...) {
            LOG_ERROR("[OptimizationManager] Unknown exception during patch initialization");
        }
        
        initialized = true;
        initializing = false;
    }
    else if (initializing) {
        LOG_ERROR("[OptimizationManager] Recursive call detected during initialization!");
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

                        // Pass both key and value to patch's LoadState
                        // This allows patches to handle Enabled state and custom settings
                        bool success = patch->LoadState(key, value);

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