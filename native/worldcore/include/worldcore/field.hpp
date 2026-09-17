// field.hpp - the analytic world model y = f(x, z) and its classification.
//
// Port of src/world/terrain/field.js (browser reference, git a59773d) plus
// the query half of terrain/index.js (normal, sample, findSpawn). This is what makes "never load the full map"
// possible: wheels, spawn and scatter ask the function, not a resident mesh.
//
// THREAD SAFETY: a Field is safe to query from any number of worker threads.
// The only mutable state is the low-frequency tile cache, whose tiles are
// immutable once built and shared by reference count, so eviction can never
// pull a tile out from under a reader.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include "worldcore/world.hpp"

namespace worldcore {

/// Intermediate terms of one height evaluation (field.js `compute` output).
struct FieldPoint {
    double h = 0, bed = 0, mtn = 0, rivN = 0, bogW = 0, flat = 0;
    bool water = false;
};

/// field.js `classify` output.
struct FieldClass {
    double r = 0, g = 0, b = 0;
    int surface = 0;
    int biome = 0;
    double rock = 0, wet = 0, snow = 0;
};

/// contract.js GroundSample, plus biome.
struct GroundSample {
    double height = 0;
    double nx = 0, ny = 1, nz = 0;
    int surfaceId = 2;
    double wetness = 0;
    int biome = 2;
};

struct Spawn {
    double x = 0, y = 0, z = 0;
    bool ok = false;
};

class Field {
public:
    static constexpr double kNormalEps = 0.6;

    explicit Field(int32_t seed = 1337);

    int32_t seed() const { return seed_; }

    FieldPoint compute(double x, double z) const;
    double height(double x, double z) const { return compute(x, z).h; }
    double veg(double x, double z) const;
    FieldClass classify_from(double x, double z, const FieldPoint& f, double ny) const;
    FieldClass classify(double x, double z, double ny) const {
        return classify_from(x, z, compute(x, z), ny);
    }

    /// Central-difference analytic normal (terrain/index.js normalAt).
    std::array<double, 3> normal(double x, double z) const;
    GroundSample sample(double x, double z) const;
    int biome_at(double x, double z) const;
    /// Flat, dry spawn on a golden-angle spiral (terrain/index.js findSpawn).
    Spawn find_spawn(double x = 0, double z = 0) const;

    size_t cached_tiles() const;
    void clear_caches();

private:
    static constexpr int kLowStep = 8;
    static constexpr int kLowTile = 32;
    static constexpr int kLowStride = kLowTile + 1;
    static constexpr size_t kLowMaxTiles = 600;

    struct Tile {
        std::array<float, kLowStride * kLowStride * 4> data;
    };
    using TilePtr = std::shared_ptr<const Tile>;

    std::shared_ptr<Tile> build_tile(int32_t tx, int32_t tz) const;
    TilePtr tile(int32_t tx, int32_t tz) const;
    /// Bilinear sample of the low-frequency lattice. float32 like the JS scratch.
    std::array<float, 4> low_at(double x, double z) const;

    int32_t seed_;
    uint64_t id_;  // distinguishes Fields in the thread-local tile cache
    mutable std::shared_mutex mutex_;
    mutable std::unordered_map<uint64_t, TilePtr> tiles_;
};

}  // namespace worldcore
