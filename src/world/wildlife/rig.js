/**
 * rig.js - skeletons, skinning and inverse kinematics for the animals.
 *
 * WHY skinned meshes and not a tree of rigid meshes: a deer's neck is the thing
 * that sells "alive", and a neck built from stacked cylinders shows its seams
 * the moment it bends. One continuous skinned surface bends smoothly, and the
 * GPU does the deforming, so the CPU only ever touches ~25 bone transforms per
 * animal. Geometry and material are shared across every animal of a species -
 * the skeleton is the only per-animal allocation.
 *
 * The bind pose is authored standing square, facing +Z, feet on y = 0.
 *
 * NOTE on merging: mergeParts() in vehicles/parts.js drops every attribute
 * except position/normal/uv and recomputes normals at the end. Skinned bodies
 * need skinIndex/skinWeight/color to survive and need their smooth normals left
 * alone, so mergeSkinned() below is mergeParts' rule (normalise indexed and
 * non-indexed to one form before concatenating) applied to a wider attribute
 * set. It must never be swapped for three's mergeGeometries directly.
 */
import * as THREE from 'three';

const _v = new THREE.Vector3();
const _a = new THREE.Vector3();
const _b = new THREE.Vector3();
const _ab = new THREE.Vector3();
const _ap = new THREE.Vector3();

/* --------------------------------------------------------------- bones ---- */

/**
 * Build a bone hierarchy from a flat spec.
 * @param {Array<{name:string, parent:?string, pos:number[]}>} spec  positions are
 *   LOCAL to the parent, which is what three expects and what makes cloning free.
 * @returns {{root:THREE.Bone, map:Object<string,THREE.Bone>, list:THREE.Bone[]}}
 */
export function buildSkeletonSpec(spec) {
  const map = Object.create(null);
  const list = [];
  let root = null;
  for (const s of spec) {
    const b = new THREE.Bone();
    b.name = s.name;
    b.position.fromArray(s.pos);
    map[s.name] = b;
    list.push(b);
    if (s.parent) map[s.parent].add(b);
    else root = b;
  }
  return { root, map, list };
}

/** World-space bind positions, keyed by bone name. Used for skin weighting. */
export function bindPositions(root, list) {
  root.updateMatrixWorld(true);
  const out = Object.create(null);
  for (const b of list) out[b.name] = new THREE.Vector3().setFromMatrixPosition(b.matrixWorld);
  return out;
}

/* ------------------------------------------------------------- skinning ---- */

/** Squared distance from p to the segment ab. Skin weights are all about this. */
function distToSegment(p, a, b) {
  _ab.subVectors(b, a);
  _ap.subVectors(p, a);
  const len2 = _ab.lengthSq();
  const t = len2 > 1e-9 ? Math.max(0, Math.min(1, _ap.dot(_ab) / len2)) : 0;
  _v.copy(_ab).multiplyScalar(t).add(a);
  return _v.distanceTo(p);
}

/**
 * Assign skin weights for one part against a SHORT list of candidate bones.
 *
 * Restricting the candidates per part is the whole trick. A global
 * nearest-bone search bleeds the left foreleg's vertices onto the right
 * foreleg bone (they are 30 cm apart and the falloff is isotropic), which
 * makes an animal's legs swing in sympathy. Parts declare what they may bind
 * to and the problem disappears.
 *
 * @param {THREE.BufferGeometry} geo   non-indexed by the time this runs
 * @param {string[]} candidates        bone names this part may bind to
 * @param {object} segs                name -> {a:Vector3, b:Vector3} bind segments
 * @param {Object<string,number>} boneIndex
 * @param {number} falloff             higher = tighter, more rigid binding
 */
