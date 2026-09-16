/**
 * chassis.js - ladder frame, live axles, four-link suspension, drivetrain.
 *
 * COORDINATES (used by every vehicle module):
 *   origin = ground level, centred between the axles
 *   +X right, +Y up, +Z forward (nose)
 *   so a wheel hub sits at (track/2, tireRadius, wheelbase/2)
 *
 * ARTICULATION
 *   Suspension links are built once as unit-length geometry pointing down +Z
 *   with their origin at the chassis pivot. Each frame we only set a rotation
 *   and a Z scale (see AimedLink) - no geometry is rebuilt, so a fully
 *   articulated axle costs a handful of matrix updates.
 */
import * as THREE from 'three';
import {
  roundedBox, plate, tube, rod, lathe, coilSpring, mesh, mirrorX, mergeParts,
} from './parts.js';
import {
  rawSteel, blackSteel, castIron, zinc, powderCoat, bedliner,
  diamondPlate, trimPlastic, rubber,
} from './materials.js';

const FWD = new THREE.Vector3(0, 0, 1);

/**
 * A rigid link that stretches and aims between two points each frame.
 * Geometry must be unit length along +Z, origin at the pivot end.
 */
export class AimedLink {
  constructor(geometry, material, pivot) {
    this.pivot = new THREE.Vector3(...pivot);
    this.mesh = new THREE.Mesh(geometry, material);
    this.mesh.castShadow = this.mesh.receiveShadow = true;
    this.mesh.position.copy(this.pivot);
    this._up = new THREE.Vector3(0, 1, 0);
  }

  /** Point the link at `target` (vehicle-space) and stretch it to reach. */
  aim(target) {
    const d = target.clone().sub(this.mesh.position);
    const len = d.length();
    if (len < 1e-5) return;
    // Avoid a degenerate up-vector when the link is near-vertical.
    this._up.set(0, 1, 0);
    if (Math.abs(d.y / len) > 0.98) this._up.set(0, 0, 1);
    this.mesh.quaternion.setFromRotationMatrix(
      new THREE.Matrix4().lookAt(new THREE.Vector3(0, 0, 0), d.normalize(), this._up),
    );
    this.mesh.scale.z = len;
  }
}

/** Unit-length cylinder along +Z, origin at one end - the AimedLink blank. */
function unitRod(radius, seg = 8) {
  const g = new THREE.CylinderGeometry(radius, radius, 1, seg, 1, false);
  g.rotateX(Math.PI / 2);
  g.translate(0, 0, 0.5);
  return g;
}

/** Unit-length boxed link (stamped steel control arm) along +Z. */
function unitBoxLink(w, h) {
  const g = roundedBox(w, h, 1, Math.min(w, h) * 0.3, 0.004, 1);
  g.translate(0, 0, 0.5);
  return g;
}

/* --------------------------------------------------------------- frame ---- */

/**
 * Boxed ladder frame: two C-section rails with a kick-up over each axle,
 * tied together by crossmembers. This is the part every other component
 * bolts to, so its rail height sets the whole vehicle's stance.
 */
