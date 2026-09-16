/**
 * procgen.js - canvas-based procedural textures.
 *
 * We ship zero image assets, so every map here is drawn at runtime into an
 * OffscreenCanvas and uploaded once. Everything is cached by key: a tread
 * normal map costs ~4ms to draw and we reuse it across every wheel in the game.
 */
import * as THREE from 'three';

const cache = new Map();

function canvas(size) {
  const c = typeof OffscreenCanvas !== 'undefined'
    ? new OffscreenCanvas(size, size)
    : Object.assign(document.createElement('canvas'), { width: size, height: size });
  return { c, ctx: c.getContext('2d', { willReadFrequently: true }) };
}

function finish(c, { repeat = 1, srgb = false, aniso = 8 } = {}) {
  const tex = new THREE.CanvasTexture(c);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(repeat, repeat);
  tex.anisotropy = aniso;
  if (srgb) tex.colorSpace = THREE.SRGBColorSpace;
  tex.needsUpdate = true;
  return tex;
}

function memo(key, fn) {
  if (!cache.has(key)) cache.set(key, fn());
  return cache.get(key);
}

/* ---------------------------------------------------------------- noise ---- */

// Value noise with a fixed permutation - deterministic across reloads so a
// vehicle always looks the same between sessions.
const PERM = new Uint8Array(512);
(() => {
  let seed = 1337;
  const p = new Uint8Array(256);
  for (let i = 0; i < 256; i++) p[i] = i;
  for (let i = 255; i > 0; i--) {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    const j = seed % (i + 1);
    [p[i], p[j]] = [p[j], p[i]];
  }
  for (let i = 0; i < 512; i++) PERM[i] = p[i & 255];
})();

const fade = (t) => t * t * t * (t * (t * 6 - 15) + 10);
const lerp = (a, b, t) => a + (b - a) * t;

function valueNoise2D(x, y) {
  const xi = Math.floor(x) & 255, yi = Math.floor(y) & 255;
  const xf = x - Math.floor(x), yf = y - Math.floor(y);
  const u = fade(xf), v = fade(yf);
  const h = (a, b) => PERM[(PERM[a] + b) & 511] / 255;
  return lerp(
    lerp(h(xi, yi), h(xi + 1, yi), u),
    lerp(h(xi, yi + 1), h(xi + 1, yi + 1), u),
    v,
  );
}

export function fbm2D(x, y, octaves = 4, lacunarity = 2, gain = 0.5) {
  let sum = 0, amp = 1, freq = 1, norm = 0;
  for (let i = 0; i < octaves; i++) {
    sum += valueNoise2D(x * freq, y * freq) * amp;
    norm += amp;
    amp *= gain;
    freq *= lacunarity;
  }
  return sum / norm;
}

/** Turn a grayscale height field into a tangent-space normal map. */
function heightToNormal(ctx, size, strength = 2.0) {
  const src = ctx.getImageData(0, 0, size, size);
  const out = ctx.createImageData(size, size);
  const at = (x, y) => src.data[((y & (size - 1)) * size + (x & (size - 1))) * 4] / 255;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx = (at(x + 1, y) - at(x - 1, y)) * strength;
      const dy = (at(x, y + 1) - at(x, y - 1)) * strength;
      const len = Math.hypot(dx, dy, 1);
      const i = (y * size + x) * 4;
      out.data[i] = ((-dx / len) * 0.5 + 0.5) * 255;
      out.data[i + 1] = ((-dy / len) * 0.5 + 0.5) * 255;
      out.data[i + 2] = (1 / len * 0.5 + 0.5) * 255;
      out.data[i + 3] = 255;
    }
  }
  ctx.putImageData(out, 0, 0);
}

/* ------------------------------------------------------------- textures ---- */