export function skinPart(geo, candidates, segs, boneIndex, falloff = 3.2) {
  const pos = geo.attributes.position;
  const count = pos.count;
  const si = new Uint16Array(count * 4);
  const sw = new Float32Array(count * 4);
  const p = new THREE.Vector3();
  const w = new Float64Array(candidates.length);

  for (let v = 0; v < count; v++) {
    p.fromBufferAttribute(pos, v);
    for (let c = 0; c < candidates.length; c++) {
      const s = segs[candidates[c]];
      const d = distToSegment(p, s.a, s.b);
      w[c] = 1 / (Math.pow(d + 0.012, falloff) + 1e-6);
    }
    // Keep the four strongest influences - three's skinning shader has exactly
    // four slots, and anything weaker than that is visually noise anyway.
    let i0 = -1, i1 = -1, i2 = -1, i3 = -1;
    let w0 = -1, w1 = -1, w2 = -1, w3 = -1;
    for (let c = 0; c < candidates.length; c++) {
      const val = w[c];
      if (val > w0) { w3 = w2; i3 = i2; w2 = w1; i2 = i1; w1 = w0; i1 = i0; w0 = val; i0 = c; }
      else if (val > w1) { w3 = w2; i3 = i2; w2 = w1; i2 = i1; w1 = val; i1 = c; }
      else if (val > w2) { w3 = w2; i3 = i2; w2 = val; i2 = c; }
      else if (val > w3) { w3 = val; i3 = c; }
    }
    const picks = [[i0, w0], [i1, w1], [i2, w2], [i3, w3]];
    let total = 0;
    for (const [, val] of picks) if (val > 0) total += val;
    if (total <= 0) { si[v * 4] = boneIndex[candidates[0]]; sw[v * 4] = 1; continue; }
    for (let k = 0; k < 4; k++) {
      const [ci, val] = picks[k];
      si[v * 4 + k] = ci >= 0 ? boneIndex[candidates[ci]] : 0;
      sw[v * 4 + k] = ci >= 0 && val > 0 ? val / total : 0;
    }
  }
  geo.setAttribute('skinIndex', new THREE.BufferAttribute(si, 4));
  geo.setAttribute('skinWeight', new THREE.BufferAttribute(sw, 4));
  return geo;
}

/** Bind every vertex of a part rigidly to one bone. Antlers, tusks, hooves. */
export function skinRigid(geo, boneName, boneIndex) {
  const count = geo.attributes.position.count;
  const si = new Uint16Array(count * 4);
  const sw = new Float32Array(count * 4);
  const bi = boneIndex[boneName];
  for (let v = 0; v < count; v++) { si[v * 4] = bi; sw[v * 4] = 1; }
  geo.setAttribute('skinIndex', new THREE.BufferAttribute(si, 4));
  geo.setAttribute('skinWeight', new THREE.BufferAttribute(sw, 4));
  return geo;
}

/** Flat colour over a whole part - the cheapest way to get markings. */
export function paint(geo, color, fn = null) {
  const pos = geo.attributes.position;
  const n = pos.count;
  const arr = new Float32Array(n * 3);
  const c = new THREE.Color();
  for (let v = 0; v < n; v++) {
    if (fn) {
      c.copy(color);
      fn(c, pos.getX(v), pos.getY(v), pos.getZ(v));
    } else c.copy(color);
    arr[v * 3] = c.r; arr[v * 3 + 1] = c.g; arr[v * 3 + 2] = c.b;
  }
  geo.setAttribute('color', new THREE.Float32BufferAttribute(arr, 3));
  return geo;
}

/** Non-indexed with a uniform attribute set - the precondition for merging. */
export function normalisePart(g) {
  const n = g.index ? g.toNonIndexed() : g.clone();
  g.dispose();
  if (!n.attributes.normal) n.computeVertexNormals();
  if (!n.attributes.uv) {
    const c = n.attributes.position.count;
    n.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(c * 2), 2));
  }
  n.clearGroups();
  return n;
}

