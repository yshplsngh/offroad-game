/**
 * shapes.js - organic geometry primitives for animal bodies.
 *
 * Vehicles are made of panels and tubes; animals are made of ONE continuous
 * surface that tapers. So instead of parts.js' roundedBox/plate vocabulary,
 * everything here lofts an elliptical cross-section along a path. That single
 * habit is what stops a deer reading as a stack of cylinders: the neck flows
 * out of the chest, the muzzle out of the skull, with no seams to catch light.
 *
 * Geometry is built INDEXED and smooth-shaded on purpose - the skinning pass in
 * rig.js expands to non-indexed afterwards, which carries the smooth normals
 * with it. Computing normals after the expansion would give flat faceting.
 *
 * All units are metres. Animals face +Z, +Y is up, +X is the animal's left.
 */
import * as THREE from 'three';

const TAU = Math.PI * 2;

/* ------------------------------------------------------------- lofting ---- */

/**
 * Loft an elliptical tube through a list of stations.
 *
 * @param {Array<{p:number[], rx:number, ry:number, flat?:number, shear?:number}>} stations
 *   `p` is the section centre, `rx`/`ry` its half-width and half-height.
 *   `flat` (0..1) squashes the underside - a boar's belly, a crow's breast.
 *   `shear` pushes the section's bottom back along the path, which is how a
 *   deer's brisket ends up ahead of its chest instead of hanging straight down.
 * @param {object} opts
 * @returns {THREE.BufferGeometry} indexed, smooth-shaded, UV'd.
 */
export function loft(stations, { radial = 10, capStart = true, capEnd = true } = {}) {
  const n = stations.length;
  const pos = [];
  const uv = [];
  const idx = [];

  // Parallel-transport-ish frame. A fixed reference axis is enough here because
  // no animal path doubles back on itself, but the reference has to flip for
  // near-vertical paths (legs) or the frame degenerates.
  const tan = new THREE.Vector3();
  const ref = new THREE.Vector3();
  const right = new THREE.Vector3();
  const up = new THREE.Vector3();
  const c = new THREE.Vector3();

  for (let i = 0; i < n; i++) {
    const s = stations[i];
    c.fromArray(s.p);
    const a = stations[Math.max(0, i - 1)];
    const b = stations[Math.min(n - 1, i + 1)];
    tan.set(b.p[0] - a.p[0], b.p[1] - a.p[1], b.p[2] - a.p[2]);
    if (tan.lengthSq() < 1e-9) tan.set(0, 0, 1);
    tan.normalize();

    ref.set(0, 1, 0);
    if (Math.abs(tan.y) > 0.9) ref.set(0, 0, 1);
    right.crossVectors(ref, tan).normalize();
    up.crossVectors(tan, right).normalize();

    const flat = s.flat ?? 0;
    const shear = s.shear ?? 0;
    for (let j = 0; j <= radial; j++) {
      const t = j / radial;
      const ang = t * TAU;
      const cs = Math.cos(ang);
      let sn = Math.sin(ang);
      // Squash only the lower half so the belly flattens without moving the spine.
      if (sn < 0) sn *= 1 - flat;
      const sx = right.x * cs * s.rx + up.x * sn * s.ry + tan.x * sn * shear;
      const sy = right.y * cs * s.rx + up.y * sn * s.ry + tan.y * sn * shear;
      const sz = right.z * cs * s.rx + up.z * sn * s.ry + tan.z * sn * shear;
      pos.push(c.x + sx, c.y + sy, c.z + sz);
      uv.push(t, i / (n - 1));
    }
  }

  const ring = radial + 1;
  for (let i = 0; i < n - 1; i++) {
    for (let j = 0; j < radial; j++) {
      const a = i * ring + j, b = a + ring;
      idx.push(a, b, a + 1, a + 1, b, b + 1);
    }
  }

  // Caps are a fan to a centre vertex pulled slightly along the path, so ends
  // read as rounded (a rump, a nose) rather than sliced off.
  const cap = (stationIndex, dir) => {
    const s = stations[stationIndex];
    const a = stations[Math.max(0, stationIndex - 1)];
    const b = stations[Math.min(n - 1, stationIndex + 1)];
    tan.set(b.p[0] - a.p[0], b.p[1] - a.p[1], b.p[2] - a.p[2]).normalize();
    const bulge = Math.min(s.rx, s.ry) * 0.75 * dir;
    const base = pos.length / 3;
    pos.push(s.p[0] + tan.x * bulge, s.p[1] + tan.y * bulge, s.p[2] + tan.z * bulge);
    uv.push(0.5, dir > 0 ? 1 : 0);
    const off = stationIndex * ring;
    for (let j = 0; j < radial; j++) {
      if (dir > 0) idx.push(off + j, off + j + 1, base);
      else idx.push(off + j + 1, off + j, base);
    }
  };
  if (capStart) cap(0, -1);
  if (capEnd) cap(n - 1, 1);

  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

/**
 * Loft a limb segment down a straight line between two joints.
 * Legs are lofted rather than cylindered so the taper from a heavy thigh to a
 * thin cannon bone is continuous - that taper is most of "slender legs".
 */
export function limb(a, b, r0, r1, { radial = 6, steps = 3, capStart = true, capEnd = true } = {}) {
  const stations = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    const r = r0 + (r1 - r0) * t;
    stations.push({
      p: [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t],
      rx: r, ry: r,
    });
  }
  return loft(stations, { radial, capStart, capEnd });
}

