/**
 * atmosphere.js - the physics and the art direction behind the sky.
 *
 * Three jobs live here, together on purpose:
 *
 *  1. SCATTERING. A Preetham-style single-scattering model, written twice -
 *     once in GLSL for the dome, once in JS for the CPU. The duplication is
 *     deliberate: the world needs a fog colour that provably matches the sky
 *     it fades into, and the only way to guarantee that is to evaluate the
 *     same function on both sides. The constants are shared from this file so
 *     the two copies cannot drift apart silently.
 *
 *  2. EPHEMERIS. Sun and moon direction from a clock hour. A real hour-angle
 *     model rather than a circle, because the azimuth swing across the day is
 *     most of what makes shadows read as "morning" or "afternoon" instead of
 *     just "some time with a low sun".
 *
 *  3. GRADE. A keyframe table hung off SUN ALTITUDE, not off the clock.
 *     Altitude is the variable that actually controls how a sky looks, so
 *     keying off it keeps the grade correct if latitude, season or day length
 *     ever change.
 *
 * Units: metres, degrees only where named, radiance in the renderer's linear
 * working space (pre tone mapping).
 */
import * as THREE from 'three';

/* ------------------------------------------------------------ constants ---- */

// Rayleigh scattering at sea level for 680/550/450 nm. Blue scatters ~5x more
// than red, which is the whole reason the sky is blue and sunsets are not.
const TOTAL_RAYLEIGH = [5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5];
// pi * pow(2pi / lambda, v - 2) * K, precomputed for the same primaries.
const MIE_CONST = [1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14];

const RAYLEIGH_ZENITH = 8.4e3;   // optical depth straight up, in metres
const MIE_ZENITH = 1.25e3;

// Preetham's own cutoff kills the sun about 2 degrees below the horizon, which
// makes dusk snap off like a light switch. A wider cutoff and softer falloff
// buy a proper civil twilight that lingers to roughly -7 degrees.
const SUN_CUTOFF = 1.676;        // 96 degrees of zenith angle
const SUN_STEEPNESS = 2.0;
const SUN_EE = 1000.0;

const THREE_OVER_16PI = 0.05968310365946075;
const ONE_OVER_4PI = 0.07957747154594767;

/** Northern-hemisphere forest, late spring. Sun up ~05:07, down ~18:53. */
export const LATITUDE = 45;
export const DECLINATION = 13;

const DEG = Math.PI / 180;
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const lerp = (a, b, t) => a + (b - a) * t;
const smoothstep = (e0, e1, x) => {
  const t = clamp((x - e0) / (e1 - e0), 0, 1);
  return t * t * (3 - 2 * t);
};

/* ------------------------------------------------------------ ephemeris ---- */

/**
 * Direction TO the sun as a unit vector, from a 0..24 clock hour.
 *
 * Altitude/azimuth from the hour angle. Azimuth is measured clockwise from
 * north, which maps onto our axes as (sin A, alt, cos A) because +Z is north.
 *
 * @param {number} hours 0..24
 * @param {THREE.Vector3} out
 */
export function sunDirection(hours, out = new THREE.Vector3(), {
  latitude = LATITUDE, declination = DECLINATION, hourOffset = 0,
} = {}) {
  const H = ((hours - 12) * 15 + hourOffset) * DEG;
  const lat = latitude * DEG;
  const dec = declination * DEG;
  const sinAlt = clamp(Math.sin(lat) * Math.sin(dec)
    + Math.cos(lat) * Math.cos(dec) * Math.cos(H), -1, 1);
  const cosAlt = Math.sqrt(Math.max(0, 1 - sinAlt * sinAlt));
  const denom = Math.cos(lat) * cosAlt;
  // Guard the degenerate case where the azimuth is undefined (sun at zenith).
  let az = Math.acos(clamp(
    denom > 1e-6 ? (Math.sin(dec) - Math.sin(lat) * sinAlt) / denom : 0, -1, 1,
  ));
  if (Math.sin(H) > 0) az = Math.PI * 2 - az;      // afternoon: swing west
  return out.set(Math.sin(az) * cosAlt, sinAlt, Math.cos(az) * cosAlt);
}

/**
 * Moon direction. Not a real lunar ephemeris - it is the same model run out of
 * phase with the sun and given its own declination, which is all that "the
 * moon is up, and somewhere believable" needs.
 *
 * Phase drives the offset, so a full moon rises as the sun sets and a crescent
 * trails close behind it - the relationship a player would actually notice.
 */
