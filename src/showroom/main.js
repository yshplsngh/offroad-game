/**
 * Showroom - a turntable rig for inspecting vehicles before they ever touch
 * the world. Exists so vehicle work can be judged on its own: articulation,
 * paint, mud, lights and triangle budget, with nothing else in the frame.
 */
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { Vehicle } from '../vehicles/vehicle.js';
import { VEHICLES, PAINT_SWATCHES } from '../vehicles/catalog.js';

const app = document.getElementById('app');

/* ---------------------------------------------------------------- setup ---- */

const renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.setSize(innerWidth, innerHeight);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
app.appendChild(renderer.domElement);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(38, innerWidth / innerHeight, 0.1, 400);
camera.position.set(5.2, 2.4, 6.4);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.minDistance = 2.2;
controls.maxDistance = 22;
controls.maxPolarAngle = Math.PI * 0.495;    // never dip under the floor
controls.target.set(0, 1.0, 0);

// Studio IBL: gives the paint, chrome and glass something to reflect.
const pmrem = new THREE.PMREMGenerator(renderer);
const envRT = pmrem.fromScene(new RoomEnvironment(), 0.04);
scene.environment = envRT.texture;

const dayBg = new THREE.Color(0x141920);
const nightBg = new THREE.Color(0x05070a);
scene.background = dayBg;
scene.fog = new THREE.Fog(dayBg, 26, 90);

/* ---------------------------------------------------------------- lights --- */

const key = new THREE.DirectionalLight(0xfff4e2, 2.6);
key.position.set(7, 11, 6);
key.castShadow = true;
key.shadow.mapSize.set(2048, 2048);
key.shadow.camera.near = 1;
key.shadow.camera.far = 40;
const S = 6;
Object.assign(key.shadow.camera, { left: -S, right: S, top: S, bottom: -S });
key.shadow.camera.updateProjectionMatrix();
key.shadow.bias = -0.0009;
key.shadow.normalBias = 0.022;
scene.add(key);

const fill = new THREE.DirectionalLight(0x9fc4e8, 0.7);
fill.position.set(-8, 5, -5);
scene.add(fill);
const rim = new THREE.DirectionalLight(0xffd9a8, 1.0);
rim.position.set(-3, 3.5, -9);
scene.add(rim);
scene.add(new THREE.HemisphereLight(0x8fa9c6, 0x1d1a16, 0.45));

/* ----------------------------------------------------------------- floor --- */

const floorMat = new THREE.MeshStandardMaterial({
  color: 0x14171c, roughness: 0.82, metalness: 0.15,
});
const floor = new THREE.Mesh(new THREE.CircleGeometry(40, 64), floorMat);
floor.rotation.x = -Math.PI / 2;
floor.receiveShadow = true;
scene.add(floor);

const grid = new THREE.GridHelper(40, 40, 0x2a3038, 0x1c2128);
grid.material.transparent = true;
grid.material.opacity = 0.5;
grid.position.y = 0.002;
scene.add(grid);

// A soft pool of light under the vehicle so it sits on the floor, not above it.
const pad = new THREE.Mesh(
  new THREE.CircleGeometry(3.6, 48),
  new THREE.MeshBasicMaterial({ color: 0x1b2028, transparent: true, opacity: 0.55 }),
);
pad.rotation.x = -Math.PI / 2;
pad.position.y = 0.001;
scene.add(pad);

/* -------------------------------------------------------------- vehicles --- */

const turntable = new THREE.Group();
scene.add(turntable);

const state = {
  index: 0, lod: 2, spinRate: 0.25, steer: 0, artic: 0, mud: 0,
  lights: false, aux: false, flex: false, roll: false, wire: false,
  shadows: true, ghost: false, night: false, rollAngle: 0,
};
const paintChoice = new Map();          // vehicle id -> chosen colour
let vehicle = null;

function load(index) {
  if (vehicle) vehicle.dispose();
  state.index = index;
  const spec = VEHICLES[index];
  vehicle = new Vehicle(spec, { detail: state.lod });
  turntable.add(vehicle.root);

  if (paintChoice.has(spec.id)) vehicle.setPaint(paintChoice.get(spec.id));
  vehicle.setMud(state.mud);
  vehicle.setLights({ head: state.lights, aux: state.aux, brake: false });
  applyWire();
  applyGhost();
  refreshUI();
}

