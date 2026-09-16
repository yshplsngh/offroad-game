/**
 * vehicle.js - assembles a spec into a finished, animatable vehicle.
 *
 * The physics layer never touches geometry. It calls four things:
 *   setSteering(rad)      - front wheels + steering wheel
 *   setWheelSpin(i, rad)  - absolute wheel rotation
 *   setTravel(i, metres)  - suspension compression per wheel
 *   update()              - resolves axles, links, coilovers
 *
 * Because both wheels on an axle are children of one axle group, articulation
 * is genuinely solid-axle: compress one side and the other side droops, the
 * housing rolls, and every link and coilover follows.
 */
import * as THREE from 'three';
import {
  buildLadderFrame, buildSolidAxle, buildFourLink, buildDrivetrain, buildArmour,
} from './chassis.js';
import { buildWheel } from './wheel.js';
import { buildBody, bodyMetrics } from './body.js';
import { buildInterior } from './interior.js';
import {
  buildRollCage, buildFrontBumper, buildRearBumper, buildSnorkel,
  buildLightBar, buildRoofRack, buildSpare, buildMiscKit,
} from './accessories.js';
import { applyMud } from './materials.js';
import { triangleCount } from './parts.js';

const DOWN = new THREE.Vector3(0, -1, 0);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

/** Point an object's -Y axis at a target. Used for coilovers. */
function aimDown(obj, target) {
  const dir = target.clone().sub(obj.position);
  if (dir.lengthSq() < 1e-8) return 0;
  const len = dir.length();
  obj.quaternion.setFromUnitVectors(DOWN, dir.divideScalar(len));
  return len;
}

/** One axle's worth of moving parts. */
class AxleRig {
  constructor({ spec, front, detail }) {
    const { wheelbase } = spec.frame;
    const track = spec.axle.track;
    const tireR = spec.tire.diameter / 2;
    this.front = front;
    this.track = track;
    this.restY = tireR;
    this.restZ = front ? wheelbase / 2 : -wheelbase / 2;
    this.travel = [0, 0];
    this.steer = 0;

    // Axle group: housing + both wheels, so it moves as one rigid body.
    this.group = new THREE.Group();
    this.group.name = front ? 'axleFront' : 'axleRear';
    this.group.position.set(0, this.restY, this.restZ);
    this.group.add(buildSolidAxle({
      track, tubeR: spec.axle.tubeR, diffOffset: spec.axle.diffOffset * (front ? -1 : 1),
      steering: front, detail,
    }));

    this.wheels = [];
    for (const side of [-1, 1]) {
      const w = buildWheel({
        diameter: spec.tire.diameter, width: spec.tire.width, rimInch: spec.tire.rimInch,
        spokes: spec.tire.spokes, rimColor: spec.tire.rimColor,
        beadlock: spec.tire.beadlock, side, detail,
      });
      w.position.set(side * (track / 2), 0, 0);
      this.group.add(w);
      this.wheels.push(w);
    }

    // Links + coilovers live on the chassis, so they are a sibling group.
    this.suspension = buildFourLink({
      spec, axleZ: this.restZ, track, axleY: this.restY, steering: front,
    });

    // Convert every link target and shock anchor from vehicle space into
    // axle-local space once, so per-frame updates are a single matrix apply.
    const restPos = new THREE.Vector3(0, this.restY, this.restZ);
    this.links = this.suspension.userData.links.map((link) => ({
      link, local: link.target.clone().sub(restPos),
    }));
    this.shocks = this.suspension.userData.shocks.map((co) => ({
      co,
      local: co.userData.anchor.clone().sub(restPos),
      restDist: co.userData.anchor.distanceTo(co.position),
    }));
  }

  update() {
    const [cl, cr] = this.travel;
    this.group.position.y = this.restY + (cl + cr) / 2;
    this.group.rotation.z = Math.asin(clamp((cr - cl) / this.track, -0.6, 0.6));
    this.group.updateMatrix();

    if (this.front) {
      this.wheels[0].rotation.y = this.steer;
      this.wheels[1].rotation.y = this.steer;
    }

    const m = this.group.matrix;
    const v = new THREE.Vector3();
    for (const { link, local } of this.links) link.aim(v.copy(local).applyMatrix4(m));
    for (const { co, local, restDist } of this.shocks) {
      const len = aimDown(co, v.copy(local).applyMatrix4(m));
      co.userData.setLength(clamp(len / restDist, 0.18, 1.25));
    }
  }
}