const SKIN_ATTRS = [
  ['position', 3, Float32Array], ['normal', 3, Float32Array], ['uv', 2, Float32Array],
  ['color', 3, Float32Array], ['skinIndex', 4, Uint16Array], ['skinWeight', 4, Float32Array],
];

/**
 * Concatenate already-normalised parts into one skinned geometry.
 * Deliberately does NOT recompute normals: the parts were smooth-shaded while
 * still indexed, and recomputing on the expanded buffer would facet everything.
 */
export function mergeSkinned(parts) {
  const list = parts.filter(Boolean);
  let total = 0;
  for (const p of list) total += p.attributes.position.count;

  const out = new THREE.BufferGeometry();
  for (const [name, size, Type] of SKIN_ATTRS) {
    const dst = new Type(total * size);
    let off = 0;
    for (const p of list) {
      const src = p.attributes[name];
      const c = p.attributes.position.count;
      if (src) dst.set(src.array.subarray(0, c * size), off);
      else if (name === 'color') dst.fill(1, off, off + c * size);
      off += c * size;
    }
    out.setAttribute(name, new THREE.BufferAttribute(dst, size));
  }
  for (const p of list) p.dispose();
  out.computeBoundingSphere();
  return out;
}

/* ------------------------------------------------------------------ IK ---- */

/**
 * Two-bone analytic IK in the sagittal plane.
 *
 * Returns ABSOLUTE limb directions measured from straight-down (-Y) in the
 * chain's root frame, positive swinging the tip toward -Z (backwards). Callers
 * convert absolute directions into local bone rotations with restToLocal(),
 * because the bind pose already has a natural bend baked in.
 *
 * @param {number} py  target offset below the joint (negative = below)
 * @param {number} pz  target offset ahead of the joint
 * @param {number} bend +1 knee points backwards (forelegs), -1 forwards (hind)
 * @returns {number[]} [dirUpper, dirLower]
 */
export function solve2Bone(py, pz, L1, L2, bend) {
  let d = Math.hypot(py, pz);
  const dMin = Math.abs(L1 - L2) + 1e-3;
  const dMax = L1 + L2 - 1e-3;
  d = Math.min(dMax, Math.max(dMin, d));
  const base = Math.atan2(-pz, -py);
  const cosA = (d * d + L1 * L1 - L2 * L2) / (2 * d * L1);
  const alpha = Math.acos(Math.min(1, Math.max(-1, cosA)));
  const cosB = (L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2);
  const beta = Math.acos(Math.min(1, Math.max(-1, cosB)));
  const dirUpper = base + bend * alpha;
  const dirLower = dirUpper - bend * (Math.PI - beta);
  return [dirUpper, dirLower];
}

/**
 * Turn absolute limb directions into local bone rotations.
 *
 * At the bind pose every bone's frame equals the chain root's frame, so a
 * bone's absolute direction is `rest_i + sum(rotation_j, j <= i)`. Inverting
 * that gives the per-bone delta below. Without this the natural bend authored
 * into the bind pose would be applied twice.
 *
 * @param {number[]} abs   absolute direction wanted per bone, root first
 * @param {number[]} rest  bind-pose direction of each bone
 * @param {number[]} out   receives local rotation.x per bone
 */
export function restToLocal(abs, rest, out) {
  let prev = 0;
  for (let i = 0; i < abs.length; i++) {
    const acc = abs[i] - rest[i];
    out[i] = acc - prev;
    prev = acc;
  }
  return out;
}

/** Bind-pose direction of a bone, from the local offset of its child joint. */
export function restDir(childLocalPos) {
  return Math.atan2(-childLocalPos[2], -childLocalPos[1]);
}

export const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
export const damp = (cur, target, lambda, dt) => cur + (target - cur) * (1 - Math.exp(-lambda * dt));
export const wrapPi = (a) => {
  while (a > Math.PI) a -= Math.PI * 2;
  while (a < -Math.PI) a += Math.PI * 2;
  return a;
};
