/**
 * flora.js - the species of the forest, built from geo.js primitives.
 *
 * Every builder returns ONE baked geometry with `color` and `aFlex` per vertex,
 * ready to hand to an InstancedMesh. That constraint shapes everything here:
 * a tree cannot have a bark material and a needle material, so bark and needles
 * are separate `assemble()` groups that end up in the same buffer.
 *
 * LOD IS A BUILD PARAMETER, NOT A RUNTIME SWITCH. A pine at detail 2 is ~250
 * triangles, at 1 it is ~100, at 0 it is a dozen crossed cards. The provider
 * keeps one instanced pool per (species, detail) and decides which pool a tree
 * joins from its chunk's ring. Nothing re-tessellates while you drive.
 *
 * Budget, per tree, is the whole design pressure: a believable pine forest is
 * ~1 stem per 250 m^2, and a 128 m chunk is 16,384 m^2. Multiply by the
 * resident ring and a 1,000-triangle tree is a million triangles before a
 * single rock exists.
 *
 * Everything is modelled with its base at the origin growing along +Y, so one
 * instance matrix (position, yaw, tilt, scale) drives every LOD.
 */
import * as THREE from 'three';
import { assemble, needleWhorl, ribbon, blob, bend, deform, fbm3 } from './geo.js';

const C = (hex) => new THREE.Color().setHex(hex, THREE.SRGBColorSpace);

/* Palettes. Kept here rather than in the builders so a whole forest can be
 * re-tinted for a season without touching geometry. */
const BARK_PINE = C(0x4a3526);
const BARK_PINE_LIT = C(0x6b4f39);
const NEEDLE = C(0x2d4426);
const NEEDLE_TIP = C(0x4a6b35);
const BARK_BIRCH = C(0xcfc9ba);
const BARK_BIRCH_DARK = C(0x3b3a36);
const LEAF = C(0x5c7a32);
const LEAF_PALE = C(0x87a047);
const DEAD = C(0x6a5c4a);
const STONE = C(0x6e6a64);
const FERN = C(0x466b2e);
const GRASS = C(0x5f7434);
const GRASS_DRY = C(0x8a8443);

/** Tapered stem. Six sides is enough - a trunk is read by its silhouette. */
function stem(bottomR, topR, height, sides = 6, offsetY = 0) {
  const g = new THREE.CylinderGeometry(topR, bottomR, height, sides, 1, true);
  g.translate(0, height / 2 + offsetY, 0);
  return g;
}

/* ------------------------------------------------------------------ pine -- */

/**
 * Conifer. Whorls of needle spokes down a tapered stem, widest about a third
 * of the way up - not at the very bottom, because the lower limbs of a forest
 * pine die off in the shade and that missing skirt is most of what makes a
 * stand read as a forest rather than a Christmas tree farm.
 */
