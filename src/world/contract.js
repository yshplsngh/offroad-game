/**
 * contract.js - the shared interfaces every world module codes against.
 *
 * This file is the integration seam. Terrain, scatter, wildlife, physics and
 * atmosphere are built independently, so they must agree on exactly three
 * things: the coordinate convention, the surface table, and the provider
 * interfaces below. Change this file only with the whole pipeline in view.
 *
 * COORDINATES (identical to the vehicle modules)
 *   +X east, +Y up, +Z north. Ground is a heightfield y = f(x, z).
 *   1 unit = 1 metre. Chunk coordinates are integers: cx = floor(x / CHUNK).
 */

/** Chunk edge length in metres. Terrain, scatter and wildlife all use this. */
export const CHUNK = 128;

/** How many chunks out from the player stay resident. */
export const VIEW_CHUNKS = 6;

/** Fixed physics timestep. */
export const FIXED_DT = 1 / 60;

/**
 * Surface types. `friction` is the peak tire grip coefficient, `drag` is the
 * rolling resistance the wheel fights through, `sink` is how far a loaded
 * tire settles into it, `deform` whether it leaves ruts.
 *
 * These numbers ARE the driving feel. Tune here, not in the tire model.
 */
export const SURFACE = {
  ROCK:   { id: 0, name: 'rock',   friction: 1.05, drag: 0.012, sink: 0.00, deform: false, color: 0x6e6a64, dust: 0.15 },
  GRAVEL: { id: 1, name: 'gravel', friction: 0.82, drag: 0.030, sink: 0.02, deform: true,  color: 0x7a7168, dust: 0.75 },
  DIRT:   { id: 2, name: 'dirt',   friction: 0.88, drag: 0.026, sink: 0.02, deform: true,  color: 0x6b5638, dust: 0.85 },
  GRASS:  { id: 3, name: 'grass',  friction: 0.76, drag: 0.034, sink: 0.03, deform: true,  color: 0x4e6136, dust: 0.20 },
  LOAM:   { id: 4, name: 'loam',   friction: 0.70, drag: 0.055, sink: 0.06, deform: true,  color: 0x4a3c26, dust: 0.30 },
  MUD:    { id: 5, name: 'mud',    friction: 0.38, drag: 0.145, sink: 0.16, deform: true,  color: 0x3b2f1d, dust: 0.00 },
  WATER:  { id: 6, name: 'water',  friction: 0.30, drag: 0.190, sink: 0.10, deform: false, color: 0x2c4442, dust: 0.00 },
  SNOW:   { id: 7, name: 'snow',   friction: 0.45, drag: 0.090, sink: 0.10, deform: true,  color: 0xd8dde4, dust: 0.00 },
};

export const SURFACE_BY_ID = Object.fromEntries(
  Object.values(SURFACE).map((s) => [s.id, s]),
);

/**
 * Biomes drive both terrain colouring and what gets scattered on them.
 * `band` is the [min, max] altitude in metres the biome occupies.
 */
export const BIOME = {
  RIVERBED:  { id: 0, name: 'riverbed',  band: [-99, 3],   surface: 'GRAVEL' },
  MUDFLAT:   { id: 1, name: 'mudflat',   band: [1, 9],     surface: 'MUD' },
  MEADOW:    { id: 2, name: 'meadow',    band: [3, 46],    surface: 'GRASS' },
  PINE:      { id: 3, name: 'pine',      band: [12, 108],  surface: 'LOAM' },
  SCREE:     { id: 4, name: 'scree',     band: [88, 168],  surface: 'GRAVEL' },
  ALPINE:    { id: 5, name: 'alpine',    band: [148, 999], surface: 'ROCK' },
};

/**
 * @typedef {object} GroundSample
 * @property {number} height      Ground height in metres at the query point.
 * @property {number} nx          Ground normal X.
 * @property {number} ny          Ground normal Y.
 * @property {number} nz          Ground normal Z.
 * @property {number} surfaceId   Key into SURFACE_BY_ID.
 * @property {number} wetness     0..1, raises drag and lowers friction.
 */

/**
 * @typedef {object} TerrainProvider
 * @property {import('three').Group} root  Scene node holding resident chunks.
 * @property {(x:number, z:number) => number} height
 *   Fast height-only query. Must be safe to call thousands of times a frame
 *   (wheel raycasts, animal steering, scatter placement).
 * @property {(x:number, z:number, out?:object) => GroundSample} sample
 *   Full ground query including normal and surface.
 * @property {(x:number, z:number) => number} biomeAt
 * @property {(centre:import('three').Vector3) => void} update
 *   Streams chunks in and out around a point. Called once per frame.
 * @property {() => void} dispose
 */

/**
 * @typedef {object} ScatterProvider
 * @property {import('three').Group} root
 * @property {(centre:import('three').Vector3) => void} update
 * @property {(x:number, z:number, radius:number) => Array<{x:number,z:number,r:number,kind:string}>} obstaclesNear
 *   Trees and rocks the physics layer must collide against. Only resident
 *   chunks need to answer.
 * @property {() => void} dispose
 */

/**
 * @typedef {object} WildlifeProvider
 * @property {import('three').Group} root
 * @property {(dt:number, focus:import('three').Vector3, threat:number) => void} update
 *   `threat` is 0..1 - engine noise and proximity, which drives fleeing.
 * @property {() => void} dispose
 */

/**
 * @typedef {object} AtmosphereProvider
 * @property {import('three').Group} root
 * @property {number} timeOfDay          0..24 hours.
 * @property {(dt:number, cameraPos:import('three').Vector3) => void} update
 * @property {import('three').DirectionalLight} sun
 * @property {() => {fogColor:import('three').Color, sunDir:import('three').Vector3}} lighting
 */

/** Deterministic hash -> [0,1). Every module seeds from this so the world is reproducible. */
export function hash2(x, y, seed = 0) {
  let h = Math.imul(x | 0, 374761393) ^ Math.imul(y | 0, 668265263) ^ Math.imul(seed, 2246822519);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}

/** Seeded PRNG for per-chunk generation. */
export function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6D2B79F5) >>> 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export const chunkKey = (cx, cz) => `${cx},${cz}`;
export const toChunk = (v) => Math.floor(v / CHUNK);
