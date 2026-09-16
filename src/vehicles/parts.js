/**
 * parts.js - low-level geometry builders shared by every vehicle.
 *
 * Rule of thumb used throughout: real hardware has no perfectly sharp edges,
 * so boxes get bevels and tubes get caps. That single habit is most of the
 * difference between "reads as a machine" and "reads as primitives".
 *
 * All units are metres. +X is right, +Y is up, +Z is forward (nose of car).
 */
import * as THREE from 'three';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

const HALF_PI = Math.PI / 2;

/* ------------------------------------------------------------- helpers ---- */

/**
 * Merge a batch of geometries safely.
 *
 * three's mergeGeometries returns null if the batch mixes indexed and
 * non-indexed geometry, or if attribute sets differ - and ExtrudeGeometry is
 * non-indexed while Cylinder/Lathe/Sphere are indexed, so almost every real
 * batch here mixes the two. Normalise to non-indexed with a uniform
 * position/normal/uv set first, then merge.
 */
export function mergeParts(geometries, { dispose = true } = {}) {
  const list = geometries.filter(Boolean);
  if (list.length === 0) return null;

  const normalised = list.map((g) => {
    let n = g.index ? g.toNonIndexed() : g;
    if (n === g) n = g.clone();
    for (const name of Object.keys(n.attributes)) {
      if (!['position', 'normal', 'uv'].includes(name)) n.deleteAttribute(name);
    }
    if (!n.attributes.normal) n.computeVertexNormals();
    if (!n.attributes.uv) {
      const count = n.attributes.position.count;
      n.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(count * 2), 2));
    }
    n.clearGroups();
    return n;
  });

  const merged = mergeGeometries(normalised, false);
  normalised.forEach((n) => n.dispose());
  if (dispose) list.forEach((g) => g.dispose());
  if (!merged) return null;
  merged.computeVertexNormals();
  return merged;
}

/** Build a mesh and place it in one call. */
export function mesh(geometry, material, {
  pos = [0, 0, 0], rot = [0, 0, 0], scale = null, name = '',
  castShadow = true, receiveShadow = true,
} = {}) {
  const m = new THREE.Mesh(geometry, material);
  m.position.set(...pos);
  m.rotation.set(...rot);
  if (scale) m.scale.set(...(Array.isArray(scale) ? scale : [scale, scale, scale]));
  m.castShadow = castShadow;
  m.receiveShadow = receiveShadow;
  if (name) m.name = name;
  return m;
}

/** Add a mesh and its mirror across the X axis - most of a car is symmetric. */
export function mirrorX(parent, geometry, material, opts = {}) {
  const a = mesh(geometry, material, opts);
  const b = mesh(geometry, material, {
    ...opts,
    pos: [-opts.pos[0], opts.pos[1], opts.pos[2]],
    rot: opts.rot ? [opts.rot[0], -opts.rot[1], -opts.rot[2]] : [0, 0, 0],
  });
  parent.add(a, b);
  return [a, b];
}

/** A rounded rectangle as a THREE.Shape, for extruding panels. */
export function roundedRectShape(w, h, r) {
  r = Math.min(r, w / 2 - 1e-4, h / 2 - 1e-4);
  const s = new THREE.Shape();
  const x = -w / 2, y = -h / 2;
  s.moveTo(x + r, y);
  s.lineTo(x + w - r, y);
  s.quadraticCurveTo(x + w, y, x + w, y + r);
  s.lineTo(x + w, y + h - r);
  s.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
  s.lineTo(x + r, y + h);
  s.quadraticCurveTo(x, y + h, x, y + h - r);
  s.lineTo(x, y + r);
  s.quadraticCurveTo(x, y, x + r, y);
  return s;
}

/**
 * Box with rounded corners in XY and a bevelled front/back face.
 * The workhorse for body panels, skid plates, battery boxes, dash bezels.
 */
export function roundedBox(w, h, d, r = 0.02, bevel = 0.008, curveSeg = 3) {
  const g = new THREE.ExtrudeGeometry(roundedRectShape(w, h, r), {
    depth: Math.max(1e-4, d - bevel * 2),
    bevelEnabled: bevel > 0,
    bevelThickness: bevel,
    bevelSize: bevel,
    bevelSegments: 2,
    curveSegments: curveSeg,
  });
  g.translate(0, 0, -(d - bevel * 2) / 2);
  g.computeVertexNormals();
  return g;
}

/** Extrude an arbitrary 2D outline (array of [x,y]) into a plate of `depth`. */
export function plate(points, depth, { bevel = 0.006, closed = true } = {}) {
  const s = new THREE.Shape();
  s.moveTo(points[0][0], points[0][1]);
  for (let i = 1; i < points.length; i++) s.lineTo(points[i][0], points[i][1]);
  if (closed) s.closePath();
  const g = new THREE.ExtrudeGeometry(s, {
    depth: Math.max(1e-4, depth - bevel * 2),
    bevelEnabled: bevel > 0,
    bevelThickness: bevel,
    bevelSize: bevel,
    bevelSegments: 1,
    curveSegments: 4,
  });
  g.translate(0, 0, -(depth - bevel * 2) / 2);
  g.computeVertexNormals();
  return g;
}

/**
 * Round tube following a series of points. Used for the roll cage, bumper
 * hoops, rock sliders and roof rack - anything that looks bent from stock.
 */
