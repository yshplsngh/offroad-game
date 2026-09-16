/**
 * scatter/material.js - one material for the entire forest.
 *
 * Every species bakes bark, needle and moss tones into vertex colours, so a
 * single MeshStandardMaterial with `vertexColors` draws all of it. What this
 * file adds is wind, in the vertex shader, for free.
 *
 * THE WIND TRICK: `aFlex` is baked per vertex - 0 at a root, 1 at a needle tip.
 * Displacing along a world-space wind vector scaled by aFlex bends the whole
 * plant about its base without a bone, a texture or a CPU update. Two sine
 * waves at incommensurate frequencies give gusts that never visibly loop.
 *
 * The displacement has to happen in OBJECT space, but the wind is a WORLD
 * direction. Projecting the wind onto the instance's own X and Z axes converts
 * it - correct for the yaw-and-uniform-scale matrices the scatter layer emits,
 * and two dot products instead of a matrix inverse.
 */
import * as THREE from 'three';

export function createFoliageMaterial({
  roughness = 0.88, side = THREE.DoubleSide, strength = 0.32,
} = {}) {
  const mat = new THREE.MeshStandardMaterial({
    vertexColors: true,
    roughness,
    metalness: 0,
    side,
  });

  const uniforms = {
    uTime: { value: 0 },
    uWind: { value: new THREE.Vector2(0.84, 0.54) },
    uStrength: { value: strength },
  };
  mat.userData.uniforms = uniforms;

  mat.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, uniforms);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', /* glsl */`
        #include <common>
        attribute float aFlex;
        uniform float uTime;
        uniform vec2 uWind;
        uniform float uStrength;
      `)
      .replace('#include <begin_vertex>', /* glsl */`
        #include <begin_vertex>
        #ifdef USE_INSTANCING
          vec3 iPos = instanceMatrix[3].xyz;
          vec3 iX = instanceMatrix[0].xyz;
          vec3 iZ = instanceMatrix[2].xyz;
          iX /= max(length(iX), 1e-5);
          iZ /= max(length(iZ), 1e-5);
          float phase = iPos.x * 0.085 + iPos.z * 0.113;
          float gust = sin(uTime * 1.15 + phase) * 0.62
                     + sin(uTime * 2.87 + phase * 1.73) * 0.38;
          vec3 w = vec3(uWind.x, 0.0, uWind.y) * (gust * aFlex * uStrength);
          transformed += vec3(dot(w, iX), 0.0, dot(w, iZ));
        #endif
      `);
  };

  mat.customProgramCacheKey = () => `ridgeline-foliage-${side}`;
  return mat;
}

/** Advance the gust clock. One call, whatever the forest costs. */
export function tickFoliage(mat, t) {
  mat.userData.uniforms.uTime.value = t;
}
