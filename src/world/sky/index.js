/**
 * sky/index.js - the AtmosphereProvider: dome, lights, fog, stars, clock.
 *
 * atmosphere.js holds the model (scattering, ephemeris, grade table). This file
 * is the scene graph around it: one inverted sphere running the GLSL twin of
 * skyRadiance(), a directional key light, a hemisphere fill, and an exponential
 * fog whose colour is the CPU evaluation of the same scattering function the
 * dome is drawing. That last part is the reason the two implementations exist -
 * if the fog were hand-tinted it would separate from the sky at dawn and the
 * horizon would show a seam.
 *
 * SHADOWS follow the camera, not the world. An ortho shadow camera parked on
 * the player covers 130 m at 2048px, which is ~6 cm a texel - enough for wheel
 * contact shadows. Snapping its centre to texel boundaries stops the shadow
 * edge crawling as you drive, which is the single most obvious artefact of a
 * moving shadow camera.
 */
import * as THREE from 'three';
import {
  sunDirection, moonDirection, altitudeOf, gradeAt, mistTimeBias,
  skyRadiance, SKY_GLSL,
} from './atmosphere.js';

const DOME_RADIUS = 4000;

const SKY_VERT = /* glsl */`
varying vec3 vDir;
void main() {
  vDir = normalize(position);
  vec4 mv = modelViewMatrix * vec4(position, 1.0);
  gl_Position = projectionMatrix * mv;
  gl_Position.z = gl_Position.w;          // pin to the far plane
}
`;

