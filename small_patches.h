#pragma once
#include "optimization.h"
#include <windows.h>
//https://www.youtube.com/watch?v=lKntlVofKqU
//literally my fav song

// Class for simple memory patches that modify game code
class LotVisibilityPatch : public OptimizationPatch {
private:
    static const uintptr_t VISIBILITY_CONDITION_ADDRESS = 0x00c63015;
    
public:
    LotVisibilityPatch() : OptimizationPatch("LotVisibility", nullptr) {}
    
    bool Install() override {
        if (isEnabled) return true;
        
        // Apply jump patch to change JZ to JMP
        DWORD oldProtect;
        if (VirtualProtect((LPVOID)VISIBILITY_CONDITION_ADDRESS, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *((BYTE*)VISIBILITY_CONDITION_ADDRESS) = 0xEB;  // Change JZ to JMP
            VirtualProtect((LPVOID)VISIBILITY_CONDITION_ADDRESS, 1, oldProtect, &oldProtect);
            
            isEnabled = true;
            OutputDebugStringA("[LotVisibilityPatch] Successfully installed\n");
            return true;
        }
        
        OutputDebugStringA("[LotVisibilityPatch] Failed to install\n");
        return false;
    }
    
    bool Uninstall() override {
        if (!isEnabled) return true;
        
        // Change back to JZ
        DWORD oldProtect;
        if (VirtualProtect((LPVOID)VISIBILITY_CONDITION_ADDRESS, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            *((BYTE*)VISIBILITY_CONDITION_ADDRESS) = 0x74;  // Change JMP back to JZ
            VirtualProtect((LPVOID)VISIBILITY_CONDITION_ADDRESS, 1, oldProtect, &oldProtect);
            
            isEnabled = false;
            OutputDebugStringA("[LotVisibilityPatch] Successfully uninstalled\n");
            return true;
        }
        
        OutputDebugStringA("[LotVisibilityPatch] Failed to uninstall\n");
        return false;
    }
};

// Renamed from FPSBasePatch to MemoryPatch to better reflect its purpose
class MemoryPatch : public OptimizationPatch {
protected:
    // Function to safely modify memory with proper protection
    bool WriteProtectedMemory(LPVOID targetAddress, LPCVOID sourceData, SIZE_T dataSize) {
        DWORD oldProtect;
        
        // Change protection to allow writing
        if (!VirtualProtect(targetAddress, dataSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            char buffer[256];
            sprintf_s(buffer, "[%s] Failed to modify memory protection at 0x%p", patchName.c_str(), targetAddress);
            OutputDebugStringA(buffer);
            return false;
        }
        
        // Copy the data
        memcpy(targetAddress, sourceData, dataSize);
        
        // Restore original protection
        VirtualProtect(targetAddress, dataSize, oldProtect, &oldProtect);
        
        // Verify the write succeeded
        if (memcmp(targetAddress, sourceData, dataSize) != 0) {
            char buffer[256];
            sprintf_s(buffer, "[%s] Failed to write memory at 0x%p", patchName.c_str(), targetAddress);
            OutputDebugStringA(buffer);
            return false;
        }
        
        return true;
    }
    
public:
    MemoryPatch(const std::string& name) : OptimizationPatch(name, nullptr) {}
};

// Class for framerate target optimizations [UNTESTED]
class FrameRatePatch : public MemoryPatch {
private:
    static const uintptr_t ADDR_FRAME_CONSTANT = 0x0108b1a0;  // Address of framerate timing constant
    
    // Original code bytes
    BYTE originalFrameConstant[4] = { 0x89, 0x88, 0x08, 0x3D }; // 30 FPS (0x3D088889)
    
    // Settings
    int targetFPS = 60;          // Target FPS (default: 60)
    
    bool ApplyFrameRatePatch() {
        // Determine the appropriate FPS value to use
        BYTE* patchedCode = nullptr;
        BYTE fps30[4] = { 0x89, 0x88, 0x08, 0x3D }; // 0x3D088889 ~= 0.03333333 (30 FPS)
        BYTE fps60[4] = { 0x89, 0x88, 0x88, 0x3C }; // 0x3C888889 ~= 0.01666667 (60 FPS)
        BYTE fps120[4] = { 0x89, 0x88, 0x08, 0x3C }; // 0x3C088889 ~= 0.00833333 (120 FPS)
        BYTE fps144[4] = { 0xD5, 0xC9, 0x92, 0x3B }; // 0x3B92C9D5 ~= 0.00694444 (144 FPS)
        BYTE fps240[4] = { 0x89, 0x88, 0x08, 0x3B }; // 0x3B088889 ~= 0.00416667 (240 FPS)
        
        switch (targetFPS) {
            case 30: patchedCode = fps30; break;
            case 60: patchedCode = fps60; break;
            case 120: patchedCode = fps120; break;
            case 144: patchedCode = fps144; break;
            case 240: patchedCode = fps240; break;
            default:
                OutputDebugStringA("[FrameRatePatch] Unsupported FPS value. Using 60 FPS instead.");
                patchedCode = fps60;
                targetFPS = 60;
                break;
        }
        
        // Apply patch
        if (!WriteProtectedMemory((void*)ADDR_FRAME_CONSTANT, patchedCode, 4)) {
            return false;
        }
        
        char buffer[256];
        sprintf_s(buffer, "[%s] Patched frame rate target to %d FPS", patchName.c_str(), targetFPS);
        OutputDebugStringA(buffer);
        return true;
    }
    
public:
    FrameRatePatch() : MemoryPatch("FrameRate") {}
    
    bool Install() override {
        if (isEnabled) return true;
        
        OutputDebugStringA("[FrameRatePatch] [UNTESTED] Installing frame rate optimization...");
        
        // Apply patch
        if (ApplyFrameRatePatch()) {
            isEnabled = true;
            OutputDebugStringA("[FrameRatePatch] Successfully installed");
            return true;
        }
        
        // If patch failed, revert
        OutputDebugStringA("[FrameRatePatch] Patch failed, reverting changes");
        Uninstall();
        return false;
    }
    
    bool Uninstall() override {
        if (!isEnabled) return true;
        
        OutputDebugStringA("[FrameRatePatch] Removing frame rate optimization...");
        
        // Revert patch
        WriteProtectedMemory((void*)ADDR_FRAME_CONSTANT, originalFrameConstant, sizeof(originalFrameConstant));
        
        isEnabled = false;
        OutputDebugStringA("[FrameRatePatch] Successfully uninstalled");
        return true;
    }
    
    // Getters and setters
    int GetTargetFPS() const { return targetFPS; }
    void SetTargetFPS(int fps) {
        targetFPS = fps;
        if (isEnabled) {
            ApplyFrameRatePatch();
        }
    }
    
    // Custom serialization
    void SaveState(std::ofstream& file) const override {
        file << "[Optimization_" << patchName << "]\n";
        file << "Enabled=" << (isEnabled ? "true" : "false") << "\n";
        file << "TargetFPS=" << targetFPS << "\n\n";
    }
    
    // Custom deserialization
    bool LoadState(const std::string& key, const std::string& value) {
        if (key == "Enabled") {
            return OptimizationPatch::LoadState(value);
        }
        else if (key == "TargetFPS") {
            try {
                SetTargetFPS(std::stoi(value));
                return true;
            }
            catch (...) {
                return false;
            }
        }
        return false;
    }
}; 