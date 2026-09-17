// field.cpp - see field.hpp. Terms, constants and their order follow
// src/world/terrain/field.js line for line (browser reference, git a59773d);
// its comments give the reasoning behind each term. Anything that differs here is about threading.
#include "worldcore/field.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include "worldcore/hash.hpp"
#include "worldcore/noise.hpp"

namespace worldcore {

namespace {

constexpr double ROLL_AMP = 21;
constexpr double ROLL_MID = 24;
constexpr double HILL_AMP = 20;
constexpr double MTN_AMP = 172;
constexpr double MAX_CUT = 74;
constexpr double BED_HALF = 9;
constexpr double RIVER_SCALE = 430;

std::atomic<uint64_t> g_next_field_id{1};

using RGB = std::array<double, 3>;
constexpr RGB C_WATER{0.055, 0.105, 0.105};
constexpr RGB C_GRAVEL{0.235, 0.215, 0.180};
constexpr RGB C_ROCK{0.150, 0.147, 0.138};
constexpr RGB C_SCREE{0.205, 0.192, 0.172};
constexpr RGB C_MUD{0.072, 0.056, 0.034};
constexpr RGB C_GRASS{0.105, 0.150, 0.052};
constexpr RGB C_LOAM{0.062, 0.082, 0.036};
constexpr RGB C_DIRT{0.135, 0.100, 0.055};
constexpr RGB C_SNOW{0.760, 0.800, 0.860};

uint64_t tile_key(int32_t tx, int32_t tz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(tx)) << 32) | static_cast<uint32_t>(tz);
}

// Per-thread memo of recently used tiles, so the hot path (a worker meshing a
// chunk touches one or two tiles tens of thousands of times) takes no lock.
struct TlsTileSlot {
    uint64_t field = 0;
    uint64_t key = 0;
    std::shared_ptr<const void> tile;
};
constexpr int kTlsSlots = 4;
thread_local std::array<TlsTileSlot, kTlsSlots> tls_tiles;
thread_local int tls_next = 0;

}  // namespace

Field::Field(int32_t seed) : seed_(seed), id_(g_next_field_id.fetch_add(1)) {}

/* ------------------------------------------------------ low-frequency ---- */

std::shared_ptr<Field::Tile> Field::build_tile(int32_t tx, int32_t tz) const {
    const int32_t S = seed_;
    auto t = std::make_shared<Tile>();
    auto& data = t->data;
    const double ox = static_cast<double>(tx) * kLowTile, oz = static_cast<double>(tz) * kLowTile;
    size_t p = 0;
    for (int j = 0; j <= kLowTile; j++) {
        const double z = (oz + j) * kLowStep;
        for (int i = 0; i <= kLowTile; i++) {
            const double x = (ox + i) * kLowStep;
            data[p] = static_cast<float>(fbm(x * 0.00068, z * 0.00068, S + 11, 3, 2.1, 0.5));
            data[p + 1] = static_cast<float>(fbm(x * 0.00045, z * 0.00045, S + 23, 2, 2.0, 0.5));
            const double rx = x + perlin2(x * 0.00063, z * 0.00063, S + 31) * 330;
            const double rz = z + perlin2(x * 0.00063 + 53.1, z * 0.00063 - 27.7, S + 32) * 330;
            data[p + 2] = static_cast<float>(
                std::fabs(fbm(rx * 0.00131, rz * 0.00131, S + 33, 2, 2.0, 0.45)) * RIVER_SCALE);
            data[p + 3] = static_cast<float>(fbm(x * 0.00192, z * 0.00192, S + 41, 2, 2.0, 0.5) * 0.5 + 0.5);
            p += 4;
        }
    }
    return t;
}

Field::TilePtr Field::tile(int32_t tx, int32_t tz) const {
    const uint64_t key = tile_key(tx, tz);
    for (auto& slot : tls_tiles) {
        if (slot.field == id_ && slot.key == key && slot.tile) {
            return std::static_pointer_cast<const Tile>(slot.tile);
        }
    }

    TilePtr t;
    {
        std::shared_lock lock(mutex_);
        auto it = tiles_.find(key);
        if (it != tiles_.end()) t = it->second;
    }
    if (!t) {
        // Built outside the lock: two threads may race to build the same tile,
        // which wastes a little work but can never produce different data.
        TilePtr built = build_tile(tx, tz);
        std::unique_lock lock(mutex_);
        auto [it, inserted] = tiles_.try_emplace(key, built);
        if (inserted && tiles_.size() > kLowMaxTiles) {
            // Arbitrary quarter. Readers hold their own reference, so this is safe.
            size_t n = kLowMaxTiles / 4;
            for (auto e = tiles_.begin(); e != tiles_.end() && n > 0;) {
                if (e->first == key) { ++e; continue; }
                e = tiles_.erase(e);
                --n;
            }
            it = tiles_.find(key);
        }
        t = it->second;
    }

    auto& slot = tls_tiles[tls_next];
    tls_next = (tls_next + 1) % kTlsSlots;
    slot.field = id_;
    slot.key = key;
    slot.tile = t;
    return t;
}