export function buildLadderFrame(spec) {
  const { wheelbase, frontOverhang, rearOverhang, frameWidth, railY, railHeight } = spec.frame;
  const g = new THREE.Group();
  g.name = 'frame';
  const steel = powderCoat(0x1d1f23);

  const zFront = wheelbase / 2 + frontOverhang;
  const zRear = -(wheelbase / 2 + rearOverhang);
  const kick = railHeight * 0.55;             // rise over the axles

  // Rail profile viewed from the side, as [z, y] pairs along the top edge.
  const railProfile = [
    [zRear, railY + kick], [zRear + rearOverhang * 0.55, railY + kick],
    [-wheelbase / 2 + 0.12, railY], [wheelbase / 2 - 0.12, railY],
    [zFront - frontOverhang * 0.55, railY + kick * 0.8], [zFront, railY + kick * 0.8],
  ];
  const top = railProfile.map(([z, y]) => [z, y + railHeight / 2]);
  const bottom = [...railProfile].reverse().map(([z, y]) => [z, y - railHeight / 2]);
  const railGeo = plate([...top, ...bottom], 0.075, { bevel: 0.008 });
  railGeo.rotateY(Math.PI / 2);               // plate extrudes on Z; rails run along Z
  mirrorX(g, railGeo, steel, { pos: [frameWidth / 2, 0, 0], name: 'rail' });

  // Crossmembers, including the tubular one under the transfer case.
  const cross = [];
  for (const [z, yOff, r] of [
    [zRear + 0.1, kick, 0.05], [-wheelbase / 2 + 0.05, 0.0, 0.045],
    [0, -0.01, 0.04], [wheelbase / 2 - 0.05, 0.0, 0.045], [zFront - 0.12, kick * 0.8, 0.05],
  ]) {
    const c = new THREE.CylinderGeometry(r, r, frameWidth, 10);
    c.rotateZ(Math.PI / 2);
    c.translate(0, railY + yOff, z);
    cross.push(c);
  }
  const cg = mergeParts(cross);
  cross.forEach((c) => c.dispose());
  g.add(mesh(cg, steel, { name: 'crossmembers' }));

  // Body mounts - small rubber pucks between rail and tub.
  const pucks = [];
  for (const z of [zRear + 0.25, -wheelbase * 0.2, wheelbase * 0.25, zFront - 0.3]) {
    for (const s of [-1, 1]) {
      const p = new THREE.CylinderGeometry(0.032, 0.032, 0.05, 10);
      p.translate(s * frameWidth / 2, railY + railHeight / 2 + 0.02, z);
      pucks.push(p);
    }
  }
  const pg = mergeParts(pucks);
  pucks.forEach((p) => p.dispose());
  g.add(mesh(pg, rubber(), { name: 'bodyMounts' }));

  return g;
}

/* ---------------------------------------------------------------- axle ---- */

/**
 * Live axle: a housing tube from hub to hub with an offset pumpkin, plus
 * knuckles. Returned group's origin is the axle centreline, so the suspension
 * code can translate and roll it as one rigid body (which is exactly what a
 * solid axle does).
 */
export function buildSolidAxle({ track, tubeR = 0.055, diffOffset = 0.18, steering = false, detail = 2 }) {
  const g = new THREE.Group();
  g.name = steering ? 'frontAxle' : 'rearAxle';
  const half = track / 2;
  const parts = [];

  // Housing tubes, stopping short of the hubs.
  for (const s of [-1, 1]) {
    const t = new THREE.CylinderGeometry(tubeR, tubeR, half - 0.09, detail > 1 ? 16 : 10);
    t.rotateZ(Math.PI / 2);
    t.translate(s * (half - 0.09) / 2 + (s * 0.045), 0, 0);
    parts.push(t);
  }

  // Pumpkin: the differential bulge, offset to one side like a real axle.
  const pump = new THREE.SphereGeometry(0.135, detail > 1 ? 20 : 12, detail > 1 ? 16 : 10);
  pump.scale(1.0, 1.05, 0.82);
  pump.translate(diffOffset, 0, 0);
  parts.push(pump);

  // Rear cover with its ring of bolts.
  const cover = lathe([
    [0, -0.1], [0.11, -0.115], [0.125, -0.06], [0.125, 0.0], [0, 0.0],
  ], detail > 1 ? 20 : 12);
  cover.rotateX(-Math.PI / 2);
  cover.rotateY(Math.PI);
  cover.translate(diffOffset, 0, -0.1);
  parts.push(cover);

  // Pinion snout pointing forward-ish toward the driveshaft.
  const snout = new THREE.CylinderGeometry(0.05, 0.062, 0.16, 12);
  snout.rotateX(Math.PI / 2);
  snout.translate(diffOffset, 0.01, 0.14);
  parts.push(snout);

  const housing = mergeParts(parts);
  parts.forEach((p) => p.dispose());
  housing.computeVertexNormals();
  g.add(mesh(housing, castIron(), { name: 'housing' }));

  // Knuckles / hub ends - these are where the wheels bolt on.
  const hubs = [];
  for (const s of [-1, 1]) {
    const knuckle = roundedBox(0.1, 0.16, 0.16, 0.03, 0.006, 2);
    knuckle.translate(s * (half - 0.05), 0, 0);
    hubs.push(knuckle);
    const flange = new THREE.CylinderGeometry(0.075, 0.075, 0.03, 12);
    flange.rotateZ(Math.PI / 2);
    flange.translate(s * (half - 0.005), 0, 0);
    hubs.push(flange);
  }
  const hg = mergeParts(hubs);
  hubs.forEach((h) => h.dispose());
  g.add(mesh(hg, blackSteel(), { name: 'knuckles' }));

  // Steering: tie rod between the knuckle arms, plus a drag link.
  if (steering) {
    const tie = new THREE.CylinderGeometry(0.018, 0.018, track - 0.16, 8);
    tie.rotateZ(Math.PI / 2);
    tie.translate(0, 0.02, -0.14);
    g.add(mesh(tie, zinc(), { name: 'tieRod' }));
    g.add(mesh(rod([half - 0.08, 0.02, -0.14], [diffOffset - 0.1, 0.09, -0.2], 0.016), zinc(),
      { name: 'dragLink' }));
  }

  // Diff guard and a breather hose - the small stuff that sells the read.
  if (detail > 1) {
    const guard = lathe([[0.12, -0.02], [0.145, 0.0], [0.145, 0.04], [0.12, 0.05]], 16);
    guard.rotateX(-Math.PI / 2);
    guard.translate(diffOffset, 0, -0.06);
    g.add(mesh(guard, rawSteel(), { name: 'diffGuard' }));
  }

  g.userData.hub = (side) => new THREE.Vector3(side * (half - 0.005), 0, 0);
  return g;
}

