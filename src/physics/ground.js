/**
 * ground.js - where is the ground under this wheel, and what is it made of.
 *
 * WHY NOT JUST RAYCAST THE MESH: the terrain is an analytic heightfield behind
 * `terrain.height()` / `terrain.sample()`. Asking the function is roughly two
 * orders of magnitude cheaper than pushing a ray through a triangle BVH, it is
 * exact rather than tessellation-limited, and it works on chunks that have not
 * been meshed yet. So the heightfield is the primary source and Rapier is only
 * consulted for things that are NOT the heightfield: trees, boulders, anything
 * the scatter layer dropped on top.
 *
 * The one wrinkle is that a suspension ray points along the body's down axis,
 * not world down, so the intersection with y = h(x, z) is not a one-liner. A
 * three-step fixed-point iteration converges to well under a millimetre for any
 * attitude a truck survives, and costs three height lookups.
 */
import { SURFACE, SURFACE_BY_ID } from '../world/contract.js';

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const DEFAULT_SURFACE = SURFACE.DIRT;

/** Reusable hit record. The probe never allocates. */
export function makeGroundHit() {
  return {
    hit: false,
    dist: Infinity,       // along the probe direction, to the ground surface
    px: 0, py: 0, pz: 0,  // contact point
    nx: 0, ny: 1, nz: 0,  // ground normal
    surface: DEFAULT_SURFACE,
    wetness: 0,
    solid: false,         // true when a Rapier collider (rock, log) was the hit
  };
}

/**
 * @param {object} deps
 * @param {import('../world/contract.js').TerrainProvider|null} deps.terrain
 * @param {object|null} deps.world   Rapier world, for the scatter fallback.
 * @param {object|null} deps.rapier  The RAPIER namespace (for Ray).
 * @param {object|null} deps.exclude Rigid body to ignore (our own chassis).
 */
export function createGroundProbe({ terrain, world, rapier, exclude }) {
  const sampleOut = { height: 0, nx: 0, ny: 1, nz: 0, surfaceId: DEFAULT_SURFACE.id, wetness: 0 };
  const ray = rapier ? new rapier.Ray({ x: 0, y: 0, z: 0 }, { x: 0, y: -1, z: 0 }) : null;

  // Flat-world fallback so the controller is testable with terrain = null.
  const height = terrain?.height
    ? (x, z) => terrain.height(x, z)
    : () => 0;
  const sample = terrain?.sample
    ? (x, z, out) => terrain.sample(x, z, out)
    : (x, z, out) => {
        out.height = 0; out.nx = 0; out.ny = 1; out.nz = 0;
        out.surfaceId = DEFAULT_SURFACE.id; out.wetness = 0;
        return out;
      };

  let useRapier = false;

  return {
    /** Turn the Rapier leg on only while obstacle colliders actually exist. */
    setSolidQueries(on) { useRapier = !!on && !!world && !!ray; },
    height,

    /**
     * Cast from `origin` along `dir` (unit, pointing roughly down) for at most
     * `maxDist` metres. Returns the shared `out` record.
     */
    probe(origin, dir, maxDist, out) {
      out.hit = false;
      out.dist = Infinity;
      out.solid = false;

      // --- heightfield leg: solve origin + dir*t == h(x(t), z(t)) ---------
      // Guard against a near-horizontal ray (truck on its side): the solve
      // degenerates, and a wheel pointing sideways has no business finding
      // ground a hundred metres away anyway.
      const down = -dir.y;
      if (down > 0.15) {
        let t = clamp((origin.y - height(origin.x, origin.z)) / down, 0, maxDist * 2);
        for (let i = 0; i < 3; i++) {
          const x = origin.x + dir.x * t;
          const z = origin.z + dir.z * t;
          const y = origin.y + dir.y * t;
          t = clamp(t + (y - height(x, z)) / down, 0, maxDist * 2);
        }
        if (t <= maxDist) {
          const x = origin.x + dir.x * t;
          const z = origin.z + dir.z * t;
          sample(x, z, sampleOut);
          out.hit = true;
          out.dist = t;
          out.px = x; out.py = sampleOut.height; out.pz = z;
          out.nx = sampleOut.nx; out.ny = sampleOut.ny; out.nz = sampleOut.nz;
          out.surface = SURFACE_BY_ID[sampleOut.surfaceId] ?? DEFAULT_SURFACE;
          out.wetness = sampleOut.wetness ?? 0;
        }
      }

      // --- solid leg: anything scatter put on top of the heightfield ------
      // Only a nearer hit wins, so driving over a boulder lifts the wheel but
      // a boulder buried under the ray's exit point is ignored.
      if (useRapier) {
        ray.origin.x = origin.x; ray.origin.y = origin.y; ray.origin.z = origin.z;
        ray.dir.x = dir.x; ray.dir.y = dir.y; ray.dir.z = dir.z;
        const limit = Math.min(maxDist, out.hit ? out.dist : maxDist);
        const hit = world.castRayAndGetNormal(ray, limit, true, undefined, undefined, undefined, exclude);
        if (hit && hit.timeOfImpact < limit) {
          const t = hit.timeOfImpact;
          out.hit = true;
          out.dist = t;
          out.px = origin.x + dir.x * t;
          out.py = origin.y + dir.y * t;
          out.pz = origin.z + dir.z * t;
          const n = hit.normal;
          // A ray that starts inside a shape can report a flipped normal.
          const s = n.y < 0 ? -1 : 1;
          out.nx = n.x * s; out.ny = n.y * s; out.nz = n.z * s;
          out.solid = true;
          // Rock and deadfall are hard and grippy whatever the ground under them.
          out.surface = SURFACE.ROCK;
          out.wetness = 0;
        }
      }

      return out;
    },
  };
}
