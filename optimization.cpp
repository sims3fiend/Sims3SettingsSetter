#include "optimization.h"
#include "patch_system.h"
#include <intrin.h>
#include <format>
#include <unordered_map>
#include <detours/detours.h>
#include "intersection_patch.h"
#include "cpu_optimization.h"
#include "utils.h"
#include "logger.h"
#include <toml++/toml.hpp>

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

    return IsVersionSupported(metadata->supportedVersions);
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

    // Check for AVX2 (CPUID leaf 7, EBX bit 5)
    __cpuidex(leaves, 7, 0);
    hasAVX2 = (leaves[1] & (1 << 5)) != 0;
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
        
        LOG_INFO("[PatchSystem] Initializing patches...");

        try {
            // Use PatchRegistry to auto-register all patches
            PatchRegistry::InstantiateAll(instance);

            // Log all registered patches
            LOG_INFO("[PatchSystem] Registered " + std::to_string(instance.patches.size()) + " patches:");
            for (const auto& patch : instance.patches) {
                LOG_INFO("  - " + patch->GetName());
            }
            LOG_INFO("[PatchSystem] Patch registration complete");
        }
        catch (const std::exception& e) {
            LOG_ERROR("[PatchSystem] Exception during patch initialization: " + std::string(e.what()));
        }
        catch (...) {
            LOG_ERROR("[PatchSystem] Unknown exception during patch initialization");
        }

        initialized = true;
        initializing = false;
    }
    else if (initializing) {
        LOG_ERROR("[PatchSystem] Recursive call detected during initialization!");
    }
    
    return instance;
}

const std::vector<std::unique_ptr<OptimizationPatch>>& OptimizationManager::GetPatches() const {
    return patches;
}

bool OptimizationManager::EnablePatch(const std::string& name) {
    for (auto& patch : patches) {
        if (patch->GetName() == name) {
            bool result = patch->Install();
            if (result) {
                m_hasUnsavedChanges = true;
            }
            return result;
        }
    }
    return false;
}

bool OptimizationManager::DisablePatch(const std::string& name) {
    for (auto& patch : patches) {
        if (patch->GetName() == name) {
            bool result = patch->Uninstall();
            if (result) {
                m_hasUnsavedChanges = true;
            }
            return result;
        }
    }
    return false;
}

void OptimizationManager::SaveToToml(toml::table& root) {
    toml::table patchesTable;

    for (const auto& patch : patches) {
        toml::table patchTable;
        patch->SaveToToml(patchTable);
        patchesTable.insert(patch->GetName(), std::move(patchTable));
    }

    if (!patchesTable.empty()) {
        root.insert("patches", std::move(patchesTable));
    }

    m_hasUnsavedChanges = false;
}

void OptimizationManager::LoadFromToml(const toml::table& root) {
    auto patchesNode = root["patches"].as_table();
    if (!patchesNode) {
        LOG_DEBUG("[PatchSystem] No patches section found in TOML");
        return;
    }

    for (auto& [patchName, patchNode] : *patchesNode) {
        auto* patchTable = patchNode.as_table();
        if (!patchTable) continue;

        std::string name(patchName.str());

        // Find the matching patch
        OptimizationPatch* patch = nullptr;
        for (auto& p : patches) {
            if (p->GetName() == name) {
                patch = p.get();
                break;
            }
        }

        if (!patch) {
            LOG_WARNING("[PatchSystem] No matching patch found for TOML section: " + name);
            continue;
        }

        patch->LoadFromToml(*patchTable);
    }
}

// Register a new patch in the system
void OptimizationManager::RegisterPatch(std::unique_ptr<OptimizationPatch> patch) {
    // Check if a patch with the same name already exists
    std::string patchName = patch->GetName();
    for (const auto& existingPatch : patches) {
        if (existingPatch->GetName() == patchName) {
            LOG_WARNING("[PatchSystem] Patch already registered: " + patchName);
            return; // Skip this patch since it's already registered
        }
    }

    // If we get here, this is a new patch
    patches.push_back(std::move(patch));
    LOG_DEBUG("[PatchSystem] Registered patch: " + patchName);
} 