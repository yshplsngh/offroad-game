/**
 * wheel.js - procedural mud-terrain tire + beadlock wheel.
 *
 * The tire is modelled, not textured: every lug is real geometry, arranged in
 * a staggered three-row pattern with shoulder lugs that wrap onto the
 * sidewall. That matters here because an offroad camera spends its life
 * looking at the wheels while they articulate over rocks.
 *
 * The wheel axis is +X. Spin happens on X, steering on Y.
 */
import * as THREE from 'three';
import {
  roundedBox, lathe, hexBolt, arcTorus, mesh, mergeParts,
} from './parts.js';
import { rubber, treadRubber, blackSteel, zinc, castIron, powderCoat } from './materials.js';

const TAU = Math.PI * 2;

/* ---------------------------------------------------------------- tire ---- */

/**
 * Tire carcass: a closed revolve whose cross-section runs bead -> sidewall ->
 * shoulder -> crown -> shoulder -> sidewall -> bead, then straight back along
 * the bead radius to seal the solid.
 */
function carcass(R, width, rimR, seg) {
  const hw = width / 2;
  const sidewallBulge = R * 0.055;            // how fat the sidewall sits
  const p = [];
  const push = (r, y) => p.push([r, y]);

  push(rimR, -hw * 0.92);
  push(rimR * 1.02, -hw * 0.99);              // bead hook
  push(rimR * 1.12, -hw);
  // Sidewall sweeping out and up to the shoulder.
  const midR = rimR + (R - rimR) * 0.55;
  push(midR, -hw - sidewallBulge * 0.55);
  push(R * 0.9, -hw - sidewallBulge * 0.3);
  push(R * 0.965, -hw * 0.9);                 // shoulder break
  push(R * 0.995, -hw * 0.72);
  push(R, -hw * 0.45);                        // crown
  push(R, 0);
  push(R, hw * 0.45);
  push(R * 0.995, hw * 0.72);
  push(R * 0.965, hw * 0.9);
  push(R * 0.9, hw + sidewallBulge * 0.3);
  push(midR, hw + sidewallBulge * 0.55);
  push(rimR * 1.12, hw);
  push(rimR * 1.02, hw * 0.99);
  push(rimR, hw * 0.92);
  push(rimR, -hw * 0.92);                     // seal the section

  const g = lathe(p, seg);
  g.rotateZ(Math.PI / 2);                     // lathe spins on Y; wheel spins on X
  return g;
}

/** One tread lug, tilted about its own radial axis then swung into place. */
function lug(w, h, len, angle, lateral, tilt, R, corner = 0.012) {
  const g = roundedBox(w, h, len, corner, 0.005, 1);
  g.rotateY(tilt);
  g.translate(lateral, R - h * 0.42, 0);
  g.rotateX(angle);
  return g;
}

function tread(R, width, lugCount, detail) {
  const parts = [];
  const hw = width / 2;
  const depth = R * 0.075;                    // lug height above the carcass
  const corner = detail > 1 ? 0.014 : 0.02;

  for (let i = 0; i < lugCount; i++) {
    const a = (i / lugCount) * TAU;
    const odd = i % 2 === 1;
    const alt = i % 4 < 2;

    // Centre row: two staggered blocks straddling the centreline, angled into
    // a soft V so the pattern self-cleans - the mud-terrain signature.
    const cw = width * 0.24, cl = R * 0.2;
    parts.push(lug(cw, depth, cl, a, odd ? width * 0.13 : -width * 0.13, odd ? 0.28 : -0.28, R, corner));
    parts.push(lug(cw * 0.8, depth * 0.94, cl * 0.8, a + TAU / lugCount * 0.5,
      odd ? -width * 0.1 : width * 0.1, odd ? -0.2 : 0.2, R, corner));

    // Shoulder rows: bigger, squarer blocks that carry the sidewall bite.
    for (const side of [-1, 1]) {
      const sw = width * 0.3, sl = R * 0.26 * (alt ? 1 : 0.86);
      const g = roundedBox(sw, depth * 1.12, sl, corner, 0.005, 1);
      g.rotateY(side * 0.12);
      g.rotateZ(side * 0.22);                 // cant outward over the shoulder
      g.translate(side * (hw - sw * 0.34), R * 0.985 - depth * 0.42, 0);
      g.rotateX(a + (side > 0 ? TAU / lugCount * 0.25 : 0));
      parts.push(g);
    }

    // Sidewall lugs - half-height blocks below the shoulder for rut crawling.
    if (detail > 1 && alt) {
      for (const side of [-1, 1]) {
        const g = roundedBox(width * 0.1, R * 0.05, R * 0.15, 0.01, 0.004, 1);
        g.rotateZ(side * 0.95);
        g.translate(side * (hw + R * 0.012), R * 0.82, 0);
        g.rotateX(a + TAU / lugCount * 0.5);
        parts.push(g);
      }
    }
  }
  const merged = mergeParts(parts);
  parts.forEach((g) => g.dispose());
  merged.computeVertexNormals();
  return merged;
}

