#pragma once
#include "optimization.h"
#include "addresses.h"

class IntersectionPatch : public OptimizationPatch {
public:
    IntersectionPatch();
    bool Install() override;
    bool Uninstall() override;

private:
    static bool __cdecl OptimizedHook();
    static IntersectionPatch* instance;
}; 