export class Vehicle {
  /**
   * @param {object} spec  A spec from catalog.js.
   * @param {object} opts
   * @param {number} opts.detail 0 far LOD, 1 mid, 2 hero.
   */
  constructor(spec, { detail = 2, interior = true } = {}) {
    this.spec = spec;
    this.detail = detail;
    this.root = new THREE.Group();
    this.root.name = spec.id;
    const m = bodyMetrics(spec);
    this.metrics = m;

    /* --- chassis --- */
    this.root.add(buildLadderFrame(spec));
    this.root.add(buildDrivetrain(spec, detail));
    this.root.add(buildArmour(spec, detail));

    /* --- axles + suspension --- */
    this.axles = {
      front: new AxleRig({ spec, front: true, detail }),
      rear: new AxleRig({ spec, front: false, detail }),
    };
    for (const a of Object.values(this.axles)) this.root.add(a.group, a.suspension);

    // Flat wheel list in a stable order: FL, FR, RL, RR - the order physics uses.
    this.wheels = [
      { node: this.axles.front.wheels[0], axle: this.axles.front, index: 0, front: true, side: -1 },
      { node: this.axles.front.wheels[1], axle: this.axles.front, index: 1, front: true, side: 1 },
      { node: this.axles.rear.wheels[0], axle: this.axles.rear, index: 0, front: false, side: -1 },
      { node: this.axles.rear.wheels[1], axle: this.axles.rear, index: 1, front: false, side: 1 },
    ];
    this.wheelRadius = spec.tire.diameter / 2;

    /* --- body + interior --- */
    this.body = buildBody(spec, detail);
    this.root.add(this.body);
    if (interior && detail > 0) {
      this.interior = buildInterior(spec, m, { detail });
      this.root.add(this.interior);
    }

    /* --- accessories --- */
    const f = spec.features;
    if (f.cage) this.root.add(buildRollCage(spec, m));
    if (f.winch !== undefined) this.root.add(buildFrontBumper(spec, m, { winch: f.winch }));
    if (f.rearBumper) this.root.add(buildRearBumper(spec, m));
    if (f.snorkel) this.root.add(buildSnorkel(spec, m, spec.rhd ? -1 : 1));
    if (f.roofRack) this.root.add(buildRoofRack(spec, m, { cargo: detail > 1 }));
    if (f.spare) this.root.add(buildSpare(spec, m, { detail }));
    if (f.kit && detail > 1) this.root.add(buildMiscKit(spec, m));
    this.lightBarNode = f.lightBar ? buildLightBar(spec, m) : null;
    if (this.lightBarNode) this.root.add(this.lightBarNode);

    this._collectLamps();
    this._makeDirtyable();

    this.steerAngle = 0;
    this.mud = 0;
    this._invRoot = new THREE.Matrix4();
    this.update();
  }

  /* ------------------------------------------------------------- lights --- */

  _collectLamps() {
    this.lamps = { head: [], tail: [], indicator: [], aux: [] };
    for (const { node, kind } of this.body.userData.lamps) {
      const mat = node.userData.lensMaterial;
      if (mat && this.lamps[kind]) this.lamps[kind].push(mat);
    }
    if (this.lightBarNode) {
      for (const l of this.lightBarNode.userData.lamps) this.lamps.aux.push(l.material);
    }

    // Two real spotlights so headlights actually light the trail at night.
    this.headlightBeams = [];
    const m = this.metrics;
    for (const s of [-1, 1]) {
      const spot = new THREE.SpotLight(0xfff0d6, 0, 70, 0.55, 0.45, 1.2);
      spot.position.set(s * (this.spec.body.width / 2 - 0.16), m.sillY + 0.5, m.zFront + 0.05);
      spot.target.position.set(s * 0.6, -1.2, m.zFront + 18);
      this.root.add(spot, spot.target);
      this.headlightBeams.push(spot);
    }
  }

