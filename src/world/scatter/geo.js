/**
 * geo.js - the shape vocabulary the forest is built from.
 *
 * Every species is assembled from a handful of primitives and then baked down
 * to ONE geometry carrying per-vertex colour and a per-vertex wind weight.
 * Baking is not an optimisation detail, it is the whole design: an
 * InstancedMesh draws a single geometry with a single material, so a tree that
 * wants bark, needles and moss has to carry those tones in its vertices or pay
 * a draw call per material.
 *
 * `aFlex` is the wind weight - 0 at a root that never moves, 1 at a needle
 * tip. materials.js reads it in the vertex shader, so nothing here costs
 * anything per frame.
 *
 * Units are metres. A species is modelled with its base at the origin and
 * growing along +Y, so one instance matrix (position, yaw, lean, scale) drives
 * every LOD of that species without re-deriving anything.
 */
import * as THREE from 'three';
import { mergeParts } from '../../vehicles/parts.js';

/* ----------------------------------------------------------------- noise ---- */

/** Deterministic 3D hash -> [0,1). Same input, same forest, every session. */
export function hash3(x, y, z) {
  let h = Math.imul(x | 0, 374761393) ^ Math.imul(y | 0, 668265263) ^ Math.imul(z | 0, 2147483647);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

const smooth = (t) => t * t * (3 - 2 * t);
const mix = (a, b, t) => a + (b - a) * t;

/** Trilinear value noise. Cheap and smooth enough for surface warping. */
export function vnoise3(x, y, z) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const u = smooth(x - xi), v = smooth(y - yi), w = smooth(z - zi);
  const c = (dx, dy, dz) => hash3(xi + dx, yi + dy, zi + dz);
  return mix(
    mix(mix(c(0, 0, 0), c(1, 0, 0), u), mix(c(0, 1, 0), c(1, 1, 0), u), v),
    mix(mix(c(0, 0, 1), c(1, 0, 1), u), mix(c(0, 1, 1), c(1, 1, 1), u), v),
    w,
  );
}

export function fbm3(x, y, z, octaves = 3) {
  let sum = 0, amp = 1, f = 1, norm = 0;
  for (let i = 0; i < octaves; i++) {
    sum += vnoise3(x * f, y * f, z * f) * amp;
    norm += amp; amp *= 0.5; f *= 2.03;
  }
  return sum / norm;
}

/* ------------------------------------------------------------ deformers ---- */

/**
 * Move every vertex through `fn`. Used for boulders and leaf blobs: because
 * the displacement is a pure function of position, duplicated vertices on a
 * non-indexed geometry move together and the surface never cracks open.
 */
export function deform(geometry, fn) {
  const p = geometry.attributes.position.array;
  const out = [0, 0, 0];
  for (let i = 0; i < p.length; i += 3) {
    fn(p[i], p[i + 1], p[i + 2], out);
    p[i] = out[0]; p[i + 1] = out[1]; p[i + 2] = out[2];
  }
  geometry.attributes.position.needsUpdate = true;
  geometry.computeVertexNormals();
  return geometry;
}

/**
 * Parabolic lean, applied to the geometry rather than the instance matrix.
 * A trunk that curves reads as grown; a trunk that is merely tilted reads as
 * placed. Instances still get an extra whole-tree tilt on top of this.
 */
export function bend(geometry, kx, kz, height) {
  const p = geometry.attributes.position.array;
  const inv = 1 / Math.max(0.001, height);
  for (let i = 0; i < p.length; i += 3) {
    const t = Math.max(0, p[i + 1] * inv);
    const s = t * t * height;
    p[i] += kx * s;
    p[i + 2] += kz * s;
  }
  geometry.attributes.position.needsUpdate = true;
  return geometry;
}

/* ------------------------------------------------------------- assembly ---- */

/**
 * Concatenate baked groups. Everything reaching here is already non-indexed
 * with the same attribute set, so this is a straight memcpy - no merge helper
 * can mix up indexed and non-indexed inputs at this point.
 */
