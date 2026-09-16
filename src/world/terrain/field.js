/**
 * field.js - the analytic world model. One function, y = f(x, z), plus the
 * classification that turns a point into a surface and a biome.
 *
 * WHY ANALYTIC: the vehicle does ~40 wheel queries a frame and wildlife does
 * more. Raycasting the rendered mesh would mean a BVH, a hit at LOD-dependent
 * accuracy, and nothing at all outside the resident chunks. Evaluating the
 * noise directly is faster, works everywhere in an unbounded world, and gives
 * scatter and physics the identical number the mesh was built from - the chunk
 * builder calls exactly these functions.
 *
 * COST CONTROL: a full evaluation is ~16 gradient-noise lookups. Roughly half
 * the terms (continental roll, massif mask, river network, bog mask) have
 * wavelengths of 500 m and up, so sampling them per-metre is waste. Those four
 * live in a cached 8 m lattice (`lowAt`) that is bilinearly interpolated, which
 * cuts a height query to ~11 lookups and, crucially, is shared by the mesh
 * builder and the physics query so the two can never disagree.
 *
 * THE TERRAIN, in the order it is assembled:
 *   1. continental roll      broad fBm, the 1.5 km scale of the place
 *   2. forest hills          domain-warped fBm, meandering 200 m ridgelines
 *   3. mountains             ridged multifractal, masked to massifs, to ~180 m
 *   4. bog basins            lerp a low, flat area to a constant level
 *   5. river valleys         V-cut toward the zero contour of a warped noise
 *   6. detail                small stuff, suppressed on bog and river beds
 */
import { hash2, SURFACE, BIOME } from '../contract.js';
import { perlin2, fbm, ridged, smoothstep, clamp01, lerp } from './noise.js';

/* --------------------------------------------------------------- tuning --- */

const ROLL_AMP = 21;        // metres of continental up-and-down
const ROLL_MID = 24;        // mean ground level, metres
const HILL_AMP = 20;        // forest hill relief
const MTN_AMP = 172;        // peak ridge height above the roll
const MAX_CUT = 74;         // deepest a river is allowed to carve, metres
const BED_HALF = 9;         // half-width of the flat gravel bed, metres
const RIVER_SCALE = 430;    // noise units -> metres for the channel distance field

// Low-field lattice. 8 m is fine enough that a 20 m wide river bed is still
// resolved, coarse enough that one node serves 64 height queries.
const LOW_STEP = 8;
const LOW_TILE = 32;                    // nodes per tile side -> 256 m tiles
const LOW_STRIDE = LOW_TILE + 1;        // +1 so bilinear never needs a neighbour tile
const LOW_TILE_LEN = LOW_STRIDE * LOW_STRIDE * 4;
const LOW_MAX_TILES = 600;

/**
 * Build the world model for one seed.
 *
 * Per-instance rather than module-global so two terrains (say the game and a
 * preview) cannot poison each other's caches.
 */