/* ---------------------------------------------------------- suspension ---- */

/**
 * Coilover shock. Returns a group plus setLength(): the body stays put, the
 * shaft slides, and the spring's pitch compresses - so full articulation is
 * visible without touching geometry.
 */
export function buildCoilover({ length = 0.42, springR = 0.055, color = 0xc23a2a }) {
  const g = new THREE.Group();
  g.name = 'coilover';

  const bodyLen = length * 0.5;
  const body = new THREE.CylinderGeometry(0.026, 0.028, bodyLen, 14);
  body.translate(0, bodyLen / 2, 0);
  g.add(mesh(body, powderCoat(color), { name: 'shockBody' }));

  const shaft = mesh(new THREE.CylinderGeometry(0.013, 0.013, length, 10), zinc(),
    { pos: [0, -length / 2, 0], name: 'shaft' });
  g.add(shaft);

  // Reservoir piggyback - an obvious offroad shock cue.
  g.add(mesh(new THREE.CylinderGeometry(0.019, 0.019, bodyLen * 0.6, 10), powderCoat(color),
    { pos: [0.05, bodyLen * 0.55, 0], rot: [0, 0, -0.12], name: 'reservoir' }));

  const spring = mesh(coilSpring(springR, length, 8, 0.011), powderCoat(0xd8d8dc),
    { pos: [0, -length * 0.5, 0], name: 'spring' });
  g.add(spring);

  // Eyelets top and bottom.
  for (const y of [length * 0.52, -length * 0.55]) {
    g.add(mesh(new THREE.TorusGeometry(0.022, 0.011, 8, 14), blackSteel(),
      { pos: [0, y, 0], rot: [0, Math.PI / 2, 0] }));
  }

  g.userData.restLength = length;
  g.userData.spring = spring;
  g.userData.shaft = shaft;
  /** @param {number} t 0 = fully compressed, 1 = fully extended. */
  g.userData.setLength = (t) => {
    const k = 0.55 + t * 0.45;                // spring never fully collapses
    spring.scale.y = k;
    spring.position.y = -length * 0.5 * k;
    shaft.position.y = -length * (0.28 + t * 0.24);
  };
  return g;
}

/**
 * Four-link + panhard bar for a solid axle. Everything that moves is an
 * AimedLink, collected into `links` for the per-frame update.
 */
