// flora.hpp - procedural prototype meshes for every scatter species, 3 LODs.
//
// Port of src/world/scatter/flora.js and geo.js (browser reference, git
// a59773d). Every prototype is a flat-shaded triangle list with a per-vertex
// linear colour, modelled with its base at the origin growing along +Y, so one
// instance transform (position, lean, yaw, scale) drives every LOD.
//
// The reference's wind weight (`aFlex`) is not carried: PLAN.md forbids
// foliage animation in the shipping build.
//
// Prototypes are generated once, at build time, by the `worldcore_bake` tool
// (tools/bake.cpp), never while the game runs. Their seed is fixed
// (kFloraSeed) so the baked set does not depend on the world seed.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "worldcore/scatter.hpp"

namespace worldcore {

inline constexpr int32_t kFloraSeed = 1337;

/// Reference: exactly the browser reference's shapes (used by the golden test).
/// Native: the shipping set. Deliberate deviations from the reference:
///  - far (detail 0) pine is a six-sided cone on a stem and far birch a low-poly
///    crown cluster, instead of crossed flat opaque cards, which fog turned into
///    pale rectangles on distant hillsides.
enum class FloraStyle { Reference, Native };

/// Triangle list, counter-clockwise front faces (the reference's convention).
struct FloraMesh {
    std::vector<float> positions;  // xyz per vertex
    std::vector<float> normals;    // flat face normals
    std::vector<float> colors;     // linear rgb
    size_t triangles() const { return positions.size() / 9; }
};

struct FloraPrototype {
    Species species;
    int detail;   // 0 far, 1 mid, 2 near
    int variant;
    bool doubleSided;  // leaf/needle/grass cards are seen from both sides
    FloraMesh mesh;
};

/// One prototype, exactly as the reference's scatter provider built it.
FloraMesh build_flora(Species species, int detail, int variant, int32_t seed = kFloraSeed,
                      FloraStyle style = FloraStyle::Native);

/// The whole set the reference instantiated: species x detail x variant,
/// skipping details a species has no prototypes for.
std::vector<FloraPrototype> build_all_flora(int32_t seed = kFloraSeed, FloraStyle style = FloraStyle::Native);

/// Binary bake format read by the GDExtension ("RLFLORA1", little-endian).
std::vector<uint8_t> serialize_flora(const std::vector<FloraPrototype>& set);
bool deserialize_flora(const std::vector<uint8_t>& bytes, std::vector<FloraPrototype>& out, std::string* error);

}  // namespace worldcore