export function buildPine(rng, detail = 2, opts = {}) {
  const height = opts.height ?? (9 + rng() * 11);
  const trunkR = height * (0.016 + rng() * 0.006);
  const crownBase = height * (0.20 + rng() * 0.14);
  const spread = height * (0.15 + rng() * 0.05);

  const bark = [];
  const foliage = [];

  if (detail === 0) {
    // Far LOD: three crossed cards. Twelve triangles that read as a conifer
    // at 400 m, which is all they have to do.
    for (let i = 0; i < 3; i++) {
      const a = (i / 3) * Math.PI;
      const w = spread * 1.5;
      const g = new THREE.PlaneGeometry(w * 2, height, 1, 2);
      g.translate(0, height / 2, 0);
      g.rotateY(a);
      foliage.push(g);
    }
    return assemble([{
      geoms: foliage,
      color: (c, x, y) => {
        c.copy(NEEDLE).lerp(NEEDLE_TIP, Math.min(1, y / height));
        c.multiplyScalar(0.8 + 0.2 * (1 - Math.abs(x) / spread));
      },
      flex: (x, y) => Math.pow(Math.max(0, y / height), 2) * 0.7,
    }]);
  }

  bark.push(stem(trunkR * 1.5, trunkR * 0.28, height, detail > 1 ? 6 : 5));

  // Whorl count buys silhouette; spoke WIDTH buys mass. Widening the spokes is
  // free, so the mid LOD gets fat needles rather than more of them - five thin
  // whorls on a pole reads as a dead stick, which is the wrong tree entirely.
  const whorls = detail > 1 ? 11 : 8;
  const spokes = detail > 1 ? 8 : 6;
  const needleWidth = detail > 1 ? 0.30 : 0.42;
  for (let i = 0; i < whorls; i++) {
    const t = i / (whorls - 1);
    const y = crownBase + (height - crownBase) * t;
    // Widest a third up, tapering to a spire.
    const shape = Math.sin(Math.min(1, (1 - t) * 1.35) * Math.PI * 0.5);
    const r = spread * shape * (0.82 + rng() * 0.36);
    if (r < 0.15) continue;
    const w = needleWhorl({
      radius: r,
      spokes: spokes + (t < 0.4 ? 2 : 0),
      droop: 0.34 + rng() * 0.22,
      rng,
      width: needleWidth,
      jitter: 0.5,
    });
    w.translate((rng() - 0.5) * r * 0.1, y, (rng() - 0.5) * r * 0.1);
    w.rotateY(rng() * Math.PI * 2);
    foliage.push(w);
  }

  // A grown lean, baked into the geometry. Instance tilt alone reads as a
  // tree someone leaned against a wall; a curve reads as a tree that grew.
  const lean = (rng() - 0.5) * 0.05;
  const leanZ = (rng() - 0.5) * 0.05;

  const geo = assemble([
    {
      geoms: bark,
      color: (c, x, y, z, nx) => {
        c.copy(BARK_PINE).lerp(BARK_PINE_LIT, Math.abs(nx) * 0.5 + (y / height) * 0.3);
      },
      flex: (x, y) => Math.pow(Math.max(0, y / height), 2.4) * 0.5,
    },
    {
      geoms: foliage,
      color: (c, x, y) => {
        const t = Math.min(1, Math.max(0, (y - crownBase) / (height - crownBase)));
        c.copy(NEEDLE).lerp(NEEDLE_TIP, t * 0.75 + Math.min(0.25, Math.hypot(x, 0) * 0.1));
      },
      flex: (x, y) => Math.pow(Math.max(0, y / height), 1.7) * 0.85 + 0.15,
    },
  ]);
  return bend(geo, lean, leanZ, height);
}

/* ----------------------------------------------------------------- birch -- */

/** Broadleaf. Pale bark with dark scars, a few limbs, leaf mass as blobs. */
export function buildBirch(rng, detail = 2, opts = {}) {
  const height = opts.height ?? (7 + rng() * 7);
  const trunkR = height * 0.013;
  const forkY = height * (0.42 + rng() * 0.12);

  if (detail === 0) {
    const foliage = [];
    for (let i = 0; i < 2; i++) {
      const g = new THREE.PlaneGeometry(height * 0.62, height * 0.75, 1, 1);
      g.translate(0, height * 0.66, 0);
      g.rotateY((i / 2) * Math.PI + rng());
      foliage.push(g);
    }
    const trunk = stem(trunkR * 1.4, trunkR, forkY, 3);
    return assemble([
      { geoms: [trunk], color: BARK_BIRCH, flex: 0 },
      { geoms: foliage, color: LEAF, flex: 0.7 },
    ]);
  }

  const bark = [stem(trunkR * 1.6, trunkR * 0.6, height * 0.92, detail > 1 ? 6 : 4)];
  const leaves = [];

  const limbs = detail > 1 ? 7 : 5;
  for (let i = 0; i < limbs; i++) {
    const t = i / limbs;
    const y = forkY + (height * 0.88 - forkY) * t;
    const a = (i / limbs) * Math.PI * 2 + rng() * 0.8;
    const len = height * (0.34 - t * 0.16) * (0.75 + rng() * 0.5);
    const limb = stem(trunkR * 0.45, trunkR * 0.16, len, 3);
    limb.rotateZ(Math.PI / 2 - (0.5 + rng() * 0.5));
    limb.rotateY(a);
    limb.translate(0, y, 0);
    bark.push(limb);

    const cluster = blob(len * 0.62, detail > 1 ? 1 : 0, rng, { warp: 0.42, freq: 2.0, squash: [1, 0.72, 1] });
    const lift = Math.cos(0.5 + rng() * 0.3) * len * 0.62;
    cluster.translate(Math.cos(a) * len * 0.62, y + lift * 0.4, Math.sin(a) * len * 0.62);
    leaves.push(cluster);
  }
  // Crown cap, so the canopy closes instead of showing sky through the middle.
  const crown = blob(height * 0.26, detail > 1 ? 1 : 0, rng, { warp: 0.4, freq: 1.8, squash: [1, 0.78, 1] });
  crown.translate(0, height * 0.82, 0);
  leaves.push(crown);

  const geo = assemble([
    {
      geoms: bark,
      color: (c, x, y, z) => {
        // Birch scars: horizontal dashes, so the noise is stretched in Y.
        const n = fbm3(x * 6 + 11, y * 26, z * 6 + 3, 2);
        c.copy(n > 0.54 ? BARK_BIRCH_DARK : BARK_BIRCH);
        c.multiplyScalar(0.78 + n * 0.34);
      },
      flex: (x, y) => Math.pow(Math.max(0, y / height), 2.2) * 0.55,
    },
    {
      geoms: leaves,
      color: (c, x, y, z, nx, ny) => {
        c.copy(LEAF).lerp(LEAF_PALE, Math.max(0, ny) * 0.55 + fbm3(x * 3, y * 3, z * 3, 2) * 0.35);
      },
      flex: (x, y) => Math.pow(Math.max(0, y / height), 1.5) * 0.9 + 0.1,
    },
  ]);
  return bend(geo, (rng() - 0.5) * 0.08, (rng() - 0.5) * 0.08, height);
}