std::array<float, 4> Field::low_at(double x, double z) const {
    const double fx = x / kLowStep, fz = z / kLowStep;
    const double nx = std::floor(fx), nz = std::floor(fz);
    const double u = fx - nx, v = fz - nz;
    const double tx = std::floor(nx / kLowTile), tz = std::floor(nz / kLowTile);
    const TilePtr t = tile(static_cast<int32_t>(tx), static_cast<int32_t>(tz));
    const auto& d = t->data;
    const size_t i = static_cast<size_t>(nx - tx * kLowTile);
    const size_t j = static_cast<size_t>(nz - tz * kLowTile);
    const size_t a = (j * kLowStride + i) * 4;
    const size_t b = a + 4;
    const size_t c = a + kLowStride * 4;
    const size_t e = c + 4;
    std::array<float, 4> out{};
    for (size_t k = 0; k < 4; k++) {
        const double top = d[a + k] + (static_cast<double>(d[b + k]) - d[a + k]) * u;
        const double bot = d[c + k] + (static_cast<double>(d[e + k]) - d[c + k]) * u;
        out[k] = static_cast<float>(top + (bot - top) * v);
    }
    return out;
}

/* --------------------------------------------------------- height model -- */

FieldPoint Field::compute(double x, double z) const {
    const int32_t S = seed_;
    const auto low = low_at(x, z);
    const double roll = low[0], massif = low[1], rivD = low[2], bogRaw = low[3];

    const double mtn = smoothstep(0.02, 0.56, massif);

    double h = ROLL_MID + roll * ROLL_AMP;

    const double wx = x + perlin2(x * 0.0037, z * 0.0037, S + 61) * 105;
    const double wz = z + perlin2(x * 0.0037 + 41.7, z * 0.0037 - 19.3, S + 62) * 105;
    h += fbm(wx * 0.0052, wz * 0.0052, S + 63, 4, 2.07, 0.5) * HILL_AMP;

    if (mtn > 0.002) {
        h += std::pow(mtn, 1.3) * ridged(x * 0.00088, z * 0.00088, S + 71, 5, 2.04, 0.46) * MTN_AMP;
    }

    const double bed = 0.5 + (roll * 0.5 + 0.5) * 3.4 + std::pow(mtn, 1.25) * 44;

    const double bogW = smoothstep(0.575, 0.735, bogRaw)
        * (1 - smoothstep(19, 46, h))
        * (1 - smoothstep(0.02, 0.24, mtn));
    if (bogW > 0.004) h += ((bed + 2.6) - h) * (bogW * 0.96);

    double rivN = 0, flat = bogW;
    bool water = false;
    const double drop = h - bed;
    if (drop > 0.4) {
        const double cut = drop < MAX_CUT ? drop : MAX_CUT;
        const double W = 26 + 2.25 * cut;
        const double t = rivD / W;
        if (t < 1) {
            const double bedFrac = BED_HALF / W < 0.4 ? BED_HALF / W : 0.4;
            double s, bedFlat;
            if (t <= bedFrac) {
                s = 1;
                bedFlat = 1;
            } else {
                const double u = (t - bedFrac) / (1 - bedFrac);
                s = 1 - u * u * (3 - 2 * u);
                bedFlat = 1 - smoothstep(0, 0.25, u);
            }
            s = std::pow(s, 0.8);
            h -= cut * s;
            rivN = s;
            if (bedFlat > flat) flat = bedFlat;

            const double wi = 11 + 0.1 * cut;
            if (rivD < wi) {
                const double ti = rivD / wi;
                h -= (1 - ti * ti) * 2.4;
                water = ti < 0.75;
            }
        }
    }

    const double calm = 1 - 0.92 * flat;
    h += fbm(x * 0.0195, z * 0.0195, S + 81, 3, 2.1, 0.48) * 2.9 * calm * (0.45 + 0.75 * mtn);
    h += fbm(x * 0.085, z * 0.085, S + 82, 2, 2.0, 0.5) * 0.42 * calm;

    return FieldPoint{h, bed, mtn, rivN, bogW, flat, water};
}

/* ----------------------------------------------------- classification ---- */

double Field::veg(double x, double z) const { return fbm(x * 0.0031, z * 0.0031, seed_ + 91, 2, 2.0, 0.5); }

