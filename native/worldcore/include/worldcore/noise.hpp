// noise.hpp - gradient noise, fBm and ridged multifractal.
//
// Port of src/world/terrain/noise.js (removed browser reference, git a59773d).
// The JS gradient tables were Float32Array, so they are float here too;
// arithmetic stays double like JS numbers.
#pragma once

#include <cmath>
#include <cstdint>

#include "worldcore/hash.hpp"

namespace worldcore {

namespace detail {
inline constexpr float kD = 0.70710678118654752f;  // Math.SQRT1_2 stored as float32
inline constexpr float GX[8] = {1, -1, 1, -1, kD, -kD, kD, -kD};
inline constexpr float GZ[8] = {kD, kD, -kD, -kD, 1, 1, -1, -1};

inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }

inline int grad(int32_t ix, int32_t iz, int32_t seed) {
    return static_cast<int>(hash2(ix, iz, seed) * 8);
}
}  // namespace detail

/// Perlin-style gradient noise, roughly [-1, 1].
inline double perlin2(double x, double z, int32_t seed) {
    using namespace detail;
    const double fx0 = std::floor(x), fz0 = std::floor(z);
    const int32_t ix = to_int32(fx0), iz = to_int32(fz0);
    const double fx = x - fx0, fz = z - fz0;

    int g = grad(ix, iz, seed);
    const double n00 = GX[g] * fx + GZ[g] * fz;
    g = grad(ix + 1, iz, seed);
    const double n10 = GX[g] * (fx - 1) + GZ[g] * fz;
    g = grad(ix, iz + 1, seed);
    const double n01 = GX[g] * fx + GZ[g] * (fz - 1);
    g = grad(ix + 1, iz + 1, seed);
    const double n11 = GX[g] * (fx - 1) + GZ[g] * (fz - 1);

    const double u = fade(fx), v = fade(fz);
    const double a = n00 + (n10 - n00) * u;
    const double b = n01 + (n11 - n01) * u;
    return (a + (b - a) * v) * 1.4;
}

/// Fractal brownian motion, ~[-1, 1].
inline double fbm(double x, double z, int32_t seed, int octaves = 4,
                  double lacunarity = 2.0, double gain = 0.5) {
    double sum = 0, amp = 1, freq = 1, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += perlin2(x * freq, z * freq, seed + i * 1013) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

/// Ridged multifractal, [0, 1].
inline double ridged(double x, double z, int32_t seed, int octaves = 5,
                     double lacunarity = 2.04, double gain = 0.46) {
    double sum = 0, amp = 1, freq = 1, norm = 0, prev = 1;
    for (int i = 0; i < octaves; i++) {
        double n = 1 - std::fabs(perlin2(x * freq, z * freq, seed + i * 7919));
        n *= n;
        n *= prev;
        prev = n * 1.7 > 1 ? 1 : n * 1.7;
        sum += n * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return sum / norm;
}

inline double clamp01(double t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

inline double smoothstep(double edge0, double edge1, double x) {
    const double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3 - 2 * t);
}

inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

}  // namespace worldcore