/* ------------------------------------------------------------------- UI ---- */

const $ = (id) => document.getElementById(id);

const vlist = $('vehicles');
VEHICLES.forEach((v, i) => {
  const b = document.createElement('button');
  b.className = 'vbtn';
  b.innerHTML = `${v.name}<small>${v.tagline}</small>`;
  b.onclick = () => load(i);
  vlist.appendChild(b);
});

const swWrap = $('swatches');
PAINT_SWATCHES.forEach((s) => {
  const d = document.createElement('div');
  d.className = 'sw';
  d.style.background = `#${s.color.toString(16).padStart(6, '0')}`;
  d.title = s.name;
  d.onclick = () => {
    paintChoice.set(VEHICLES[state.index].id, s.color);
    vehicle.setPaint(s.color);
    refreshUI();
  };
  swWrap.appendChild(d);
});

function refreshUI() {
  [...vlist.children].forEach((b, i) => b.classList.toggle('on', i === state.index));
  const cur = paintChoice.get(VEHICLES[state.index].id) ?? VEHICLES[state.index].body.color;
  [...swWrap.children].forEach((d, i) => d.classList.toggle('on', PAINT_SWATCHES[i].color === cur));
  [...$('lods').children].forEach((b) => b.classList.toggle('on', +b.dataset.lod === state.lod));
}

/** Wire a slider to state, with a formatted readout. */
function slider(id, key, fmt, onChange) {
  const el = $(id), out = $(`${id}Out`);
  const apply = () => {
    state[key] = +el.value;
    out.textContent = fmt(state[key]);
    onChange?.(state[key]);
  };
  el.addEventListener('input', apply);
  apply();
}

slider('steer', 'steer', (v) => `${Math.round(v * 35)}°`);
slider('artic', 'artic', (v) => v.toFixed(2));
slider('mud', 'mud', (v) => `${Math.round(v * 100)}%`, (v) => vehicle?.setMud(v));
slider('spin', 'spinRate', (v) => v.toFixed(2));

/** Toggle button bound to a state flag. */
function toggle(id, key, onChange) {
  const el = $(id);
  el.onclick = () => {
    state[key] = !state[key];
    el.classList.toggle('on', state[key]);
    onChange?.(state[key]);
  };
}

toggle('tgLights', 'lights', (v) => vehicle.setLights({ head: v }));
toggle('tgAux', 'aux', (v) => vehicle.setLights({ aux: v }));
toggle('tgFlex', 'flex');
toggle('tgRoll', 'roll');
toggle('tgWire', 'wire', applyWire);
toggle('tgShadow', 'shadows', (v) => { renderer.shadowMap.enabled = v; key.castShadow = v; refreshShadows(); });
toggle('tgGhost', 'ghost', applyGhost);
toggle('tgNight', 'night', (v) => {
  scene.background = v ? nightBg : dayBg;
  scene.fog.color = v ? nightBg : dayBg;
  key.intensity = v ? 0.12 : 2.6;
  fill.intensity = v ? 0.08 : 0.7;
  rim.intensity = v ? 0.15 : 1.0;
  renderer.toneMappingExposure = v ? 1.25 : 1.05;
});

$('lods').querySelectorAll('button').forEach((b) => {
  b.onclick = () => { state.lod = +b.dataset.lod; load(state.index); };
});

function applyWire() {
  vehicle?.root.traverse((o) => { if (o.isMesh) o.material.wireframe = state.wire; });
}

/** Fade the paint so the frame, drivetrain and cage are readable. */
function applyGhost() {
  if (!vehicle) return;
  const p = vehicle.paint;
  p.transparent = state.ghost;
  p.opacity = state.ghost ? 0.16 : 1;
  p.depthWrite = !state.ghost;
  p.needsUpdate = true;
  vehicle.body.traverse((o) => {
    if (o.isMesh && o.name.includes('Glass')) o.visible = !state.ghost;
  });
}

function refreshShadows() {
  scene.traverse((o) => { if (o.isMesh) o.castShadow = state.shadows && o !== floor; });
  floor.receiveShadow = state.shadows;
}

