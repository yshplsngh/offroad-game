/**
 * pelt.js - coats, materials and the canvas textures behind them.
 *
 * A coat is not a colour, it is a direction: hair lies along the body, catches
 * a rim of light at the silhouette and goes matte in the middle. So every
 * animal here gets the same three things - a streaked normal map for hair
 * direction, sheen for the rim, and vertex colours for markings (rump patch,
 * white belly, dark stockings). Markings do more for reading an animal at 30 m
 * than any amount of surface detail, and they cost nothing at runtime.
 *
 * Everything is cached and SHARED. Sixty animals must not mean sixty
 * materials, or the renderer stops batching and the iGPU falls over.
 */
import * as THREE from 'three';

const cache = new Map();
const memo = (key, fn) => {
  if (!cache.has(key)) cache.set(key, fn());
  return cache.get(key);
};

function canvas(size) {
  const c = typeof OffscreenCanvas !== 'undefined'
    ? new OffscreenCanvas(size, size)
    : Object.assign(document.createElement('canvas'), { width: size, height: size });
  return { c, ctx: c.getContext('2d', { willReadFrequently: true }) };
}

/** Grayscale hair strokes -> tangent-space normal map. */
function strokesToNormal(size, strokes, strength) {
  const { c, ctx } = canvas(size);
  ctx.fillStyle = '#808080';
  ctx.fillRect(0, 0, size, size);
  let seed = 9176;
  const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
  ctx.lineCap = 'round';
  for (let i = 0; i < strokes; i++) {
    const x = rnd() * size;
    const y = rnd() * size;
    // Hair runs roughly along U (nose to tail once the loft UVs are applied),
    // with enough scatter that it never reads as corduroy.
    const ang = (rnd() - 0.5) * 0.7 + Math.PI / 2;
    const len = 6 + rnd() * 22;
    const bright = rnd() > 0.5;
    ctx.strokeStyle = bright ? 'rgba(255,255,255,0.32)' : 'rgba(0,0,0,0.32)';
    ctx.lineWidth = 0.7 + rnd() * 1.6;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.lineTo(x + Math.cos(ang) * len, y + Math.sin(ang) * len);
    ctx.stroke();
  }
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
  const tex = new THREE.CanvasTexture(c);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.anisotropy = 4;
  return tex;
}

export const furNormal = () => memo('furNormal', () => {
  const t = strokesToNormal(256, 2600, 2.4);
  t.repeat.set(4, 6);
  return t;
});

export const featherNormal = () => memo('featherNormal', () => {
  const t = strokesToNormal(128, 900, 3.2);
  t.repeat.set(3, 3);
  return t;
});

/**
 * A coat material. `tone` shifts the base colour so a herd is not clones -
 * three tones per species is enough variety and still only three materials.
 */
export function coat(color, { rough = 0.93, sheen = 0.55, sheenColor = 0xb09a7c, feather = false } = {}) {
  const key = `coat:${color}:${rough}:${sheen}:${sheenColor}:${feather}`;
  return memo(key, () => {
    const m = new THREE.MeshPhysicalMaterial({
      color: new THREE.Color(color),
      roughness: rough,
      metalness: 0.0,
      vertexColors: true,
      sheen,
      sheenRoughness: 0.72,
      sheenColor: new THREE.Color(sheenColor),
      normalMap: feather ? featherNormal() : furNormal(),
      normalScale: new THREE.Vector2(feather ? 0.5 : 0.75, feather ? 0.5 : 0.75),
      envMapIntensity: 0.55,
      flatShading: false,
    });
    m.name = 'coat';
    return m;
  });
}

/** Antler / horn / hoof / beak keratin - harder and shinier than hair. */
export const keratin = (color = 0x6a5a42) => memo(`keratin:${color}`, () => new THREE.MeshStandardMaterial({
  color: new THREE.Color(color),
  roughness: 0.62,
  metalness: 0.05,
  vertexColors: true,
  envMapIntensity: 0.7,
}));

/** Wet dark eye. Emissive-free but very smooth, so it always catches a highlight. */
export const eyeball = () => memo('eye', () => new THREE.MeshPhysicalMaterial({
  color: 0x0a0806,
  roughness: 0.08,
  metalness: 0.0,
  clearcoat: 1.0,
  clearcoatRoughness: 0.05,
  vertexColors: true,
  envMapIntensity: 1.6,
}));

export function disposePelt() {
  for (const v of cache.values()) v.dispose?.();
  cache.clear();
}