function joinGeometries(list) {
  const kept = list.filter(Boolean);
  if (kept.length === 0) return null;
  if (kept.length === 1) return kept[0];

  let total = 0;
  for (const g of kept) total += g.attributes.position.count;
  const P = new Float32Array(total * 3);
  const N = new Float32Array(total * 3);
  const U = new Float32Array(total * 2);
  const C = new Float32Array(total * 3);
  const F = new Float32Array(total);

  let v = 0;
  for (const g of kept) {
    const n = g.attributes.position.count;
    P.set(g.attributes.position.array, v * 3);
    N.set(g.attributes.normal.array, v * 3);
    U.set(g.attributes.uv.array.subarray(0, n * 2), v * 2);
    C.set(g.attributes.color.array, v * 3);
    F.set(g.attributes.aFlex.array, v);
    v += n;
    g.dispose();
  }
  const out = new THREE.BufferGeometry();
  out.setAttribute('position', new THREE.BufferAttribute(P, 3));
  out.setAttribute('normal', new THREE.BufferAttribute(N, 3));
  out.setAttribute('uv', new THREE.BufferAttribute(U, 2));
  out.setAttribute('color', new THREE.BufferAttribute(C, 3));
  out.setAttribute('aFlex', new THREE.BufferAttribute(F, 1));
  return out;
}

/**
 * Bake groups of primitives into one geometry.
 *
 * A group is `{ geoms, color, flex }` where colour is a THREE.Color or
 * `(color, x, y, z, nx, ny, nz) => void`, and flex is a number or
 * `(x, y, z) => number`. Grouping exists so bark and needles can be coloured
 * by different rules while still ending up in a single buffer.
 *
 * mergeParts() does the primitive merging because three's mergeGeometries
 * returns null the moment a batch mixes indexed and non-indexed inputs, and
 * these batches always do.
 */