export function buildFourLink({ spec, axleZ, track, axleY, steering }) {
  const { frameWidth, railY } = spec.frame;
  const g = new THREE.Group();
  g.name = 'suspension';
  const links = [];
  const armMat = powderCoat(0x24262b);
  const dir = Math.sign(axleZ) || 1;         // links trail away from the axle

  const lowerBlank = unitBoxLink(0.05, 0.062);
  const upperBlank = unitRod(0.024, 8);

  for (const s of [-1, 1]) {
    // Lower arms: wide-set, near frame level, doing the fore-aft work.
    const lower = new AimedLink(lowerBlank, armMat,
      [s * (frameWidth / 2 - 0.02), railY - 0.06, axleZ - dir * 0.62]);
    lower.target = new THREE.Vector3(s * (track / 2 - 0.34), axleY - 0.03, axleZ - dir * 0.02);
    links.push(lower);
    g.add(lower.mesh);

    // Upper arms: narrower and angled inward, controlling axle roll.
    const upper = new AimedLink(upperBlank, armMat,
      [s * (frameWidth / 2 - 0.06), railY + 0.12, axleZ - dir * 0.48]);
    upper.target = new THREE.Vector3(s * 0.19, axleY + 0.14, axleZ - dir * 0.03);
    links.push(upper);
    g.add(upper.mesh);
  }

  // Panhard bar locating the axle laterally.
  const panhard = new AimedLink(unitRod(0.02, 8), zinc(),
    [-(frameWidth / 2 - 0.01), railY - 0.02, axleZ + dir * 0.1]);
  panhard.target = new THREE.Vector3(track / 2 - 0.3, axleY + 0.1, axleZ + dir * 0.1);
  links.push(panhard);
  g.add(panhard.mesh);

  // Coilovers, mounted just inboard of the hubs.
  const shocks = [];
  for (const s of [-1, 1]) {
    const co = buildCoilover({
      length: spec.suspension.travel * 1.7,
      springR: 0.058,
      color: spec.suspension.shockColor ?? 0xc23a2a,
    });
    co.position.set(s * (track / 2 - 0.26), railY + 0.2, axleZ + dir * 0.06);
    co.userData.anchor = new THREE.Vector3(s * (track / 2 - 0.24), axleY + 0.02, axleZ + dir * 0.06);
    co.userData.side = s;
    shocks.push(co);
    g.add(co);
  }

  // Bump stops on the frame above the axle.
  for (const s of [-1, 1]) {
    g.add(mesh(new THREE.CylinderGeometry(0.03, 0.038, 0.09, 10), rubber(),
      { pos: [s * (track / 2 - 0.42), railY - 0.1, axleZ], name: 'bumpStop' }));
  }

  if (steering) {
    // Steering box and pitman arm hanging off the driver-side rail.
    g.add(mesh(roundedBox(0.11, 0.14, 0.11, 0.02, 0.005, 1), castIron(),
      { pos: [frameWidth / 2 + 0.04, railY + 0.02, axleZ - 0.16], name: 'steeringBox' }));
  }

  g.userData.links = links;
  g.userData.shocks = shocks;
  return g;
}

/* ---------------------------------------------------------- drivetrain ---- */