addEventListener('keydown', (e) => {
  const n = +e.key;
  if (n >= 1 && n <= VEHICLES.length) load(n - 1);
  if (e.key.toLowerCase() === 'f') $('tgFlex').click();
  if (e.key.toLowerCase() === 'l') $('tgLights').click();
  if (e.key.toLowerCase() === 'n') $('tgNight').click();
});

addEventListener('resize', () => {
  camera.aspect = innerWidth / innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});

/* ----------------------------------------------------------------- loop ---- */

const statsEl = $('stats');
let last = performance.now(), fpsAccum = 0, fpsFrames = 0, fps = 0, statTimer = 0;

function animate(now) {
  requestAnimationFrame(animate);
  const dt = Math.min(0.05, (now - last) / 1000);
  last = now;

  turntable.rotation.y += dt * state.spinRate;

  if (vehicle) {
    vehicle.setSteering(state.steer * 0.62);

    // Articulation: a diagonal twist, which is how an axle actually loads up
    // on an off-camber climb - front-left stuffed, rear-right drooped.
    const t = state.flex ? Math.sin(now / 900) : state.artic;
    const lim = vehicle.spec.suspension.travel;
    vehicle.setTravel(0, t * lim);
    vehicle.setTravel(1, -t * lim * 0.55);
    vehicle.setTravel(2, -t * lim * 0.8);
    vehicle.setTravel(3, t * lim * 0.45);

    if (state.roll) {
      state.rollAngle += dt * 3.2;
      for (let i = 0; i < 4; i++) vehicle.setWheelSpin(i, state.rollAngle);
    }
    vehicle.update();
  }

  controls.update();
  renderer.render(scene, camera);

  fpsAccum += dt; fpsFrames++; statTimer += dt;
  if (statTimer > 0.4) {
    fps = fpsFrames / fpsAccum;
    fpsAccum = 0; fpsFrames = 0; statTimer = 0;
    const info = renderer.info.render;
    statsEl.innerHTML = `<b>${VEHICLES[state.index].name}</b><br>`
      + `${(vehicle?.triangles ?? 0).toLocaleString()} tris<br>`
      + `${info.calls} draw calls<br>`
      + `<b>${fps.toFixed(0)}</b> fps`;
  }
}

/* ------------------------------------------------------------ deep link ---- */

/**
 * Query params drive every control, so a given view is reproducible:
 *   ?v=2&cam=210&el=8&dist=9&artic=.8&mud=.6&lights=1&spin=0
 * Used for headless screenshots and for sharing a specific setup.
 */
function applyQuery() {
  const q = new URLSearchParams(location.search);
  if (!q.size) return null;
  const num = (k, d) => (q.has(k) ? parseFloat(q.get(k)) : d);
  const flag = (k) => q.has(k) && q.get(k) !== '0' && q.get(k) !== 'false';

  state.lod = num('lod', state.lod);
  const index = Math.min(VEHICLES.length - 1, Math.max(0, num('v', 0) | 0));

  for (const [key, id] of [['steer', 'steer'], ['artic', 'artic'], ['mud', 'mud'], ['spinRate', 'spin']]) {
    if (q.has(id)) {
      state[key] = num(id, state[key]);
      const el = $(id);
      el.value = String(state[key]);
      el.dispatchEvent(new Event('input'));
    }
  }
  for (const [id, key] of [['tgLights', 'lights'], ['tgAux', 'aux'], ['tgWire', 'wire'],
    ['tgGhost', 'ghost'], ['tgNight', 'night'], ['tgFlex', 'flex']]) {
    if (flag(key) !== state[key]) $(id).click();
  }
  return { index, cam: num('cam', null), el: num('el', 14), dist: num('dist', 8.4) };
}

const deep = applyQuery();
load(deep?.index ?? 0);
if (deep?.cam !== null && deep?.cam !== undefined) {
  // Place the camera on a sphere around the vehicle: cam is the azimuth in
  // degrees (0 = dead ahead of the nose), el the elevation.
  const a = THREE.MathUtils.degToRad(deep.cam);
  const e = THREE.MathUtils.degToRad(deep.el);
  camera.position.set(
    Math.sin(a) * Math.cos(e) * deep.dist,
    Math.sin(e) * deep.dist + 1.0,
    Math.cos(a) * Math.cos(e) * deep.dist,
  );
  controls.update();
}

document.getElementById('loading').remove();
requestAnimationFrame(animate);
