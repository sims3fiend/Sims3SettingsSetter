#pragma once
#include "optimization.h"

class IntersectionPatch : public OptimizationPatch {
public:
    IntersectionPatch();
    bool Install() override;
    bool Uninstall() override;

private:
    static bool __cdecl OptimizedHook();
    static IntersectionPatch* instance;
}; 