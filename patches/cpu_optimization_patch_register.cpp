#include "../patch_system.h"
#include "../cpu_optimization.h"

// Register the existing CPUOptimizationPatch with metadata
REGISTER_PATCH(CPUOptimizationPatch, {
    .displayName = "CPU Thread Optimization",
    .description = "Optimizes thread placement for modern CPUs to better utilize modern CPU architectures.",
    .category = "Performance",
    .experimental = false,
    .supportedVersions = allGameVersionsMask,
    .technicalDetails = {
        "Intel Hybrid (P/E-cores): Prioritizes Performance cores",
        "AMD: Distributes threads across the first CCX (L3 cache group)",
        "Hooks SetThreadIdealProcessor to assign game threads intelligently",
        "Detects CPU topology using CPUID and Windows APIs"
    }
})

