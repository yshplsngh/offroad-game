/**
 * interior.js - cabin. Kept deliberately lightweight: it is only ever seen
 * through glass or from the cockpit camera, so the budget goes into the few
 * things that read at a glance - seats, dash, and a steering wheel that turns.
 *
 * buildInterior() returns a group whose userData exposes `steeringWheel` and
 * `shifters` so the vehicle rig can animate them.
 */
import * as THREE from 'three';
import { roundedBox, plate, tube, lathe, arcTorus, mesh, mergeParts } from './parts.js';
import {
  seatFabric, dashPlastic, trimPlastic, blackSteel, zinc, powderCoat,
  rubber, chrome, lens, bedliner,
} from './materials.js';

/** Bucket seat with bolsters and a harness. */
function buildSeat({ harness = true } = {}) {
  const g = new THREE.Group();
  g.name = 'seat';

  // Base cushion + side bolsters.
  g.add(mesh(roundedBox(0.46, 0.12, 0.48, 0.05, 0.01, 2), seatFabric(), { pos: [0, 0.22, 0] }));
  for (const s of [-1, 1]) {
    g.add(mesh(roundedBox(0.09, 0.14, 0.42, 0.04, 0.008, 2), seatFabric(),
      { pos: [s * 0.2, 0.26, 0.01], rot: [0, 0, -s * 0.12] }));
  }

  // Backrest, reclined, with its own bolsters and a headrest.
  const back = new THREE.Group();
  back.add(mesh(roundedBox(0.44, 0.62, 0.11, 0.05, 0.01, 2), seatFabric(), { pos: [0, 0.31, 0] }));
  for (const s of [-1, 1]) {
    back.add(mesh(roundedBox(0.08, 0.56, 0.14, 0.04, 0.008, 2), seatFabric(),
      { pos: [s * 0.19, 0.3, 0.03], rot: [0, -s * 0.12, 0] }));
  }
  back.add(mesh(roundedBox(0.26, 0.2, 0.12, 0.05, 0.01, 2), seatFabric(), { pos: [0, 0.71, 0.01] }));
  back.position.set(0, 0.26, -0.22);
  back.rotation.x = 0.2;
  g.add(back);

  // Frame rails under the cushion.
  g.add(mesh(roundedBox(0.4, 0.04, 0.42, 0.01, 0.004, 1), blackSteel(), { pos: [0, 0.13, 0] }));
  for (const s of [-1, 1]) {
    g.add(mesh(roundedBox(0.03, 0.11, 0.4, 0.008, 0.003, 1), blackSteel(), { pos: [s * 0.18, 0.06, 0] }));
  }

  if (harness) {
    // Four-point harness webbing over the shoulders and lap.
    for (const s of [-1, 1]) {
      g.add(mesh(roundedBox(0.055, 0.5, 0.012, 0.004, 0.002, 1), powderCoat(0x9a2b2b),
        { pos: [s * 0.13, 0.62, -0.13], rot: [0.28, 0, -s * 0.06] }));
    }
    g.add(mesh(roundedBox(0.07, 0.07, 0.03, 0.008, 0.003, 1), zinc(), { pos: [0, 0.3, -0.02] }));
  }
  return g;
}

