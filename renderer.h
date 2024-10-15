#pragma once
#include <cstdint>
#include <unordered_map>

enum class ToggleType {
    Wireframe,
    Shadow,
    ScenePost,
    Backfaces,
    SceneBoxes,
    CustomSceneBoxes,
    ScenePips,
    DropShadows,
    ShadowMapDebug,
    MultithreadedCulling,
    Use1x1Viewport,
    ObjDensityByTileSize,
    ObjDensityBySurfaceArea,
    CameraTarget,
    ShadowCamera,
    PrecipitationShadow,
    CullBackfacingPortals,
    SceneCull,
    VisibleInCAS,
    ShowVisualEffectOnly,
    Axis
};

struct ToggleInfo {
    size_t offset;
    const char* name;
};

enum class ValueType {
    MaxLots,
    // More to come perhaps..
};

struct ValueInfo {
    uintptr_t baseOffset;
    size_t additionalOffset;
    const char* name;
    int minValue;
    int maxValue;
};

extern std::unordered_map<ToggleType, ToggleInfo> toggles;
extern std::unordered_map<ValueType, ValueInfo> values;

void SetRendererStructureAddress();
void* GetRendererStructureAddress();
void DumpRendererMemory();
void DumpMemoryRegion(void* address, size_t size);
void AnalyzeBitfield();

template<typename T>
T ReadValue(size_t offset);

template<typename T>
void WriteValue(size_t offset, T value);

bool ReadBoolValue(size_t offset);
void WriteBoolValue(size_t offset, bool value);

int ReadIntValue(ValueType type);
void WriteIntValue(ValueType type, int value);