/* -------------------------------------------------------------- deadfall -- */

/** A fallen trunk. Half the character of an old forest is what died in it. */
export function buildDeadfall(rng, detail = 2) {
  const len = 3.5 + rng() * 5;
  const r = 0.16 + rng() * 0.16;
  const log = new THREE.CylinderGeometry(r * 0.72, r, len, detail > 1 ? 7 : 5, 1);
  log.rotateZ(Math.PI / 2);
  log.translate(0, r * 0.85, 0);

  const parts = [log];
  if (detail > 1) {
    const stubs = 2 + Math.floor(rng() * 3);
    for (let i = 0; i < stubs; i++) {
      const s = new THREE.CylinderGeometry(r * 0.1, r * 0.22, 0.3 + rng() * 0.5, 4);
      s.rotateZ((rng() - 0.5) * 1.2);
      s.rotateX((rng() - 0.5) * 2.2);
      s.translate((rng() - 0.5) * len * 0.8, r * 1.1, 0);
      parts.push(s);
    }
  }
  return assemble([{
    geoms: parts,
    color: (c, x, y, z) => {
      const n = fbm3(x * 2.2, y * 5, z * 2.2, 2);
      c.copy(DEAD).multiplyScalar(0.58 + n * 0.55);
      // Moss on the upper surface only.
      if (y > r * 1.1) c.lerp(NEEDLE, Math.min(0.55, (n - 0.35) * 1.4));
    },
    flex: 0,
  }]);
}

/* --------------------------------------------------------------- boulder -- */

export function buildBoulder(rng, detail = 2, opts = {}) {
  const r = opts.radius ?? (0.6 + rng() * 2.2);
  const g = blob(r, detail > 1 ? 2 : 1, rng, {
    warp: 0.30, freq: 1.5, squash: [1, 0.66 + rng() * 0.3, 1], flatten: 0.55,
  });
  g.translate(0, r * 0.5, 0);
  return assemble([{
    geoms: [g],
    color: (c, x, y, z, nx, ny) => {
      const n = fbm3(x * 1.4, y * 1.4, z * 1.4, 3);
      c.copy(STONE).multiplyScalar(0.52 + n * 0.72);
      // Lichen takes the top and the north face, never the underside.
      if (ny > 0.45) c.lerp(C(0x7d8a4e), Math.max(0, (n - 0.42)) * 1.5);
    },
    flex: 0,
  }]);
}

/* ------------------------------------------------------- ground cover ---- */