export function moonDirection(hours, out = new THREE.Vector3(), phase = 0.72) {
  const offset = 180 * (0.5 + phase * 0.5);
  return sunDirection(hours, out, { declination: -8, hourOffset: offset });
}

/** Sun altitude in degrees - the single number the whole grade hangs off. */
export const altitudeOf = (dir) => Math.asin(clamp(dir.y, -1, 1)) / DEG;

/* ---------------------------------------------------------------- grade ---- */

/**
 * Colour keyframes by sun altitude in DEGREES.
 *
 *   sun/lux   directional key light colour and intensity
 *   sky/gnd   hemisphere light sky and ground colours
 *   amb       hemisphere intensity
 *   fog       FogExp2 density
 *   mist      how much low valley fog the mist layers show
 *   exp       tone-mapping exposure the renderer should use
 *   star      star visibility
 *   turb/ray/mie  scattering knobs: hazier and redder as the sun drops
 */
const GRADE = [
  { alt: -90, sun: 0x24304d, lux: 0.00, sky: 0x070b16, gnd: 0x04050a, amb: 0.055, fog: 0.0013, mist: 0.40, exp: 1.50, star: 1.00, turb: 2.0, ray: 2.4, mie: 0.004 },
  { alt: -12, sun: 0x2f3c60, lux: 0.00, sky: 0x0d1428, gnd: 0x06080f, amb: 0.075, fog: 0.0016, mist: 0.60, exp: 1.42, star: 0.96, turb: 2.1, ray: 2.5, mie: 0.005 },
  { alt: -6,  sun: 0x4d4f7a, lux: 0.05, sky: 0x1d2b4e, gnd: 0x0d1016, amb: 0.20, fog: 0.0021, mist: 0.85, exp: 1.26, star: 0.55, turb: 2.6, ray: 3.0, mie: 0.007 },
  { alt: -2,  sun: 0x9b6a63, lux: 0.30, sky: 0x3b4a75, gnd: 0x1c1a1c, amb: 0.42, fog: 0.0026, mist: 1.00, exp: 1.12, star: 0.14, turb: 3.4, ray: 3.2, mie: 0.010 },
  { alt: 1,   sun: 0xff7a3c, lux: 0.95, sky: 0x6e7fa6, gnd: 0x2e2a22, amb: 0.55, fog: 0.0029, mist: 1.00, exp: 1.04, star: 0.00, turb: 4.0, ray: 3.2, mie: 0.012 },
  { alt: 5,   sun: 0xffa055, lux: 1.75, sky: 0x92a6c8, gnd: 0x423a2c, amb: 0.68, fog: 0.0024, mist: 0.88, exp: 0.99, star: 0.00, turb: 3.4, ray: 3.0, mie: 0.009 },
  { alt: 12,  sun: 0xffc98e, lux: 2.45, sky: 0xa2bade, gnd: 0x4d4534, amb: 0.82, fog: 0.0015, mist: 0.45, exp: 0.95, star: 0.00, turb: 2.8, ray: 2.8, mie: 0.006 },
  { alt: 28,  sun: 0xffe8c6, lux: 3.05, sky: 0xb0cbee, gnd: 0x56503c, amb: 0.98, fog: 0.00095, mist: 0.15, exp: 0.91, star: 0.00, turb: 2.4, ray: 2.6, mie: 0.005 },
  { alt: 50,  sun: 0xfff6e8, lux: 3.45, sky: 0xbdd7f7, gnd: 0x5d5744, amb: 1.10, fog: 0.00072, mist: 0.04, exp: 0.88, star: 0.00, turb: 2.1, ray: 2.5, mie: 0.0045 },
  { alt: 90,  sun: 0xfffdf6, lux: 3.60, sky: 0xc6ddfa, gnd: 0x605a48, amb: 1.16, fog: 0.00065, mist: 0.00, exp: 0.87, star: 0.00, turb: 2.0, ray: 2.4, mie: 0.004 },
];

const NUMERIC_KEYS = ['lux', 'amb', 'fog', 'mist', 'exp', 'star', 'turb', 'ray', 'mie'];
const COLOR_KEYS = ['sun', 'sky', 'gnd'];

/** Reusable output so the per-frame grade lookup allocates nothing. */
function makeGradeOut() {
  const o = {};
  for (const k of NUMERIC_KEYS) o[k] = 0;
  for (const k of COLOR_KEYS) o[k] = new THREE.Color();
  return o;
}

const tmpA = new THREE.Color();
const tmpB = new THREE.Color();

/**
 * Interpolate the grade at a sun altitude.
 * Colours lerp in linear space so a warm key never darkens through the middle
 * of a transition the way an sRGB lerp would.
 */
