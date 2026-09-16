/**
 * scatter/index.js - the ScatterProvider: forest, rock and ground cover.
 *
 * ONE SAMPLE, ONE DECISION. The naive design gives every species its own grid
 * and its own pass, which means the same square metre gets a full terrain
 * sample six times. Instead there is a single jittered grid per chunk: sample
 * once, then let the biome, slope, wetness and altitude at that point decide
 * what - if anything - grows there. It is both cheaper and better, because
 * species then compete for a spot instead of being layered on independently
 * and ending up inside each other.
 *
 * PLACEMENT IS DERIVED, NEVER STORED. Every position comes from mulberry32
 * seeded on the chunk coordinate, so a chunk that streams out and back in comes
 * back identical, and nothing has to be saved. The same property is what lets
 * the physics layer ask `obstaclesNear()` for a chunk it cannot see.
 *
 * INSTANCING, WITH LOD BY RING. One InstancedMesh per (species, detail,
 * variant). A tree joins the pool matching its chunk's ring, so the near ring
 * gets 250-triangle pines and the far ring gets 12-triangle cards, and no
 * geometry is ever rebuilt while driving.
 *
 * COLLIDERS FOLLOW THE TRUCK, NOT THE CHUNKS. There are tens of thousands of
 * resident trees and Rapier wants none of them. A ring of ~48 colliders is
 * synced around the vehicle and recycled as it moves, which is the only part
 * of the forest that can actually be hit.
 */
import * as THREE from 'three';
import { CHUNK, chunkKey, toChunk, mulberry32, hash2, BIOME } from '../contract.js';
import { createFoliageMaterial, tickFoliage } from './material.js';
import {
  buildPine, buildBirch, buildSnag, buildDeadfall, buildBoulder, buildShrub,
  buildFern, buildGrass,
} from './flora.js';
import { triCount } from './geo.js';

const HIDDEN = new THREE.Matrix4().makeScale(0, 0, 0);

/**
 * Species table. Arrays are indexed by DETAIL LEVEL: [far, mid, near].
 *
 * `rings` is the outermost chunk ring the species appears in. `r` is the
 * collision radius; 0 means the physics layer never sees it. Densities are
 * stems per SQUARE METRE and live in `pick()` - never per cell, or changing the
 * grid spacing would silently change how thick the forest is.
 */
const SPECIES = {
  pine:     { build: buildPine,     variants: [2, 4, 4], caps: [7000, 3400, 260], rings: 4, r: 0.34, shadow: true,  tiltMax: 0.05, scale: [0.8, 1.25] },
  birch:    { build: buildBirch,    variants: [2, 3, 3], caps: [3200, 1600, 140], rings: 4, r: 0.26, shadow: true,  tiltMax: 0.07, scale: [0.8, 1.2] },
  snag:     { build: buildSnag,     variants: [1, 2, 3], caps: [700, 340, 60],    rings: 3, r: 0.22, shadow: true,  tiltMax: 0.12, scale: [0.8, 1.2] },
  deadfall: { build: buildDeadfall, variants: [1, 2, 3], caps: [420, 260, 60],    rings: 2, r: 0.45, shadow: false, tiltMax: 0.10, scale: [0.85, 1.2] },
  boulder:  { build: buildBoulder,  variants: [2, 3, 4], caps: [2200, 1100, 180], rings: 3, r: 0.90, shadow: true,  tiltMax: 0.18, scale: [0.7, 1.6] },
  shrub:    { build: buildShrub,    variants: [0, 0, 3], caps: [0, 0, 460],       rings: 1, r: 0,    shadow: false, tiltMax: 0.10, scale: [0.7, 1.4] },
  fern:     { build: buildFern,     variants: [0, 0, 3], caps: [0, 0, 1100],      rings: 1, r: 0,    shadow: false, tiltMax: 0.12, scale: [0.7, 1.5] },
  grass:    { build: buildGrass,    variants: [0, 0, 3], caps: [0, 0, 1900],      rings: 1, r: 0,    shadow: false, tiltMax: 0.06, scale: [0.7, 1.6] },
};