/**
 * Flattened blade - ears, wings, tail fans, a fin of antler palm.
 * Built from a 2D outline swept to a thin thickness with rounded edges.
 */
export function blade(outline, thickness, { taper = 0.55 } = {}) {
  const n = outline.length;
  const pos = [];
  const uv = [];
  const idx = [];
  // Two shells (+/- thickness) joined at the rim. Thickness tapers to the tip
  // so an ear is fleshy at the base and papery at the point.
  for (const side of [1, -1]) {
    for (let i = 0; i < n; i++) {
      const [x, y] = outline[i];
      const t = i / (n - 1);
      const th = thickness * (1 - taper * t);
      pos.push(x, y, side * th);
      uv.push(t, side > 0 ? 1 : 0);
    }
  }
  for (let i = 0; i < n - 1; i++) {
    const a = i, b = i + n;
    idx.push(a, a + 1, b, a + 1, b + 1, b);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeVertexNormals();
  return g;
}

/**
 * Ear cone: a curled blade so it catches light like a real ear, with the
 * opening facing +X. Rotated/placed by the caller.
 */
export function ear(length, width, { curl = 0.45, radial = 7, steps = 5 } = {}) {
  const stations = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps;
    // Widest a third of the way up, pointed at the tip - the classic cervid ear.
    const w = width * Math.sin(Math.min(1, 0.25 + t * 0.85) * Math.PI) * 1.05;
    stations.push({
      p: [Math.sin(t * curl) * length * 0.25, t * length, 0],
      rx: Math.max(0.004, w),
      ry: Math.max(0.003, w * 0.34),
    });
  }
  return loft(stations, { radial, capStart: true, capEnd: true });
}

/** Small sphere - eyes, nose pads, joint bulges. Kept to few segments. */
export function bead(r, seg = 7) {
  return new THREE.SphereGeometry(r, seg, Math.max(4, seg - 2));
}

/**
 * Branching tube along a poly-line, used for antlers and thin twiggy shapes.
 * Straight rods would read as wire; a taper plus a slight curve reads as bone.
 */
export function branch(points, r0, r1, { radial = 5 } = {}) {
  const stations = points.map((p, i) => {
    const t = i / Math.max(1, points.length - 1);
    const r = r0 + (r1 - r0) * t;
    return { p, rx: r, ry: r };
  });
  return loft(stations, { radial, capStart: true, capEnd: true });
}

/** Translate/rotate/scale a geometry in place - saves a Matrix4 at every site. */
export function place(g, { pos = null, rotX = 0, rotY = 0, rotZ = 0, scale = null } = {}) {
  if (scale) g.scale(scale[0], scale[1], scale[2]);
  if (rotZ) g.rotateZ(rotZ);
  if (rotY) g.rotateY(rotY);
  if (rotX) g.rotateX(rotX);
  if (pos) g.translate(pos[0], pos[1], pos[2]);
  return g;
}

/** Mirror a geometry across X. Animals are symmetric; build once, flip once. */
export function mirrored(g) {
  const m = g.clone();
  m.scale(-1, 1, 1);
  // Flipping one axis inverts winding, so the faces would be inside-out.
  const index = m.getIndex();
  if (index) {
    const a = index.array;
    for (let i = 0; i < a.length; i += 3) { const t = a[i]; a[i] = a[i + 2]; a[i + 2] = t; }
    index.needsUpdate = true;
  }
  m.computeVertexNormals();
  return m;
}

export { TAU };
