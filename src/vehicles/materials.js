/**
 * materials.js - the vehicle material palette.
 *
 * Materials are shared instances (one rubber material for every tire in the
 * scene) so the renderer can batch them. Anything that needs per-vehicle
 * variation - body colour, mud level - is cloned through makePaint()/dirty().
 */
import * as THREE from 'three';
import {
  paintNormal, rubberNormal, metalRoughness,
  plasticNormal, diamondPlateNormal, meshAlpha, mudMask,
} from './procgen.js';

const shared = new Map();
const once = (key, fn) => {
  if (!shared.has(key)) shared.set(key, fn());
  return shared.get(key);
};

/** Two-layer automotive paint: metallic base + clearcoat with orange peel. */
export function makePaint(color, { metallic = 0.75, flake = true, matte = false } = {}) {
  const m = new THREE.MeshPhysicalMaterial({
    color: new THREE.Color(color),
    metalness: matte ? 0.1 : metallic,
    roughness: matte ? 0.72 : 0.28,
    clearcoat: matte ? 0.1 : 1.0,
    clearcoatRoughness: matte ? 0.6 : 0.055,
    normalMap: flake ? paintNormal() : null,
    normalScale: new THREE.Vector2(0.14, 0.14),
    envMapIntensity: 1.15,
  });
  m.name = 'paint';
  return m;
}

export const rubber = () => once('rubber', () => new THREE.MeshStandardMaterial({
  color: 0x14151a,
  roughness: 0.92,
  metalness: 0.0,
  normalMap: rubberNormal(),
  normalScale: new THREE.Vector2(0.85, 0.85),
  envMapIntensity: 0.45,
}));

/** Tread face - scuffed lighter than the sidewall from contact with the ground. */
export const treadRubber = () => once('treadRubber', () => new THREE.MeshStandardMaterial({
  color: 0x1c1d22,
  roughness: 0.98,
  metalness: 0.0,
  normalMap: rubberNormal(),
  normalScale: new THREE.Vector2(0.5, 0.5),
  envMapIntensity: 0.3,
}));

export const rawSteel = () => once('rawSteel', () => new THREE.MeshStandardMaterial({
  color: 0x9aa0a8,
  metalness: 1.0,
  roughness: 0.42,
  roughnessMap: metalRoughness(),
  envMapIntensity: 1.0,
}));

export const blackSteel = () => once('blackSteel', () => new THREE.MeshStandardMaterial({
  color: 0x2a2c30,
  metalness: 0.92,
  roughness: 0.48,
  roughnessMap: metalRoughness(),
  envMapIntensity: 0.8,
}));

/** Powder-coated tube - bumpers, cages, sliders. Slightly soft sheen. */
export const powderCoat = (color = 0x1a1c20) => new THREE.MeshStandardMaterial({
  color: new THREE.Color(color),
  metalness: 0.35,
  roughness: 0.58,
  normalMap: plasticNormal(),
  normalScale: new THREE.Vector2(0.3, 0.3),
  envMapIntensity: 0.75,
});

export const castIron = () => once('castIron', () => new THREE.MeshStandardMaterial({
  color: 0x4e5258,
  metalness: 0.85,
  roughness: 0.74,
  roughnessMap: metalRoughness(),
  envMapIntensity: 0.6,
}));

/** Coilover springs, winch line guides - bright zinc. */
export const zinc = () => once('zinc', () => new THREE.MeshStandardMaterial({
  color: 0xc8ccd2,
  metalness: 1.0,
  roughness: 0.25,
  roughnessMap: metalRoughness(),
  envMapIntensity: 1.3,
}));

export const chrome = () => once('chrome', () => new THREE.MeshStandardMaterial({
  color: 0xf2f4f8,
  metalness: 1.0,
  roughness: 0.06,
  envMapIntensity: 1.6,
}));

