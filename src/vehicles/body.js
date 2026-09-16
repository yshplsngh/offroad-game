/**
 * body.js - body panels, greenhouse, lights.
 *
 * Panels are extruded 2D outlines in the (z, y) plane, mirrored to both sides.
 * Doors are genuine cut-outs in the side panel with a separate door panel
 * dropped into the hole, so every vehicle has real shut lines and a real
 * shadow gap instead of a painted-on rectangle.
 */
import * as THREE from 'three';
import { roundedBox, roundedRectShape, plate, tube, lathe, mesh, mirrorX, mergeParts } from './parts.js';
import {
  makePaint, trimPlastic, blackSteel, rawSteel, chrome, glass, glassCheap,
  lens, reflector, grilleMesh, bedliner, powderCoat, rubber, zinc,
} from './materials.js';

const PANEL = 0.022;                      // sheet-metal thickness we model at
const GAP = 0.007;                        // panel shut-line gap

/** Points along a circular arc, for wheel arches and rounded corners. */
function arc(cx, cy, r, a0, a1, steps = 12) {
  const out = [];
  for (let i = 0; i <= steps; i++) {
    const a = a0 + (a1 - a0) * (i / steps);
    out.push([cx + Math.cos(a) * r, cy + Math.sin(a) * r]);
  }
  return out;
}

/**
 * Wheel arch outline, swept left-to-right along the sill.
 *
 * The arc is centred on the AXLE, not on the sill - that is what makes an
 * opening hug the tire instead of opening into a cave above it. We intersect
 * the arch circle with the sill line and only draw the part above it.
 */
function archOutline(axleZ, axleY, archR, sillY, steps = 16) {
  const dy = sillY - axleY;
  if (dy >= archR) return [[axleZ, sillY]];       // sill above the arch: no cut
  const dz = Math.sqrt(archR * archR - dy * dy);  // half-width at the sill line
  const a0 = Math.atan2(dy, -dz);                 // rear intersection
  const a1 = Math.atan2(dy, dz);                  // front intersection
  return arc(axleZ, axleY, archR, a0, a1, steps);
}

/** A rectangular hole (door opening) as a THREE.Path for ExtrudeGeometry. */
function rectHole(z0, z1, y0, y1, r = 0.03) {
  const p = new THREE.Path();
  const w = z1 - z0, h = y1 - y0;
  const shape = roundedRectShape(w, h, Math.min(r, w / 2, h / 2));
  const pts = shape.getPoints(6);
  p.moveTo(pts[0].x + z0 + w / 2, pts[0].y + y0 + h / 2);
  for (let i = 1; i < pts.length; i++) p.lineTo(pts[i].x + z0 + w / 2, pts[i].y + y0 + h / 2);
  p.closePath();
  return p;
}

/** Extrude a (z,y) outline into a body panel lying in the XZ-oriented plane. */
function sidePanel(outline, holes, thickness = PANEL) {
  const shape = new THREE.Shape();
  shape.moveTo(outline[0][0], outline[0][1]);
  for (let i = 1; i < outline.length; i++) shape.lineTo(outline[i][0], outline[i][1]);
  shape.closePath();
  if (holes) shape.holes.push(...holes);
  const g = new THREE.ExtrudeGeometry(shape, {
    depth: thickness, bevelEnabled: true, bevelThickness: 0.005,
    bevelSize: 0.005, bevelSegments: 1, curveSegments: 4,
  });
  g.rotateY(Math.PI / 2);                 // (z,y) plane -> panel facing ±X
  g.computeVertexNormals();
  return g;
}

/* -------------------------------------------------------------- lights ---- */

/** Round headlamp: bowl, lens, chrome ring, plus a real SpotLight at night. */
export function buildHeadlamp({ radius = 0.1, color = 0xfff2dc } = {}) {
  const g = new THREE.Group();
  g.name = 'headlamp';
  const bowl = lathe([[0, -0.09], [radius * 0.45, -0.07], [radius * 0.85, -0.02], [radius, 0.01]], 20);
  bowl.rotateX(Math.PI / 2);
  g.add(mesh(bowl, reflector(), { name: 'bowl', castShadow: false }));

  const lensMat = lens(color, false, 3.6);
  g.add(mesh(lathe([[0, 0.03], [radius * 0.7, 0.025], [radius, 0.005], [radius, -0.005]], 20)
    .rotateX(Math.PI / 2), lensMat, { name: 'lens', castShadow: false }));
  g.add(mesh(new THREE.TorusGeometry(radius * 1.03, 0.012, 8, 24), chrome(),
    { rot: [0, 0, 0], name: 'bezel' }));

  g.userData.lensMaterial = lensMat;
  return g;
}

