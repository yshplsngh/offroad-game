// scatter.cpp - see scatter.hpp. pick() and populate() follow the reference
// line for line, including the order random numbers are drawn in: every draw
// shifts every later tree in the cell.
#include "worldcore/scatter.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "worldcore/hash.hpp"
#include "worldcore/world.hpp"

namespace worldcore {

namespace {

/// THREE.MathUtils.smoothstep(x, min, max)
double smooth(double x, double min, double max) {
    if (x <= min) return 0;
    if (x >= max) return 1;
    x = (x - min) / (max - min);
    return x * x * (3 - 2 * x);
}

/// What grows here, if anything (reference pick()).
std::optional<Species> pick(Mulberry32& rng, const GroundSample& s, double area, bool fine, double density) {
    const int biome = s.biome;
    const double ny = s.ny, wet = s.wetness, height = s.height;

    if (s.surfaceId == SURFACE_WATER) return std::nullopt;
    if (ny < 0.55) {
        if (rng() < 0.002 * area) return SPECIES_BOULDER;
        return std::nullopt;
    }

    const double slopeOk = smooth(ny, 0.62, 0.86);
    const double treeline = 1 - smooth(height, 104, 136);
    const double lowline = smooth(height, 1.5, 6);
    const double dry = 1 - smooth(wet, 0.25, 0.62);

    double forest = 0, broadleaf = 0.25;
    switch (biome) {
        case BIOME_PINE: forest = 0.0062; broadleaf = 0.16; break;
        case BIOME_MEADOW: forest = 0.0013; broadleaf = 0.55; break;
        case BIOME_SCREE: forest = 0.0004; broadleaf = 0.05; break;
        case BIOME_RIVERBED: forest = 0.0005; broadleaf = 0.80; break;
        case BIOME_MUDFLAT: forest = 0.0002; broadleaf = 0.70; break;
        default: forest = 0; break;
    }
    forest *= slopeOk * treeline * lowline * dry * density * area;

    const double roll = rng();
    if (roll < forest) {
        if (rng() < 0.07) return SPECIES_SNAG;
        return rng() < broadleaf ? SPECIES_BIRCH : SPECIES_PINE;
    }

    const double rocky = ((biome == BIOME_SCREE || biome == BIOME_ALPINE) ? 0.0022
                          : biome == BIOME_RIVERBED ? 0.0016
                                                    : 0.0005 * (1 - slopeOk * 0.5)) *
                         density * area;
    if (roll < forest + rocky) return SPECIES_BOULDER;

    const double rot = forest > 0.02 ? 0.0004 * density * area : 0;
    if (roll < forest + rocky + rot) return SPECIES_DEADFALL;

    if (!fine) return std::nullopt;

    const double cover = (1 - smooth(height, 100, 130)) * dry * density * area;
    const double r2 = rng();
    if (biome == BIOME_PINE) {
        if (r2 < 0.0060 * cover) return SPECIES_FERN;
        if (r2 < 0.0085 * cover) return SPECIES_SHRUB;
        if (r2 < 0.0140 * cover) return SPECIES_GRASS;
    } else if (biome == BIOME_MEADOW || biome == BIOME_RIVERBED) {
        if (r2 < 0.0110 * cover) return SPECIES_GRASS;
        if (r2 < 0.0140 * cover) return SPECIES_SHRUB;
    }
    return std::nullopt;
}

/// JS `(cx * 73856093) ^ (cz * 19349663) ^ (seed * 83492791)`: double products, ToInt32, xor.
uint32_t cell_seed(int32_t seed, int32_t cx, int32_t cz) {
    const int32_t a = to_int32(static_cast<double>(cx) * 73856093.0);
    const int32_t b = to_int32(static_cast<double>(cz) * 19349663.0);
    const int32_t c = to_int32(static_cast<double>(seed) * 83492791.0);
    return static_cast<uint32_t>(a ^ b ^ c);
}

/// Quaternion.setFromUnitVectors((0,1,0), to) for a unit `to`.
Quat from_up(const Vec3& to) {
    const double r = to.y + 1;
    Quat q;
    if (r < 2.220446049250313e-16) {
        q = {0, 0, 1, 0};  // opposite vectors: three's choice for from = +Y
    } else {
        q = {to.z, 0, -to.x, r};
    }
    const double len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

Quat multiply(const Quat& a, const Quat& b) {
    return {a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y, a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z,
            a.z * b.w + a.w * b.z + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

/// populate(cx, cz, ring) with `fine` and `detail` made explicit.
CellPlacement populate(const Field& field, int32_t seed, int32_t cx, int32_t cz, int ring, bool fine, int detail,
                       bool ringCutoff, double density) {
    const double cell = fine ? 3.5 : 7;
    const int n = static_cast<int>(js_round(kChunk / cell));
    const double step = kChunk / n;
    const double area = step * step;
    Mulberry32 rng(cell_seed(seed, cx, cz));

    CellPlacement out;
    out.cx = cx;
    out.cz = cz;
    const double ox = cx * kChunk, oz = cz * kChunk;

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            const double x = ox + (i + 0.15 + rng() * 0.7) * step;
            const double z = oz + (j + 0.15 + rng() * 0.7) * step;

            const GroundSample s = field.sample(x, z);
            const std::optional<Species> key = pick(rng, s, area, fine, density);
            if (!key) continue;

            const SpeciesInfo& spec = kSpecies[*key];
            if (ringCutoff && ring > spec.rings) continue;
            // The reference falls back to near-detail prototypes when a detail has none.
            const int variants = spec.variants[static_cast<size_t>(detail)] > 0
                ? spec.variants[static_cast<size_t>(detail)]
                : spec.variants[2];
            const auto variant = static_cast<uint8_t>(static_cast<int>(rng() * variants));

            const double scale = spec.scaleMin + rng() * (spec.scaleMax - spec.scaleMin);
            const double yaw = rng() * std::numbers::pi * 2;
            const Vec3 axis = Vec3{s.nx, s.ny + 0.12 / std::max(0.02, spec.tiltMax), s.nz}.normalized();
            const Quat rotation = multiply(from_up(axis), Quat::axis_angle({0, 1, 0}, yaw));

            const double v = 0.86 + rng() * 0.28;
            const double tr = v * (0.97 + rng() * 0.06);
            const double tb = v * (0.95 + rng() * 0.1);

            Placement p;
            p.species = *key;
            p.variant = variant;
            p.position = {x, s.height - 0.06 * scale, z};
            p.groundY = s.height;
            p.scale = scale;
            p.rotation = rotation;
            p.tint = {static_cast<float>(tr), static_cast<float>(v), static_cast<float>(tb)};
            out.items.push_back(p);
        }
    }
    return out;
}

}  // namespace

CellPlacement place_cell(const Field& field, int32_t seed, int32_t cx, int32_t cz, double density) {
    return populate(field, seed, cx, cz, 0, true, 2, false, density);
}

CellPlacement place_cell_reference(const Field& field, int32_t seed, int32_t cx, int32_t cz, int ring,
                                   double density) {
    const int detail = ring == 0 ? 2 : ring <= 2 ? 1 : 0;
    return populate(field, seed, cx, cz, ring, ring <= 1, detail, true, density);
}

}  // namespace worldcore
