/**
 * accessories.js - the bolt-on kit that makes a 4x4 read as built rather
 * than bought: roll cage, winch bumper, snorkel, light bar, roof rack,
 * spare and recovery gear.
 *
 * Every builder takes (spec, metrics) from body.js so parts land correctly
 * on any wheelbase or body height.
 */
import * as THREE from 'three';
import { roundedBox, plate, tube, rod, lathe, arcTorus, mesh, mergeParts } from './parts.js';
import {
  powderCoat, blackSteel, rawSteel, zinc, chrome, trimPlastic, rubber,
  diamondPlate, lens, reflector, bedliner, makePaint,
} from './materials.js';
import { buildTire, buildRim } from './wheel.js';

/* ------------------------------------------------------------ roll cage ---- */

/**
 * Internal cage: main hoop, A-pillar bars, rear stays, door bars and a
 * harness bar. Built as one tube network so it welds together visually.
 */
export function buildRollCage(spec, m, { color = 0xd8d2c4 } = {}) {
  const g = new THREE.Group();
  g.name = 'rollCage';
  const R = 0.028;
  const mat = powderCoat(color);
  const x = m.halfW - 0.08;
  const yTop = m.roofY - 0.05;
  const yFloor = m.sillY + 0.04;
  const zHoop = spec.body.doors === 4 ? -spec.frame.wheelbase * 0.02 : m.zCowl - 1.0;
  const zWs = m.zCowl - m.rake;

  const pieces = [];

  // Main hoop: up one side, across the roof, down the other.
  pieces.push(tube([
    [x, yFloor, zHoop], [x, m.beltY, zHoop], [x - 0.02, yTop - 0.04, zHoop],
    [x - 0.06, yTop, zHoop], [-(x - 0.06), yTop, zHoop],
    [-(x - 0.02), yTop - 0.04, zHoop], [-x, m.beltY, zHoop], [-x, yFloor, zHoop],
  ], R, { radialSeg: 10, tension: 0.4 }));

  // A-pillar bars following the windshield, tying into the hoop at the roof.
  for (const s of [-1, 1]) {
    pieces.push(tube([
      [s * x, yFloor, m.zCowl - 0.1], [s * x, m.beltY, m.zCowl - 0.08],
      [s * (x - 0.05), yTop, zWs + 0.05], [s * (x - 0.06), yTop, zHoop],
    ], R, { radialSeg: 10, tension: 0.4 }));
    // Rear stays running back to the body.
    pieces.push(tube([
      [s * (x - 0.06), yTop, zHoop], [s * (x - 0.02), m.beltY + 0.1, zHoop - 0.5],
      [s * x, yFloor + 0.05, m.zCabRear + 0.12],
    ], R * 0.92, { radialSeg: 8, tension: 0.4 }));
    // Door bar (NASCAR-style X leg) across the opening.
    pieces.push(rod([s * x, m.beltY - 0.06, zHoop - 0.02],
      [s * x, yFloor + 0.18, m.zCowl - 0.12], R * 0.85, 8));
  }

  // Windshield header bar and harness bar.
  pieces.push(rod([x - 0.05, yTop, zWs + 0.05], [-(x - 0.05), yTop, zWs + 0.05], R * 0.9, 8));
  pieces.push(rod([x - 0.02, m.beltY + 0.14, zHoop - 0.02],
    [-(x - 0.02), m.beltY + 0.14, zHoop - 0.02], R * 0.85, 8));

  const merged = mergeParts(pieces);
  pieces.forEach((p) => p.dispose());
  merged.computeVertexNormals();
  g.add(mesh(merged, mat, { name: 'cageTubes' }));

  // Footplates where the cage lands on the floor.
  const plates = [];
  for (const s of [-1, 1]) {
    for (const z of [zHoop, m.zCowl - 0.1, m.zCabRear + 0.12]) {
      const p = roundedBox(0.1, 0.008, 0.1, 0.014, 0.003, 1);
      p.translate(s * x, yFloor, z);
      plates.push(p);
    }
  }
  const pg = mergeParts(plates);
  plates.forEach((p) => p.dispose());
  g.add(mesh(pg, rawSteel(), { name: 'cagePlates' }));

  // Padding on the bars nearest the occupants' heads.
  for (const s of [-1, 1]) {
    g.add(mesh(new THREE.CylinderGeometry(R * 1.7, R * 1.7, 0.3, 10), rubber(),
      { pos: [s * x, m.beltY + 0.2, zHoop], name: 'cagePadding' }));
  }
  return g;
}