const SKY_FRAG = /* glsl */`
uniform vec3 uSunDir;
uniform vec3 uMoonDir;
uniform float uTurbidity, uRayleigh, uMie, uMieG, uNight, uMoonUp;
varying vec3 vDir;

${SKY_GLSL}

void main() {
  vec3 dir = normalize(vDir);
  SkyTerms t = skyTerms(dir, uSunDir, uTurbidity, uRayleigh, uMie, uMieG);

  vec3 L0 = 0.1 * t.fex;
  // The solar disc. 0.9995 is roughly a half-degree - the real angular size.
  float cosSun = dot(dir, uSunDir);
  L0 += (t.sunE * 19000.0 * t.fex) * smoothstep(0.99955, 0.99975, cosSun);

  vec3 col = (t.lin + L0) * 0.04;
  col += vec3(0.0, 0.0003, 0.00075);
  col += nightFloor(dir, uNight, uMoonUp);

  // Moon disc plus a soft halo, faded in with the night term.
  float cosMoon = dot(dir, uMoonDir);
  float disc = smoothstep(0.9990, 0.9995, cosMoon);
  float halo = pow(max(0.0, cosMoon), 900.0) * 0.28;
  col += vec3(0.62, 0.66, 0.78) * (disc * 1.5 + halo) * uNight * uMoonUp;

  gl_FragColor = vec4(col, 1.0);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;

/**
 * @param {object} opts
 * @param {THREE.Scene} opts.scene
 * @param {number} opts.hour        Starting time of day, 0..24.
 * @param {number} opts.rate        Game hours per real second.
 * @param {boolean} opts.shadows
 */
export function createAtmosphere({
  scene, hour = 8.5, rate = 0.02, shadows = true, shadowRange = 65,
} = {}) {
  const root = new THREE.Group();
  root.name = 'atmosphere';

  /* ------------------------------------------------------------- dome --- */

  const uniforms = {
    uSunDir: { value: new THREE.Vector3(0, 1, 0) },
    uMoonDir: { value: new THREE.Vector3(0, -1, 0) },
    uTurbidity: { value: 2.6 },
    uRayleigh: { value: 2.8 },
    uMie: { value: 0.006 },
    uMieG: { value: 0.8 },
    uNight: { value: 0 },
    uMoonUp: { value: 0 },
  };

  const dome = new THREE.Mesh(
    new THREE.SphereGeometry(DOME_RADIUS, 32, 20),
    new THREE.ShaderMaterial({
      uniforms,
      vertexShader: SKY_VERT,
      fragmentShader: SKY_FRAG,
      side: THREE.BackSide,
      depthWrite: false,
      depthTest: false,
      fog: false,
    }),
  );
  dome.renderOrder = -1000;
  dome.frustumCulled = false;
  root.add(dome);

  /* ------------------------------------------------------ environment --- */

  // Without an IBL, anything with clearcoat, metal or glass - which is most of
  // the truck - has nothing to reflect and renders as black plastic. The sky is
  // already a full radiance field, so prefilter it and hand it to the scene.
  // A second mesh sharing the dome's geometry and material, because an object
  // cannot be in two scenes at once.
  const envScene = new THREE.Scene();
  envScene.add(new THREE.Mesh(dome.geometry, dome.material));
  let pmrem = null;
  let envTarget = null;
  let envAlt = -999;

  /* ------------------------------------------------------------ stars --- */

  const STAR_COUNT = 1400;
  const starPos = new Float32Array(STAR_COUNT * 3);
  const starSize = new Float32Array(STAR_COUNT);
  for (let i = 0; i < STAR_COUNT; i++) {
    // Uniform on the upper hemisphere - below the horizon is wasted geometry.
    const u = Math.random() * 2 - 1;
    const a = Math.random() * Math.PI * 2;
    const r = Math.sqrt(Math.max(0, 1 - u * u));
    const y = Math.abs(u) * 0.98 + 0.02;
    starPos[i * 3] = Math.cos(a) * r * DOME_RADIUS * 0.96;
    starPos[i * 3 + 1] = y * DOME_RADIUS * 0.96;
    starPos[i * 3 + 2] = Math.sin(a) * r * DOME_RADIUS * 0.96;
    starSize[i] = 6 + Math.pow(Math.random(), 3) * 26;
  }
  const starGeo = new THREE.BufferGeometry();
  starGeo.setAttribute('position', new THREE.BufferAttribute(starPos, 3));
  starGeo.setAttribute('aSize', new THREE.BufferAttribute(starSize, 1));
  const starMat = new THREE.ShaderMaterial({
    uniforms: { uOpacity: { value: 0 } },
    vertexShader: /* glsl */`
      attribute float aSize;
      varying float vTwinkle;
      void main() {
        vec4 mv = modelViewMatrix * vec4(position, 1.0);
        gl_Position = projectionMatrix * mv;
        gl_Position.z = gl_Position.w;
        gl_PointSize = aSize * 0.06;
        vTwinkle = fract(sin(position.x * 12.9898 + position.z * 78.233) * 43758.5453);
      }`,
    fragmentShader: /* glsl */`
      uniform float uOpacity;
      varying float vTwinkle;
      void main() {
        float d = length(gl_PointCoord - 0.5);
        float a = smoothstep(0.5, 0.05, d) * uOpacity * (0.55 + vTwinkle * 0.45);
        if (a < 0.01) discard;
        gl_FragColor = vec4(vec3(0.86, 0.89, 1.0), a);
        #include <colorspace_fragment>
      }`,
    transparent: true,
    depthWrite: false,
    depthTest: false,
    fog: false,
    toneMapped: false,
  });
  const stars = new THREE.Points(starGeo, starMat);
  stars.renderOrder = -999;
  stars.frustumCulled = false;
  root.add(stars);

  /* ----------------------------------------------------------- lights --- */

  const sun = new THREE.DirectionalLight(0xffffff, 3);
  sun.position.set(60, 90, 40);
  if (shadows) {
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    sun.shadow.camera.near = 1;
    sun.shadow.camera.far = 360;
    sun.shadow.camera.left = -shadowRange;
    sun.shadow.camera.right = shadowRange;
    sun.shadow.camera.top = shadowRange;
    sun.shadow.camera.bottom = -shadowRange;
    sun.shadow.bias = -0.0009;
    sun.shadow.normalBias = 0.06;
  }
  root.add(sun, sun.target);

  const hemi = new THREE.HemisphereLight(0xbdd7f7, 0x5d5744, 1);
  root.add(hemi);

  // A second, shadowless light from the opposite side. Pure hemisphere fill
  // leaves shadowed faces looking like flat paint; a weak bounce gives them
  // enough directionality to read as form.
  const bounce = new THREE.DirectionalLight(0xffffff, 0.25);
  root.add(bounce);

  /* ------------------------------------------------------------- fog ---- */

  const fog = new THREE.FogExp2(0x9fb2c8, 0.0012);
  if (scene) {
    scene.fog = fog;
    scene.add(root);
  }

  /* ----------------------------------------------------------- state ---- */

  const state = {
    timeOfDay: hour,
    rate,
    paused: false,
    exposure: 0.95,
    mist: 0,
    night: 0,
  };

  const grade = gradeAt.create();
  const sunDir = new THREE.Vector3();
  const moonDir = new THREE.Vector3();
  const fogColor = new THREE.Color();
  const horizonDir = new THREE.Vector3();
  const lighting = { fogColor, sunDir, exposure: 0.95, altitude: 0, mist: 0, night: 0 };
  const scatterParams = { turbidity: 2.6, rayleigh: 2.8, mie: 0.006, mieG: 0.8, night: 0, moonUp: 0 };

  const camPos = new THREE.Vector3();
  const camFwd = new THREE.Vector3(0, 0, 1);

  function applyGrade() {
    sunDirection(state.timeOfDay, sunDir);
    moonDirection(state.timeOfDay, moonDir);
    const alt = altitudeOf(sunDir);
    gradeAt(alt, grade);

    const night = THREE.MathUtils.clamp(1 - (alt + 6) / 8, 0, 1);
    const moonUp = THREE.MathUtils.clamp(moonDir.y * 3, 0, 1);
    state.night = night;

    uniforms.uSunDir.value.copy(sunDir);
    uniforms.uMoonDir.value.copy(moonDir);
    uniforms.uTurbidity.value = grade.turb;
    uniforms.uRayleigh.value = grade.ray;
    uniforms.uMie.value = grade.mie;
    uniforms.uNight.value = night;
    uniforms.uMoonUp.value = moonUp;
    starMat.uniforms.uOpacity.value = grade.star;

    sun.color.copy(grade.sun);
    sun.intensity = grade.lux;
    hemi.color.copy(grade.sky);
    hemi.groundColor.copy(grade.gnd);
    hemi.intensity = grade.amb;
    bounce.color.copy(grade.sky);
    bounce.intensity = grade.amb * 0.22;

    // Fog colour = the sky the fog is standing in front of. Sampled just above
    // the horizon along the view, which is where the terrain actually fades.
    scatterParams.turbidity = grade.turb;
    scatterParams.rayleigh = grade.ray;
    scatterParams.mie = grade.mie;
    scatterParams.night = night;
    scatterParams.moonUp = moonUp;
    horizonDir.set(camFwd.x, 0.06, camFwd.z).normalize();
    skyRadiance(horizonDir, sunDir, scatterParams, fogColor);
    fog.color.copy(fogColor);

    const mist = grade.mist * mistTimeBias(state.timeOfDay);
    state.mist = mist;
    fog.density = grade.fog * (1 + mist * 0.85);

    state.exposure = grade.exp;
    lighting.exposure = grade.exp;
    lighting.altitude = alt;
    lighting.mist = mist;
    lighting.night = night;
  }

  applyGrade();

  /**
   * @param {number} dt
   * @param {THREE.Vector3|THREE.Camera} camera Position, or a camera (which also
   *   gives us a view direction for the fog colour).
   */
  function update(dt, camera) {
    if (camera?.isCamera) {
      camPos.setFromMatrixPosition(camera.matrixWorld);
      camFwd.set(0, 0, -1).applyQuaternion(camera.quaternion);
    } else if (camera) {
      camPos.copy(camera);
    }

    if (!state.paused) state.timeOfDay = (state.timeOfDay + dt * state.rate) % 24;
    applyGrade();

    dome.position.copy(camPos);
    stars.position.copy(camPos);

    // Park the sun 150 m up-sun of the player and snap to shadow texels.
    const texel = (shadowRange * 2) / 2048;
    const tx = Math.round(camPos.x / texel) * texel;
    const tz = Math.round(camPos.z / texel) * texel;
    sun.target.position.set(tx, camPos.y, tz);
    sun.position.copy(sun.target.position).addScaledVector(sunDir, 150);
    bounce.position.copy(sun.target.position)
      .addScaledVector(sunDir, -120).add(new THREE.Vector3(0, 90, 0));
    bounce.target = sun.target;
  }

  return {
    root,
    sun,
    hemi,
    fog,
    state,
    get timeOfDay() { return state.timeOfDay; },
    set timeOfDay(v) { state.timeOfDay = ((v % 24) + 24) % 24; applyGrade(); },
    /** Jump the clock. Used by the time-of-day keys. */
    skip(hours) { this.timeOfDay = state.timeOfDay + hours; },
    update,

    /**
     * Re-prefilter the sky into `target.environment`, but only once the sun has
     * moved enough to matter. A PMREM pass is a few milliseconds; doing it every
     * frame for a sun that creeps a third of a degree a second is pure waste.
     *
     * @param {THREE.WebGLRenderer} renderer
     * @param {THREE.Scene} target
     * @param {boolean} force
     */
    refreshEnvironment(renderer, target, force = false) {
      const alt = lighting.altitude;
      if (!force && Math.abs(alt - envAlt) < 1.5) return false;
      envAlt = alt;
      if (!pmrem) {
        pmrem = new THREE.PMREMGenerator(renderer);
        pmrem.compileEquirectangularShader();
      }
      const next = pmrem.fromScene(envScene, 0, 1, DOME_RADIUS * 1.2);
      envTarget?.dispose();
      envTarget = next;
      target.environment = next.texture;
      target.environmentIntensity = 1;
      return true;
    },

    lighting: () => lighting,
    dispose() {
      dome.geometry.dispose(); dome.material.dispose();
      starGeo.dispose(); starMat.dispose();
      envTarget?.dispose();
      pmrem?.dispose();
      root.removeFromParent();
    },
  };
}