/** Dash with a binnacle, centre stack, vents and switch panel. */
function buildDash(spec, m) {
  const g = new THREE.Group();
  g.name = 'dash';
  const z = m.zCowl - 0.18;
  const w = spec.body.width - 0.1;
  const y = m.beltY - 0.1;

  // Main dash pad, swept back at the top.
  g.add(mesh(roundedBox(w, 0.22, 0.34, 0.05, 0.01, 2), dashPlastic(),
    { pos: [0, y, z - 0.1], rot: [0.18, 0, 0], name: 'dashPad' }));
  // Lower knee bolster.
  g.add(mesh(roundedBox(w, 0.24, 0.16, 0.03, 0.008, 2), dashPlastic(),
    { pos: [0, y - 0.22, z - 0.04], name: 'lowerDash' }));

  // Instrument binnacle over the wheel.
  const side = spec.rhd ? -1 : 1;
  const bx = side * (spec.body.width * 0.24);
  g.add(mesh(roundedBox(0.34, 0.18, 0.2, 0.05, 0.01, 2), dashPlastic(),
    { pos: [bx, y + 0.08, z - 0.14], rot: [0.3, 0, 0], name: 'binnacle' }));
  for (const [dx, r] of [[-0.075, 0.062], [0.075, 0.062]]) {
    g.add(mesh(new THREE.CylinderGeometry(r, r, 0.012, 18), lens(0x2a3a44, true, 0.35),
      { pos: [bx + dx, y + 0.1, z - 0.23], rot: [Math.PI / 2 - 0.3, 0, 0], name: 'gauge', castShadow: false }));
  }

  // Centre stack: vents, a screen, switch bank.
  g.add(mesh(roundedBox(0.3, 0.42, 0.1, 0.03, 0.008, 2), dashPlastic(),
    { pos: [0, y - 0.08, z - 0.16], name: 'centreStack' }));
  g.add(mesh(roundedBox(0.22, 0.13, 0.02, 0.012, 0.004, 1), lens(0x1b2a33, true, 0.3),
    { pos: [0, y + 0.02, z - 0.21], name: 'screen', castShadow: false }));
  for (let i = 0; i < 6; i++) {
    g.add(mesh(roundedBox(0.035, 0.022, 0.015, 0.004, 0.002, 1), trimPlastic(),
      { pos: [-0.09 + (i % 3) * 0.09, y - 0.13 - Math.floor(i / 3) * 0.04, z - 0.21] }));
  }
  // Vents.
  for (const s of [-1, 1]) {
    g.add(mesh(roundedBox(0.13, 0.06, 0.03, 0.012, 0.004, 1), trimPlastic(),
      { pos: [s * 0.28, y + 0.04, z - 0.2] }));
  }
  // Grab handle for the passenger.
  g.add(mesh(tube([
    [-side * (spec.body.width * 0.18), y + 0.03, z - 0.2],
    [-side * (spec.body.width * 0.3), y + 0.06, z - 0.22],
    [-side * (spec.body.width * 0.36), y - 0.02, z - 0.19],
  ], 0.016, { radialSeg: 8 }), blackSteel(), { name: 'grabHandle' }));

  return g;
}

/** Three-spoke wheel; the returned group is what the rig rotates on Z. */
function buildSteeringWheel(radius = 0.185) {
  const g = new THREE.Group();
  g.name = 'steeringWheel';
  g.add(mesh(new THREE.TorusGeometry(radius, 0.018, 10, 30), rubber(), { name: 'rim' }));
  for (let i = 0; i < 3; i++) {
    const a = (i / 3) * Math.PI * 2 + Math.PI / 2;
    const spokeGeo = roundedBox(0.03, radius * 0.92, 0.014, 0.006, 0.002, 1);
    spokeGeo.translate(0, radius * 0.46, 0);
    spokeGeo.rotateZ(a - Math.PI / 2);
    g.add(mesh(spokeGeo, blackSteel(), { name: 'spoke' }));
  }
  g.add(mesh(lathe([[0, 0], [0.05, 0.006], [0.052, 0.03], [0, 0.034]], 16), trimPlastic(),
    { rot: [Math.PI / 2, 0, 0], name: 'hub' }));
  return g;
}