/* --------------------------------------------------------------- bumpers ---- */

/** Winch-ready front bumper: tube hoop, wings, D-rings and the winch itself. */
export function buildFrontBumper(spec, m, { winch = true } = {}) {
  const g = new THREE.Group();
  g.name = 'frontBumper';
  const z = m.zFront + 0.11;
  const y = m.sillY + 0.06;
  const x = m.halfW + 0.04;
  const mat = powderCoat(0x1b1d21);

  // Main beam with wrapped wings.
  g.add(mesh(tube([
    [-x, y, z - 0.26], [-x * 0.94, y, z - 0.02], [-x * 0.6, y, z],
    [x * 0.6, y, z], [x * 0.94, y, z - 0.02], [x, y, z - 0.26],
  ], 0.055, { radialSeg: 12, tension: 0.35 }), mat, { name: 'bumperBeam' }));

  // Face plate and a bash bar below.
  g.add(mesh(roundedBox(spec.body.width - 0.06, 0.2, 0.05, 0.03, 0.006, 2), mat,
    { pos: [0, y + 0.1, z - 0.03], name: 'bumperFace' }));
  g.add(mesh(tube([
    [-x * 0.8, y - 0.14, z - 0.06], [0, y - 0.16, z - 0.02], [x * 0.8, y - 0.14, z - 0.06],
  ], 0.032, { radialSeg: 8 }), mat, { name: 'bashBar' }));

  // Recovery D-rings on shackle tabs.
  for (const s of [-1, 1]) {
    g.add(mesh(roundedBox(0.012, 0.09, 0.07, 0.006, 0.003, 1), rawSteel(),
      { pos: [s * 0.3, y + 0.04, z - 0.02] }));
    g.add(mesh(arcTorus(0.042, 0.011, Math.PI * 1.55, 8, 18), zinc(),
      { pos: [s * 0.31, y + 0.02, z + 0.01], rot: [0, Math.PI / 2, -0.4], name: 'dRing' }));
  }

  if (winch) {
    const w = new THREE.Group();
    w.name = 'winch';
    // Drum, motor and gearbox ends.
    w.add(mesh(new THREE.CylinderGeometry(0.06, 0.06, 0.24, 16), zinc(),
      { rot: [0, 0, Math.PI / 2], name: 'drum' }));
    // Wound synthetic line.
    w.add(mesh(new THREE.CylinderGeometry(0.082, 0.082, 0.22, 20), trimPlastic(),
      { rot: [0, 0, Math.PI / 2], name: 'line' }));
    for (const s of [-1, 1]) {
      w.add(mesh(lathe([[0, 0], [0.055, 0.01], [0.058, 0.13], [0.03, 0.15], [0, 0.15]], 14),
        powderCoat(0x8c1f1f), { pos: [s * 0.13, 0, 0], rot: [0, 0, -s * Math.PI / 2], name: 'winchMotor' }));
    }
    w.position.set(0, y + 0.12, z - 0.13);
    g.add(w);

    // Hawse fairlead with the hook parked in it.
    g.add(mesh(roundedBox(0.16, 0.07, 0.035, 0.02, 0.005, 2), rawSteel(),
      { pos: [0, y + 0.1, z + 0.02], name: 'fairlead' }));
    g.add(mesh(arcTorus(0.032, 0.01, Math.PI * 1.7, 8, 16), zinc(),
      { pos: [0, y + 0.06, z + 0.05], rot: [Math.PI / 2, 0, 0], name: 'winchHook' }));
  }
  return g;
}

export function buildRearBumper(spec, m) {
  const g = new THREE.Group();
  g.name = 'rearBumper';
  const z = m.zRear - 0.1;
  const y = m.sillY + 0.04;
  const x = m.halfW + 0.03;
  const mat = powderCoat(0x1b1d21);

  g.add(mesh(tube([
    [-x, y, z + 0.22], [-x * 0.9, y, z], [x * 0.9, y, z], [x, y, z + 0.22],
  ], 0.05, { radialSeg: 12, tension: 0.35 }), mat, { name: 'rearBeam' }));
  // Step pad and a receiver hitch.
  g.add(mesh(roundedBox(0.34, 0.012, 0.12, 0.012, 0.004, 1), diamondPlate(),
    { pos: [0, y + 0.05, z - 0.01], name: 'stepPad' }));
  g.add(mesh(roundedBox(0.07, 0.07, 0.2, 0.008, 0.003, 1), rawSteel(),
    { pos: [0, y - 0.02, z + 0.1], name: 'hitch' }));
  for (const s of [-1, 1]) {
    g.add(mesh(arcTorus(0.04, 0.011, Math.PI * 1.55, 8, 18), zinc(),
      { pos: [s * 0.26, y - 0.01, z - 0.01], rot: [0, Math.PI / 2, 0.4], name: 'dRing' }));
  }
  return g;
}

