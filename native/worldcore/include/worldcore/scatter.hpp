// scatter.hpp - deterministic vegetation and rock placement per 128 m cell.
//
// Port of placement in src/world/scatter/index.js (browser reference, git
// a59773d): one jittered grid per cell, one terrain sample per grid point, and
// pick() lets slope, wetness, altitude and biome decide what grows there.
// Densities are stems per m^2 multiplied by the grid cell area.
//
// Two entry points:
//   place_cell()           the native rule. IMMUTABLE per cell: always the fine
//                          3.5 m grid, so a cell's trees never change with
//                          distance; distance only picks the mesh LOD to draw.
//   place_cell_reference() the reference's ring-dependent rule (7 m grid and
//                          fewer variants beyond ring 1), kept only to verify
//                          the port against tests/golden/scatter.json.
// Both are pure functions of (field, seed, cell): safe on any worker thread.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "worldcore/field.hpp"
#include "worldcore/vmath.hpp"

namespace worldcore {

enum Species : uint8_t {
    SPECIES_PINE = 0,
    SPECIES_BIRCH,
    SPECIES_SNAG,
    SPECIES_DEADFALL,
    SPECIES_BOULDER,
    SPECIES_SHRUB,
    SPECIES_FERN,
    SPECIES_GRASS,
    SPECIES_COUNT,
};

struct SpeciesInfo {
    const char* name;
    std::array<int, 3> variants;  // prototypes per detail level [far, mid, near]
    int rings;                    // reference only: outermost ring it appears in
    double radius;                // collision radius at scale 1; 0 = never collides
    double tiltMax;
    double scaleMin, scaleMax;
    bool groundCover;
};

inline constexpr std::array<SpeciesInfo, SPECIES_COUNT> kSpecies{{
    {"pine", {2, 4, 4}, 4, 0.34, 0.05, 0.8, 1.25, false},
    {"birch", {2, 3, 3}, 4, 0.26, 0.07, 0.8, 1.2, false},
    {"snag", {1, 2, 3}, 3, 0.22, 0.12, 0.8, 1.2, false},
    {"deadfall", {1, 2, 3}, 2, 0.45, 0.10, 0.85, 1.2, false},
    {"boulder", {2, 3, 4}, 3, 0.90, 0.18, 0.7, 1.6, false},
    {"shrub", {0, 0, 3}, 1, 0.0, 0.10, 0.7, 1.4, true},
    {"fern", {0, 0, 3}, 1, 0.0, 0.12, 0.7, 1.5, true},
    {"grass", {0, 0, 3}, 1, 0.0, 0.06, 0.7, 1.6, true},
}};

/// One placed stem or rock, in world space.
struct Placement {
    Species species;
    uint8_t variant;   // index into the near-detail prototypes (kSpecies.variants[2])
    Vec3 position;     // base of the instance: ground height - 0.06 * scale
    double groundY;    // terrain height at (x, z)
    double scale;
    Quat rotation;     // lean toward the ground normal, then yaw about +Y
    std::array<float, 3> tint;

    double collision_radius() const { return kSpecies[species].radius * scale; }
};

struct CellPlacement {
    int32_t cx = 0, cz = 0;
    std::vector<Placement> items;
};

/// The native, immutable placement for one cell. `density` is the graphics-preset multiplier.
CellPlacement place_cell(const Field& field, int32_t seed, int32_t cx, int32_t cz, double density = 1.0);

/// The reference's ring-dependent placement (verification only).
CellPlacement place_cell_reference(const Field& field, int32_t seed, int32_t cx, int32_t cz, int ring,
                                   double density = 1.0);

}  // namespace worldcore