export function buildInterior(spec, m, { detail = 2 } = {}) {
  const g = new THREE.Group();
  g.name = 'interior';
  const side = spec.rhd ? -1 : 1;
  const seatZ = m.zCowl - 0.95;
  const floorY = m.sillY + 0.04;

  // Front seats.
  for (const s of [-1, 1]) {
    const seat = buildSeat({ harness: spec.features.cage });
    seat.position.set(s * (spec.body.width * 0.24), floorY, seatZ);
    g.add(seat);
  }
  // Rear bench on four-door bodies.
  if (spec.body.doors === 4 && detail > 1) {
    const bench = new THREE.Group();
    bench.add(mesh(roundedBox(spec.body.width - 0.2, 0.14, 0.46, 0.05, 0.01, 2), seatFabric(),
      { pos: [0, floorY + 0.2, 0] }));
    bench.add(mesh(roundedBox(spec.body.width - 0.2, 0.56, 0.12, 0.05, 0.01, 2), seatFabric(),
      { pos: [0, floorY + 0.48, -0.22], rot: [0.16, 0, 0] }));
    bench.position.set(0, 0, seatZ - 0.82);
    g.add(bench);
  }

  g.add(buildDash(spec, m));

  // Steering column + wheel, raked toward the driver.
  const column = new THREE.Group();
  column.name = 'steeringColumn';
  const wheel = buildSteeringWheel(spec.body.steeringR ?? 0.185);
  column.add(wheel);
  column.add(mesh(new THREE.CylinderGeometry(0.026, 0.03, 0.34, 12), trimPlastic(),
    { pos: [0, 0, -0.18], rot: [Math.PI / 2, 0, 0], name: 'column' }));
  column.position.set(side * (spec.body.width * 0.24), m.beltY - 0.14, m.zCowl - 0.42);
  column.rotation.x = -0.42;                 // lay the wheel back like a truck
  g.add(column);

  // Transmission tunnel with the main shifter and a transfer-case lever.
  const shifters = [];
  g.add(mesh(roundedBox(0.34, 0.26, 1.0, 0.06, 0.01, 2), bedliner(),
    { pos: [0, floorY + 0.1, m.zCowl - 0.7], name: 'tunnel' }));
  for (const [dx, len, label] of [[-0.02, 0.3, 'gear'], [0.13, 0.24, 'transfer']]) {
    const lever = new THREE.Group();
    lever.name = `shifter_${label}`;
    lever.add(mesh(new THREE.CylinderGeometry(0.013, 0.016, len, 8), zinc(),
      { pos: [0, len / 2, 0] }));
    lever.add(mesh(new THREE.SphereGeometry(0.032, 14, 10), trimPlastic(), { pos: [0, len, 0] }));
    lever.add(mesh(new THREE.ConeGeometry(0.05, 0.07, 12), rubber(), { pos: [0, 0.03, 0] }));
    lever.position.set(dx, floorY + 0.22, m.zCowl - 0.62);
    lever.userData.restZ = 0;
    shifters.push(lever);
    g.add(lever);
  }

  // Pedals.
  for (let i = 0; i < 3; i++) {
    g.add(mesh(roundedBox(0.06, 0.13, 0.02, 0.01, 0.003, 1), blackSteel(), {
      pos: [side * (spec.body.width * 0.24) + (i - 1) * 0.085, floorY + 0.14, m.zCowl - 0.26],
      rot: [-0.35, 0, 0], name: 'pedal',
    }));
  }

  // Rear-view mirror.
  g.add(mesh(roundedBox(0.22, 0.06, 0.03, 0.012, 0.004, 1), trimPlastic(),
    { pos: [0, m.roofY - 0.1, m.zCowl - m.rake + 0.12], rot: [0.2, 0, 0], name: 'rearMirror' }));

  // Floor mats so the cabin floor doesn't read as bare paint.
  g.add(mesh(roundedBox(spec.body.width - 0.16, 0.012, 1.3, 0.03, 0.004, 1), bedliner(),
    { pos: [0, floorY + 0.008, m.zCowl - 0.8], name: 'floorMat' }));

  g.userData.steeringWheel = wheel;
  g.userData.steeringColumn = column;
  g.userData.shifters = shifters;
  return g;
}