/**
 * Ring -> detail level. Only the chunk you are standing in gets hero trees:
 * one ring of 250-triangle pines is 100k triangles, and two is 900k.
 */
const DETAIL_FOR_RING = (r) => (r === 0 ? 2 : r <= 2 ? 1 : 0);

/** Ground cover exists only where you can actually see a leaf. */
const FINE_RINGS = 1;

/** A fixed-capacity instance pool with a free list. */
class Pool {
  constructor(geometry, material, capacity, { shadow = false } = {}) {
    this.mesh = new THREE.InstancedMesh(geometry, material, capacity);
    this.mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
    this.mesh.count = 0;
    this.mesh.castShadow = shadow;
    this.mesh.receiveShadow = true;
    this.mesh.frustumCulled = false;      // instances span the whole ring
    this.mesh.instanceColor = new THREE.InstancedBufferAttribute(
      new Float32Array(capacity * 3).fill(1), 3,
    );
    this.mesh.instanceColor.setUsage(THREE.DynamicDrawUsage);
    this.capacity = capacity;
    this.top = 0;
    this.free = [];
    this.dirty = false;
    this.tris = triCount(geometry);
  }

  alloc(matrix, tint) {
    let i;
    if (this.free.length) i = this.free.pop();
    else if (this.top < this.capacity) i = this.top++;
    else return -1;
    this.mesh.setMatrixAt(i, matrix);
    this.mesh.setColorAt(i, tint);
    if (i >= this.mesh.count) this.mesh.count = i + 1;
    this.dirty = true;
    return i;
  }

  release(i) {
    this.mesh.setMatrixAt(i, HIDDEN);
    this.free.push(i);
    this.dirty = true;
  }

  flush() {
    if (!this.dirty) return;
    this.mesh.instanceMatrix.needsUpdate = true;
    if (this.mesh.instanceColor) this.mesh.instanceColor.needsUpdate = true;
    this.dirty = false;
  }

  dispose() {
    this.mesh.geometry.dispose();
    this.mesh.removeFromParent();
    this.mesh.dispose();
  }
}

/**
 * @param {object} opts
 * @param {object} opts.terrain  TerrainProvider.
 * @param {number} opts.seed
 * @param {number} opts.view     Chunk radius for scatter (<= terrain view).
 * @param {number} opts.budget   Chunks populated per update().
 * @param {number} opts.density  Global multiplier, for graphics presets.
 * @param {object} [opts.world]  Rapier world, for trunk colliders.
 * @param {object} [opts.rapier]
 */