/** Textured black trim: flares, bumper caps, mirror housings. */
export const trimPlastic = () => once('trimPlastic', () => new THREE.MeshStandardMaterial({
  color: 0x232529,
  metalness: 0.0,
  roughness: 0.86,
  normalMap: plasticNormal(),
  normalScale: new THREE.Vector2(0.75, 0.75),
  envMapIntensity: 0.5,
}));

export const bedliner = () => once('bedliner', () => new THREE.MeshStandardMaterial({
  color: 0x1b1d21,
  metalness: 0.05,
  roughness: 0.95,
  normalMap: plasticNormal(),
  normalScale: new THREE.Vector2(1.3, 1.3),
  envMapIntensity: 0.35,
}));

export const diamondPlate = () => once('diamondPlate', () => new THREE.MeshStandardMaterial({
  color: 0x8d939b,
  metalness: 0.95,
  roughness: 0.45,
  normalMap: diamondPlateNormal(),
  normalScale: new THREE.Vector2(1.0, 1.0),
  envMapIntensity: 0.95,
}));

/**
 * Tinted laminated glass.
 *
 * Deliberately NOT using `transmission`: it forces three into an extra
 * full-scene render pass per frame, and against a bright environment it
 * blows out to white anyway. A dark tinted alpha blend reads more like
 * automotive glass and costs nothing - which matters once a forest full of
 * trees is behind it.
 *
 * @param {number} tint 0 = near clear, 1 = limo.
 */
export function glass(tint = 0.12) {
  return new THREE.MeshPhysicalMaterial({
    color: new THREE.Color(0x141c24),
    metalness: 0.0,
    roughness: 0.055,
    clearcoat: 1.0,
    clearcoatRoughness: 0.04,
    transparent: true,
    opacity: 0.38 + tint * 0.5,
    envMapIntensity: 0.85,
    side: THREE.DoubleSide,
    depthWrite: false,
  });
}

/** Cheap glass for LOD1+ and for mirrors, where refraction is wasted. */
export const glassCheap = () => once('glassCheap', () => new THREE.MeshPhysicalMaterial({
  color: 0x151c22,
  metalness: 0.1,
  roughness: 0.1,
  transparent: true,
  opacity: 0.62,
  envMapIntensity: 0.7,
  side: THREE.DoubleSide,
  depthWrite: false,
}));

/** Lamp lenses. `on` drives emissive so we can switch headlights at night. */
export function lens(color = 0xfff0d8, on = false, intensity = 3.2) {
  const m = new THREE.MeshPhysicalMaterial({
    color: new THREE.Color(color),
    metalness: 0.0,
    roughness: 0.12,
    transparent: true,
    opacity: 0.88,
    emissive: new THREE.Color(color),
    emissiveIntensity: on ? intensity : 0.0,
    envMapIntensity: 1.4,
  });
  m.userData.onIntensity = intensity;
  return m;
}

/** Reflector bowl behind a lens. */
export const reflector = () => once('reflector', () => new THREE.MeshStandardMaterial({
  color: 0xffffff,
  metalness: 1.0,
  roughness: 0.14,
  side: THREE.BackSide,
  envMapIntensity: 2.0,
}));

export const grilleMesh = () => once('grilleMesh', () => new THREE.MeshStandardMaterial({
  color: 0x17191c,
  metalness: 0.9,
  roughness: 0.55,
  alphaMap: meshAlpha(),
  transparent: true,
  alphaTest: 0.5,
  side: THREE.DoubleSide,
}));

export const seatFabric = () => once('seatFabric', () => new THREE.MeshPhysicalMaterial({
  color: 0x2e2a26,
  roughness: 0.95,
  metalness: 0.0,
  sheen: 0.6,
  sheenRoughness: 0.85,
  sheenColor: new THREE.Color(0x6b5f52),
  normalMap: plasticNormal(),
  normalScale: new THREE.Vector2(0.5, 0.5),
  envMapIntensity: 0.4,
}));