/** Rectangular lamp used for tails, indicators and reverse lights. */
export function buildLamp({ w = 0.1, h = 0.16, color = 0xd81f26, intensity = 2.4 } = {}) {
  const g = new THREE.Group();
  const m = lens(color, false, intensity);
  g.add(mesh(roundedBox(w, h, 0.05, 0.018, 0.005, 2), m, { name: 'lens', castShadow: false }));
  g.add(mesh(roundedBox(w * 1.14, h * 1.12, 0.03, 0.02, 0.005, 1), trimPlastic(),
    { pos: [0, 0, -0.03], name: 'housing' }));
  g.userData.lensMaterial = m;
  return g;
}

/* ------------------------------------------------------------ geometry ---- */

/**
 * Derive every body landmark from the frame + tire spec so the proportions
 * stay coherent when a vehicle's wheelbase or tire size changes.
 */
export function bodyMetrics(spec) {
  const { wheelbase, frontOverhang, rearOverhang, railY, railHeight } = spec.frame;
  const b = spec.body;
  const sillY = railY + railHeight / 2 + 0.05;
  return {
    sillY,
    beltY: sillY + b.sideHeight,
    hoodY: sillY + b.sideHeight - b.hoodDrop,
    roofY: sillY + b.sideHeight + b.glassHeight,
    zFront: wheelbase / 2 + frontOverhang - 0.08,
    zRear: -(wheelbase / 2 + rearOverhang - 0.08),
    zCowl: wheelbase / 2 - b.cowlSetback,
    zCabRear: b.style === 'pickup' ? -wheelbase * 0.06 : -(wheelbase / 2 + rearOverhang - 0.1),
    halfW: b.width / 2,
    archY: spec.tire.diameter / 2,
    archR: spec.tire.diameter / 2 + b.archClearance,
    // Half-width where the arch circle meets the sill - doors must clear this
    // or the door hole intersects the outline and triangulation collapses.
    archHalf: (() => {
      const r = spec.tire.diameter / 2 + b.archClearance;
      const dy = sillY - spec.tire.diameter / 2;
      return dy >= r ? 0 : Math.sqrt(r * r - dy * dy);
    })(),
    frontAxleZ: wheelbase / 2,
    rearAxleZ: -wheelbase / 2,
    rake: b.windshieldRake,
  };
}

/** The lower body outline in (z, y), with wheel arches carved into the sill. */
function tubOutline(m, spec) {
  const out = [];
  const pickup = spec.body.style === 'pickup';
  const bodyRear = pickup ? m.zCabRear : m.zRear;

  out.push([bodyRear, m.sillY]);
  if (!pickup) {
    // Rear arch (only the wagon's tub spans the rear axle).
    out.push(...archOutline(m.rearAxleZ, m.archY, m.archR, m.sillY, 16));
  }
  out.push(...archOutline(m.frontAxleZ, m.archY, m.archR, m.sillY, 16));
  out.push([m.zFront, m.sillY]);
  out.push([m.zFront, m.hoodY]);           // up the front of the fender
  out.push([m.zCowl, m.hoodY]);            // flat fender top
  out.push([m.zCowl - 0.06, m.beltY]);     // cowl rise
  out.push([bodyRear, m.beltY]);
  return out;
}

/* ---------------------------------------------------------------- body ---- */

