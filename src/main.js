/**
 * main.js - the integration layer. Terrain, sky, physics, vehicle, camera, HUD.
 *
 * Everything below is wiring. The interesting decisions all live in the modules
 * this file imports; what it owns is the ORDER, and the order is not arbitrary:
 *
 *   input -> physics (fixed 60 Hz) -> visual sync -> streaming -> camera -> render
 *
 * Physics runs on a fixed accumulator because the tire model is a stiff system:
 * a variable timestep changes the effective spring rate every frame and the
 * truck develops a speed-dependent personality. Rendering stays on whatever the
 * display gives us. Streaming happens after physics so chunks build around where
 * the truck IS, not where it was, and the camera updates after the vehicle has
 * been moved so it never renders a frame behind.
 */
import * as THREE from 'three';
import RAPIER from '@dimforge/rapier3d-compat';

import { VEHICLES } from './vehicles/catalog.js';
import { Vehicle } from './vehicles/vehicle.js';
import { createTerrain } from './world/terrain/index.js';
import { createScatter } from './world/scatter/index.js';
import { createAtmosphere } from './world/sky/index.js';
import { createController } from './physics/controller.js';
import { CameraRig } from './game/camera.js';
import { createInput } from './game/input.js';
import { createHUD } from './game/hud.js';
import { BIOME, FIXED_DT, VIEW_CHUNKS } from './world/contract.js';

const BIOME_NAME = Object.fromEntries(Object.values(BIOME).map((b) => [b.id, b.name]));

/* ----------------------------------------------------------- settings ---- */

const params = new URLSearchParams(location.search);
const num = (k, d) => (params.has(k) ? Number(params.get(k)) : d);

const SETTINGS = {
  seed: num('seed', 1337),
  vehicle: Math.max(0, Math.min(VEHICLES.length - 1, num('v', 0))),
  hour: num('hour', 8.5),
  view: Math.max(2, Math.min(8, num('view', VIEW_CHUNKS))),
  scatterView: Math.max(0, Math.min(6, num('trees', 4))),
  density: num('density', 1),
  shadows: params.get('shadows') !== '0',
  maxPixelRatio: num('dpr', 1.5),
};

/* ---------------------------------------------------------- boot report -- */

const overlay = document.getElementById('boot');
const bootMsg = document.getElementById('boot-msg');
const say = (m) => { if (bootMsg) bootMsg.textContent = m; };

function fatal(err) {
  console.error(err);
  if (!overlay) return;
  overlay.classList.add('error');
  overlay.innerHTML = `<div class="box"><h2>STARTUP ERROR</h2><pre>${
    String(err?.stack || err).replace(/[<>&]/g, (c) => ({ '<': '&lt;', '>': '&gt;', '&': '&amp;' }[c]))
  }</pre></div>`;
}
window.addEventListener('error', (e) => fatal(e.error || e.message));
window.addEventListener('unhandledrejection', (e) => fatal(e.reason));

/* ---------------------------------------------------------------- boot --- */