export function gradeAt(altDeg, out = makeGradeOut()) {
  let i = 0;
  while (i < GRADE.length - 2 && GRADE[i + 1].alt < altDeg) i++;
  const a = GRADE[i];
  const b = GRADE[i + 1];
  const t = clamp((altDeg - a.alt) / (b.alt - a.alt), 0, 1);
  // Ease the blend so keyframe boundaries do not show up as a crease in the
  // light as the sun crawls past them.
  const s = t * t * (3 - 2 * t);
  for (const k of NUMERIC_KEYS) out[k] = lerp(a[k], b[k], s);
  for (const k of COLOR_KEYS) {
    tmpA.setHex(a[k], THREE.SRGBColorSpace);
    tmpB.setHex(b[k], THREE.SRGBColorSpace);
    out[k].copy(tmpA).lerp(tmpB, s);
  }
  return out;
}

gradeAt.create = makeGradeOut;

/**
 * Valley mist is a dawn phenomenon: it forms overnight as the ground radiates
 * heat away, then burns off within an hour or two of the sun hitting it. A
 * pure altitude curve would put the same mist on both ends of the day, so
 * weight it toward morning and leave only a thin haze at dusk.
 */
export function mistTimeBias(hours) {
  const morning = Math.exp(-(((hours - 5.5) / 2.6) ** 2));
  const evening = Math.exp(-(((hours - 20.0) / 2.2) ** 2)) * 0.45;
  const night = hours < 5.5 || hours > 21 ? 0.55 : 0.0;
  return clamp(Math.max(morning, evening, night), 0.06, 1.0);
}

/* ----------------------------------------------------------- scattering ---- */

function sunIntensity(cosZenith) {
  const c = clamp(cosZenith, -1, 1);
  return SUN_EE * Math.max(0, 1 - Math.exp(-((SUN_CUTOFF - Math.acos(c)) / SUN_STEEPNESS)));
}

const betaR = [0, 0, 0];
const betaM = [0, 0, 0];
const fex = [0, 0, 0];

/**
 * Sky radiance along a view direction, in linear working space, matching the
 * dome shader below. Used for fog colour and for tinting FX particles.
 *
 * @param {THREE.Vector3} dir      unit view direction
 * @param {THREE.Vector3} sun      unit direction TO the sun
 * @param {object} p               { turbidity, rayleigh, mie, mieG, night, moonUp }
 * @param {THREE.Color} out
 */
export function skyRadiance(dir, sun, p, out = new THREE.Color()) {
  const mieC = 0.2 * p.turbidity * 10e-18 * 0.434 * p.mie;
  for (let i = 0; i < 3; i++) {
    betaR[i] = TOTAL_RAYLEIGH[i] * p.rayleigh;
    betaM[i] = MIE_CONST[i] * mieC;
  }

  // Optical depth along the view ray. The 93.885 term is Preetham's fit for
  // the atmosphere thickening toward the horizon.
  const zen = Math.acos(Math.max(0, dir.y));
  const inv = 1 / (Math.cos(zen) + 0.15 * Math.pow(93.885 - zen / DEG, -1.253));
  const sR = RAYLEIGH_ZENITH * inv;
  const sM = MIE_ZENITH * inv;

  const sunE = sunIntensity(sun.y);
  const cosTheta = dir.x * sun.x + dir.y * sun.y + dir.z * sun.z;
  const rc = cosTheta * 0.5 + 0.5;
  const rPhase = THREE_OVER_16PI * (1 + rc * rc);
  const g2 = p.mieG * p.mieG;
  const mPhase = ONE_OVER_4PI * ((1 - g2)
    / Math.pow(Math.max(1e-4, 1 - 2 * p.mieG * cosTheta + g2), 1.5));
  const horizonBias = clamp(Math.pow(Math.max(0, 1 - sun.y), 5), 0, 1);

  const rgb = [0, 0, 0];
  for (let i = 0; i < 3; i++) {
    fex[i] = Math.exp(-(betaR[i] * sR + betaM[i] * sM));
    const ratio = (betaR[i] * rPhase + betaM[i] * mPhase) / (betaR[i] + betaM[i]);
    let lin = Math.pow(sunE * ratio * (1 - fex[i]), 1.5);
    lin *= lerp(1, Math.sqrt(Math.max(0, sunE * ratio * fex[i])), horizonBias);
    const l0 = 0.1 * fex[i];
    rgb[i] = (lin + l0) * 0.04;
  }
  rgb[1] += 0.0003;
  rgb[2] += 0.00075;

  // Night floor. Preetham goes to pure black below the horizon, which reads as
  // a hole in the world; real night sky is a dim desaturated blue lifted by
  // airglow and, when it is up, the moon.
  if (p.night > 0) {
    const horiz = Math.pow(1 - clamp(dir.y, 0, 1), 6);
    const moon = 0.55 + 0.45 * (p.moonUp ?? 0);
    rgb[0] += p.night * moon * lerp(0.0075, 0.016, horiz);
    rgb[1] += p.night * moon * lerp(0.0105, 0.019, horiz);
    rgb[2] += p.night * moon * lerp(0.0215, 0.028, horiz);
  }
  return out.setRGB(rgb[0], rgb[1], rgb[2], THREE.LinearSRGBColorSpace);
}

