// hash.hpp - the deterministic seeds every world module derives from.
//
// Port of hash2 / mulberry32 in src/world/contract.js (browser reference,
// git a59773d). These must stay bit-identical to it: scatter placement, terrain noise and spawn
// all re-derive from them, so any drift moves trees and mountains.
// JS Math.imul and `>>>` are wrapping 32-bit operations, which is exactly
// uint32_t arithmetic.
#pragma once

#include <cmath>
#include <cstdint>

namespace worldcore {

/// JS `v | 0` (ToInt32) for a double that is already integral and finite.
inline int32_t to_int32(double v) {
    const double m = std::fmod(std::trunc(v), 4294967296.0);
    const int64_t i = static_cast<int64_t>(m < 0 ? m + 4294967296.0 : m);
    return static_cast<int32_t>(static_cast<uint32_t>(i));
}

/// JS Math.round: halves round toward +infinity, unlike std::round.
inline double js_round(double v) { return std::floor(v + 0.5); }

/// Deterministic hash -> [0, 1).
inline double hash2(int32_t x, int32_t y, int32_t seed = 0) {
    uint32_t h = (static_cast<uint32_t>(x) * 374761393u)
        ^ (static_cast<uint32_t>(y) * 668265263u)
        ^ (static_cast<uint32_t>(seed) * 2246822519u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<double>(h ^ (h >> 16)) / 4294967296.0;
}

/// Seeded PRNG for per-cell generation. Same sequence as contract.js mulberry32.
class Mulberry32 {
public:
    explicit Mulberry32(uint32_t seed) : a_(seed) {}

    double operator()() {
        a_ += 0x6D2B79F5u;
        uint32_t t = (a_ ^ (a_ >> 15)) * (1u | a_);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
    }

private:
    uint32_t a_;
};

}  // namespace worldcore