/* --------------------------------------------------------------- snorkel ---- */

export function buildSnorkel(spec, m, side = 1) {
  const g = new THREE.Group();
  g.name = 'snorkel';
  const x = side * (m.halfW + 0.03);
  const path = [
    [x - side * 0.06, m.hoodY - 0.12, m.zCowl + 0.18],
    [x, m.hoodY + 0.02, m.zCowl + 0.1],
    [x, m.beltY + 0.12, m.zCowl - 0.02],
    [x, m.roofY - 0.14, m.zCowl - m.rake * 0.55],
  ];
  g.add(mesh(tube(path, 0.045, { radialSeg: 12, tension: 0.4 }), trimPlastic(), { name: 'snorkelTube' }));
  // Ram-air head facing forward.
  g.add(mesh(lathe([
    [0, 0], [0.058, 0.01], [0.062, 0.12], [0.05, 0.14], [0.05, 0.02], [0, 0.02],
  ], 16), trimPlastic(), {
    pos: [x, m.roofY - 0.1, m.zCowl - m.rake * 0.55], rot: [-Math.PI / 2, 0, 0], name: 'ramHead',
  }));
  // Hose clamps.
  for (const t of [0.25, 0.6]) {
    const i = Math.floor(t * (path.length - 1));
    g.add(mesh(new THREE.TorusGeometry(0.048, 0.006, 6, 14), zinc(),
      { pos: path[i], rot: [Math.PI / 2 - 0.4, 0, 0] }));
  }
  return g;
}

/* ------------------------------------------------------------- light bar ---- */

export function buildLightBar(spec, m, { pods = 6, width = null, y = null, z = null } = {}) {
  const g = new THREE.Group();
  g.name = 'lightBar';
  const w = width ?? spec.body.width - 0.14;
  const yy = y ?? m.roofY + 0.08;
  const zz = z ?? m.zCowl - m.rake + 0.02;
  const lamps = [];

  g.add(mesh(roundedBox(w, 0.07, 0.06, 0.012, 0.004, 1), powderCoat(0x17191d),
    { pos: [0, yy, zz], name: 'barHousing' }));
  for (let i = 0; i < pods; i++) {
    const px = -w / 2 + (w / pods) * (i + 0.5);
    const lm = lens(0xf2f8ff, false, 5.0);
    g.add(mesh(new THREE.CylinderGeometry(w / pods * 0.36, w / pods * 0.36, 0.02, 14), lm,
      { pos: [px, yy, zz + 0.032], rot: [Math.PI / 2, 0, 0], name: 'pod', castShadow: false }));
    lamps.push({ node: g, kind: 'aux', material: lm });
  }
  // Mounting feet.
  for (const s of [-1, 1]) {
    g.add(mesh(roundedBox(0.03, 0.09, 0.05, 0.008, 0.003, 1), powderCoat(0x17191d),
      { pos: [s * (w / 2 - 0.03), yy - 0.07, zz] }));
  }
  g.userData.lamps = lamps;
  return g;
}

/* ------------------------------------------------------------- roof rack ---- */