export const dashPlastic = () => once('dashPlastic', () => new THREE.MeshStandardMaterial({
  color: 0x1e2024,
  roughness: 0.9,
  metalness: 0.0,
  normalMap: plasticNormal(),
  normalScale: new THREE.Vector2(0.6, 0.6),
  envMapIntensity: 0.35,
}));

export const dryMud = () => once('dryMud', () => new THREE.MeshStandardMaterial({
  color: 0x6b5438,
  roughness: 1.0,
  metalness: 0.0,
  normalMap: plasticNormal(),
  normalScale: new THREE.Vector2(1.6, 1.6),
  envMapIntensity: 0.25,
}));

/** Free every shared material. Call on teardown. */
export function disposeMaterials() {
  for (const m of shared.values()) m.dispose?.();
  shared.clear();
}

/* ------------------------------------------------------------------ mud ---- */

/**
 * Make a material accumulate mud.
 *
 * Dirt is driven by height in VEHICLE space, so it cakes on low (sills,
 * arches, bumpers, tires) and thins out toward the roof. It deliberately is
 * not object space: every part is modelled around its own local origin, so
 * object-space Y is ~0 for the roof and the sill alike and the whole truck
 * would cake uniformly.
 *
 * Getting there needs the vehicle root's inverse world matrix, which the
 * caller feeds in each frame via the returned `inv` uniform.
 *
 * @returns {{amount: {value:number}, inv: {value:THREE.Matrix4}}}
 */
export function applyMud(material, { lowY = 0.2, falloff = 0.85 } = {}) {
  if (material.userData.mudUniform) return material.userData.mudUniform;
  const u = { value: 0 };
  const inv = { value: new THREE.Matrix4() };
  const mask = { value: mudMask() };
  const handle = { amount: u, inv };
  material.userData.mudUniform = handle;

  material.onBeforeCompile = (shader) => {
    shader.uniforms.uMud = u;
    shader.uniforms.uMudInv = inv;
    shader.uniforms.uMudMask = mask;
    shader.uniforms.uMudLowY = { value: lowY };
    shader.uniforms.uMudFalloff = { value: falloff };

    shader.vertexShader = 'varying vec3 vMudPos;\nuniform mat4 uMudInv;\n'
      + shader.vertexShader.replace(
        '#include <begin_vertex>',
        '#include <begin_vertex>\n'
        + '  vMudPos = (uMudInv * modelMatrix * vec4(transformed, 1.0)).xyz;',
      );

    shader.fragmentShader = `
uniform float uMud;
uniform float uMudLowY;
uniform float uMudFalloff;
uniform sampler2D uMudMask;
varying vec3 vMudPos;
float mudAmount() {
  // Triplanar-ish lookup: mud doesn't follow the panel's UVs, it follows the
  // shape, so sample in object space and blend the dominant axes.
  vec2 uvA = vMudPos.xz * 0.6;
  vec2 uvB = vMudPos.zy * 0.6;
  float m = mix(texture2D(uMudMask, uvA).r, texture2D(uMudMask, uvB).r, 0.5);
  float h = clamp(1.0 - (vMudPos.y - uMudLowY) * uMudFalloff, 0.0, 1.0);
  return clamp(uMud * h * (0.35 + m * 1.15), 0.0, 1.0);
}
` + shader.fragmentShader
      .replace('#include <map_fragment>', `#include <map_fragment>
  float mudA = mudAmount();
  diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.243, 0.180, 0.110), mudA);`)
      .replace('#include <roughnessmap_fragment>', `#include <roughnessmap_fragment>
  roughnessFactor = mix(roughnessFactor, 0.97, mudAmount());`)
      .replace('#include <metalnessmap_fragment>', `#include <metalnessmap_fragment>
  metalnessFactor = mix(metalnessFactor, 0.0, mudAmount());`);
  };

  // Without this, three reuses the unpatched program from its shader cache.
  material.customProgramCacheKey = () => 'mudded';
  material.needsUpdate = true;
  return handle;
}