/**
 * GLSL twin of skyRadiance(). Shared by the dome and by anything else that
 * wants sky colour in a shader (the height-fog patch reuses the tail of it).
 *
 * Expects uniforms: uSunDir, uMoonDir, uTurbidity, uRayleigh, uMie, uMieG,
 * uNight, uMoonUp.
 */
// GLSL has no implicit int -> float conversion, and `${2.0}` stringifies to
// "2", so every constant interpolated below must be forced to carry a point.
const glslFloat = (v) => (Number.isInteger(v) ? v.toFixed(1) : String(v));

export const SKY_GLSL = /* glsl */`
const vec3 TOTAL_RAYLEIGH = vec3(5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5);
const vec3 MIE_CONST = vec3(1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14);
const float RAYLEIGH_ZENITH = 8.4e3;
const float MIE_ZENITH = 1.25e3;
const float SUN_CUTOFF = ${glslFloat(SUN_CUTOFF)};
const float SUN_STEEPNESS = ${glslFloat(SUN_STEEPNESS)};
const float SUN_EE = ${glslFloat(SUN_EE)};
const float THREE_OVER_16PI = ${glslFloat(THREE_OVER_16PI)};
const float ONE_OVER_4PI = ${glslFloat(ONE_OVER_4PI)};
const float PI_ = 3.14159265358979;

float sunIntensity(float cosZenith) {
  cosZenith = clamp(cosZenith, -1.0, 1.0);
  return SUN_EE * max(0.0, 1.0 - exp(-((SUN_CUTOFF - acos(cosZenith)) / SUN_STEEPNESS)));
}

struct SkyTerms { vec3 lin; vec3 fex; float sunE; };

SkyTerms skyTerms(vec3 dir, vec3 sunDir, float turbidity, float rayleigh, float mie, float mieG) {
  vec3 bR = TOTAL_RAYLEIGH * rayleigh;
  vec3 bM = MIE_CONST * (0.2 * turbidity * 10e-18 * 0.434 * mie);

  float zen = acos(max(0.0, dir.y));
  float inv = 1.0 / (cos(zen) + 0.15 * pow(93.885 - degrees(zen), -1.253));
  vec3 fex = exp(-(bR * RAYLEIGH_ZENITH * inv + bM * MIE_ZENITH * inv));

  float sunE = sunIntensity(sunDir.y);
  float cosTheta = dot(dir, sunDir);
  float rc = cosTheta * 0.5 + 0.5;
  float rPhase = THREE_OVER_16PI * (1.0 + rc * rc);
  float g2 = mieG * mieG;
  float mPhase = ONE_OVER_4PI * ((1.0 - g2) / pow(max(1e-4, 1.0 - 2.0 * mieG * cosTheta + g2), 1.5));

  vec3 ratio = (bR * rPhase + bM * mPhase) / (bR + bM);
  vec3 lin = pow(sunE * ratio * (1.0 - fex), vec3(1.5));
  lin *= mix(vec3(1.0), sqrt(max(vec3(0.0), sunE * ratio * fex)),
             clamp(pow(max(0.0, 1.0 - sunDir.y), 5.0), 0.0, 1.0));

  SkyTerms t;
  t.lin = lin; t.fex = fex; t.sunE = sunE;
  return t;
}

vec3 nightFloor(vec3 dir, float night, float moonUp) {
  float horiz = pow(1.0 - clamp(dir.y, 0.0, 1.0), 6.0);
  float moon = 0.55 + 0.45 * moonUp;
  return night * moon * mix(vec3(0.0075, 0.0105, 0.0215), vec3(0.016, 0.019, 0.028), horiz);
}
`;

/* ------------------------------------------------------------ utilities ---- */

export { clamp, lerp, smoothstep };