export function createScatter({
  terrain, seed = 1337, view = 4, budget = 1, density = 1,
  world = null, rapier = null,
} = {}) {
  const root = new THREE.Group();
  root.name = 'scatter';

  const solidMat = createFoliageMaterial({ side: THREE.FrontSide, roughness: 0.92 });
  const leafMat = createFoliageMaterial({ side: THREE.DoubleSide, roughness: 0.84 });

  /* ------------------------------------------------------------- pools --- */

  // pools[speciesKey][detail] = Pool[]
  const pools = {};
  let prototypeTris = 0;

  for (const [key, spec] of Object.entries(SPECIES)) {
    pools[key] = [[], [], []];
    for (let d = 0; d <= 2; d++) {
      const n = spec.variants[d];
      const cap = spec.caps[d];
      if (!n || !cap) continue;
      const per = Math.max(16, Math.ceil(cap / n));
      for (let v = 0; v < n; v++) {
        const rng = mulberry32(seed * 7919 + hash2(v, d, key.length) * 1e6);
        const geo = spec.build(rng, d);
        if (!geo) continue;
        prototypeTris += triCount(geo);
        const mat = key === 'boulder' || key === 'deadfall' || key === 'snag'
          ? solidMat : leafMat;
        const pool = new Pool(geo, mat, per, { shadow: spec.shadow && d === 2 });
        pool.mesh.name = `${key}-L${d}-${v}`;
        pools[key][d].push(pool);
        root.add(pool.mesh);
      }
    }
  }

  /* ------------------------------------------------------- placement ----- */

  const sample = { height: 0, nx: 0, ny: 1, nz: 0, surfaceId: 2, wetness: 0, biome: 2 };
  const m4 = new THREE.Matrix4();
  const quat = new THREE.Quaternion();
  const spin = new THREE.Quaternion();
  const posV = new THREE.Vector3();
  const sclV = new THREE.Vector3();
  const tint = new THREE.Color();
  const axis = new THREE.Vector3();
  const UP = new THREE.Vector3(0, 1, 0);

  /**
   * What grows here, if anything. Returns a species key or null.
   *
   * The rules are about SLOPE and WETNESS first and biome second: a tree cannot
   * hold a 35-degree scree face whatever the biome map says, and nothing at all
   * grows in standing water.
   *
   * @param {number} area Cell area in m^2 - every density below is per m^2, so
   *   the grid can be re-spaced without changing how thick the forest looks.
   */
  function pick(rng, s, biome, height, area, fine) {
    const ny = s.ny;
    const wet = s.wetness;

    if (s.surfaceId === 6) return null;                     // open water
    if (ny < 0.55) return rng() < 0.002 * area ? 'boulder' : null;   // cliff

    // Trees thin out on slope and die out above the treeline.
    const slopeOk = THREE.MathUtils.smoothstep(ny, 0.62, 0.86);
    const treeline = 1 - THREE.MathUtils.smoothstep(height, 104, 136);
    const lowline = THREE.MathUtils.smoothstep(height, 1.5, 6);
    const dry = 1 - THREE.MathUtils.smoothstep(wet, 0.25, 0.62);

    // Stems per m^2. 0.006 is one tree per 167 m^2 - a walkable forest, not a
    // plantation, and about as much as the triangle budget will carry.
    let forest = 0;
    let broadleaf = 0.25;
    switch (biome) {
      case BIOME.PINE.id: forest = 0.0062; broadleaf = 0.16; break;
      case BIOME.MEADOW.id: forest = 0.0013; broadleaf = 0.55; break;
      case BIOME.SCREE.id: forest = 0.0004; broadleaf = 0.05; break;
      case BIOME.RIVERBED.id: forest = 0.0005; broadleaf = 0.80; break;
      case BIOME.MUDFLAT.id: forest = 0.0002; broadleaf = 0.70; break;
      default: forest = 0; break;                            // alpine
    }
    forest *= slopeOk * treeline * lowline * dry * density * area;

    const roll = rng();
    if (roll < forest) {
      if (rng() < 0.07) return 'snag';
      return rng() < broadleaf ? 'birch' : 'pine';
    }

    // Rock: the opposite gradient - more of it where trees give up.
    const rocky = ((biome === BIOME.SCREE.id || biome === BIOME.ALPINE.id) ? 0.0022
      : biome === BIOME.RIVERBED.id ? 0.0016
        : 0.0005 * (1 - slopeOk * 0.5)) * density * area;
    if (roll < forest + rocky) return 'boulder';

    const rot = forest > 0.02 ? 0.0004 * density * area : 0;
    if (roll < forest + rocky + rot) return 'deadfall';

    if (!fine) return null;

    // Ground cover only exists in the innermost rings, where you can see it.
    const cover = (1 - THREE.MathUtils.smoothstep(height, 100, 130)) * dry * density * area;
    const r2 = rng();
    if (biome === BIOME.PINE.id) {
      if (r2 < 0.0060 * cover) return 'fern';
      if (r2 < 0.0085 * cover) return 'shrub';
      if (r2 < 0.0140 * cover) return 'grass';
    } else if (biome === BIOME.MEADOW.id || biome === BIOME.RIVERBED.id) {
      if (r2 < 0.0110 * cover) return 'grass';
      if (r2 < 0.0140 * cover) return 'shrub';
    }
    return null;
  }

  /* -------------------------------------------------------- chunk life --- */

  /** key -> { cx, cz, ring, placed: [{pool, index}], obstacles: [...] } */
  const live = new Map();
  let queue = [];
  let centreCx = NaN;
  let centreCz = NaN;

  function populate(cx, cz, ring) {
    const detail = DETAIL_FOR_RING(ring);
    const fine = ring <= FINE_RINGS;
    // 3.5 m cells up close, 7 m further out. Nothing small is placed beyond the
    // near rings anyway, so the finer grid would only cost terrain samples.
    const cell = fine ? 3.5 : 7;
    const n = Math.round(CHUNK / cell);
    const step = CHUNK / n;
    const area = step * step;
    const rng = mulberry32((cx * 73856093) ^ (cz * 19349663) ^ (seed * 83492791));

    const placed = [];
    const obstacles = [];
    const ox = cx * CHUNK;
    const oz = cz * CHUNK;

    for (let j = 0; j < n; j++) {
      for (let i = 0; i < n; i++) {
        const x = ox + (i + 0.15 + rng() * 0.7) * step;
        const z = oz + (j + 0.15 + rng() * 0.7) * step;

        terrain.sample(x, z, sample);
        const key = pick(rng, sample, sample.biome, sample.height, area, fine);
        if (!key) continue;

        const spec = SPECIES[key];
        if (ring > spec.rings) continue;
        const bucket = pools[key][detail].length ? pools[key][detail] : pools[key][2];
        if (!bucket.length) continue;
        const pool = bucket[(rng() * bucket.length) | 0];

        const scale = spec.scale[0] + rng() * (spec.scale[1] - spec.scale[0]);
        posV.set(x, sample.height - 0.06 * scale, z);

        // Stand normal to the ground, but only partly: a pine grows toward the
        // light, so it leans far less than the hillside it is on.
        const yaw = rng() * Math.PI * 2;
        axis.set(sample.nx, sample.ny + 0.12 / Math.max(0.02, spec.tiltMax), sample.nz).normalize();
        quat.setFromUnitVectors(UP, axis);
        spin.setFromAxisAngle(UP, yaw);
        quat.multiply(spin);
        sclV.setScalar(scale);
        m4.compose(posV, quat, sclV);

        const v = 0.86 + rng() * 0.28;
        tint.setRGB(v * (0.97 + rng() * 0.06), v, v * (0.95 + rng() * 0.1));

        const index = pool.alloc(m4, tint);
        if (index < 0) continue;
        placed.push({ pool, index });

        if (spec.r > 0) obstacles.push({ x, z, y: sample.height, r: spec.r * scale, kind: key });
      }
    }

    return { cx, cz, ring, placed, obstacles };
  }

  function clear(entry) {
    for (const { pool, index } of entry.placed) pool.release(index);
  }

  function rebuildQueue() {
    queue.length = 0;
    for (let dz = -view; dz <= view; dz++) {
      for (let dx = -view; dx <= view; dx++) {
        const ring = Math.max(Math.abs(dx), Math.abs(dz));
        if (ring > view) continue;
        const cx = centreCx + dx;
        const cz = centreCz + dz;
        const key = chunkKey(cx, cz);
        const have = live.get(key);
        if (have && have.ring === ring) continue;
        queue.push({ cx, cz, ring, key, d: dx * dx + dz * dz });
      }
    }
    queue.sort((a, b) => a.d - b.d);

    for (const [key, entry] of live) {
      if (Math.abs(entry.cx - centreCx) > view || Math.abs(entry.cz - centreCz) > view) {
        clear(entry);
        live.delete(key);
      }
    }
  }

  /* --------------------------------------------------------- colliders --- */

  const COLLIDER_RANGE = 34;
  const COLLIDER_MAX = 56;
  const colliders = [];
  let colliderCx = NaN;
  let colliderCz = NaN;

  function syncColliders(centre) {
    if (!world || !rapier) return;
    // Only redo the ring when the truck has actually moved a useful distance.
    const gx = Math.round(centre.x / 12);
    const gz = Math.round(centre.z / 12);
    if (gx === colliderCx && gz === colliderCz) return;
    colliderCx = gx; colliderCz = gz;

    for (const c of colliders) world.removeCollider(c, false);
    colliders.length = 0;

    const near = obstaclesNear(centre.x, centre.z, COLLIDER_RANGE);
    near.sort((a, b) => ((a.x - centre.x) ** 2 + (a.z - centre.z) ** 2)
      - ((b.x - centre.x) ** 2 + (b.z - centre.z) ** 2));

    for (const o of near.slice(0, COLLIDER_MAX)) {
      let desc;
      if (o.kind === 'boulder') {
        desc = rapier.ColliderDesc.ball(o.r)
          .setTranslation(o.x, o.y + o.r * 0.45, o.z);
      } else if (o.kind === 'deadfall') {
        desc = rapier.ColliderDesc.cuboid(o.r * 2.4, o.r * 0.5, o.r * 0.5)
          .setTranslation(o.x, o.y + o.r * 0.5, o.z);
      } else {
        // Trunks are tall cylinders: you hit them, you do not drive over them.
        desc = rapier.ColliderDesc.cylinder(3, o.r)
          .setTranslation(o.x, o.y + 3, o.z);
      }
      colliders.push(world.createCollider(desc.setFriction(0.8).setRestitution(0.1)));
    }
  }

  /* -------------------------------------------------------------- query -- */

  function obstaclesNear(x, z, radius) {
    const out = [];
    const r2 = radius * radius;
    const c0 = toChunk(x - radius);
    const c1 = toChunk(x + radius);
    const d0 = toChunk(z - radius);
    const d1 = toChunk(z + radius);
    for (let cz = d0; cz <= d1; cz++) {
      for (let cx = c0; cx <= c1; cx++) {
        const entry = live.get(chunkKey(cx, cz));
        if (!entry) continue;
        for (const o of entry.obstacles) {
          const dx = o.x - x;
          const dz = o.z - z;
          if (dx * dx + dz * dz <= r2) out.push(o);
        }
      }
    }
    return out;
  }

  /* -------------------------------------------------------------- frame -- */

  let clock = 0;

  function update(centre, dt = 0) {
    clock += dt;
    tickFoliage(solidMat, clock);
    tickFoliage(leafMat, clock);

    const cx = toChunk(centre.x);
    const cz = toChunk(centre.z);
    if (cx !== centreCx || cz !== centreCz) {
      centreCx = cx; centreCz = cz;
      rebuildQueue();
    }

    let n = 0;
    while (queue.length && n < budget) {
      const job = queue.shift();
      const old = live.get(job.key);
      if (old) { clear(old); live.delete(job.key); }
      live.set(job.key, populate(job.cx, job.cz, job.ring));
      n++;
    }

    for (const key of Object.keys(pools)) {
      for (const bucket of pools[key]) for (const p of bucket) p.flush();
    }

    syncColliders(centre);
  }

  /** Build the near rings up front so the game does not start on bare ground. */
  function prime(centre, rings = 1) {
    centreCx = toChunk(centre.x);
    centreCz = toChunk(centre.z);
    rebuildQueue();
    const inner = [];
    const rest = [];
    for (const job of queue) (job.ring <= rings ? inner : rest).push(job);
    queue = inner.concat(rest);
    const save = budget;
    budget = inner.length;
    update(centre, 0);
    budget = save;
  }

  return {
    root,
    update,
    prime,
    obstaclesNear,
    /** True once anything exists for the physics layer to hit. */
    get hasColliders() { return colliders.length > 0; },
    stats() {
      let live1 = 0;
      let tris = 0;
      for (const key of Object.keys(pools)) {
        for (const bucket of pools[key]) {
          for (const p of bucket) {
            const used = p.top - p.free.length;
            live1 += used;
            tris += used * p.tris;
          }
        }
      }
      return { instances: live1, tris, chunks: live.size, pending: queue.length, prototypeTris };
    },
    dispose() {
      for (const c of colliders) world?.removeCollider(c, false);
      colliders.length = 0;
      for (const key of Object.keys(pools)) {
        for (const bucket of pools[key]) for (const p of bucket) p.dispose();
      }
      solidMat.dispose();
      leafMat.dispose();
      root.removeFromParent();
    },
  };
}