export function buildRoofRack(spec, m, { cargo = true } = {}) {
  const g = new THREE.Group();
  g.name = 'roofRack';
  const zFront = m.zCowl - m.rake - 0.02;
  const zRear = m.zCabRear + 0.02;
  const len = Math.abs(zFront - zRear);
  const w = spec.body.width - 0.04;
  const y = m.roofY + 0.055;
  const mat = powderCoat(0x202329);

  // Perimeter rail.
  g.add(mesh(tube([
    [-w / 2, y, zRear], [-w / 2, y, zFront], [w / 2, y, zFront], [w / 2, y, zRear], [-w / 2, y, zRear],
  ], 0.022, { radialSeg: 8, tension: 0.0, closed: true }), mat, { name: 'rackRail' }));

  // Slatted deck.
  const slats = [];
  const n = Math.max(5, Math.round(len / 0.13));
  for (let i = 0; i < n; i++) {
    const s = roundedBox(w - 0.03, 0.012, 0.045, 0.005, 0.002, 1);
    s.translate(0, y - 0.012, zRear + (len / (n - 1)) * i * Math.sign(zFront - zRear));
    slats.push(s);
  }
  const sg = mergeParts(slats);
  slats.forEach((s) => s.dispose());
  g.add(mesh(sg, mat, { name: 'rackDeck' }));

  // Feet down to the gutters.
  for (const s of [-1, 1]) {
    for (const t of [0.15, 0.5, 0.85]) {
      g.add(mesh(roundedBox(0.03, 0.055, 0.05, 0.008, 0.003, 1), mat,
        { pos: [s * (w / 2 - 0.01), y - 0.04, zRear + (zFront - zRear) * t] }));
    }
  }

  if (cargo) {
    // Jerry cans.
    for (let i = 0; i < 2; i++) {
      const can = new THREE.Group();
      can.add(mesh(roundedBox(0.17, 0.35, 0.09, 0.022, 0.006, 2), powderCoat(0x3f4a37), { name: 'jerryCan' }));
      can.add(mesh(roundedBox(0.09, 0.02, 0.06, 0.008, 0.003, 1), powderCoat(0x2e3629),
        { pos: [0, 0.18, 0] }));
      can.position.set(-w / 2 + 0.11 + i * 0.12, y + 0.17, zRear + len * 0.22);
      can.rotation.y = Math.PI / 2;
      g.add(can);
    }
    // Traction boards strapped to the side.
    for (let i = 0; i < 2; i++) {
      g.add(mesh(roundedBox(0.28, 0.022, len * 0.5, 0.02, 0.005, 1), powderCoat(0xd2761f),
        { pos: [w / 2 - 0.2 - i * 0.03, y + 0.02 + i * 0.026, zRear + len * 0.55], name: 'tractionBoard' }));
    }
    // Rolled recovery strap.
    g.add(mesh(new THREE.TorusGeometry(0.075, 0.028, 8, 18), powderCoat(0x8d3030),
      { pos: [0, y + 0.03, zFront - 0.12], rot: [Math.PI / 2, 0, 0], name: 'strap' }));
  }
  return g;
}

/* ----------------------------------------------------------------- spare ---- */

export function buildSpare(spec, m, { detail = 2 } = {}) {
  const g = new THREE.Group();
  g.name = 'spare';
  const t = buildTire({ ...spec.tire, detail: Math.max(1, detail - 1) });
  const r = buildRim({
    rimInch: spec.tire.rimInch, width: spec.tire.width * 0.88,
    spokes: spec.tire.spokes, color: spec.tire.rimColor, detail: Math.max(1, detail - 1),
  });
  g.add(t, r);
  g.rotation.y = Math.PI / 2;               // stand it up facing rearward
  const isPickup = spec.body.style === 'pickup';
  // Centre it on the tailgate. A 35" spare is taller than the tailgate and
  // will stand proud of it - that is correct, not a bug.
  g.position.set(
    isPickup ? m.halfW - 0.3 : 0,
    isPickup ? m.sillY + 0.3 : (m.sillY + m.beltY) / 2 + 0.1,
    isPickup ? m.zCabRear - 0.3 : m.zRear - 0.22,
  );
  if (isPickup) g.rotation.set(0, 0, Math.PI / 2);

  // Carrier arm.
  if (!isPickup) {
    g.add(mesh(roundedBox(0.06, 0.06, 0.16, 0.012, 0.004, 1), powderCoat(0x1b1d21),
      { pos: [0, 0, 0.14], rot: [0, Math.PI / 2, 0], name: 'carrier' }));
  }
  return g;
}

/* ------------------------------------------------------------ misc kit ---- */

export function buildMiscKit(spec, m) {
  const g = new THREE.Group();
  g.name = 'kit';
  // Whip antenna - reads as an offroad radio rig and catches the light.
  g.add(mesh(new THREE.CylinderGeometry(0.004, 0.002, 1.2, 6), trimPlastic(),
    { pos: [m.halfW - 0.02, m.hoodY + 0.62, m.zCowl + 0.04], rot: [0, 0, -0.08], name: 'antenna' }));
  g.add(mesh(new THREE.CylinderGeometry(0.014, 0.014, 0.03, 10), blackSteel(),
    { pos: [m.halfW - 0.02, m.hoodY + 0.02, m.zCowl + 0.04] }));
  // Hi-lift jack on the sill.
  g.add(mesh(roundedBox(0.03, 0.05, 1.0, 0.006, 0.002, 1), rawSteel(),
    { pos: [-(m.halfW + 0.02), m.sillY + 0.3, m.zCowl - 0.75], rot: [0.1, 0, 0], name: 'hiLift' }));
  return g;
}