export function buildBody(spec, detail = 2) {
  const m = bodyMetrics(spec);
  const paint = makePaint(spec.body.color, { matte: spec.body.matte });
  const g = new THREE.Group();
  g.name = 'body';
  g.userData.paint = paint;
  g.userData.lamps = [];
  g.userData.metrics = m;

  const pickup = spec.body.style === 'pickup';
  const bodyRear = pickup ? m.zCabRear : m.zRear;

  /* --- side panels with real door cut-outs --- */
  // Doors live in the gap between the two arches, with a margin either side.
  const doorCount = spec.body.doors ?? 2;
  const CLEAR = 0.06;
  const doorZ0 = Math.min(m.zCowl - 0.14, m.frontAxleZ - m.archHalf - CLEAR);
  const doorZMin = pickup
    ? bodyRear + 0.14
    : Math.max(bodyRear + 0.16, m.rearAxleZ + m.archHalf + CLEAR);
  const doorSpan = Math.max(0.5, doorZ0 - doorZMin);
  const doorLen = doorCount === 4 ? doorSpan / 2 - GAP * 2 : Math.min(doorSpan, 1.18);
  const doorY0 = m.sillY + 0.2, doorY1 = m.beltY - 0.012;

  const openings = [];
  const doorRects = [];
  for (let i = 0; i < doorCount / 2; i++) {
    const z1 = doorZ0 - i * (doorLen + GAP * 2);
    const z0 = z1 - doorLen;
    openings.push(rectHole(z0, z1, doorY0, doorY1, 0.05));
    doorRects.push([z0, z1]);
  }

  const panelGeo = sidePanel(tubOutline(m, spec), openings);
  mirrorX(g, panelGeo, paint, { pos: [m.halfW - PANEL / 2, 0, 0], name: 'sidePanel' });

  /* --- doors sitting in the openings --- */
  for (const [z0, z1] of doorRects) {
    const w = z1 - z0 - GAP * 2, h = doorY1 - doorY0 - GAP * 2;
    const door = new THREE.Group();
    door.name = 'door';
    const skin = new THREE.ExtrudeGeometry(roundedRectShape(w, h, 0.045), {
      depth: PANEL, bevelEnabled: true, bevelThickness: 0.006, bevelSize: 0.006,
      bevelSegments: 1, curveSegments: 4,
    });
    skin.rotateY(Math.PI / 2);
    door.add(mesh(skin, paint, { pos: [0, (doorY0 + doorY1) / 2, (z0 + z1) / 2], name: 'doorSkin' }));
    // Handle and lock barrel.
    door.add(mesh(roundedBox(0.03, 0.035, 0.14, 0.012, 0.004, 1), trimPlastic(),
      { pos: [0.026, doorY1 - 0.13, (z0 + z1) / 2 - w * 0.22], name: 'handle' }));
    if (detail > 1) {
      door.add(mesh(new THREE.CylinderGeometry(0.011, 0.011, 0.012, 10), chrome(),
        { pos: [0.026, doorY1 - 0.13, (z0 + z1) / 2 - w * 0.36], rot: [0, 0, Math.PI / 2] }));
    }
    for (const s of [-1, 1]) {
      const d = door.clone();
      d.position.x = s * (m.halfW - PANEL / 2);
      d.scale.x = s;
      g.add(d);
    }
  }

  /* --- floor, firewall, rear panel --- */
  g.add(mesh(roundedBox(spec.body.width - PANEL * 2, 0.03, Math.abs(bodyRear - m.zCowl) + 0.5, 0.02, 0.005, 1),
    bedliner(), { pos: [0, m.sillY + 0.02, (bodyRear + m.zCowl) / 2 - 0.1], name: 'floor' }));
  g.add(mesh(roundedBox(spec.body.width - PANEL * 2, m.beltY - m.sillY, PANEL, 0.02, 0.005, 1),
    paint, { pos: [0, (m.sillY + m.beltY) / 2, m.zCowl + 0.02], name: 'firewall' }));

  if (!pickup) {
    // Tailgate and rear body panel.
    g.add(mesh(roundedBox(spec.body.width - 0.06, m.beltY - m.sillY - 0.04, PANEL, 0.03, 0.006, 2),
      paint, { pos: [0, (m.sillY + m.beltY) / 2, bodyRear + PANEL / 2], name: 'tailgate' }));
    g.add(mesh(roundedBox(0.16, 0.05, 0.03, 0.012, 0.004, 1), trimPlastic(),
      { pos: [0, m.beltY - 0.14, bodyRear - 0.01], name: 'tailgateHandle' }));
  } else {
    // Cab back panel, then the bed as its own structure.
    g.add(mesh(roundedBox(spec.body.width - PANEL * 2, m.beltY - m.sillY, PANEL, 0.02, 0.005, 1),
      paint, { pos: [0, (m.sillY + m.beltY) / 2, bodyRear], name: 'cabBack' }));
    g.add(buildBed(spec, m, paint, detail));
  }

  /* --- hood --- */
  const hoodLen = m.zFront - m.zCowl;
  const hood = new THREE.Group();
  hood.name = 'hood';
  hood.add(mesh(roundedBox(spec.body.width - 0.1, 0.035, hoodLen - GAP * 2, 0.03, 0.008, 2), paint,
    { pos: [0, m.hoodY + 0.012, (m.zFront + m.zCowl) / 2], name: 'hoodPanel' }));
  if (detail > 1) {
    // Power bulge + hood latches.
    hood.add(mesh(roundedBox(0.44, 0.045, hoodLen * 0.5, 0.05, 0.01, 3), paint,
      { pos: [0, m.hoodY + 0.042, (m.zFront + m.zCowl) / 2 + 0.05], name: 'hoodBulge' }));
    for (const s of [-1, 1]) {
      hood.add(mesh(roundedBox(0.05, 0.02, 0.07, 0.008, 0.003, 1), rawSteel(),
        { pos: [s * (spec.body.width * 0.3), m.hoodY + 0.03, m.zFront - 0.06], name: 'hoodLatch' }));
    }
  }
  g.add(hood);

  /* --- front fascia, grille, headlamps --- */
  const front = new THREE.Group();
  front.name = 'frontEnd';
  const grilleH = m.hoodY - m.sillY - 0.1;
  front.add(mesh(roundedBox(spec.body.width - 0.14, grilleH, 0.04, 0.03, 0.006, 2), trimPlastic(),
    { pos: [0, m.sillY + 0.06 + grilleH / 2, m.zFront + 0.01], name: 'grilleSurround' }));
  front.add(mesh(new THREE.PlaneGeometry(spec.body.width - 0.26, grilleH - 0.06), grilleMesh(),
    { pos: [0, m.sillY + 0.06 + grilleH / 2, m.zFront + 0.033], name: 'grilleMesh', castShadow: false }));

  const lampY = m.sillY + 0.06 + grilleH * 0.62;
  for (const s of [-1, 1]) {
    const h = buildHeadlamp({ radius: spec.body.headlampR ?? 0.1 });
    h.position.set(s * (spec.body.width / 2 - 0.16), lampY, m.zFront + 0.03);
    g.userData.lamps.push({ node: h, kind: 'head' });
    front.add(h);
    // Indicator below the headlamp.
    const ind = buildLamp({ w: 0.075, h: 0.06, color: 0xff8a1e, intensity: 2.0 });
    ind.position.set(s * (spec.body.width / 2 - 0.16), lampY - 0.17, m.zFront + 0.02);
    g.userData.lamps.push({ node: ind, kind: 'indicator' });
    front.add(ind);
  }
  g.add(front);

  /* --- tail lamps --- */
  const tailZ = pickup ? m.zRear : bodyRear;
  for (const s of [-1, 1]) {
    const t = buildLamp({ w: 0.09, h: 0.2, color: 0xd81f26, intensity: 2.2 });
    t.position.set(s * (spec.body.width / 2 - 0.12), m.sillY + 0.34, tailZ - 0.01);
    t.rotation.y = Math.PI;
    g.userData.lamps.push({ node: t, kind: 'tail' });
    g.add(t);
  }

  /* --- wheel arch flares --- */
  if (spec.features.flares) {
    for (const s of [-1, 1]) {
      for (const z of [m.frontAxleZ, m.rearAxleZ]) {
        if (pickup && z === m.rearAxleZ) continue;   // bed carries its own flares
        g.add(buildFlare(m, s, z));
      }
    }
  }

  /* --- greenhouse --- */
  g.add(buildGreenhouse(spec, m, paint, detail));

  /* --- mirrors --- */
  for (const s of [-1, 1]) {
    const mir = new THREE.Group();
    mir.add(mesh(roundedBox(0.03, 0.03, 0.16, 0.012, 0.004, 1), trimPlastic(),
      { pos: [s * 0.06, 0, -0.05], rot: [0, 0, s * 0.5] }));
    mir.add(mesh(roundedBox(0.045, 0.16, 0.13, 0.025, 0.006, 2), trimPlastic(), { name: 'mirrorHousing' }));
    mir.add(mesh(new THREE.PlaneGeometry(0.115, 0.14), chrome(),
      { pos: [s * 0.024, 0, 0], rot: [0, s * Math.PI / 2, 0], name: 'mirrorGlass', castShadow: false }));
    mir.position.set(s * (m.halfW + 0.1), m.beltY + 0.1, m.zCowl - 0.16);
    g.add(mir);
  }

  return g;
}