export function assemble(groups) {
  const baked = [];
  const tmp = new THREE.Color();

  for (const g of groups) {
    const geoms = (g.geoms || []).filter(Boolean);
    if (geoms.length === 0) continue;
    const merged = mergeParts(geoms);
    if (!merged) continue;

    const pos = merged.attributes.position.array;
    const nrm = merged.attributes.normal.array;
    const count = merged.attributes.position.count;
    const col = new Float32Array(count * 3);
    const flx = new Float32Array(count);
    const colFn = typeof g.color === 'function' ? g.color : null;
    const flexFn = typeof g.flex === 'function' ? g.flex : null;
    if (!colFn) tmp.copy(g.color);

    for (let i = 0; i < count; i++) {
      const x = pos[i * 3], y = pos[i * 3 + 1], z = pos[i * 3 + 2];
      if (colFn) colFn(tmp, x, y, z, nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
      col[i * 3] = tmp.r; col[i * 3 + 1] = tmp.g; col[i * 3 + 2] = tmp.b;
      flx[i] = flexFn ? flexFn(x, y, z) : (g.flex || 0);
    }
    merged.setAttribute('color', new THREE.BufferAttribute(col, 3));
    merged.setAttribute('aFlex', new THREE.BufferAttribute(flx, 1));
    baked.push(merged);
  }
  return joinGeometries(baked);
}

/* ------------------------------------------------------------ primitives --- */

/**
 * A whorl of needle branches: the single most important shape in a conifer.
 *
 * Each spoke is a three-rib strip folded along its spine, so it never
 * disappears when seen edge-on the way a flat card does, and the fold catches
 * a highlight that reads as needle mass. Spokes get jittered length and angle
 * because a perfectly radial whorl reads as an umbrella.
 */
export function needleWhorl({
  radius, spokes = 8, droop = 0.45, rng, width = 0.19, jitter = 0.4, lift = 0.07,
}) {
  const ribs = 3;                       // hub, mid, tip
  const fr = [0.08, 0.58, 1.0];
  const verts = new Float32Array(spokes * ribs * 3 * 3);
  const idx = [];
  let vp = 0;

  for (let s = 0; s < spokes; s++) {
    const a = (s / spokes) * Math.PI * 2 + (rng() - 0.5) * (Math.PI * 2 / spokes) * jitter;
    const len = radius * (1 - jitter * 0.45 + rng() * jitter * 0.9);
    const ca = Math.cos(a), sa = Math.sin(a);
    const base = vp / 3;

    for (let r = 0; r < ribs; r++) {
      const t = fr[r];
      const rad = len * t;
      const y = -droop * len * t * t + lift * radius * (1 - t);
      const hw = width * len * (1 - t * 0.92);
      const up = lift * len * 0.55 * (1 - t);
      // left rib, raised spine, right rib
      verts[vp++] = ca * rad - sa * hw; verts[vp++] = y; verts[vp++] = sa * rad + ca * hw;
      verts[vp++] = ca * rad;           verts[vp++] = y + up; verts[vp++] = sa * rad;
      verts[vp++] = ca * rad + sa * hw; verts[vp++] = y; verts[vp++] = sa * rad - ca * hw;
    }
    for (let r = 0; r < ribs - 1; r++) {
      const a0 = base + r * 3, b0 = base + (r + 1) * 3;
      idx.push(a0, b0, a0 + 1, a0 + 1, b0, b0 + 1);
      idx.push(a0 + 1, b0 + 1, a0 + 2, a0 + 2, b0 + 1, b0 + 2);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(verts, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

/**
 * Tapered strip along a polyline - grass blades, fern stems, leaflets.
 * `side` is the width axis; blades keep a fixed one so they stay planar and
 * bend in a single plane the way a real blade does.
 */
export function ribbon(points, halfWidths, side = [1, 0, 0]) {
  const n = points.length;
  const verts = new Float32Array(n * 2 * 3);
  const idx = [];
  for (let i = 0; i < n; i++) {
    const [x, y, z] = points[i];
    const hw = halfWidths[i];
    verts[i * 6 + 0] = x + side[0] * hw;
    verts[i * 6 + 1] = y + side[1] * hw;
    verts[i * 6 + 2] = z + side[2] * hw;
    verts[i * 6 + 3] = x - side[0] * hw;
    verts[i * 6 + 4] = y - side[1] * hw;
    verts[i * 6 + 5] = z - side[2] * hw;
  }
  for (let i = 0; i < n - 1; i++) {
    const a = i * 2;
    idx.push(a, a + 2, a + 1, a + 1, a + 2, a + 3);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(verts, 3));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

/** A single triangular leaflet, used in pairs along a fern frond. */
export function leaflet(ax, ay, az, bx, by, bz, cx, cy, cz) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(
    new Float32Array([ax, ay, az, bx, by, bz, cx, cy, cz]), 3));
  g.computeVertexNormals();
  return g;
}

/**
 * Lumpy sphere. Boulders, leaf clusters and shrub masses are all this shape
 * with different noise settings - which is why nothing in the forest repeats:
 * every call warps from a different noise offset.
 */
export function blob(radius, detail, rng, {
  warp = 0.34, freq = 1.6, squash = [1, 1, 1], flatten = 0,
} = {}) {
  const g = new THREE.IcosahedronGeometry(radius, detail);
  const ox = rng() * 64, oy = rng() * 64, oz = rng() * 64;
  const f = freq / Math.max(0.05, radius);
  deform(g, (x, y, z, out) => {
    const n = fbm3(x * f + ox, y * f + oy, z * f + oz, 2) - 0.5;
    const k = 1 + n * warp * 2;
    out[0] = x * k * squash[0];
    out[1] = y * k * squash[1];
    out[2] = z * k * squash[2];
    if (flatten > 0 && out[1] < -radius * flatten) out[1] = -radius * flatten;
  });
  return g;
}

/** Triangle count of a finished geometry - the pools report with this. */
export function triCount(geometry) {
  if (!geometry) return 0;
  return Math.round(geometry.index
    ? geometry.index.count / 3
    : geometry.attributes.position.count / 3);
}
