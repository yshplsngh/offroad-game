/**
 * noise.js - the deterministic noise primitives the whole terrain is built from.
 *
 * Everything here seeds off contract.js `hash2`, which means the world is
 * reproducible: the same seed gives the same mountains on every machine, in
 * every session, in the editor and in the game. That matters more than it
 * sounds - scatter, wildlife and physics all re-derive positions from the
 * height field, so if the noise drifted, trees would float and wheels would
 * sink.
 *
 * These functions are the hot path. `height()` is called tens of thousands of
 * times a second, and a single height evaluation costs ~16 gradient-noise
 * lookups, so the code here is deliberately flat: no allocation, no objects,
 * no closures per call, table lookups instead of trig.
 */
import { hash2 } from '../contract.js';

/* ---------------------------------------------------------- gradients ---- */

// Eight unit directions. Normalised diagonals keep the noise amplitude even -
// unnormalised ones make diagonal cells visibly louder, which shows up as a
// 45 degree weave across an open hillside.
const D = Math.SQRT1_2;
const GX = new Float32Array([1, -1, 1, -1, D, -D, D, -D]);
const GZ = new Float32Array([D, D, -D, -D, 1, 1, -1, -1]);

/** Quintic fade - C2 continuous, so lighting normals derived by differencing stay smooth. */
const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10);

/**
 * Perlin-style gradient noise, roughly [-1, 1].
 *
 * Value noise was tried first (it is what procgen.js uses for textures) but at
 * terrain scale its axis-aligned lattice reads as a grid of square blobs from
 * the air. Gradient noise costs one extra multiply per corner and looks like
 * landscape.
 */
export function perlin2(x, z, seed) {
  const ix = Math.floor(x), iz = Math.floor(z);
  const fx = x - ix, fz = z - iz;

  let g = (hash2(ix, iz, seed) * 8) | 0;
  const n00 = GX[g] * fx + GZ[g] * fz;
  g = (hash2(ix + 1, iz, seed) * 8) | 0;
  const n10 = GX[g] * (fx - 1) + GZ[g] * fz;
  g = (hash2(ix, iz + 1, seed) * 8) | 0;
  const n01 = GX[g] * fx + GZ[g] * (fz - 1);
  g = (hash2(ix + 1, iz + 1, seed) * 8) | 0;
  const n11 = GX[g] * (fx - 1) + GZ[g] * (fz - 1);

  const u = fade(fx), v = fade(fz);
  const a = n00 + (n10 - n00) * u;
  const b = n01 + (n11 - n01) * u;
  return (a + (b - a) * v) * 1.4;              // 1.4 pushes the practical range to ~[-1,1]
}

/** Fractal brownian motion, ~[-1, 1]. Rolling, soft, no preferred direction. */
export function fbm(x, z, seed, octaves = 4, lacunarity = 2.0, gain = 0.5) {
  let sum = 0, amp = 1, freq = 1, norm = 0;
  for (let i = 0; i < octaves; i++) {
    sum += perlin2(x * freq, z * freq, seed + i * 1013) * amp;
    norm += amp;
    amp *= gain;
    freq *= lacunarity;
  }
  return sum / norm;
}

/**
 * Ridged multifractal, [0, 1]. This is what makes mountains look like
 * mountains instead of dunes.
 *
 * Two things do the work: folding the noise through `1 - |n|` turns smooth
 * humps into sharp crests, and weighting each octave by the previous one
 * (`prev`) concentrates the fine detail on the ridges and leaves the valleys
 * smooth - which is also why the flanks stay drivable instead of turning into
 * a field of spikes.
 */
export function ridged(x, z, seed, octaves = 5, lacunarity = 2.04, gain = 0.46) {
  let sum = 0, amp = 1, freq = 1, norm = 0, prev = 1;
  for (let i = 0; i < octaves; i++) {
    let n = 1 - Math.abs(perlin2(x * freq, z * freq, seed + i * 7919));
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

/* ------------------------------------------------------------- shaping ---- */

export function clamp01(t) { return t < 0 ? 0 : t > 1 ? 1 : t; }

export function smoothstep(edge0, edge1, x) {
  const t = clamp01((x - edge0) / (edge1 - edge0));
  return t * t * (3 - 2 * t);
}

export function lerp(a, b, t) { return a + (b - a) * t; }
