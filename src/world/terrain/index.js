/**
 * terrain/index.js - the streaming heightfield. This is the TerrainProvider.
 *
 * field.js answers "what is the ground at (x, z)". This file turns that into
 * geometry that exists around the player and nowhere else, because the world is
 * unbounded and a 1.5 km view at 2 m resolution is 560,000 quads if you are
 * naive about it.
 *
 * THREE DECISIONS WORTH KNOWING:
 *
 *  1. NORMALS ARE ANALYTIC, NOT COMPUTED FROM THE MESH. Central-differencing
 *     the height function gives the same normal on both sides of a chunk
 *     boundary and at every LOD, so seams never light differently. Deriving
 *     them from triangles would make the LOD ring visible as a shading crease
 *     that moves with the player, which is far worse than the geometric gap.
 *
 *  2. SKIRTS, NOT STITCHING. Where a 2 m chunk meets a 4 m chunk the edges do
 *     not line up and you see through the world. Proper stitching means
 *     special-case index buffers per neighbour-LOD combination - six variants
 *     per chunk, rebuilt whenever a neighbour changes ring. A skirt is a wall
 *     hanging down from the border that costs one quad ring and cannot ever be
 *     wrong. The crack is still there; you just cannot see it.
 *
 *  3. BUILDING IS BUDGETED. A near chunk is ~4,200 vertices and each one costs
 *     a full field evaluation plus two more for the normal. Building the whole
 *     ring in one frame is a two-second stall. `update()` builds at most
 *     `budget` chunks per call, nearest first, so streaming shows up as terrain
 *     resolving ahead of you rather than as a hitch.
 */
import * as THREE from 'three';
import { CHUNK, VIEW_CHUNKS, chunkKey, toChunk } from '../contract.js';
import { createField } from './field.js';
import { createTerrainMaterial } from './material.js';

/** Quads per chunk edge, by LOD ring. 128 m / 64 = 2 m at the player. */
const LOD_SEG = [64, 32, 16];
/** Ring index -> LOD. Ring is the Chebyshev chunk distance from the centre. */
const LOD_FOR_RING = (r) => (r <= 1 ? 0 : r <= 3 ? 1 : 2);
/** How far the skirt hangs below the border, metres. Must exceed the worst
 *  height error between adjacent LODs, which is a few metres in the mountains. */
const SKIRT = 9;

const NORMAL_EPS = 0.6;

/**
 * @param {object} opts
 * @param {number} opts.seed
 * @param {number} opts.view    Chunk radius that stays resident.
 * @param {number} opts.budget  Max chunks meshed per update() call.
 */