async function boot() {
  say('starting physics');
  await RAPIER.init();

  /* --- renderer --- */
  const canvas = document.getElementById('view');
  const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: window.devicePixelRatio < 1.5,
    powerPreference: 'high-performance',
    stencil: false,
  });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, SETTINGS.maxPixelRatio));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 0.95;
  if (SETTINGS.shadows) {
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;
  }

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(48, window.innerWidth / window.innerHeight, 0.25, 4200);

  /* --- world --- */
  say('raising mountains');
  const terrain = createTerrain({ seed: SETTINGS.seed, view: SETTINGS.view, budget: 2 });
  scene.add(terrain.root);

  const spawn = terrain.findSpawn(0, 0);
  say('meshing terrain');
  terrain.prime(new THREE.Vector3(spawn.x, spawn.y, spawn.z), 2);

  say('lighting the sky');
  const sky = createAtmosphere({ scene, hour: SETTINGS.hour, shadows: SETTINGS.shadows });

  /* --- physics world --- */
  const world = new RAPIER.World({ x: 0, y: -9.81, z: 0 });
  world.timestep = FIXED_DT;

  say('planting the forest');
  const scatter = createScatter({
    terrain,
    seed: SETTINGS.seed,
    view: SETTINGS.scatterView,
    density: SETTINGS.density,
    budget: 1,
    world,
    rapier: RAPIER,
  });
  scene.add(scatter.root);
  scatter.prime(new THREE.Vector3(spawn.x, spawn.y, spawn.z), 1);

  /* --- vehicle + controller --- */
  say('building the truck');
  let vehicleIndex = SETTINGS.vehicle;
  let vehicle = null;
  let controller = null;

  function spawnVehicle(index, at) {
    if (controller) controller.dispose();
    if (vehicle) vehicle.dispose();

    vehicleIndex = ((index % VEHICLES.length) + VEHICLES.length) % VEHICLES.length;
    const spec = VEHICLES[vehicleIndex];
    vehicle = new Vehicle(spec, { detail: 2, interior: true });
    vehicle.root.traverse((o) => {
      if (o.isMesh) { o.castShadow = true; o.receiveShadow = true; }
    });
    scene.add(vehicle.root);

    const ground = terrain.height(at.x, at.z);
    controller = createController({
      vehicle,
      rapier: RAPIER,
      world,
      terrain,
      spawn: { x: at.x, y: ground + 1, z: at.z },
      heading: at.heading ?? 0,
    });
    rig.follow(vehicle);
    return spec;
  }

  const rig = new CameraRig(camera, { domElement: canvas });
  const spec = spawnVehicle(vehicleIndex, { x: spawn.x, z: spawn.z });

  /* --- io --- */
  const input = createInput();
  const hud = createHUD();
  hud.say(`${spec.name} - press / for controls`, 4);

  const lights = { head: false, aux: false };
  let paused = false;

  function applyLights(braking) {
    const auto = sky.state.night > 0.35;
    vehicle.setLights({
      head: lights.head || auto,
      aux: lights.aux,
      brake: braking,
    });
  }

  /* ------------------------------------------------------------ actions -- */

  function handle(action) {
    const t = controller.state;
    switch (action) {
      case 'gearUp':
        if (controller.drivetrain.shiftUp()) hud.say(`gear ${controller.drivetrain.gearName()}`, 0.8);
        break;
      case 'gearDown':
        if (controller.drivetrain.shiftDown()) hud.say(`gear ${controller.drivetrain.gearName()}`, 0.8);
        break;
      case 'lock':
        controller.drivetrain.cycleLock();
        hud.say(`diff lock: ${controller.drivetrain.lockName()}`);
        break;
      case 'range':
        if (controller.drivetrain.toggleRange(t.speed)) {
          hud.say(controller.drivetrain.state.lowRange ? 'LOW range' : 'HIGH range');
        } else hud.say('slow down to change range', 1.2);
        break;
      case 'lights': lights.head = !lights.head; hud.say(`headlights ${lights.head ? 'on' : 'off'}`, 0.9); break;
      case 'aux': lights.aux = !lights.aux; hud.say(`light bar ${lights.aux ? 'on' : 'off'}`, 0.9); break;
      case 'camera': rig.cycleMode(); hud.say(`camera: ${rig.mode}`, 0.9); break;
      case 'recover':
        hud.say(controller.flip() ? 'recovered' : 'recovery cooling down', 1.2);
        rig.impulse(0.6);
        break;
      case 'winch':
        hud.say(controller.winchAttach() ? 'winch anchored - hold G to reel in' : 'winch released');
        break;
      case 'timeForward': sky.skip(1); break;
      case 'timeBack': sky.skip(-1); break;
      case 'wash': controller.state.mud = 0; hud.say('washed'); break;
      case 'nextVehicle': {
        const at = { x: controller.position.x, z: controller.position.z, heading: controller.state.heading };
        const next = spawnVehicle(vehicleIndex + 1, at);
        hud.say(next.name, 2);
        break;
      }
      case 'map': hud.toggle(); break;
      case 'help': hud.toggleHelp(); break;
      case 'debug': hud.toggleStats(); break;
      case 'pause':
        paused = !paused;
        hud.say(paused ? 'paused' : 'resumed');
        break;
      default: break;
    }
  }

  /* -------------------------------------------------------------- loop --- */

  let prev = performance.now();
  let acc = 0;
  let fpsT = 0;
  let fpsN = 0;
  let fps = 0;

  function frame(now) {
    requestAnimationFrame(frame);
    const dt = Math.min(0.1, (now - prev) / 1000);
    prev = now;

    fpsN++; fpsT += dt;
    if (fpsT > 0.5) { fps = fpsN / fpsT; fpsN = 0; fpsT = 0; }

    const axes = input.poll();
    for (const a of input.actions()) handle(a);

    if (!paused) {
      controller.setInput(axes);

      acc += dt;
      // Cap the catch-up so a tab switch cannot spiral into a hundred steps.
      let steps = 0;
      while (acc >= FIXED_DT && steps < 5) {
        controller.step(FIXED_DT);
        acc -= FIXED_DT;
        steps++;
      }
      if (steps === 5) acc = 0;

      controller.syncVisual();
      terrain.update(controller.position);
      scatter.update(controller.position, dt);
      controller.setSolidQueries(scatter.hasColliders);
      sky.update(dt, camera);
      sky.refreshEnvironment(renderer, scene);
      if (controller.state.impact > 0.02) rig.impulse(controller.state.impact);
    }

    const tel = controller.telemetry();
    applyLights(axes.brake > 0.05 || axes.handbrake > 0.5);
    rig.update(dt, tel);
    renderer.toneMappingExposure = sky.state.exposure;

    hud.update(tel, {
      dt,
      clock: sky.state.timeOfDay,
      biome: BIOME_NAME[terrain.biomeAt(controller.position.x, controller.position.z)],
      lights: lights.head || sky.state.night > 0.35,
      throttle: axes.throttle,
      brake: Math.max(axes.brake, axes.handbrake),
      statsText: `${fps.toFixed(0)} fps | ${renderer.info.render.calls} draws | `
        + `${(renderer.info.render.triangles / 1000).toFixed(0)}k tris | `
        + `chunks ${terrain.stats().chunks} (+${terrain.stats().pending}) | `
        + `scatter ${scatter.stats().instances} (+${scatter.stats().pending}) | `
        + `x ${controller.position.x.toFixed(0)} z ${controller.position.z.toFixed(0)}`,
    });

    renderer.render(scene, camera);
  }

  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });

  // One frame before the overlay goes, so the first thing seen is the world.
  sky.update(0, camera);
  sky.refreshEnvironment(renderer, scene, true);
  rig.update(0, controller.telemetry());
  renderer.render(scene, camera);
  overlay?.remove();

  requestAnimationFrame((t) => { prev = t; frame(t); });

  // Handy from the console while tuning.
  window.RIDGELINE = { scene, renderer, camera, terrain, scatter, sky, world, rig, hud,
    get vehicle() { return vehicle; }, get controller() { return controller; } };
}

boot().catch(fatal);