export function tube(points, radius, {
  radialSeg = 10, tension = 0.5, closed = false, tubularSeg = null, caps = true,
} = {}) {
  const pts = points.map((p) => (p.isVector3 ? p : new THREE.Vector3(...p)));
  // 'centripetal' instead of 'catmullrom': uniform Catmull-Rom overshoots on
  // tight corners, which pushed cage bars out through the bodywork.
  const curve = new THREE.CatmullRomCurve3(pts, closed, 'centripetal', tension);
  const segs = tubularSeg ?? Math.max(12, Math.round(curve.getLength() / radius * 1.4));
  const g = new THREE.TubeGeometry(curve, Math.min(segs, 220), radius, radialSeg, closed);
  if (!caps || closed) return g;

  // TubeGeometry leaves the ends open; weld on two discs so cut tube ends
  // don't show through as holes.
  const parts = [g];
  for (const t of [0, 1]) {
    const cap = new THREE.CircleGeometry(radius, radialSeg);
    const p = curve.getPointAt(t);
    const tan = curve.getTangentAt(t);
    const q = new THREE.Quaternion().setFromUnitVectors(
      new THREE.Vector3(0, 0, t === 0 ? -1 : 1), tan.clone().multiplyScalar(t === 0 ? 1 : 1),
    );
    cap.applyQuaternion(q);
    cap.translate(p.x, p.y, p.z);
    parts.push(cap);
  }
  return mergeParts(parts, { dispose: false }) ?? g;
}

/** Straight tube between two points - cheaper than tube() for links and rods. */
export function rod(a, b, radius, radialSeg = 8) {
  const va = new THREE.Vector3(...a), vb = new THREE.Vector3(...b);
  const dir = new THREE.Vector3().subVectors(vb, va);
  const len = dir.length();
  const g = new THREE.CylinderGeometry(radius, radius, len, radialSeg, 1, false);
  g.rotateX(HALF_PI);                        // align +Y cylinder to +Z
  const q = new THREE.Quaternion().setFromUnitVectors(
    new THREE.Vector3(0, 0, 1), dir.normalize(),
  );
  g.applyQuaternion(q);
  g.translate((va.x + vb.x) / 2, (va.y + vb.y) / 2, (va.z + vb.z) / 2);
  return g;
}

/** Hex bolt head with a washer. Scattered over panels, they sell the scale. */
export function hexBolt(radius = 0.008, height = 0.006, washer = true) {
  const parts = [new THREE.CylinderGeometry(radius, radius * 0.94, height, 6)];
  parts[0].translate(0, height / 2, 0);
  if (washer) {
    const w = new THREE.CylinderGeometry(radius * 1.55, radius * 1.55, height * 0.28, 10);
    w.translate(0, height * 0.14, 0);
    parts.push(w);
  }
  const g = mergeParts(parts);
  g.rotateX(-HALF_PI);                       // point along +Z, ready to sit on a panel
  return g;
}

/** Revolve a 2D profile (array of [radius, y]) around Y. Rims, hubs, cans. */
export function lathe(profile, segments = 32, { flipNormals = false } = {}) {
  const pts = profile.map(([r, y]) => new THREE.Vector2(Math.max(1e-5, r), y));
  const g = new THREE.LatheGeometry(pts, segments);
  if (flipNormals) g.scale(-1, 1, 1);
  g.computeVertexNormals();
  return g;
}

/** Coil spring wound around a vertical axis - real geometry, not a texture. */
export function coilSpring(radius, length, coils = 7, wire = 0.014, radialSeg = 7) {
  const pts = [];
  const steps = coils * 14;
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const a = t * coils * Math.PI * 2;
    // Squash the coils slightly at each end, like a real closed-end spring.
    const ease = 0.82 + 0.18 * Math.sin(Math.PI * Math.min(1, Math.max(0, t)));
    pts.push(new THREE.Vector3(
      Math.cos(a) * radius * ease, t * length, Math.sin(a) * radius * ease,
    ));
  }
  const curve = new THREE.CatmullRomCurve3(pts, false, 'catmullrom', 0.5);
  return new THREE.TubeGeometry(curve, steps, wire, radialSeg, false);
}

/** Torus arc - fender lips, tire beads, steering wheel rims. */
export function arcTorus(radius, tubeR, arc = Math.PI * 2, radialSeg = 10, tubularSeg = 48) {
  return new THREE.TorusGeometry(radius, tubeR, radialSeg, tubularSeg, arc);
}

/** Flatten a group down to one merged geometry per material. */
export function mergeGroupByMaterial(group) {
  const buckets = new Map();
  group.updateMatrixWorld(true);
  group.traverse((o) => {
    if (!o.isMesh) return;
    const key = o.material;
    const g = o.geometry.clone();
    g.applyMatrix4(o.matrixWorld);
    // Merge requires identical attribute sets across the batch.
    for (const attr of Object.keys(g.attributes)) {
      if (!['position', 'normal', 'uv'].includes(attr)) g.deleteAttribute(attr);
    }
    if (!g.attributes.uv) {
      const count = g.attributes.position.count;
      g.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(count * 2), 2));
    }
    if (!buckets.has(key)) buckets.set(key, []);
    buckets.get(key).push(g);
  });
  const out = new THREE.Group();
  for (const [material, geos] of buckets) {
    const merged = mergeParts(geos, { dispose: false });
    if (!merged) continue;
    const m = new THREE.Mesh(merged, material);
    m.castShadow = m.receiveShadow = true;
    out.add(m);
    geos.forEach((g) => g.dispose());
  }
  return out;
}

/** Count triangles under a node - used by the showroom stats readout. */
export function triangleCount(node) {
  let n = 0;
  node.traverse((o) => {
    if (!o.isMesh) return;
    const g = o.geometry;
    n += g.index ? g.index.count / 3 : g.attributes.position.count / 3;
  });
  return Math.round(n);
}

export { HALF_PI, mergeGeometries };