/**
 * @param {object} o
 * @param {number} o.diameter  Overall tire diameter in metres (35" ~= 0.889).
 * @param {number} o.width     Section width in metres (12.5" ~= 0.318).
 * @param {number} o.rimInch   Rim diameter in inches.
 * @param {number} o.detail    0 = far LOD, 1 = mid, 2 = hero.
 */
export function buildTire({ diameter = 0.889, width = 0.318, rimInch = 17, detail = 2 } = {}) {
  const R = diameter / 2;
  const rimR = (rimInch * 0.0254) / 2;
  const seg = detail > 1 ? 48 : detail > 0 ? 28 : 16;
  const lugs = detail > 1 ? 26 : detail > 0 ? 16 : 10;

  const g = new THREE.Group();
  g.name = 'tire';
  g.add(mesh(carcass(R, width, rimR, seg), rubber(), { name: 'carcass' }));
  g.add(mesh(tread(R, width, lugs, detail), treadRubber(), { name: 'tread' }));

  if (detail > 1) {
    // Raised sidewall ring the lettering would sit on.
    for (const side of [-1, 1]) {
      const ring = arcTorus(R * 0.74, 0.006, TAU, 6, 40);
      ring.rotateY(Math.PI / 2);
      ring.translate(side * (width / 2 + R * 0.016), 0, 0);
      g.add(mesh(ring, rubber(), { name: 'sidewallRing' }));
    }
  }
  return g;
}

/* ---------------------------------------------------------------- rim ---- */

function barrel(rimR, width, seg) {
  const hw = width / 2;
  const p = [
    [rimR * 0.98, -hw], [rimR, -hw * 0.94], [rimR * 0.9, -hw * 0.88],
    [rimR * 0.88, -hw * 0.2], [rimR * 0.9, hw * 0.4],                 // drop centre
    [rimR * 0.9, hw * 0.88], [rimR, hw * 0.94], [rimR * 0.98, hw],
    [rimR * 0.94, hw],        [rimR * 0.86, hw * 0.9],                // inner wall back
    [rimR * 0.84, -hw * 0.2], [rimR * 0.86, -hw * 0.9], [rimR * 0.94, -hw],
    [rimR * 0.98, -hw],
  ];
  const g = lathe(p, seg);
  g.rotateZ(Math.PI / 2);
  return g;
}

/** Tapered spoke running from the hub boss out to the rim lip. */
function spoke(innerR, outerR, thick, wIn, wOut, faceX) {
  const len = outerR - innerR;
  const shape = new THREE.Shape();
  shape.moveTo(-wIn / 2, 0);
  shape.lineTo(wIn / 2, 0);
  shape.lineTo(wOut / 2, len);
  shape.lineTo(-wOut / 2, len);
  shape.closePath();
  const g = new THREE.ExtrudeGeometry(shape, {
    depth: thick, bevelEnabled: true, bevelThickness: 0.006,
    bevelSize: 0.006, bevelSegments: 1, curveSegments: 1,
  });
  // Shape lives in XY with length on +Y; stand it up in the wheel plane.
  g.rotateY(Math.PI / 2);
  g.translate(faceX - thick / 2, innerR, 0);
  return g;
}

export function buildRim({
  rimInch = 17, width = 0.28, spokes = 6, beadlock = true,
  color = 0x2b2e33, detail = 2,
} = {}) {
  const rimR = (rimInch * 0.0254) / 2;
  const seg = detail > 1 ? 40 : 24;
  const faceX = width * 0.28;                 // how far the face sits outboard
  const g = new THREE.Group();
  g.name = 'rim';

  const body = [barrel(rimR, width, seg)];

  // Hub boss + face dish.
  const boss = lathe([
    [0, faceX - 0.012], [0.055, faceX - 0.012], [0.06, faceX - 0.004],
    [0.062, faceX + 0.01], [0.05, faceX + 0.012], [0, faceX + 0.012],
  ], seg);
  boss.rotateZ(Math.PI / 2);
  body.push(boss);

  for (let i = 0; i < spokes; i++) {
    const s = spoke(0.052, rimR * 0.93, 0.022, 0.062, 0.088, faceX);
    s.rotateX((i / spokes) * TAU);
    body.push(s);
  }
  const rimMat = powderCoat(color);
  const merged = mergeParts(body);
  body.forEach((b) => b.dispose());
  merged.computeVertexNormals();
  g.add(mesh(merged, rimMat, { name: 'rimBody' }));

  // Beadlock ring: the outer clamp that pins the tire bead, plus its bolts.
  if (beadlock) {
    const ring = lathe([
      [rimR * 0.9, width / 2], [rimR * 1.0, width / 2],
      [rimR * 1.0, width / 2 + 0.014], [rimR * 0.9, width / 2 + 0.014],
      [rimR * 0.9, width / 2],
    ], seg);
    ring.rotateZ(Math.PI / 2);
    g.add(mesh(ring, blackSteel(), { name: 'beadlockRing' }));

    const boltCount = detail > 1 ? 24 : 12;
    const bolts = [];
    for (let i = 0; i < boltCount; i++) {
      const a = (i / boltCount) * TAU;
      const b = hexBolt(0.0075, 0.007);
      b.rotateY(Math.PI / 2);                 // bolts face outboard along +X
      b.translate(width / 2 + 0.014, Math.cos(a) * rimR * 0.95, Math.sin(a) * rimR * 0.95);
      bolts.push(b);
    }
    const bg = mergeParts(bolts);
    bolts.forEach((b) => b.dispose());
    g.add(mesh(bg, zinc(), { name: 'beadlockBolts' }));
  }

  // Lug nuts on the hub face.
  const lugs = [];
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * TAU;
    const n = new THREE.CylinderGeometry(0.011, 0.012, 0.016, 6);
    n.rotateZ(Math.PI / 2);
    n.translate(faceX + 0.014, Math.cos(a) * 0.038, Math.sin(a) * 0.038);
    lugs.push(n);
  }
  const lg = mergeParts(lugs);
  lugs.forEach((l) => l.dispose());
  g.add(mesh(lg, zinc(), { name: 'lugNuts' }));

  return g;
}

