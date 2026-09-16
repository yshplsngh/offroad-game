/**
 * material.js - the ground shader.
 *
 * The mesh already carries a per-vertex colour from field.classify(), but a
 * vertex colour on a 2 m grid is a 2 m-wide blur: from inside the cab it reads
 * as coloured plastic. Three things fix that without a single texture fetch:
 *
 *  1. MACRO BREAKUP. Two octaves of world-space value noise at 9 m and 1.4 m
 *     multiply the albedo. This is what stops a meadow being one flat green.
 *  2. ROCK OVERLAY. `aMask.x` is the slope term from the classifier. Steep
 *     ground fades to a grey stone tone with its own higher-frequency noise,
 *     so cliffs read as rock even where the biome underneath says forest.
 *  3. WETNESS. `aMask.y` darkens and smooths the albedo, which is all a wet
 *     surface actually does to diffuse light. Cheaper and more convincing than
 *     a specular hack.
 *
 * Detail fades out with distance so the far field does not shimmer - noise at
 * a metre scale a kilometre away is pure aliasing.
 */
import * as THREE from 'three';

const GROUND_GLSL = /* glsl */`
float tHash(vec2 p) {
  p = fract(p * vec2(123.34, 345.45));
  p += dot(p, p + 34.345);
  return fract(p.x * p.y);
}
float tNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(tHash(i), tHash(i + vec2(1.0, 0.0)), f.x),
             mix(tHash(i + vec2(0.0, 1.0)), tHash(i + vec2(1.0, 1.0)), f.x), f.y);
}
`;

/**
 * @param {object} opts
 * @param {number} opts.detailFade Metres at which the fine detail is gone.
 */
export function createTerrainMaterial({ detailFade = 240 } = {}) {
  const mat = new THREE.MeshStandardMaterial({
    vertexColors: true,
    roughness: 0.97,
    metalness: 0.0,
    dithering: true,
  });

  mat.userData.uniforms = {
    uDetailFade: { value: detailFade },
  };

  mat.onBeforeCompile = (shader) => {
    shader.uniforms.uDetailFade = mat.userData.uniforms.uDetailFade;

    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', /* glsl */`
        #include <common>
        attribute vec3 aMask;
        varying vec3 vMask;
        varying vec3 vWPos;
      `)
      .replace('#include <begin_vertex>', /* glsl */`
        #include <begin_vertex>
        vMask = aMask;
        vWPos = (modelMatrix * vec4(transformed, 1.0)).xyz;
      `);

    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', /* glsl */`
        #include <common>
        uniform float uDetailFade;
        varying vec3 vMask;
        varying vec3 vWPos;
        ${GROUND_GLSL}
      `)
      // Injected here, not at <color_fragment>, because `roughnessFactor` is
      // declared by this chunk - touching it any earlier will not compile.
      .replace('#include <roughnessmap_fragment>', /* glsl */`
        #include <roughnessmap_fragment>

        float fade = 1.0 - smoothstep(uDetailFade * 0.45, uDetailFade, length(vWPos - cameraPosition));

        // Macro breakup. Two scales: patchiness, then grain.
        float m = tNoise(vWPos.xz * 0.11) * 0.62 + tNoise(vWPos.xz * 0.71) * 0.38;
        diffuseColor.rgb *= 1.0 + (m - 0.5) * 0.46 * fade;

        // Rock overlay on slope. Its own noise so it does not share the
        // ground's patch pattern and give the blend away.
        float rock = vMask.x;
        if (rock > 0.001) {
          float rn = tNoise(vWPos.xz * 0.33 + vWPos.y * 0.21) * 0.6
                   + tNoise(vWPos.xz * 1.9) * 0.4;
          vec3 stone = vec3(0.132, 0.126, 0.118) * (0.62 + rn * 0.85);
          diffuseColor.rgb = mix(diffuseColor.rgb, stone, rock * (0.55 + 0.45 * fade));
          roughnessFactor = mix(roughnessFactor, 0.82, rock);
        }

        // Wet ground is darker and smoother. That is the entire effect.
        float wet = vMask.y;
        diffuseColor.rgb *= 1.0 - wet * 0.42;
        roughnessFactor = mix(roughnessFactor, 0.30, wet * 0.8);

        // Snow goes the other way: bright, and it fills in the macro noise.
        float snow = vMask.z;
        diffuseColor.rgb = mix(diffuseColor.rgb, vec3(0.74, 0.78, 0.86), snow * 0.35);
        roughnessFactor = mix(roughnessFactor, 0.55, snow);
      `);
  };

  // Force a recompile key so two terrains never share a cached program.
  mat.customProgramCacheKey = () => 'ridgeline-ground';
  return mat;
}