/** Engine, gearbox, transfer case, driveshafts, exhaust, fuel tank. */
export function buildDrivetrain(spec, detail = 2) {
  const { wheelbase, railY, frameWidth } = spec.frame;
  const g = new THREE.Group();
  g.name = 'drivetrain';

  // Engine block with a valve cover and an intake plenum.
  const block = new THREE.Group();
  block.add(mesh(roundedBox(0.44, 0.4, 0.5, 0.04, 0.008, 2), castIron(), { name: 'block' }));
  block.add(mesh(roundedBox(0.4, 0.09, 0.44, 0.03, 0.006, 2), powderCoat(0x2b2f35),
    { pos: [0, 0.24, 0], name: 'valveCover' }));
  block.add(mesh(roundedBox(0.3, 0.1, 0.24, 0.04, 0.006, 2), powderCoat(0x1a1c20),
    { pos: [0, 0.33, 0.02], name: 'intake' }));
  block.position.set(0, railY + 0.26, wheelbase * 0.28);
  g.add(block);

  // Gearbox tapering back into the transfer case.
  g.add(mesh(lathe([
    [0.14, 0], [0.14, 0.2], [0.1, 0.42], [0.1, 0.6], [0, 0.6],
  ], 14), castIron(), { pos: [0, railY + 0.16, wheelbase * 0.28 - 0.25], rot: [Math.PI / 2, 0, 0], name: 'gearbox' }));
  g.add(mesh(roundedBox(0.26, 0.28, 0.26, 0.05, 0.008, 2), castIron(),
    { pos: [-0.04, railY + 0.1, wheelbase * 0.28 - 0.62], name: 'transferCase' }));

  // Driveshafts with their U-joints. Front one clears the pumpkin offset.
  const shafts = [];
  for (const [z0, z1, x1] of [
    [wheelbase * 0.28 - 0.7, wheelbase / 2 - 0.05, spec.axle.diffOffset],
    [wheelbase * 0.28 - 0.7, -wheelbase / 2 + 0.1, spec.axle.diffOffset * 0.6],
  ]) {
    shafts.push(rod([-0.04, railY + 0.08, z0], [x1, railY - 0.16, z1], 0.028, 8));
  }
  const sg = mergeParts(shafts);
  shafts.forEach((s) => s.dispose());
  g.add(mesh(sg, zinc(), { name: 'driveshafts' }));

  // Exhaust: header collector back to a muffler and a turned-down tip.
  if (detail > 1) {
    const path = [
      [0.22, railY + 0.18, wheelbase * 0.28 - 0.1],
      [0.3, railY - 0.02, wheelbase * 0.1],
      [frameWidth / 2 + 0.03, railY - 0.06, -wheelbase * 0.2],
      [frameWidth / 2 + 0.05, railY - 0.04, -wheelbase / 2 - 0.15],
    ];
    g.add(mesh(tube(path, 0.032, { radialSeg: 10 }), rawSteel(), { name: 'exhaust' }));
    g.add(mesh(lathe([[0, 0], [0.075, 0.02], [0.075, 0.34], [0, 0.36]], 14), rawSteel(),
      { pos: [frameWidth / 2 + 0.03, railY - 0.06, -wheelbase * 0.12], rot: [Math.PI / 2, 0, 0], name: 'muffler' }));
  }

  // Fuel tank with a skid strap.
  g.add(mesh(roundedBox(frameWidth * 0.82, 0.22, 0.5, 0.05, 0.008, 2), blackSteel(),
    { pos: [-0.03, railY - 0.06, -wheelbase * 0.36], name: 'fuelTank' }));

  return g;
}

/* -------------------------------------------------------- protection ---- */

/** Skid plates and rock sliders - the armour that makes it look purposeful. */
export function buildArmour(spec, detail = 2) {
  const { wheelbase, frameWidth, railY } = spec.frame;
  const g = new THREE.Group();
  g.name = 'armour';

  // Engine and transfer-case skids.
  for (const [z, len, w] of [
    [wheelbase * 0.3, 0.72, frameWidth + 0.06],
    [wheelbase * 0.28 - 0.6, 0.55, frameWidth * 0.8],
  ]) {
    const skid = plate([
      [-w / 2, 0], [w / 2, 0], [w / 2 - 0.05, -len * 0.5],
      [-w / 2 + 0.05, -len * 0.5],
    ], 0.014, { bevel: 0.004 });
    skid.rotateX(-Math.PI / 2);
    g.add(mesh(skid, diamondPlate(), { pos: [0, railY - 0.12, z + len * 0.25], name: 'skidPlate' }));
  }

  // Rock sliders: a tube with a step plate, kicked out past the body line.
  if (spec.features.sliders) {
    const y = railY - 0.1;
    const z0 = wheelbase / 2 - 0.35, z1 = -wheelbase / 2 + 0.3;
    for (const s of [-1, 1]) {
      const x = frameWidth / 2 + 0.13;
      const sliderTube = tube([
        [s * (frameWidth / 2 - 0.02), y + 0.03, z0 + 0.22],
        [s * x, y, z0], [s * x, y, z1],
        [s * (frameWidth / 2 - 0.02), y + 0.03, z1 - 0.2],
      ], 0.036, { radialSeg: 10, tension: 0.35 });
      g.add(mesh(sliderTube, powderCoat(0x1b1d21), { name: 'slider' }));

      if (detail > 1) {
        // Shape is 0.12 wide on X and runs its length up +Y; a +90deg X
        // rotation lays that length along +Z with the thickness in Y.
        const step = plate([[-0.06, 0], [0.06, 0], [0.06, z0 - z1], [-0.06, z0 - z1]], 0.01);
        step.rotateX(Math.PI / 2);
        g.add(mesh(step, diamondPlate(), { pos: [s * (x - 0.02), y + 0.035, z1], name: 'sliderStep' }));
      }
    }
  }

  return g;
}