/* ------------------------------------------------------------- flares ---- */

function buildFlare(m, side, axleZ) {
  // A torus arc spanning exactly the visible part of the opening, squashed
  // flat so it reads as a bolt-on lip standing proud of the body side.
  const r = m.archR + 0.03;
  const dy = m.sillY - m.archY;
  const span = dy >= r ? Math.PI : Math.PI - 2 * Math.asin(Math.max(-1, Math.min(1, dy / r)));
  const t = new THREE.TorusGeometry(r, 0.05, 8, 26, span);
  t.rotateZ((Math.PI - span) / 2);          // centre the arc on vertical
  t.rotateY(Math.PI / 2);                   // swing it into the (z, y) plane
  t.scale(1.5, 1, 1);                       // flatten into a lip, proud on X
  const g = new THREE.Mesh(t, trimPlastic());
  g.name = 'flare';
  g.position.set(side * (m.halfW - 0.01), m.archY, axleZ);
  g.castShadow = g.receiveShadow = true;
  return g;
}

/* ----------------------------------------------------------- greenhouse ---- */

function buildGreenhouse(spec, m, paint, detail) {
  const g = new THREE.Group();
  g.name = 'greenhouse';
  const wsTop = m.zCowl - m.rake;
  const roofRear = m.zCabRear;
  const pillar = 0.05;
  const gl = detail > 1 ? glass(spec.body.glassTint ?? 0.12) : glassCheap();

  // A-pillars (raked) and the windshield header.
  for (const s of [-1, 1]) {
    g.add(mesh(tube([
      [s * (m.halfW - pillar / 2), m.beltY - 0.02, m.zCowl - 0.06],
      [s * (m.halfW - pillar / 2), m.roofY, wsTop],
    ], pillar / 2, { radialSeg: 8 }), paint, { name: 'aPillar' }));
  }
  g.add(mesh(roundedBox(spec.body.width - pillar, pillar, pillar, 0.018, 0.005, 2), paint,
    { pos: [0, m.roofY, wsTop], name: 'header' }));
  g.add(mesh(roundedBox(spec.body.width - pillar, pillar * 0.8, pillar, 0.018, 0.005, 2), paint,
    { pos: [0, m.beltY - 0.02, m.zCowl - 0.06], name: 'cowlRail' }));

  // Windshield glass, raked to match the pillars.
  const wsH = Math.hypot(m.roofY - m.beltY, m.rake - 0.06);
  // A PlaneGeometry is already upright and facing +Z, so it only needs the
  // rake: a negative X rotation leans the top of the glass backwards.
  const ws = mesh(new THREE.PlaneGeometry(spec.body.width - pillar * 1.6, wsH), gl,
    { pos: [0, (m.beltY + m.roofY) / 2 - 0.01, (m.zCowl - 0.06 + wsTop) / 2], name: 'windshield', castShadow: false });
  ws.rotation.x = -Math.atan2(m.rake - 0.06, m.roofY - m.beltY);
  g.add(ws);

  // Roof panel with a slight crown.
  const roofLen = Math.abs(wsTop - roofRear);
  g.add(mesh(roundedBox(spec.body.width - 0.02, 0.035, roofLen, 0.06, 0.01, 3), paint,
    { pos: [0, m.roofY + 0.012, (wsTop + roofRear) / 2], name: 'roof' }));
  // Rain gutters.
  for (const s of [-1, 1]) {
    g.add(mesh(roundedBox(0.03, 0.028, roofLen, 0.01, 0.004, 1), paint,
      { pos: [s * (m.halfW - 0.005), m.roofY - 0.005, (wsTop + roofRear) / 2], name: 'gutter' }));
  }

  // B/C pillars and the side glass between them.
  // Even a two-door needs a B-pillar behind the door, or the side reads as one
  // implausible sheet of glass from windscreen to tailgate.
  const pillarZs = spec.body.doors === 4
    ? [wsTop - roofLen * 0.42, roofRear + 0.04]
    : [wsTop - roofLen * 0.5, roofRear + 0.04];
  for (const s of [-1, 1]) {
    for (const z of pillarZs) {
      g.add(mesh(roundedBox(pillar, m.roofY - m.beltY, pillar, 0.015, 0.004, 1), paint,
        { pos: [s * (m.halfW - pillar / 2), (m.beltY + m.roofY) / 2, z], name: 'pillar' }));
    }
    // One glass pane per bay between pillars.
    const bounds = [wsTop - 0.05, ...pillarZs];
    for (let i = 0; i < bounds.length - 1; i++) {
      const z0 = bounds[i], z1 = bounds[i + 1];
      g.add(mesh(new THREE.PlaneGeometry(Math.abs(z1 - z0) - pillar, m.roofY - m.beltY - 0.04), gl,
        { pos: [s * (m.halfW - 0.012), (m.beltY + m.roofY) / 2, (z0 + z1) / 2], rot: [0, s * Math.PI / 2, 0], name: 'sideGlass', castShadow: false }));
    }
  }

  // Rear window (wagon only - a pickup's cab back gets a small slider).
  g.add(mesh(new THREE.PlaneGeometry(spec.body.width - pillar * 1.8, m.roofY - m.beltY - 0.04), gl,
    { pos: [0, (m.beltY + m.roofY) / 2, roofRear + 0.02], name: 'rearGlass', castShadow: false }));
  g.add(mesh(roundedBox(spec.body.width - 0.02, pillar * 0.8, pillar, 0.015, 0.004, 1), paint,
    { pos: [0, m.roofY, roofRear + 0.02], name: 'rearHeader' }));

  return g;
}