/** Fern: arcing fronds of paired leaflets. Understorey for the pine biome. */
export function buildFern(rng) {
  const fronds = 5 + Math.floor(rng() * 4);
  const geoms = [];
  for (let i = 0; i < fronds; i++) {
    const a = (i / fronds) * Math.PI * 2 + rng() * 0.6;
    const len = 0.45 + rng() * 0.4;
    const arc = 0.55 + rng() * 0.35;
    const pts = [];
    const widths = [];
    const N = 5;
    for (let s = 0; s <= N; s++) {
      const t = s / N;
      pts.push([
        Math.cos(a) * len * t,
        len * (Math.sin(t * arc * Math.PI) * 0.62 + t * 0.15),
        Math.sin(a) * len * t,
      ]);
      widths.push(0.055 * len * (1 - t * 0.85) * (t < 0.12 ? t / 0.12 : 1));
    }
    geoms.push(ribbon(pts, widths, [-Math.sin(a), 0, Math.cos(a)]));
  }
  return assemble([{
    geoms,
    color: (c, x, y) => c.copy(FERN).multiplyScalar(0.72 + y * 0.9),
    flex: (x, y) => 0.35 + y * 1.1,
  }]);
}

/** Grass tuft. Cheap, and only ever placed in the innermost ring. */
export function buildGrass(rng) {
  const blades = 7 + Math.floor(rng() * 6);
  const geoms = [];
  for (let i = 0; i < blades; i++) {
    const a = rng() * Math.PI * 2;
    const h = 0.22 + rng() * 0.3;
    const flop = 0.25 + rng() * 0.5;
    const pts = [];
    const widths = [];
    for (let s = 0; s <= 3; s++) {
      const t = s / 3;
      pts.push([Math.cos(a) * h * flop * t * t, h * t * (1 - t * flop * 0.4), Math.sin(a) * h * flop * t * t]);
      widths.push(0.014 * (1 - t * 0.9));
    }
    geoms.push(ribbon(pts, widths, [-Math.sin(a), 0, Math.cos(a)]));
  }
  return assemble([{
    geoms,
    color: (c, x, y) => c.copy(GRASS).lerp(GRASS_DRY, Math.min(1, y * 1.8)),
    flex: (x, y) => 0.4 + y * 2.2,
  }]);
}

/** Low shrub. Fills the gap between grass and trees on meadow edges. */
export function buildShrub(rng, detail = 2) {
  const r = 0.4 + rng() * 0.6;
  const lobes = detail > 1 ? 3 + Math.floor(rng() * 3) : 2;
  const geoms = [];
  for (let i = 0; i < lobes; i++) {
    const b = blob(r * (0.5 + rng() * 0.5), detail > 1 ? 1 : 0, rng, { warp: 0.45, freq: 2.2 });
    b.translate((rng() - 0.5) * r, r * (0.45 + rng() * 0.5), (rng() - 0.5) * r);
    geoms.push(b);
  }
  return assemble([{
    geoms,
    color: (c, x, y, z, nx, ny) => {
      c.copy(LEAF).lerp(LEAF_PALE, Math.max(0, ny) * 0.4 + fbm3(x * 4, y * 4, z * 4, 2) * 0.4);
      c.multiplyScalar(0.8);
    },
    flex: (x, y) => 0.25 + y * 0.8,
  }]);
}

/** A dead standing snag - a bare trunk. Cheap silhouette variety. */
export function buildSnag(rng, detail = 2) {
  const height = 3.5 + rng() * 6;
  const r = height * 0.018;
  const parts = [stem(r * 1.7, r * 0.5, height, detail > 1 ? 5 : 4)];
  if (detail > 1) {
    for (let i = 0; i < 3; i++) {
      const len = height * (0.14 + rng() * 0.12);
      const b = stem(r * 0.4, r * 0.1, len, 3);
      b.rotateZ(Math.PI / 2 - (0.3 + rng() * 0.7));
      b.rotateY(rng() * Math.PI * 2);
      b.translate(0, height * (0.45 + rng() * 0.45), 0);
      parts.push(b);
    }
  }
  const g = assemble([{
    geoms: parts,
    color: (c, x, y, z) => c.copy(DEAD).multiplyScalar(0.55 + fbm3(x * 5, y * 3, z * 5, 2) * 0.6),
    flex: (x, y) => Math.pow(Math.max(0, y / height), 3) * 0.3,
  }]);
  return bend(g, (rng() - 0.5) * 0.1, (rng() - 0.5) * 0.1, height);
}

export { deform };