export function createField(seed = 1337) {
  const S = seed | 0;
  const tiles = new Map();
  const low = new Float32Array(4);        // scratch for heightRaw/compute
  const low2 = new Float32Array(4);       // scratch for classify - never nested with `low`

  /* ------------------------------------------------------- low-frequency --- */

  function buildTile(tx, tz) {
    const data = new Float32Array(LOW_TILE_LEN);
    const ox = tx * LOW_TILE, oz = tz * LOW_TILE;
    let p = 0;
    for (let j = 0; j <= LOW_TILE; j++) {
      const z = (oz + j) * LOW_STEP;
      for (let i = 0; i <= LOW_TILE; i++) {
        const x = (ox + i) * LOW_STEP;

        // Continental roll - which part of the map is high ground at all.
        data[p] = fbm(x * 0.00068, z * 0.00068, S + 11, 3, 2.1, 0.5);

        // Massif mask - mountains only exist where this is high, so the world
        // gets distinct ranges with forest between them instead of uniform
        // corrugation everywhere.
        data[p + 1] = fbm(x * 0.00045, z * 0.00045, S + 23, 2, 2.0, 0.5);

        // River network. The channels are the zero contour of a warped noise
        // field, so |n| is (approximately, and that is good enough) the
        // distance to the nearest watercourse. Warping first makes them
        // meander and occasionally braid instead of running in smooth arcs.
        const rx = x + perlin2(x * 0.00063, z * 0.00063, S + 31) * 330;
        const rz = z + perlin2(x * 0.00063 + 53.1, z * 0.00063 - 27.7, S + 32) * 330;
        data[p + 2] = Math.abs(fbm(rx * 0.00131, rz * 0.00131, S + 33, 2, 2.0, 0.45)) * RIVER_SCALE;

        // Bog mask - where the flat wet basins want to be.
        data[p + 3] = fbm(x * 0.00192, z * 0.00192, S + 41, 2, 2.0, 0.5) * 0.5 + 0.5;

        p += 4;
      }
    }
    return data;
  }

  function getTile(tx, tz) {
    // Numeric key beats a template string here: this runs once per height query.
    const key = (tx + 0x8000) * 0x10000 + (tz + 0x8000);
    let t = tiles.get(key);
    if (t === undefined) {
      if (tiles.size >= LOW_MAX_TILES) {
        // Insertion order is a decent stand-in for recency when the player is
        // driving in one direction, which is the only case that ever fills this.
        let n = LOW_MAX_TILES >> 2;
        for (const k of tiles.keys()) { tiles.delete(k); if (--n <= 0) break; }
      }
      t = buildTile(tx, tz);
      tiles.set(key, t);
    }
    return t;
  }

  /** Bilinear sample of the cached low-frequency lattice into `out` (4 channels). */
  function lowAt(x, z, out) {
    const fx = x / LOW_STEP, fz = z / LOW_STEP;
    const nx = Math.floor(fx), nz = Math.floor(fz);
    const u = fx - nx, v = fz - nz;
    const tx = Math.floor(nx / LOW_TILE), tz = Math.floor(nz / LOW_TILE);
    const t = getTile(tx, tz);
    const i = nx - tx * LOW_TILE, j = nz - tz * LOW_TILE;
    const a = (j * LOW_STRIDE + i) * 4;
    const b = a + 4;                       // +x
    const c = a + LOW_STRIDE * 4;          // +z
    const d = c + 4;                       // +x +z
    for (let k = 0; k < 4; k++) {
      const top = t[a + k] + (t[b + k] - t[a + k]) * u;
      const bot = t[c + k] + (t[d + k] - t[c + k]) * u;
      out[k] = top + (bot - top) * v;
    }
    return out;
  }

  /* --------------------------------------------------------- height model -- */

  /**
   * Full evaluation. Fills `o` with the height plus the intermediate terms the
   * surface classifier needs, so a caller that wants both does not pay twice.
   *
   * @param {{h:number,bed:number,mtn:number,rivN:number,bogW:number,flat:number,water:boolean}} o
   */
  function compute(x, z, o) {
    lowAt(x, z, low);
    const roll = low[0], massif = low[1], rivD = low[2], bogRaw = low[3];

    const mtn = smoothstep(0.02, 0.56, massif);

    // 1. continental roll
    let h = ROLL_MID + roll * ROLL_AMP;

    // 2. forest hills. The warp is the whole trick: plain fBm gives blobs,
    //    warped fBm gives ridgelines that wander and spurs that run off them.
    const wx = x + perlin2(x * 0.0037, z * 0.0037, S + 61) * 105;
    const wz = z + perlin2(x * 0.0037 + 41.7, z * 0.0037 - 19.3, S + 62) * 105;
    h += fbm(wx * 0.0052, wz * 0.0052, S + 63, 4, 2.07, 0.5) * HILL_AMP;

    // 3. mountains. mtn^1.3 rather than mtn keeps the massif edges from
    //    erupting out of the forest - the range gains height over ~600 m of
    //    approach, which is what leaves a drivable flank to get up onto.
    if (mtn > 0.002) {
      h += Math.pow(mtn, 1.3) * ridged(x * 0.00088, z * 0.00088, S + 71, 5, 2.04, 0.46) * MTN_AMP;
    }

    // Drainage level: where water would end up here. Low in the flats, high in
    // the ranges, because a mountain stream is not at sea level.
    const bed = 0.5 + (roll * 0.5 + 0.5) * 3.4 + Math.pow(mtn, 1.25) * 44;

    // 4. bogs. Being "flat" is not left to chance - the terrain is lerped to a
    //    constant level, so whatever relief was there is erased. They are
    //    gated to genuinely low, genuinely non-mountain ground so they read as
    //    basins the water drained into rather than plateaus.
    const bogW = smoothstep(0.575, 0.735, bogRaw)
      * (1 - smoothstep(19, 46, h))
      * (1 - smoothstep(0.02, 0.24, mtn));
    if (bogW > 0.004) h += ((bed + 2.6) - h) * (bogW * 0.96);

    // 5. river valleys. Width scales with depth (2.25:1) so a 70 m cut opens a
    //    ~190 m valley - about 20 degrees of flank, inside the 30-35 the truck
    //    can climb. Fixed-width channels gave vertical-sided slots in the hills.
    let rivN = 0, flat = bogW, water = false;
    const drop = h - bed;
    if (drop > 0.4) {
      const cut = drop < MAX_CUT ? drop : MAX_CUT;
      const W = 26 + 2.25 * cut;
      const t = rivD / W;
      if (t < 1) {
        const bedFrac = BED_HALF / W < 0.4 ? BED_HALF / W : 0.4;
        let s, bedFlat;
        if (t <= bedFrac) {
          s = 1; bedFlat = 1;
        } else {
          const u = (t - bedFrac) / (1 - bedFrac);
          s = 1 - u * u * (3 - 2 * u);
          bedFlat = 1 - smoothstep(0, 0.25, u);
        }
        s = Math.pow(s, 0.8);            // fills out the V so flanks are concave, not conical
        h -= cut * s;
        rivN = s;
        if (bedFlat > flat) flat = bedFlat;

        // A narrow notch inside the flat bed. Without it the "river" is a
        // smooth trough you cannot tell from a dry valley; with it there is an
        // obvious channel to ford or follow.
        const wi = 11 + 0.1 * cut;
        if (rivD < wi) {
          const ti = rivD / wi;
          h -= (1 - ti * ti) * 2.4;
          water = ti < 0.75;
        }
      }
    }

    // 6. detail. Suppressed on bogs and river beds, stronger up high where
    //    rock is broken - flat ground has to stay flat to read as flat.
    const calm = 1 - 0.92 * flat;
    h += fbm(x * 0.0195, z * 0.0195, S + 81, 3, 2.1, 0.48) * 2.9 * calm * (0.45 + 0.75 * mtn);
    h += fbm(x * 0.085, z * 0.085, S + 82, 2, 2.0, 0.5) * 0.42 * calm;

    o.h = h; o.bed = bed; o.mtn = mtn; o.rivN = rivN; o.bogW = bogW;
    o.flat = flat; o.water = water;
    return o;
  }

  const scratch = { h: 0, bed: 0, mtn: 0, rivN: 0, bogW: 0, flat: 0, water: false };
  function heightRaw(x, z) { return compute(x, z, scratch).h; }

  /* --------------------------------------------------------- memo cache ---- */

  // Direct-mapped, exact-key cache. Physics asks for the same point more than
  // once per frame (height, then sample, then a normal difference), and a
  // sample() costs five evaluations on its own. Keys are compared exactly, so
  // this can never introduce a quantisation error into the height.
  const MEMO = 1 << 13, MEMO_MASK = MEMO - 1;
  const mkx = new Float64Array(MEMO).fill(NaN);
  const mkz = new Float64Array(MEMO);
  const mval = new Float64Array(MEMO);

  function height(x, z) {
    const i = (Math.imul(Math.round(x * 32) | 0, 374761393)
      ^ Math.imul(Math.round(z * 32) | 0, 668265263)) & MEMO_MASK;
    if (mkx[i] === x && mkz[i] === z) return mval[i];
    const h = heightRaw(x, z);
    mkx[i] = x; mkz[i] = z; mval[i] = h;
    return h;
  }

  /* ----------------------------------------------------- classification ---- */

  // Linear-ish colours. They are multiplied by procedural detail in the shader,
  // so they sit a little brighter and flatter than the final look.
  const C = {
    water:  [0.055, 0.105, 0.105],
    gravel: [0.235, 0.215, 0.180],
    rock:   [0.150, 0.147, 0.138],
    scree:  [0.205, 0.192, 0.172],
    mud:    [0.072, 0.056, 0.034],
    grass:  [0.105, 0.150, 0.052],
    loam:   [0.062, 0.082, 0.036],
    dirt:   [0.135, 0.100, 0.055],
    snow:   [0.760, 0.800, 0.860],
  };

  /** Where the treeline sits here - jittered so scatter never gets a contour line. */
  function veg(x, z) { return fbm(x * 0.0031, z * 0.0031, S + 91, 2, 2.0, 0.5); }

  /**
   * Turn a computed point into a surface, a biome and a vertex colour.
   * `ny` is the ground normal's Y - slope beats altitude, always: a cliff face
   * in a meadow is still rock.
   */
  function classifyFrom(x, z, f, ny, out) {
    const h = f.h;
    const v = veg(x, z);
    const wet = clamp01(Math.max(f.bogW * 0.95, f.rivN * f.rivN));
    // 0 below ~26 degrees, 1 above ~42 - drives the triplanar rock overlay.
    const rock = smoothstep(0.90, 0.74, ny);
    const snowLine = 150 + v * 22;
    const snow = smoothstep(snowLine, snowLine + 26, h) * smoothstep(0.62, 0.80, ny);

    let surf, biome, col;
    if (f.water && ny > 0.80) {
      surf = SURFACE.WATER.id; biome = BIOME.RIVERBED.id; col = C.water;
    } else if (ny < 0.74) {
      surf = SURFACE.ROCK.id; col = C.rock;
      biome = h > 148 ? BIOME.ALPINE.id : h > 88 ? BIOME.SCREE.id : BIOME.PINE.id;
    } else if (ny < 0.87) {
      surf = SURFACE.GRAVEL.id; col = C.scree;
      biome = h > 148 ? BIOME.ALPINE.id : h > 88 ? BIOME.SCREE.id
        : h > 26 + v * 18 ? BIOME.PINE.id : BIOME.MEADOW.id;
    } else if (f.rivN > 0.42) {
      surf = SURFACE.GRAVEL.id; biome = BIOME.RIVERBED.id; col = C.gravel;
    } else if (f.bogW > 0.45) {
      surf = SURFACE.MUD.id; biome = BIOME.MUDFLAT.id; col = C.mud;
    } else if (snow > 0.5) {
      surf = SURFACE.SNOW.id; biome = BIOME.ALPINE.id; col = C.snow;
    } else if (h > 96 + v * 14) {
      surf = SURFACE.GRAVEL.id; biome = BIOME.SCREE.id; col = C.scree;
    } else if (h > 26 + v * 18) {
      surf = SURFACE.LOAM.id; biome = BIOME.PINE.id; col = C.loam;
    } else if (h < 3) {
      surf = SURFACE.GRAVEL.id; biome = BIOME.RIVERBED.id; col = C.gravel;
    } else {
      surf = SURFACE.GRASS.id; biome = BIOME.MEADOW.id; col = C.grass;
    }

    // Blend rather than switch at the meadow/forest and rock/scree seams, and
    // let snow creep down the gentle ground above the line.
    let r = col[0], g = col[1], b = col[2];
    if (surf === SURFACE.GRASS.id || surf === SURFACE.LOAM.id) {
      const t = smoothstep(18 + v * 16, 38 + v * 20, h);
      r = lerp(C.grass[0], C.loam[0], t); g = lerp(C.grass[1], C.loam[1], t); b = lerp(C.grass[2], C.loam[2], t);
      const dry = smoothstep(0.4, 0.0, ny > 0.93 ? 1 : 0);  // benches pick up dirt
      r = lerp(r, C.dirt[0], dry * 0.15); g = lerp(g, C.dirt[1], dry * 0.15); b = lerp(b, C.dirt[2], dry * 0.15);
    }
    if (snow > 0.001) {
      r = lerp(r, C.snow[0], snow); g = lerp(g, C.snow[1], snow); b = lerp(b, C.snow[2], snow);
    }
    // Per-point tint break-up. A hash, not noise: it costs nothing and the
    // shader's macro texture supplies the large-scale variation.
    const j = 0.94 + hash2(Math.round(x * 0.5), Math.round(z * 0.5), S + 7) * 0.12;
    out.r = r * j; out.g = g * j; out.b = b * j;
    out.surface = surf; out.biome = biome;
    out.rock = rock * (1 - snow * 0.7);
    out.wet = wet;
    out.snow = snow;
    return out;
  }

  const cScratch = { h: 0, bed: 0, mtn: 0, rivN: 0, bogW: 0, flat: 0, water: false };
  const cOut = { r: 0, g: 0, b: 0, surface: 0, biome: 0, rock: 0, wet: 0, snow: 0 };

  function classify(x, z, ny, out = cOut) {
    // classify() from the outside has no computed point to reuse - do it here,
    // into a scratch that compute()'s own `low` buffer never aliases.
    lowAt(x, z, low2);
    return classifyFrom(x, z, compute(x, z, cScratch), ny, out);
  }

  return {
    seed: S,
    compute,
    heightRaw,
    height,
    classify,
    classifyFrom,
    veg,
    newPoint: () => ({ h: 0, bed: 0, mtn: 0, rivN: 0, bogW: 0, flat: 0, water: false }),
    newClass: () => ({ r: 0, g: 0, b: 0, surface: 0, biome: 0, rock: 0, wet: 0, snow: 0 }),
    clearCaches() { tiles.clear(); mkx.fill(NaN); },
    stats: () => ({ lowTiles: tiles.size }),
  };
}