/** Fine orange-peel in the clearcoat. Subtle, but it kills the "plastic toy" look. */
export function paintNormal() {
  return memo('paintNormal', () => {
    const size = 512;
    const { c, ctx } = canvas(size);
    const img = ctx.createImageData(size, size);
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const n = fbm2D(x * 0.22, y * 0.22, 3);
        const v = 118 + n * 34;
        const i = (y * size + x) * 4;
        img.data[i] = img.data[i + 1] = img.data[i + 2] = v;
        img.data[i + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    heightToNormal(ctx, size, 0.6);
    return finish(c, { repeat: 6 });
  });
}

/** Sidewall rubber: fine radial grain plus moulding pits. */
export function rubberNormal() {
  return memo('rubberNormal', () => {
    const size = 512;
    const { c, ctx } = canvas(size);
    const img = ctx.createImageData(size, size);
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        let n = fbm2D(x * 0.5, y * 0.5, 4) * 0.7;
        n += fbm2D(x * 3.1, y * 3.1, 2) * 0.3;
        const i = (y * size + x) * 4;
        const v = 90 + n * 110;
        img.data[i] = img.data[i + 1] = img.data[i + 2] = v;
        img.data[i + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    heightToNormal(ctx, size, 1.4);
    return finish(c, { repeat: 4 });
  });
}

/** Brushed/cast metal for skid plates, bumpers, diff housings. */
export function metalRoughness() {
  return memo('metalRoughness', () => {
    const size = 512;
    const { c, ctx } = canvas(size);
    const img = ctx.createImageData(size, size);
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        // Stretched on X so it reads as a brushed/extruded direction.
        const n = fbm2D(x * 0.08, y * 1.9, 3);
        const blotch = fbm2D(x * 0.05, y * 0.05, 3);
        const v = 120 + n * 60 + blotch * 50;
        const i = (y * size + x) * 4;
        img.data[i] = img.data[i + 1] = img.data[i + 2] = Math.min(255, v);
        img.data[i + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    return finish(c, { repeat: 2 });
  });
}

/** Pebbled ABS / bedliner finish used on trim, flares and the tub. */
export function plasticNormal() {
  return memo('plasticNormal', () => {
    const size = 512;
    const { c, ctx } = canvas(size);
    ctx.fillStyle = '#808080';
    ctx.fillRect(0, 0, size, size);
    let seed = 99;
    const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296);
    for (let i = 0; i < 9000; i++) {
      const r = 1 + rnd() * 3.2;
      const v = Math.floor(120 + rnd() * 120);
      ctx.fillStyle = `rgb(${v},${v},${v})`;
      ctx.beginPath();
      ctx.arc(rnd() * size, rnd() * size, r, 0, Math.PI * 2);
      ctx.fill();
    }
    heightToNormal(ctx, size, 1.1);
    return finish(c, { repeat: 8 });
  });
}

/**
 * Mud accumulation mask. White = caked, black = clean. Driven by an
 * altitude-ish gradient so mud collects low on the body and thins out upward,
 * which is how a real 4x4 dirties up.
 */
export function mudMask() {
  return memo('mudMask', () => {
    const size = 512;
    const { c, ctx } = canvas(size);
    const img = ctx.createImageData(size, size);
    for (let y = 0; y < size; y++) {
      // v=0 at the top of the UV, 1 at the bottom.
      const heightFalloff = Math.pow(y / size, 1.6);
      for (let x = 0; x < size; x++) {
        const splatter = fbm2D(x * 0.09, y * 0.09, 5);
        const fine = fbm2D(x * 0.6, y * 0.6, 3);
        let v = heightFalloff * 1.5 * (splatter * 0.75 + fine * 0.25);
        v = Math.max(0, Math.min(1, (v - 0.18) * 2.2));
        const i = (y * size + x) * 4;
        img.data[i] = img.data[i + 1] = img.data[i + 2] = v * 255;
        img.data[i + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    return finish(c, { repeat: 1 });
  });
}

/** Knurled/diamond-plate for rock sliders and bed floors. */
export function diamondPlateNormal() {
  return memo('diamondPlateNormal', () => {
    const size = 512;
    const { c, ctx } = canvas(size);
    ctx.fillStyle = '#6a6a6a';
    ctx.fillRect(0, 0, size, size);
    ctx.fillStyle = '#e8e8e8';
    const step = 64;
    for (let y = 0; y < size; y += step) {
      for (let x = 0; x < size; x += step) {
        const off = ((y / step) & 1) ? step / 2 : 0;
        for (const [dx, dy, rot] of [[0, 0, 0.6], [step / 2, step / 2, -0.6]]) {
          ctx.save();
          ctx.translate(x + off + dx + step / 4, y + dy + step / 4);
          ctx.rotate(rot);
          ctx.fillRect(-13, -4, 26, 8);
          ctx.restore();
        }
      }
    }
    ctx.filter = 'blur(2px)';
    ctx.drawImage(c, 0, 0);
    ctx.filter = 'none';
    heightToNormal(ctx, size, 2.6);
    return finish(c, { repeat: 3 });
  });
}

/** Perforated mesh alpha for grilles and lamp guards. */
export function meshAlpha() {
  return memo('meshAlpha', () => {
    const size = 256;
    const { c, ctx } = canvas(size);
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, size, size);
    ctx.fillStyle = '#fff';
    const step = 16;
    for (let y = step / 2; y < size; y += step) {
      for (let x = step / 2; x < size; x += step) {
        ctx.beginPath();
        ctx.arc(x + (((y / step) | 0) & 1 ? step / 2 : 0), y, 5.2, 0, Math.PI * 2);
        ctx.fill();
      }
    }
    return finish(c, { repeat: 1 });
  });
}

export function clearTextureCache() {
  for (const t of cache.values()) t.dispose?.();
  cache.clear();
}
