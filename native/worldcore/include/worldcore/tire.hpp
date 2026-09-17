// tire.hpp - the contact patch. Port of src/physics/tire.js (browser reference,
// `git show a59773d:src/physics/tire.js` explains the curve shape and why slip
// is normalised).
#pragma once

#include <algorithm>
#include <cmath>

#include "worldcore/tune.hpp"

namespace worldcore {

struct TireResult {
    double fx = 0, fy = 0;
    double slipRatio = 0, slipAngle = 0;
    double combined = 0;
};

/// Normalised slip -> fraction of peak grip. `s` is already peak-relative.
inline double slip_curve(double s, double falloff) {
    constexpr double B = 1.853, C = 1.6, E = 0.5;
    const double bs = B * s;
    const double f = std::sin(C * std::atan(bs - E * (bs - std::atan(bs))));
    return s > 1 ? f / (1 + falloff * (s - 1)) : f;
}

/// Effective peak friction at one contact patch.
inline double peak_friction(const SurfaceInfo& surface, double wetness, double load, double nominal,
                            const Tune& tune) {
    const double wet = 1 - tune.wetnessGripLoss * wetness;
    const double loadDrop = 1 - tune.loadSensitivity * (load / std::max(1.0, nominal) - 1);
    return std::max(0.05, surface.friction * wet * tune.gripScale * std::max(0.55, loadDrop));
}

/// Combined-slip tire force.
inline TireResult tire_force(double load, double mu, double vLong, double vLat, double wheelSpeed,
                             const Tune& tune) {
    TireResult out;
    const double vRef = std::max(std::fabs(vLong), tune.slipRefSpeed);
    out.slipRatio = (wheelSpeed - vLong) / vRef;
    out.slipAngle = std::atan2(vLat, vRef);

    const double kn = out.slipRatio / tune.peakSlipRatio;
    const double an = out.slipAngle / tune.peakSlipAngle;
    const double s = std::hypot(kn, an);
    if (s < 1e-6 || load <= 0) return out;

    const double curve = slip_curve(s, tune.slipFalloff);
    const double grip = curve / s;
    const double cap = mu * load;
    out.fx = cap * grip * kn;
    out.fy = -cap * grip * an;
    out.combined = std::min(1.0, curve);
    return out;
}

/// Rolling resistance torque at the wheel, N m, always opposing rotation.
inline double rolling_resistance(const SurfaceInfo& surface, double wetness, double load, double radius,
                                 const Tune& tune) {
    const double c = surface.drag * (1 + 0.5 * wetness) * tune.rollingResistScale;
    return c * load * radius;
}

}  // namespace worldcore
