// vegetation.hpp - one streamed cell of static vegetation, ready for instancing.
//
// PLAN.md milestone 3: one instanced batch per visible cell + species + LOD +
// variant, each with an exact AABB (so the engine culls at batch granularity),
// no animation, no shadows. This builds those batches from place_cell() on a
// worker thread; the host only uploads them.
#pragma once

#include <array>
#include <vector>

#include "worldcore/flora.hpp"
#include "worldcore/scatter.hpp"

namespace worldcore {

/// Per-prototype bounds in model space, from the baked flora set.
struct PrototypeExtent {
    float radiusXZ = 0;  // max horizontal distance of any vertex from the stem axis
    float minY = 0, maxY = 0;
};

/// extents[species][detail][variant]
using FloraExtents = std::array<std::array<std::vector<PrototypeExtent>, 3>, SPECIES_COUNT>;
FloraExtents flora_extents(const std::vector<FloraPrototype>& set);

struct VegetationBatch {
    Species species;
    int detail;
    int variant;
    size_t count = 0;
    /// Per instance: 3x4 row-major transform (basis rows with origin in the 4th
    /// column, the layout Godot's MultiMesh buffer uses) then rgba tint: 16 floats.
    std::vector<float> buffer;
    Vec3 aabbMin, aabbMax;  // world space, covers every instance's prototype
};

struct VegetationCell {
    int32_t cx = 0, cz = 0;
    int detail = 2;
    std::vector<VegetationBatch> batches;
    size_t instances() const;
};

/// Highest (farthest) detail level at which a species is still drawn.
/// Ground cover only near, deadfall out to mid, trees and rocks at every LOD.
int max_far_lod(Species s);

/// `lod`: 0 near .. 2 far (StreamScheduler::lod_for_distance); detail = 2 - lod.
VegetationCell build_vegetation_cell(const Field& field, int32_t seed, int32_t cx, int32_t cz, int lod,
                                     const FloraExtents& extents, double density = 1.0);

}  // namespace worldcore