  /**
   * @param {{head?:boolean, aux?:boolean, brake?:boolean, indicator?:number}} s
   */
  setLights(s = {}) {
    const set = (mats, on, mul = 1) => {
      for (const mm of mats) mm.emissiveIntensity = on ? (mm.userData.onIntensity ?? 2) * mul : 0;
    };
    if (s.head !== undefined) {
      set(this.lamps.head, s.head);
      for (const b of this.headlightBeams) b.intensity = s.head ? 140 : 0;
    }
    if (s.aux !== undefined) set(this.lamps.aux, s.aux);
    // Tails glow dim when lit and hard under braking.
    if (s.head !== undefined || s.brake !== undefined) {
      const braking = !!s.brake;
      set(this.lamps.tail, braking || !!s.head, braking ? 1.6 : 0.35);
    }
    if (s.indicator !== undefined) set(this.lamps.indicator, !!s.indicator);
  }

  /* ---------------------------------------------------------------- mud --- */

  /**
   * Give this vehicle its own copy of every surface material so dirt is
   * per-vehicle rather than global, then patch each for mud.
   */
  _makeDirtyable() {
    const map = new Map();
    this._ownMaterials = [];
    this._mudUniforms = [];
    this.root.traverse((o) => {
      if (!o.isMesh || !o.material) return;
      const src = o.material;
      if (src.userData.onIntensity) return;            // lamp lenses stay clean
      if (!map.has(src)) {
        const c = src.clone();
        c.userData = { ...src.userData };
        map.set(src, c);
        this._ownMaterials.push(c);
        this._mudUniforms.push(applyMud(c, { lowY: this.metrics.sillY * 0.5 }));
      }
      o.material = map.get(src);
    });
    // buildBody handed us the original paint; re-point it at our clone.
    this.paint = map.get(this.body.userData.paint) ?? this.body.userData.paint;
  }

  /** @param {number} v 0 = showroom clean, 1 = caked. */
  setMud(v) {
    this.mud = clamp(v, 0, 1);
    for (const h of this._mudUniforms) h.amount.value = this.mud;
  }

  /**
   * Feed the mud shader the transform that takes world space back to vehicle
   * space, so dirt stays pinned to the bodywork however the truck is pitched.
   */
  _syncMudTransform() {
    this.root.updateWorldMatrix(true, false);
    this._invRoot.copy(this.root.matrixWorld).invert();
    for (const h of this._mudUniforms) h.inv.value.copy(this._invRoot);
  }

  /** @param {number|string} color */
  setPaint(color) {
    this.paint.color.set(color);
  }

  /* ------------------------------------------------------------ dynamics --- */

  /** @param {number} rad Road-wheel angle, positive steers left. */
  setSteering(rad) {
    this.steerAngle = rad;
    this.axles.front.steer = rad;
    if (this.interior) {
      // Steering wheel turns roughly 3 turns lock to lock against the wheels.
      this.interior.userData.steeringWheel.rotation.z = -rad * 4.2;
    }
  }

  /** @param {number} i Wheel index FL,FR,RL,RR. @param {number} rad absolute. */
  setWheelSpin(i, rad) {
    this.wheels[i].node.userData.spin.rotation.x = rad;
  }

  /** @param {number} i Wheel index. @param {number} metres + = compressed. */
  setTravel(i, metres) {
    const w = this.wheels[i];
    const lim = this.spec.suspension.travel;
    w.axle.travel[w.index] = clamp(metres, -lim, lim);
  }

  /** Resolve axle transforms, links and coilovers. Call once per frame. */
  update() {
    this.axles.front.update();
    this.axles.rear.update();
    if (this.mud > 0) this._syncMudTransform();
  }

  /* --------------------------------------------------------------- misc --- */

  /** Half-extents and centre for the physics chassis collider. */
  getChassisBox() {
    const m = this.metrics;
    return {
      halfExtents: [
        this.spec.body.width / 2,
        (m.roofY - m.sillY) / 2 + 0.1,
        (m.zFront - m.zRear) / 2,
      ],
      centre: [0, (m.sillY + m.roofY) / 2, (m.zFront + m.zRear) / 2],
    };
  }

  get triangles() { return triangleCount(this.root); }

  dispose() {
    this.root.traverse((o) => { if (o.isMesh) o.geometry.dispose(); });
    for (const mm of this._ownMaterials) mm.dispose();
    this.root.removeFromParent();
  }
}

export function createVehicle(spec, opts) { return new Vehicle(spec, opts); }
