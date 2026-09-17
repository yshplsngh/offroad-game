#include "worldcore/ground_texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "worldcore/hash.hpp"

namespace worldcore {

namespace {

constexpr double TAU = 2 * std::numbers::pi;

/// Value noise on a lattice of `period` cells that wraps exactly at the texture edge.
double wrapped_noise(double u, double v, int period, int32_t seed) {
    const double x = u * period, y = v * period;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    auto h = [&](int ix, int iy) {
        return hash2(((ix % period) + period) % period, ((iy % period) + period) % period, seed);
    };
    const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const double a = h(x0, y0) + (h(x0 + 1, y0) - h(x0, y0)) * sx;
    const double b = h(x0, y0 + 1) + (h(x0 + 1, y0 + 1) - h(x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

double wrapped_fbm(double u, double v, int basePeriod, int octaves, int32_t seed) {
    double sum = 0, amp = 1, norm = 0;
    int period = basePeriod;
    for (int o = 0; o < octaves; o++) {
        sum += wrapped_noise(u, v, period, seed + o * 101) * amp;
        norm += amp;
        amp *= 0.5;
        period *= 2;
    }
    return sum / norm;
}

uint8_t to_byte(double v) { return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); }

}  // namespace

GroundTextures build_ground_textures(int size, uint32_t seed) {
    GroundTextures t;
    t.size = size;
    const size_t n = static_cast<size_t>(size) * size;
    std::vector<double> height(n, 0.0);
    std::vector<double> r(n), g(n), b(n);

    // Base: soil showing through, broken up by two noise scales.
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            const double u = static_cast<double>(x) / size, v = static_cast<double>(y) / size;
            const double patch = wrapped_fbm(u, v, 4, 3, static_cast<int32_t>(seed));
            const double grain = wrapped_fbm(u, v, 48, 2, static_cast<int32_t>(seed) + 7);
            const size_t i = static_cast<size_t>(y) * size + x;
            const double k = 0.38 + patch * 0.3 + grain * 0.14;  // dark soil and shade between tufts
            r[i] = k * 0.98;
            g[i] = k;
            b[i] = k * 0.92;
            height[i] = grain * 0.15;
        }
    }

    // Blades grow in tufts: each tuft is a cluster of strokes fanning out from a
    // root, lighter toward the tips, drawn with wraparound so the tile has no
    // seams. Sizes are chosen for a ~4 m tile, so blades span 5-20 cm on the
    // ground and survive mipmapping at driving distance.
    Mulberry32 rng(seed ^ 0x9E3779B9u);
    const double unit = size / 512.0;  // blade sizes are authored for 512 px
    const int tufts = static_cast<int>(size * size / 260);
    double rootX = 0, rootY = 0, tuftShade = 1;
    for (int k = 0; k < tufts * 9; k++) {
        if (k % 9 == 0) {
            rootX = rng() * size;
            rootY = rng() * size;
            tuftShade = 0.8 + rng() * 0.5;
        }
        const double spread = 5.0 * unit;
        const double cx = rootX + (rng() - 0.5) * spread, cy = rootY + (rng() - 0.5) * spread;
        const double angle = rng() * TAU;
        const double len = (7.0 + rng() * 17.0) * unit;
        const double width = (1.0 + rng() * 1.4) * unit;
        const double shade = tuftShade * (0.85 + rng() * 0.35);  // some blades catch more light
        const bool dry = rng() < 0.1;                            // a few straw-coloured ones
        const double dx = std::cos(angle), dy = std::sin(angle);
        const int x0 = static_cast<int>(std::floor(cx - len - 2)), x1 = static_cast<int>(std::ceil(cx + len + 2));
        const int y0 = static_cast<int>(std::floor(cy - len - 2)), y1 = static_cast<int>(std::ceil(cy + len + 2));
        for (int py = y0; py <= y1; py++) {
            for (int px = x0; px <= x1; px++) {
                // Distance from the pixel centre to the blade segment.
                const double qx = px + 0.5 - cx, qy = py + 0.5 - cy;
                const double along = std::clamp(qx * dx + qy * dy, 0.0, len);
                const double ex = qx - dx * along, ey = qy - dy * along;
                const double taper = width * (1.0 - 0.75 * along / len);  // pointed tip
                const double d = std::sqrt(ex * ex + ey * ey);
                const double cover = std::clamp(taper + 0.5 - d, 0.0, 1.0);  // anti-aliased edge
                if (cover <= 0) continue;
                const int wx = ((px % size) + size) % size, wy = ((py % size) + size) % size;
                const size_t i = static_cast<size_t>(wy) * size + wx;
                const double tipLight = 0.75 + 0.7 * along / len;
                const double lum = shade * tipLight;
                const double br = dry ? lum * 1.18 : lum * 0.9;
                const double bg = dry ? lum * 1.08 : lum * 1.1;
                const double bb = dry ? lum * 0.62 : lum * 0.72;
                r[i] += (br - r[i]) * cover;
                g[i] += (bg - g[i]) * cover;
                b[i] += (bb - b[i]) * cover;
                height[i] = std::max(height[i], cover * (0.35 + 0.65 * along / len));
            }
        }
    }

