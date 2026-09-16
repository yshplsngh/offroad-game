/**
 * camera.js - the camera rig.
 *
 * Offroad driving is a slow, technical camera problem, not a racing one. The
 * player needs to read terrain ahead, see where the front tires are about to
 * land, and feel the chassis pitch without getting seasick. So:
 *
 *  - The chase camera follows POSITION tightly but HEADING loosely. Snapping
 *    yaw to the chassis makes a crawling truck feel like it is swinging a
 *    camera on a stick every time a wheel drops into a rut.
 *  - Roll is never inherited. A rolled horizon reads as a bug, not as drama.
 *  - Pitch is inherited only partially, so cresting a climb still feels steep.
 *  - The rig raises itself on steep descents so you can see the line down.
 *
 * Every mode shares one damped follow so switching does not jolt.
 */
import * as THREE from 'three';

const UP = new THREE.Vector3(0, 1, 0);
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

/** Frame-rate independent exponential smoothing. */
const damp = (current, target, lambda, dt) =>
  THREE.MathUtils.lerp(current, target, 1 - Math.exp(-lambda * dt));

export const CAMERA_MODES = ['chase', 'close', 'cockpit', 'bonnet', 'orbit', 'cinematic'];

export class CameraRig {
  /**
   * @param {THREE.PerspectiveCamera} camera
   * @param {object} opts
   * @param {HTMLElement} opts.domElement Element to bind look/zoom input to.
   */
  constructor(camera, { domElement = window } = {}) {
    this.camera = camera;
    this.mode = 'chase';
    this.target = null;              // the Vehicle instance we follow
    this.metrics = null;

    // Smoothed follow state, kept in world space.
    this._pos = new THREE.Vector3();
    this._look = new THREE.Vector3();
    this._yaw = 0;                   // smoothed heading we orbit around
    this._pitch = 0;
    this._fov = camera.fov;
    this._shake = 0;

    // Player look offset, applied on top of the followed heading.
    this.lookYaw = 0;
    this.lookPitch = 0;
    this.distance = 1;               // zoom multiplier
    this._dragging = false;

    this._bindInput(domElement);
  }

  /** @param {import('../vehicles/vehicle.js').Vehicle} vehicle */
  follow(vehicle) {
    this.target = vehicle;
    this.metrics = vehicle.metrics;
    const p = vehicle.root.position;
    this._pos.copy(p).add(new THREE.Vector3(0, 3, -7));
    this._look.copy(p);
    this._yaw = vehicle.root.rotation.y;
  }

  setMode(mode) {
    if (!CAMERA_MODES.includes(mode)) return;
    this.mode = mode;
    // Free look only makes sense where the player is not steering the view.
    if (mode !== 'orbit') { this.lookYaw = 0; this.lookPitch = 0; }
  }

  cycleMode(dir = 1) {
    const i = CAMERA_MODES.indexOf(this.mode);
    this.setMode(CAMERA_MODES[(i + dir + CAMERA_MODES.length) % CAMERA_MODES.length]);
  }

  /** Kick the camera - call on hard landings and collisions. */
  impulse(strength = 1) { this._shake = Math.min(1.6, this._shake + strength); }

  _bindInput(el) {
    const down = (e) => { if (e.button === 0 || e.button === 2) this._dragging = true; };
    const up = () => { this._dragging = false; };
    const move = (e) => {
      if (!this._dragging) return;
      this.lookYaw -= e.movementX * 0.0035;
      this.lookPitch = clamp(this.lookPitch - e.movementY * 0.0030, -0.9, 0.7);
      if (this.mode !== 'orbit') this.setMode('orbit');
    };
    const wheel = (e) => {
      this.distance = clamp(this.distance * (1 + Math.sign(e.deltaY) * 0.1), 0.45, 3.2);
      e.preventDefault();
    };
    el.addEventListener?.('pointerdown', down);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointermove', move);
    el.addEventListener?.('wheel', wheel, { passive: false });
    el.addEventListener?.('contextmenu', (e) => e.preventDefault());
    this._unbind = () => {
      el.removeEventListener?.('pointerdown', down);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointermove', move);
      el.removeEventListener?.('wheel', wheel);
    };
  }