/* -------------------------------------------------------------- brakes ---- */

export function buildBrakes({ rimInch = 17, side = 1, detail = 2 } = {}) {
  const rimR = (rimInch * 0.0254) / 2;
  const discR = rimR * 0.76;
  const g = new THREE.Group();
  g.name = 'brakes';

  // Vented rotor: two faces with a gap, plus a top hat.
  const parts = [];
  for (const x of [-0.016, 0.016]) {
    const face = new THREE.CylinderGeometry(discR, discR, 0.008, detail > 1 ? 32 : 18, 1, false);
    face.rotateZ(Math.PI / 2);
    face.translate(x, 0, 0);
    parts.push(face);
  }
  const hat = lathe([
    [0.045, -0.02], [discR * 0.55, -0.02], [discR * 0.55, 0.02], [0.045, 0.02], [0.045, -0.02],
  ], detail > 1 ? 24 : 14);
  hat.rotateZ(Math.PI / 2);
  parts.push(hat);

  if (detail > 1) {
    // Cross-drilling: a ring of through-holes reads instantly as a disc brake.
    for (let i = 0; i < 20; i++) {
      const a = (i / 20) * TAU;
      const hole = new THREE.CylinderGeometry(0.006, 0.006, 0.05, 6);
      hole.rotateZ(Math.PI / 2);
      hole.translate(0, Math.cos(a) * discR * 0.8, Math.sin(a) * discR * 0.8);
      parts.push(hole);
    }
  }
  const rotor = mergeParts(parts);
  parts.forEach((p) => p.dispose());
  rotor.computeVertexNormals();
  g.add(mesh(rotor, castIron(), { name: 'rotor' }));

  // Caliper straddling the rotor at the rear of the wheel.
  const cal = new THREE.Group();
  cal.add(mesh(roundedBox(0.075, 0.075, 0.13, 0.016, 0.006, 2), powderCoat(0x8a2020),
    { pos: [0, discR * 0.86, -0.01], name: 'caliperBody' }));
  for (const x of [-0.03, 0.03]) {
    cal.add(mesh(roundedBox(0.018, 0.06, 0.1, 0.008, 0.004, 1), powderCoat(0x8a2020),
      { pos: [x, discR * 0.78, -0.01] }));
  }
  cal.rotation.x = side > 0 ? 0.35 : -0.35;
  g.add(cal);
  return g;
}

/* -------------------------------------------------------------- export ---- */

/**
 * A complete corner: tire + rim + brakes, grouped so the physics rig can spin
 * `spin` on X and leave `steer` to the parent.
 */
export function buildWheel({
  diameter = 0.889, width = 0.318, rimInch = 17, spokes = 6,
  rimColor = 0x2b2e33, beadlock = true, side = 1, detail = 2,
} = {}) {
  const g = new THREE.Group();
  g.name = 'wheel';

  const spin = new THREE.Group();
  spin.name = 'spin';
  spin.add(buildTire({ diameter, width, rimInch, detail }));
  spin.add(buildRim({ rimInch, width: width * 0.88, spokes, beadlock, color: rimColor, detail }));
  g.add(spin);

  // Brakes are hub-mounted: they steer with the wheel but never spin.
  g.add(buildBrakes({ rimInch, side, detail }));

  g.userData.spin = spin;
  g.userData.radius = diameter / 2;
  // Wheels are mirrored so the tread V points the same way on both sides.
  if (side < 0) spin.scale.x = -1;
  return g;
}