/* ------------------------------------------------------------------ bed ---- */

function buildBed(spec, m, paint, detail) {
  const g = new THREE.Group();
  g.name = 'bed';
  const z0 = m.zRear, z1 = m.zCabRear - 0.04;
  const len = Math.abs(z1 - z0);
  const sideH = m.beltY - m.sillY - 0.06;

  // Bed sides with the rear wheel arch carved out.
  const out = [];
  out.push([z0, m.sillY]);
  out.push(...archOutline(m.rearAxleZ, m.archY, m.archR, m.sillY, 14));
  out.push([z1, m.sillY]);
  out.push([z1, m.sillY + sideH]);
  out.push([z0, m.sillY + sideH]);
  const geo = sidePanel(out, null);
  mirrorX(g, geo, paint, { pos: [m.halfW - PANEL / 2, 0, 0], name: 'bedSide' });

  // Bed floor and tailgate.
  g.add(mesh(roundedBox(spec.body.width - PANEL * 2, 0.03, len, 0.01, 0.004, 1), bedliner(),
    { pos: [0, m.sillY + 0.12, (z0 + z1) / 2], name: 'bedFloor' }));
  g.add(mesh(roundedBox(spec.body.width - 0.04, sideH, PANEL, 0.025, 0.006, 2), paint,
    { pos: [0, m.sillY + sideH / 2, z0 + PANEL / 2], name: 'tailgate' }));

  if (spec.features.flares) {
    for (const s of [-1, 1]) g.add(buildFlare(m, s, m.rearAxleZ));
  }
  if (detail > 1) {
    // Bed rails and tie-down cleats.
    for (const s of [-1, 1]) {
      g.add(mesh(roundedBox(0.05, 0.03, len, 0.012, 0.004, 1), trimPlastic(),
        { pos: [s * (m.halfW - 0.02), m.sillY + sideH + 0.015, (z0 + z1) / 2], name: 'bedRail' }));
      for (const t of [0.25, 0.75]) {
        g.add(mesh(new THREE.TorusGeometry(0.022, 0.006, 6, 12), zinc(),
          { pos: [s * (m.halfW - 0.05), m.sillY + sideH - 0.06, z0 + len * t], rot: [0, Math.PI / 2, 0] }));
      }
    }
  }
  return g;
}