export function createTerrain({
  seed = 1337,
  view = VIEW_CHUNKS,
  budget = 2,
} = {}) {
  const field = createField(seed);
  const material = createTerrainMaterial({ detailFade: 260 });

  const root = new THREE.Group();
  root.name = 'terrain';
  root.matrixAutoUpdate = false;

  /** key -> {cx, cz, lod, mesh} */
  const live = new Map();
  /** Chunks wanted but not yet built, rebuilt every update(). */
  let queue = [];
  let centreCx = NaN;
  let centreCz = NaN;
  let built = 0;

  const cls = field.newClass();

  /* --------------------------------------------------------- queries ----- */

  function height(x, z) { return field.height(x, z); }

  /** Analytic normal. Three height lookups on top of the one you already did. */
  function normalAt(x, z, out) {
    const e = NORMAL_EPS;
    const hx = field.height(x + e, z) - field.height(x - e, z);
    const hz = field.height(x, z + e) - field.height(x, z - e);
    const nx = -hx, ny = 2 * e, nz = -hz;
    const inv = 1 / Math.hypot(nx, ny, nz);
    out.nx = nx * inv; out.ny = ny * inv; out.nz = nz * inv;
    return out;
  }

  const sampleScratch = { nx: 0, ny: 1, nz: 0 };
  const sampleOut = { height: 0, nx: 0, ny: 1, nz: 0, surfaceId: 2, wetness: 0, biome: 2 };

  function sample(x, z, out = sampleOut) {
    out.height = field.height(x, z);
    normalAt(x, z, sampleScratch);
    out.nx = sampleScratch.nx; out.ny = sampleScratch.ny; out.nz = sampleScratch.nz;
    const c = field.classify(x, z, out.ny, cls);
    out.surfaceId = c.surface;
    out.wetness = c.wet;
    // Beyond the GroundSample contract, but free here and it saves the scatter
    // layer a second full classify on every candidate point.
    out.biome = c.biome;
    return out;
  }

  function biomeAt(x, z) {
    normalAt(x, z, sampleScratch);
    return field.classify(x, z, sampleScratch.ny, cls).biome;
  }

  /* ----------------------------------------------------------- meshing --- */

  /**
   * One chunk. The grid is (seg+1)^2 interior vertices plus a one-vertex border
   * ring that shares the interior's x/z but sits SKIRT metres lower.
   */
  function buildChunk(cx, cz, lod) {
    const seg = LOD_SEG[lod];
    const step = CHUNK / seg;
    const n = seg + 1;
    const wide = n + 2;                 // + skirt ring on every side
    const count = wide * wide;

    const pos = new Float32Array(count * 3);
    const nrm = new Float32Array(count * 3);
    const col = new Float32Array(count * 3);
    const msk = new Float32Array(count * 3);

    const ox = cx * CHUNK;
    const oz = cz * CHUNK;
    const nrmTmp = { nx: 0, ny: 1, nz: 0 };

    let p = 0;
    for (let j = 0; j < wide; j++) {
      // Border rows clamp onto the edge row and drop.
      const gj = Math.min(seg, Math.max(0, j - 1));
      const skirtJ = j === 0 || j === wide - 1;
      for (let i = 0; i < wide; i++) {
        const gi = Math.min(seg, Math.max(0, i - 1));
        const skirt = skirtJ || i === 0 || i === wide - 1;

        const lx = gi * step;
        const lz = gj * step;
        const wx = ox + lx;
        const wz = oz + lz;

        const h = field.height(wx, wz);
        normalAt(wx, wz, nrmTmp);
        const c = field.classify(wx, wz, nrmTmp.ny, cls);

        pos[p] = lx;
        pos[p + 1] = skirt ? h - SKIRT : h;
        pos[p + 2] = lz;
        nrm[p] = nrmTmp.nx; nrm[p + 1] = nrmTmp.ny; nrm[p + 2] = nrmTmp.nz;
        col[p] = c.r; col[p + 1] = c.g; col[p + 2] = c.b;
        msk[p] = c.rock; msk[p + 1] = c.wet; msk[p + 2] = c.snow;
        p += 3;
      }
    }

    // Indices. 16-bit is enough up to 255^2 vertices; LOD 0 is 66^2 = 4,356.
    const quads = (wide - 1) * (wide - 1);
    const idx = new Uint16Array(quads * 6);
    let q = 0;
    for (let j = 0; j < wide - 1; j++) {
      for (let i = 0; i < wide - 1; i++) {
        const a = j * wide + i;
        const b = a + 1;
        const c2 = a + wide;
        const d = c2 + 1;
        idx[q] = a; idx[q + 1] = c2; idx[q + 2] = b;
        idx[q + 3] = b; idx[q + 4] = c2; idx[q + 5] = d;
        q += 6;
      }
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('normal', new THREE.BufferAttribute(nrm, 3));
    geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.setAttribute('aMask', new THREE.BufferAttribute(msk, 3));
    geo.setIndex(new THREE.BufferAttribute(idx, 1));
    // Bounding sphere by hand: computeBoundingSphere would walk every vertex,
    // and we already know the chunk is CHUNK wide and at most MTN_AMP tall.
    geo.boundingSphere = new THREE.Sphere(
      new THREE.Vector3(CHUNK / 2, pos[(wide * (wide >> 1) + (wide >> 1)) * 3 + 1], CHUNK / 2),
      CHUNK * 1.4,
    );

    const mesh = new THREE.Mesh(geo, material);
    mesh.position.set(ox, 0, oz);
    mesh.receiveShadow = true;
    mesh.castShadow = false;
    mesh.matrixAutoUpdate = false;
    mesh.updateMatrix();
    mesh.name = `chunk ${cx},${cz} L${lod}`;
    return mesh;
  }

  function disposeChunk(entry) {
    root.remove(entry.mesh);
    entry.mesh.geometry.dispose();
  }

  /* ---------------------------------------------------------- streaming --- */

  function rebuildQueue() {
    queue.length = 0;
    for (let dz = -view; dz <= view; dz++) {
      for (let dx = -view; dx <= view; dx++) {
        const ring = Math.max(Math.abs(dx), Math.abs(dz));
        if (ring > view) continue;
        const cx = centreCx + dx;
        const cz = centreCz + dz;
        const lod = LOD_FOR_RING(ring);
        const key = chunkKey(cx, cz);
        const have = live.get(key);
        if (have && have.lod === lod) continue;
        queue.push({ cx, cz, lod, key, d: dx * dx + dz * dz });
      }
    }
    queue.sort((a, b) => a.d - b.d);

    // Evict anything outside the ring.
    for (const [key, entry] of live) {
      if (Math.abs(entry.cx - centreCx) > view || Math.abs(entry.cz - centreCz) > view) {
        disposeChunk(entry);
        live.delete(key);
      }
    }
  }

  function flushQueue(limit) {
    let n = 0;
    while (queue.length && n < limit) {
      const job = queue.shift();
      const old = live.get(job.key);
      if (old) { disposeChunk(old); live.delete(job.key); }
      const mesh = buildChunk(job.cx, job.cz, job.lod);
      root.add(mesh);
      live.set(job.key, { cx: job.cx, cz: job.cz, lod: job.lod, mesh });
      n++;
      built++;
    }
  }

  function update(centre) {
    const cx = toChunk(centre.x);
    const cz = toChunk(centre.z);
    if (cx !== centreCx || cz !== centreCz) {
      centreCx = cx; centreCz = cz;
      rebuildQueue();
    }
    flushQueue(budget);
  }

  /** Build everything in the inner rings up front so the game never starts on a hole. */
  function prime(centre, rings = 2) {
    centreCx = toChunk(centre.x);
    centreCz = toChunk(centre.z);
    rebuildQueue();
    const inner = queue.filter((j) => Math.max(
      Math.abs(j.cx - centreCx), Math.abs(j.cz - centreCz)) <= rings);
    const rest = queue.filter((j) => !inner.includes(j));
    queue = inner.concat(rest);
    flushQueue(inner.length);
  }

  /**
   * A good place to start: flat, dry, not a cliff and not in a river. Rather
   * than taking the first acceptable point it scores a spiral of candidates and
   * keeps the flattest, because "merely drivable" spawns you on a camber with
   * the truck already sliding.
   */
  function findSpawn(x = 0, z = 0) {
    const out = { x, z, y: 0, ok: false };
    const s = field.newClass();
    let bestScore = -Infinity;
    for (let r = 0; r < 90; r++) {
      const a = r * 2.399963;                       // golden-angle spiral
      const rad = r * 12;
      const px = x + Math.cos(a) * rad;
      const pz = z + Math.sin(a) * rad;
      normalAt(px, pz, sampleScratch);
      if (sampleScratch.ny < 0.96) continue;
      const c = field.classify(px, pz, sampleScratch.ny, s);
      if (c.wet > 0.2 || c.surface === 6) continue;

      // Flatness first, then penalise walking away from the requested point.
      const score = sampleScratch.ny * 2000 - rad * 0.05;
      if (score <= bestScore) continue;
      bestScore = score;
      out.x = px; out.z = pz; out.y = field.height(px, pz); out.ok = true;
      if (sampleScratch.ny > 0.997) break;          // flat enough, stop looking
    }
    if (!out.ok) out.y = field.height(x, z);
    return out;
  }

  return {
    root,
    field,
    material,
    height,
    sample,
    biomeAt,
    normalAt,
    update,
    prime,
    findSpawn,
    stats: () => ({ chunks: live.size, pending: queue.length, built, ...field.stats() }),
    dispose() {
      for (const entry of live.values()) disposeChunk(entry);
      live.clear();
      queue.length = 0;
      material.dispose();
    },
  };
}