  /**
   * @param {number} dt
   * @param {object} [telemetry] From the vehicle controller: `speed` (m/s),
   *   `reverse`, `pitch`, `roll` (radians).
   */
  update(dt, telemetry = {}) {
    if (!this.target) return;
    const root = this.target.root;
    const m = this.metrics;
    const speed = telemetry.speed ?? 0;

    // Chassis basis. We take heading and pitch from it but deliberately
    // discard roll - see the module note.
    const q = root.quaternion;
    const fwd = new THREE.Vector3(0, 0, 1).applyQuaternion(q);
    const heading = Math.atan2(fwd.x, fwd.z);
    const chassisPitch = Math.asin(clamp(fwd.y, -1, 1));

    if (this.mode === 'cockpit' || this.mode === 'bonnet') {
      this._updateRigid(dt, root, q, m);
      return;
    }

    // Loosely track the chassis heading; slower at low speed so inching over
    // rocks does not whip the view around.
    const yawLambda = THREE.MathUtils.lerp(1.6, 5.0, clamp(Math.abs(speed) / 12, 0, 1));
    const wanted = this.mode === 'orbit' ? this._yaw : heading + (telemetry.reverse ? Math.PI : 0);
    this._yaw = this._dampAngle(this._yaw, wanted, yawLambda, dt);
    this._pitch = damp(this._pitch, chassisPitch * 0.45, 3.0, dt);

    const cfg = this._config(speed);
    const yaw = this._yaw + this.lookYaw;
    // Descending steeply? Lift and look further down the hill.
    const drop = clamp(-chassisPitch, 0, 0.6);
    const pitch = this._pitch + this.lookPitch - drop * 0.5;

    const dist = cfg.dist * this.distance;
    const offset = new THREE.Vector3(
      Math.sin(yaw) * -dist * Math.cos(pitch),
      cfg.height + dist * Math.sin(-pitch) + drop * 1.6,
      Math.cos(yaw) * -dist * Math.cos(pitch),
    );

    const anchor = root.position.clone().add(new THREE.Vector3(0, m.beltY * 0.75, 0));
    const wantPos = anchor.clone().add(offset);
    // Lead the look point in the direction of travel so you see the line ahead.
    const wantLook = anchor.clone().addScaledVector(fwd, cfg.lead * clamp(speed / 8, -1, 1.6));

    this._pos.x = damp(this._pos.x, wantPos.x, cfg.lambda, dt);
    this._pos.y = damp(this._pos.y, wantPos.y, cfg.lambda * 0.8, dt);
    this._pos.z = damp(this._pos.z, wantPos.z, cfg.lambda, dt);
    this._look.lerp(wantLook, 1 - Math.exp(-cfg.lambda * 1.4 * dt));

    this.camera.position.copy(this._pos);
    this.camera.up.copy(UP);
    this.camera.lookAt(this._look);
    this._applyShake(dt);

    // Widen the lens with speed - cheap, effective sense of pace.
    const wantFov = cfg.fov + clamp(Math.abs(speed) / 14, 0, 1) * 7;
    this._fov = damp(this._fov, wantFov, 3, dt);
    if (Math.abs(this.camera.fov - this._fov) > 0.01) {
      this.camera.fov = this._fov;
      this.camera.updateProjectionMatrix();
    }
  }

  /** Cockpit and bonnet ride with the body, roll included - you are inside it. */
  _updateRigid(dt, root, q, m) {
    const local = this.mode === 'cockpit'
      ? new THREE.Vector3(
        (this.target.spec.rhd ? -1 : 1) * this.target.spec.body.width * 0.24,
        m.beltY - 0.02, m.zCowl - 0.72)
      : new THREE.Vector3(0, m.hoodY + 0.12, m.zCowl + 0.1);

    const wantPos = local.applyQuaternion(q).add(root.position);
    this._pos.lerp(wantPos, 1 - Math.exp(-28 * dt));
    this.camera.position.copy(this._pos);

    // Inherit full orientation, then let the player glance around.
    this.camera.quaternion.copy(q);
    this.camera.rotateY(this.lookYaw);
    this.camera.rotateX(this.lookPitch);
    this._applyShake(dt);

    const wantFov = this.mode === 'cockpit' ? 68 : 62;
    this._fov = damp(this._fov, wantFov, 5, dt);
    this.camera.fov = this._fov;
    this.camera.updateProjectionMatrix();
  }

  _config(speed) {
    const fast = clamp(Math.abs(speed) / 16, 0, 1);
    switch (this.mode) {
      case 'close':     return { dist: 5.0, height: 1.5, lead: 2.2, lambda: 7.0, fov: 52 };
      case 'orbit':     return { dist: 8.0, height: 1.8, lead: 0.0, lambda: 5.0, fov: 46 };
      case 'cinematic': return { dist: 12 + fast * 5, height: 3.4, lead: 6.0, lambda: 1.2, fov: 38 };
      default:          return { dist: 7.4, height: 2.1, lead: 3.2, lambda: 5.5, fov: 48 };
    }
  }

  /** Shortest-path angular damping, so we never unwind the long way round. */
  _dampAngle(current, target, lambda, dt) {
    let delta = (target - current) % (Math.PI * 2);
    if (delta > Math.PI) delta -= Math.PI * 2;
    if (delta < -Math.PI) delta += Math.PI * 2;
    return current + delta * (1 - Math.exp(-lambda * dt));
  }

  _applyShake(dt) {
    if (this._shake <= 0.001) return;
    const s = this._shake * 0.05;
    this.camera.position.x += (Math.random() - 0.5) * s;
    this.camera.position.y += (Math.random() - 0.5) * s;
    this.camera.rotateZ((Math.random() - 0.5) * this._shake * 0.012);
    this._shake = Math.max(0, this._shake - dt * 3.2);
  }

  dispose() { this._unbind?.(); }
}