    // Normalise the albedo multiplier to a mean of 1.0 so biome colours keep their brightness.
    double mean = 0;
    for (size_t i = 0; i < n; i++) mean += (r[i] + g[i] + b[i]) / 3.0;
    mean /= static_cast<double>(n);
    t.albedo.resize(n * 4);
    for (size_t i = 0; i < n; i++) {
        t.albedo[i * 4] = to_byte(r[i] / mean * 0.5);
        t.albedo[i * 4 + 1] = to_byte(g[i] / mean * 0.5);
        t.albedo[i * 4 + 2] = to_byte(b[i] / mean * 0.5);
        t.albedo[i * 4 + 3] = 255;
    }

    // Normal map from the height field (wrapped central differences).
    constexpr double strength = 3.5;
    t.normal.resize(n * 4);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            auto H = [&](int ix, int iy) {
                return height[static_cast<size_t>(((iy % size) + size) % size) * size + ((ix % size) + size) % size];
            };
            const double sx = (H(x + 1, y) - H(x - 1, y)) * 0.5 * strength;
            const double sy = (H(x, y + 1) - H(x, y - 1)) * 0.5 * strength;
            double nx = -sx, ny = -sy, nz = 1.0;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;
            const size_t i = static_cast<size_t>(y) * size + x;
            t.normal[i * 4] = to_byte(nx * 0.5 + 0.5);
            t.normal[i * 4 + 1] = to_byte(ny * 0.5 + 0.5);
            t.normal[i * 4 + 2] = to_byte(nz * 0.5 + 0.5);
            t.normal[i * 4 + 3] = 255;
        }
    }
    return t;
}

std::vector<uint8_t> serialize_ground(const GroundTextures& t) {
    std::vector<uint8_t> b;
    const char magic[8] = {'R', 'L', 'G', 'R', 'N', 'D', '0', '1'};
    b.insert(b.end(), magic, magic + 8);
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(static_cast<uint32_t>(t.size) >> (8 * i)));
    b.insert(b.end(), t.albedo.begin(), t.albedo.end());
    b.insert(b.end(), t.normal.begin(), t.normal.end());
    return b;
}

bool deserialize_ground(const std::vector<uint8_t>& bytes, GroundTextures& out, std::string* error) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RLGRND01", 8) != 0) {
        if (error) *error = "not a RLGRND01 file";
        return false;
    }
    uint32_t size = 0;
    for (int i = 0; i < 4; i++) size |= static_cast<uint32_t>(bytes[8 + i]) << (8 * i);
    const size_t n = static_cast<size_t>(size) * size * 4;
    if (size == 0 || size > 8192 || bytes.size() != 12 + n * 2) {
        if (error) *error = "bad ground texture size";
        return false;
    }
    out.size = static_cast<int>(size);
    out.albedo.assign(bytes.begin() + 12, bytes.begin() + 12 + static_cast<long>(n));
    out.normal.assign(bytes.begin() + 12 + static_cast<long>(n), bytes.end());
    return true;
}

}  // namespace worldcore
