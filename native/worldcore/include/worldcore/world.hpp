// world.hpp - the contract: coordinates, chunk size, surface and biome ids.
//
// Mirrors the browser reference's src/world/contract.js (git a59773d); the
// neutral table is native/godot/data/world.json. Ids are what cross module
// boundaries; feel numbers live in data.
#pragma once

#include <cmath>
#include <cstdint>

namespace worldcore {

// +X east, +Y up, +Z north/forward. 1 unit = 1 metre.
inline constexpr double kChunk = 128.0;
inline constexpr double kFixedDt = 1.0 / 60.0;

enum Surface : int {
    SURFACE_ROCK = 0,
    SURFACE_GRAVEL = 1,
    SURFACE_DIRT = 2,
    SURFACE_GRASS = 3,
    SURFACE_LOAM = 4,
    SURFACE_MUD = 5,
    SURFACE_WATER = 6,
    SURFACE_SNOW = 7,
    SURFACE_COUNT = 8,
};

enum Biome : int {
    BIOME_RIVERBED = 0,
    BIOME_MUDFLAT = 1,
    BIOME_MEADOW = 2,
    BIOME_PINE = 3,
    BIOME_SCREE = 4,
    BIOME_ALPINE = 5,
    BIOME_COUNT = 6,
};

inline int32_t to_chunk(double v) { return static_cast<int32_t>(std::floor(v / kChunk)); }

}  // namespace worldcore
