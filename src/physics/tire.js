/**
 * tire.js - the contact patch. One saturating curve, used twice.
 *
 * WHY A MAGIC-FORMULA SHAPE AT ALL: a linear tire (F = C * slip, clamped) has
 * no memory of having let go. It grips right up to the clamp and then holds the
 * clamp forever, so wheelspin costs nothing and a slide never gets worse. A
 * Pacejka-ish curve peaks and then DECAYS, which is the whole reason a rock
 * crawler is a game: once a tire breaks loose it makes less force than it did a
 * moment ago, and the only way back is to lift, not to push harder.
 *
 * WHY NORMALISED SLIP: the classic B/C/D/E coefficients are per-tire-per-load
 * lab fits we have no data for. Instead the curve is normalised so it peaks at
 * slip = 1, and the caller divides real slip by the peak-slip knobs in TUNE.
 * Peak grip itself is not a tire constant here - it comes from
 * SURFACE[..].friction at the contact point, which is exactly where PLAN.md
 * says the driving feel is supposed to live.
 *
 * Combined slip is the friction-circle construction: build one normalised slip
 * vector from (longitudinal, lateral), evaluate the curve once on its
 * magnitude, and hand each axis its share. A tire that is spinning cannot also
 * steer - that falls out of this for free, and it is what makes a mud pit feel
 * like a mud pit.
 */

/* Shape coefficients, solved so the curve peaks at exactly 1.0 when slip = 1.
 * Past the peak it decays to ~0.88 at 3x peak slip and ~0.77 at 6x, which is
 * the grip you throw away by lighting up a tire. */
const B = 1.853;
const C = 1.6;
const E = 0.5;

/** Normalised slip -> fraction of peak grip. `s` is already peak-relative. */
export function slipCurve(s, falloff) {
  const bs = B * s;
  const f = Math.sin(C * Math.atan(bs - E * (bs - Math.atan(bs))));
  // A little extra tail decay: mud tires keep polishing the hole they dug.
  return s > 1 ? f / (1 + falloff * (s - 1)) : f;
}

/**
 * Effective peak friction at one contact patch.
 *
 * Load sensitivity is small but it matters: it is why lifting a wheel over a
 * ledge costs the axle more grip than the other three gain, and why a heavily
 * loaded outside tire in a turn cannot make up for an unloaded inside one.
 *
 * @param {object} surface  A row from SURFACE.
 * @param {number} wetness  0..1 from the ground sample.
 * @param {number} load     Normal load, N.
 * @param {number} nominal  Static per-wheel load, N.
 * @param {object} tune
 */
export function peakFriction(surface, wetness, load, nominal, tune) {
  const wet = 1 - tune.wetnessGripLoss * wetness;
  const loadDrop = 1 - tune.loadSensitivity * (load / Math.max(1, nominal) - 1);
  return Math.max(0.05, surface.friction * wet * tune.gripScale * Math.max(0.55, loadDrop));
}

/**
 * Combined-slip tire force.
 *
 * @param {object} out         {fx, fy, slipRatio, slipAngle, combined}
 * @param {number} load        Normal load, N (already >= 0).
 * @param {number} mu          Peak friction from peakFriction().
 * @param {number} vLong       Contact-patch speed along the wheel heading, m/s.
 * @param {number} vLat        Contact-patch speed across the wheel, m/s.
 * @param {number} wheelSpeed  omega * radius, m/s.
 * @param {object} tune
 */
export function tireForce(out, load, mu, vLong, vLat, wheelSpeed, tune) {
  // A single reference speed keeps slip finite at a crawl. Below it, slip is
  // measured against the reference instead of against a vanishing denominator,
  // which is the difference between a stable 0.3 m/s ledge climb and a solver
  // that detonates the first time the truck stops.
  const vRef = Math.max(Math.abs(vLong), tune.slipRefSpeed);

  const slipRatio = (wheelSpeed - vLong) / vRef;
  const slipAngle = Math.atan2(vLat, vRef);

  const kn = slipRatio / tune.peakSlipRatio;
  const an = slipAngle / tune.peakSlipAngle;
  const s = Math.hypot(kn, an);

  if (s < 1e-6 || load <= 0) {
    out.fx = 0; out.fy = 0;
    out.slipRatio = slipRatio; out.slipAngle = slipAngle; out.combined = 0;
    return out;
  }

  const grip = slipCurve(s, tune.slipFalloff) / s;   // per unit of normalised slip
  const cap = mu * load;
  out.fx = cap * grip * kn;
  out.fy = -cap * grip * an;
  out.slipRatio = slipRatio;
  out.slipAngle = slipAngle;
  out.combined = Math.min(1, slipCurve(s, tune.slipFalloff));
  return out;
}

/**
 * Rolling resistance torque at the wheel, N m, always opposing rotation.
 * SURFACE.drag is the coefficient; wetness thickens whatever it is.
 */
export function rollingResistance(surface, wetness, load, radius, tune) {
  const c = surface.drag * (1 + 0.5 * wetness) * tune.rollingResistScale;
  return c * load * radius;
}
