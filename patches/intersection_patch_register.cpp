#include "../patch_system.h"
#include "../intersection_patch.h"

// Register the existing IntersectionPatch with metadata
REGISTER_PATCH(IntersectionPatch, {.displayName = "Intersection Optimization",
                                      .description = "Optimizes intersection calculations with SIMD instructions",
                                      .category = "Performance",
                                      .experimental = true,
                                      .supportedVersions = VERSION_ALL,
                                      .technicalDetails = {"Uses SSE4.1/FMA instructions when available", "Improves performance for initial navmesh creation on maps that don't have it precalculated",
                                          "Negligible impact on pathfinding after initial load"}})
