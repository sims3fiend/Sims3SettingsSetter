//Everything in this will get deleted/moved in to hooks/something else, structure is incomplete. For now have merged it into "Live Edit"
#include "renderer.h"
#include "utils.h"
#include <sstream>
#include <iomanip>
#include <Windows.h>

void* rendererStructureAddress = nullptr;

std::unordered_map<ToggleType, ToggleInfo> toggles = {
    {ToggleType::Wireframe, {0x25c, "Wireframe"}},
    {ToggleType::Shadow, {0x260, "Shadow"}},
    {ToggleType::ScenePost, {0x265, "Scene Post"}},
    {ToggleType::Backfaces, {0x263, "Scene Backfaces"}},
    {ToggleType::SceneBoxes, {0x25d, "Scene Boxes"}},
    {ToggleType::CustomSceneBoxes, {0x25e, "Custom Scene Boxes"}},
    {ToggleType::ScenePips, {0x25f, "Scene Pips"}},
    {ToggleType::DropShadows, {0x267, "Drop Shadows"}},
    {ToggleType::ShadowMapDebug, {0x271, "Shadow Map Debug"}},
    {ToggleType::MultithreadedCulling, {0x26c, "Multithreaded Culling"}},
    {ToggleType::Use1x1Viewport, {0x276, "Use 1x1 Viewport"}},
    {ToggleType::ObjDensityByTileSize, {0x277, "Obj density by tile size"}},
    {ToggleType::ObjDensityBySurfaceArea, {0x278, "Obj density by surface area"}},
    {ToggleType::CameraTarget, {0x26f, "Camera Target"}},
    {ToggleType::ShadowCamera, {0x273, "Shadow Camera"}},
    {ToggleType::PrecipitationShadow, {0x261, "Scene Precipitation Shadow"}},
    {ToggleType::CullBackfacingPortals, {0x272, "Cull Backfacing Portals"}},
    {ToggleType::SceneCull, {0x262, "Scene Cull"}},
    {ToggleType::VisibleInCAS, {0x264, "Visible In CAS[idk]"}},
    {ToggleType::ShowVisualEffectOnly, {0x274, "Show VisualEffect Only[Does nothing]"}},
    {ToggleType::Axis, {0x26e, "Axis Gizmo"}} // Not the actual name, couldn't find any references idk, just changed a bunch of values and it popped up
};

std::unordered_map<ValueType, ValueInfo> values = {
    {ValueType::MaxLots, {0x00DECBC4, 0xE4, "Max Lots", 0, 50}}
};

template<typename T>
T ReadValue(size_t offset) {
    if (rendererStructureAddress) {
        return *reinterpret_cast<T*>(static_cast<char*>(rendererStructureAddress) + offset);
    }
    return T();
}

template<typename T>
void WriteValue(size_t offset, T value) {
    if (rendererStructureAddress) {
        *reinterpret_cast<T*>(static_cast<char*>(rendererStructureAddress) + offset) = value;
    }
}

bool ReadBoolValue(size_t offset) {
    if (rendererStructureAddress) {
        uint8_t byte = *reinterpret_cast<uint8_t*>(static_cast<char*>(rendererStructureAddress) + offset);
        return (byte & 1) != 0;
    }
    return false;
}

void WriteBoolValue(size_t offset, bool value) {
    if (rendererStructureAddress) {
        uint8_t* byte = reinterpret_cast<uint8_t*>(static_cast<char*>(rendererStructureAddress) + offset);
        if (value) {
            *byte |= 1;
        }
        else {
            *byte &= ~1;
        }
    }
}

int ReadIntValue(ValueType type) {
    if (rendererStructureAddress) {
        const auto& info = values[type];
        uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(L"TS3W.exe"));
        uintptr_t* intermediatePtr = reinterpret_cast<uintptr_t*>(baseAddress + info.baseOffset);
        if (intermediatePtr && *intermediatePtr) {
            return *reinterpret_cast<int*>(*intermediatePtr + info.additionalOffset);
        }
    }
    return 0;
}

void WriteIntValue(ValueType type, int value) {
    if (rendererStructureAddress) {
        const auto& info = values[type];
        uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(L"TS3W.exe"));
        uintptr_t* intermediatePtr = reinterpret_cast<uintptr_t*>(baseAddress + info.baseOffset);
        if (intermediatePtr && *intermediatePtr) {
            *reinterpret_cast<int*>(*intermediatePtr + info.additionalOffset) = value;
        }
    }
}

void SetRendererStructureAddress() {
    // Get the base address of TS3W.exe
    HMODULE hModule = GetModuleHandle(L"TS3W.exe");
    if (!hModule) {
        Log("Failed to get TS3W.exe base address");
        return;
    }

    // Calculate the address of the pointer
    uintptr_t pointerAddress = reinterpret_cast<uintptr_t>(hModule) + 0x00DD1868;

    // Read the pointer value
    void* newAddress = *reinterpret_cast<void**>(pointerAddress);

    if (newAddress != rendererStructureAddress) {
        rendererStructureAddress = newAddress;
        std::ostringstream oss;
        oss << "Updated renderer structure address to 0x" << std::hex << reinterpret_cast<uintptr_t>(rendererStructureAddress);
        Log(oss.str());

    }
}

void* GetRendererStructureAddress() {
    if (!rendererStructureAddress) {
        SetRendererStructureAddress();
    }
    return rendererStructureAddress;
}

void AnalyzeBitfield() {
    std::ostringstream oss;
    oss << "Bitfield Analysis:\n";

    for (const auto& [type, info] : toggles) {
        uint8_t byte = ReadValue<uint8_t>(info.offset);

        oss << info.name << " (offset 0x" << std::hex << info.offset << "):\n";
        oss << "  Byte value: 0x" << std::hex << static_cast<int>(byte) << "\n";
        oss << "  Bit pattern: ";

        for (int i = 7; i >= 0; --i) {
            oss << ((byte & (1 << i)) ? '1' : '0');
            if (i == 4) oss << ' ';
        }
        oss << "\n\n";
    }

    Log(oss.str());
}

void DumpRendererMemory() {
    void* dumpStart = static_cast<char*>(rendererStructureAddress) - 256;
    DumpMemoryRegion(dumpStart, 1024);
}

void DumpMemoryRegion(void* start, size_t size) {
    std::ostringstream oss;
    oss << "Memory Dump starting at " << start << " for " << size << " bytes:\n";

    unsigned char* p = static_cast<unsigned char*>(start);
    for (size_t i = 0; i < size; i += 16) {
        oss << std::setw(8) << std::setfill('0') << std::hex << (DWORD)(p + i) << ": ";

        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                oss << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(p[i + j]) << " ";
            }
            else {
                oss << "   ";
            }
        }

        oss << " ";

        for (size_t j = 0; j < 16; ++j) {
            if (i + j < size) {
                char c = p[i + j];
                oss << (c >= 32 && c <= 126 ? c : '.');
            }
        }

        oss << "\n";
    }

    Log(oss.str());
}

// Explicit instantiations for the template functions
template uint32_t ReadValue<uint32_t>(size_t offset);
template float ReadValue<float>(size_t offset);
template uint8_t ReadValue<uint8_t>(size_t offset);
template void WriteValue<uint32_t>(size_t offset, uint32_t value);
template void WriteValue<float>(size_t offset, float value);
template void WriteValue<uint8_t>(size_t offset, uint8_t value);