FieldClass Field::classify_from(double x, double z, const FieldPoint& f, double ny) const {
    const double h = f.h;
    const double v = veg(x, z);
    const double wet = clamp01(std::max(f.bogW * 0.95, f.rivN * f.rivN));
    const double rock = smoothstep(0.90, 0.74, ny);
    const double snowLine = 150 + v * 22;
    const double snow = smoothstep(snowLine, snowLine + 26, h) * smoothstep(0.62, 0.80, ny);

    int surf, biome;
    RGB col;
    if (f.water && ny > 0.80) {
        surf = SURFACE_WATER; biome = BIOME_RIVERBED; col = C_WATER;
    } else if (ny < 0.74) {
        surf = SURFACE_ROCK; col = C_ROCK;
        biome = h > 148 ? BIOME_ALPINE : h > 88 ? BIOME_SCREE : BIOME_PINE;
    } else if (ny < 0.87) {
        surf = SURFACE_GRAVEL; col = C_SCREE;
        biome = h > 148 ? BIOME_ALPINE : h > 88 ? BIOME_SCREE
            : h > 26 + v * 18 ? BIOME_PINE : BIOME_MEADOW;
    } else if (f.rivN > 0.42) {
        surf = SURFACE_GRAVEL; biome = BIOME_RIVERBED; col = C_GRAVEL;
    } else if (f.bogW > 0.45) {
        surf = SURFACE_MUD; biome = BIOME_MUDFLAT; col = C_MUD;
    } else if (snow > 0.5) {
        surf = SURFACE_SNOW; biome = BIOME_ALPINE; col = C_SNOW;
    } else if (h > 96 + v * 14) {
        surf = SURFACE_GRAVEL; biome = BIOME_SCREE; col = C_SCREE;
    } else if (h > 26 + v * 18) {
        surf = SURFACE_LOAM; biome = BIOME_PINE; col = C_LOAM;
    } else if (h < 3) {
        surf = SURFACE_GRAVEL; biome = BIOME_RIVERBED; col = C_GRAVEL;
    } else {
        surf = SURFACE_GRASS; biome = BIOME_MEADOW; col = C_GRASS;
    }

    double r = col[0], g = col[1], b = col[2];
    if (surf == SURFACE_GRASS || surf == SURFACE_LOAM) {
        const double t = smoothstep(18 + v * 16, 38 + v * 20, h);
        r = lerp(C_GRASS[0], C_LOAM[0], t);
        g = lerp(C_GRASS[1], C_LOAM[1], t);
        b = lerp(C_GRASS[2], C_LOAM[2], t);
        const double dry = smoothstep(0.4, 0.0, ny > 0.93 ? 1 : 0);
        r = lerp(r, C_DIRT[0], dry * 0.15);
        g = lerp(g, C_DIRT[1], dry * 0.15);
        b = lerp(b, C_DIRT[2], dry * 0.15);
    }
    if (snow > 0.001) {
        r = lerp(r, C_SNOW[0], snow);
        g = lerp(g, C_SNOW[1], snow);
        b = lerp(b, C_SNOW[2], snow);
    }
    const double j = 0.94 + hash2(to_int32(js_round(x * 0.5)), to_int32(js_round(z * 0.5)), seed_ + 7) * 0.12;

    FieldClass out;
    out.r = r * j;
    out.g = g * j;
    out.b = b * j;
    out.surface = surf;
    out.biome = biome;
    out.rock = rock * (1 - snow * 0.7);
    out.wet = wet;
    out.snow = snow;
    return out;
}

/* ------------------------------------------------------------ queries ---- */

std::array<double, 3> Field::normal(double x, double z) const {
    const double e = kNormalEps;
    const double hx = height(x + e, z) - height(x - e, z);
    const double hz = height(x, z + e) - height(x, z - e);
    const double nx = -hx, ny = 2 * e, nz = -hz;
    const double inv = 1 / std::hypot(nx, ny, nz);
    return {nx * inv, ny * inv, nz * inv};
}

GroundSample Field::sample(double x, double z) const {
    GroundSample out;
    out.height = height(x, z);
    const auto n = normal(x, z);
    out.nx = n[0];
    out.ny = n[1];
    out.nz = n[2];
    const FieldClass c = classify(x, z, out.ny);
    out.surfaceId = c.surface;
    out.wetness = c.wet;
    out.biome = c.biome;
    return out;
}

int Field::biome_at(double x, double z) const { return classify(x, z, normal(x, z)[1]).biome; }

Spawn Field::find_spawn(double x, double z) const {
    Spawn out{x, 0, z, false};
    double best = -INFINITY;
    for (int r = 0; r < 90; r++) {
        const double a = r * 2.399963;
        const double rad = r * 12;
        const double px = x + std::cos(a) * rad;
        const double pz = z + std::sin(a) * rad;
        const double ny = normal(px, pz)[1];
        if (ny < 0.96) continue;
        const FieldClass c = classify(px, pz, ny);
        if (c.wet > 0.2 || c.surface == SURFACE_WATER) continue;

        const double score = ny * 2000 - rad * 0.05;
        if (score <= best) continue;
        best = score;
        out.x = px;
        out.z = pz;
        out.y = height(px, pz);
        out.ok = true;
        if (ny > 0.997) break;
    }
    if (!out.ok) out.y = height(x, z);
    return out;
}

size_t Field::cached_tiles() const {
    std::shared_lock lock(mutex_);
    return tiles_.size();
}

void Field::clear_caches() {
    std::unique_lock lock(mutex_);
    tiles_.clear();
    // Thread-local slots of other threads still hold their tiles; they are
    // valid data for this seed, so leaving them is harmless.
}

}  // namespace worldcore
