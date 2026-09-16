/**
 * controller.js - the driving model. This is where the game is.
 *
 * A Rapier dynamic body carries the chassis; everything below the frame rails
 * is solved here, per wheel, per fixed step:
 *
 *   probe the ground  ->  suspension force  ->  normal load
 *   normal load       ->  tire force        ->  reaction torque on the wheel
 *   engine + clutch   ->  drive torque      ->  wheel spin
 *   wheel spin        ->  slip              ->  back to tire force next step
 *
 * WHY NOT RAPIER'S VEHICLE CONTROLLER: its raycast car assumes a friction
 * coefficient per wheel and a spring you cannot look inside. We need the tire
 * to read SURFACE at the exact contact point, to sink into mud, and to lose
 * grip the moment it spins - none of which survive being handed to a black box.
 * Rapier still does what Rapier is good at: rigid-body integration, the
 * inertia tensor, and raycasts against scatter colliders.
 *
 * THE ORDER MATTERS. Loads are computed for all four corners BEFORE any tire
 * force, because anti-roll is a coupling between the two wheels on an axle and
 * weight transfer is the whole reason a climb feels different from a flat. Then
 * the drivetrain splits torque, then each wheel integrates, then the diffs
 * equalise speeds. Reordering any of those turns the locker into a no-op.
 *
 * SIGN CONVENTION: steering angle is the wheel's rotation about +Y, so a
 * POSITIVE angle points the wheel toward +X and the truck turns right. Left on
 * the stick therefore produces a negative angle. The visual rig uses exactly
 * the same number, so the front wheels always point where the physics thinks.
 */
import * as THREE from 'three';
import { TUNE } from './tune.js';
import { createDrivetrain, REVERSE, NEUTRAL } from './drivetrain.js';
import { tireForce, peakFriction, rollingResistance } from './tire.js';
import { createGroundProbe, makeGroundHit } from './ground.js';
import { SURFACE, FIXED_DT } from '../world/contract.js';

const G = 9.81;
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const clamp01 = (v) => clamp(v, 0, 1);
const approach = (cur, target, rate, dt) => {
  const d = target - cur;
  const step = rate * dt;
  return Math.abs(d) <= step ? target : cur + Math.sign(d) * step;
};

/**
 * @param {object} opts
 * @param {import('../vehicles/vehicle.js').Vehicle} opts.vehicle
 * @param {object} opts.rapier   The initialised RAPIER namespace.
 * @param {object} opts.world    A RAPIER.World.
 * @param {object} opts.terrain  A TerrainProvider.
 * @param {object} [opts.tune]
 * @param {{x:number,y:number,z:number}} [opts.spawn]
 */
export function createController({
  vehicle, rapier, world, terrain, tune = TUNE, spawn = { x: 0, y: 2, z: 0 }, heading = 0,
}) {
  const spec = vehicle.spec;
  const mass = spec.perf.mass;
  const tireR = vehicle.wheelRadius;
  const travel = spec.suspension.travel;
  const track = spec.axle.track;
  const wheelbase = spec.frame.wheelbase;
  const box = vehicle.getChassisBox();

  /* ------------------------------------------------------------- body --- */

  const half = box.halfExtents;
  const comLocal = {
    x: 0,
    y: vehicle.metrics.sillY + tune.comAboveSill,
    z: -tune.comRearBias * wheelbase,
  };
  // Box inertia about the COM, then trimmed per axis. Roll is reduced and
  // pitch/yaw raised because a real truck carries its mass at the ends.
  const Ix = (mass / 12) * (4 * half[1] * half[1] + 4 * half[2] * half[2]) * tune.inertiaPitch;
  const Iy = (mass / 12) * (4 * half[0] * half[0] + 4 * half[2] * half[2]) * tune.inertiaYaw;
  const Iz = (mass / 12) * (4 * half[0] * half[0] + 4 * half[1] * half[1]) * tune.inertiaRoll;

  const spawnQ = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), heading);

  const bodyDesc = rapier.RigidBodyDesc.dynamic()
    .setTranslation(spawn.x, spawn.y, spawn.z)
    .setRotation({ x: spawnQ.x, y: spawnQ.y, z: spawnQ.z, w: spawnQ.w })
    .setLinearDamping(tune.bodyLinearDamping)
    .setAngularDamping(tune.bodyAngularDamping)
    .setCanSleep(false)
    .setAdditionalMassProperties(
      mass, comLocal, { x: Ix, y: Iy, z: Iz }, { x: 0, y: 0, z: 0, w: 1 },
    );
  const body = world.createRigidBody(bodyDesc);

  // Density 0: the mass properties above are the truth. A collider that also
  // contributed mass would silently double it and ruin every tuning number.
  const colDesc = rapier.ColliderDesc.cuboid(half[0], half[1], half[2])
    .setTranslation(box.centre[0], box.centre[1], box.centre[2])
    .setDensity(0)
    .setFriction(0.35)
    .setRestitution(0.02);
  const collider = world.createCollider(colDesc, body);

  const probe = createGroundProbe({ terrain, world, rapier, exclude: body });

  /* ------------------------------------------------------------ wheels --- */

  const staticLoad = (mass * G) / 4;
  const wheelMass = mass * tune.wheelMassFraction;
  const wheelInertia = tune.wheelInertiaFactor * wheelMass * tireR * tireR;

  const drivetrain = createDrivetrain(spec, tune, wheelInertia);

  const wheels = [];
  for (let i = 0; i < 4; i++) {
    const front = i < 2;
    const side = i % 2 === 0 ? -1 : 1;
    wheels.push({
      index: i,
      front,
      side,
      // Hub rest position in body space. The vehicle origin is at ground level
      // between the axles, so a hub sits exactly one tire radius up.
      hub: new THREE.Vector3(side * (track / 2), tireR, front ? wheelbase / 2 : -wheelbase / 2),
      steer: 0,
      omega: 0,
      spin: 0,
      travel: 0,        // + compressed, metres
      u: 0,             // compression measured from full droop
      load: 0,
      sink: 0,
      slip: 0,
      slipRatio: 0,
      contact: false,
      solid: false,
      surface: SURFACE.DIRT,
      wetness: 0,
      hit: makeGroundHit(),
      springForce: 0,
      long: 0,
      lat: 0,
      contactPoint: new THREE.Vector3(),
      normal: new THREE.Vector3(0, 1, 0),
    });
  }

  // Spring rate from the sag target: at rest the suspension must hold a
  // quarter of the truck at `staticSag` of its total travel.
  const totalTravel = travel * 2;
  const sagU = Math.max(0.01, tune.staticSag * totalTravel);
  const springK = staticLoad / sagU;
  const critical = 2 * Math.sqrt(springK * (mass / 4));
  const dampBump = critical * tune.dampBump;
  const dampRebound = critical * tune.dampRebound;
  const maxSpring = staticLoad * tune.maxSpringForceG;
  const rayLen = totalTravel + tireR + 0.6;
  const bumpStopU = tune.bumpStopStart * totalTravel;
  /** Compression at rest, and therefore how high the origin floats above the
   *  ground once the truck has settled. Spawning anywhere else is a drop. */
  const restTravel = sagU - travel;
  const rideHeight = -restTravel;

  const omega = [0, 0, 0, 0];
  const driveTorque = [0, 0, 0, 0];
  const brakes = [0, 0, 0, 0];

  /* ------------------------------------------------------------ inputs --- */

  const input = {
    throttle: 0, brake: 0, handbrake: 0, steer: 0, clutch: 0, winch: 0,
  };
  let steerAngle = 0;

  /* ------------------------------------------------------------- state --- */

  const state = {
    speed: 0,
    forwardSpeed: 0,
    reverse: false,
    pitch: 0,
    roll: 0,
    heading: 0,
    altitude: 0,
    airborne: 0,
    stuck: 0,
    mud: 0,
    odometer: 0,
    impact: 0,
    surface: SURFACE.DIRT,
    wetness: 0,
    slipMax: 0,
    flipTimer: 0,
    winchAnchor: null,
    winchLength: 0,
    winchTension: 0,
  };

  /* ------------------------------------------------------------ scratch -- */

  const q = new THREE.Quaternion();
  const up = new THREE.Vector3();
  const fwd = new THREE.Vector3();
  const right = new THREE.Vector3();
  const pos = new THREE.Vector3();
  const com = new THREE.Vector3();
  const linvel = new THREE.Vector3();
  const angvel = new THREE.Vector3();
  const anchor = new THREE.Vector3();
  const down = new THREE.Vector3();
  const contact = new THREE.Vector3();
  const rel = new THREE.Vector3();
  const pv = new THREE.Vector3();
  const nrm = new THREE.Vector3();
  const wFwd = new THREE.Vector3();
  const wRight = new THREE.Vector3();
  const force = new THREE.Vector3();
  const point = new THREE.Vector3();
  const tmp = new THREE.Vector3();
  const tire = { fx: 0, fy: 0, slipRatio: 0, slipAngle: 0, combined: 0 };
  const euler = new THREE.Euler();
  let lastSpeed = 0;

  const hasWorldCom = typeof body.worldCom === 'function';

  function readBody() {
    const t = body.translation();
    pos.set(t.x, t.y, t.z);
    const r = body.rotation();
    q.set(r.x, r.y, r.z, r.w);
    if (hasWorldCom) {
      const c = body.worldCom();
      com.set(c.x, c.y, c.z);
    } else {
      com.set(comLocal.x, comLocal.y, comLocal.z).applyQuaternion(q).add(pos);
    }
    const v = body.linvel();
    linvel.set(v.x, v.y, v.z);
    const a = body.angvel();
    angvel.set(a.x, a.y, a.z);
    up.set(0, 1, 0).applyQuaternion(q);
    fwd.set(0, 0, 1).applyQuaternion(q);
    right.set(1, 0, 0).applyQuaternion(q);
  }

  /** World-space velocity of a point rigidly attached to the body. */
  function pointVelocity(p, out) {
    rel.copy(p).sub(com);
    out.copy(angvel).cross(rel).add(linvel);
    return out;
  }

  function addForce(f, p) {
    body.addForceAtPoint({ x: f.x, y: f.y, z: f.z }, { x: p.x, y: p.y, z: p.z }, true);
  }

  /* --------------------------------------------------------- suspension -- */

  function solveSuspension(dt) {
    for (const w of wheels) {
      anchor.copy(w.hub).setY(tireR + travel).applyQuaternion(q).add(pos);
      down.copy(up).multiplyScalar(-1);

      probe.probe(anchor, down, rayLen, w.hit);
      const hit = w.hit;

      if (!hit.hit) {
        w.contact = false;
        w.travel = -travel;
        w.u = 0;
        w.load = 0;
        w.springForce = 0;
        w.sink = Math.max(0, w.sink - dt * tune.sinkRate * 0.5);
        continue;
      }

      w.surface = hit.surface;
      w.wetness = hit.wetness;
      w.solid = hit.solid;

      // Soft ground: the tire settles in over a fraction of a second, so mud
      // gets worse the longer you sit in it rather than instantly.
      const sinkTarget = hit.surface.sink * tune.sinkScale;
      w.sink += (sinkTarget - w.sink) * clamp01(dt * tune.sinkRate);

      const c = clamp(travel - (hit.dist - tireR - w.sink), -travel, travel);
      w.travel = c;
      w.u = c + travel;
      w.contact = c > -travel + 1e-4;
      if (!w.contact) { w.load = 0; w.springForce = 0; continue; }

      contact.set(hit.px, hit.py, hit.pz);
      nrm.set(hit.nx, hit.ny, hit.nz);
      pointVelocity(contact, pv);
      // Positive = the body is coming down on the wheel.
      const vSusp = -pv.dot(up);

      let f = springK * w.u;
      if (w.u > bumpStopU) {
        // The stop is a rubber cone, not a wall: it needs its own damping or
        // every hard landing is answered with a launch.
        const over = w.u - bumpStopU;
        f += springK * tune.bumpStopRate * over;
        if (vSusp > 0) f += dampBump * tune.bumpStopRate * 0.08 * over * vSusp;
      }
      f += (vSusp > 0 ? dampBump : dampRebound) * vSusp;

      w.springForce = clamp(f, 0, maxSpring);
      w.contactPoint.copy(contact);
      w.normal.copy(nrm);
    }

    // Anti-roll bars. The compressed side gains force and the drooping side
    // loses it, which is what resists roll - getting this sign backwards turns
    // the bar into a pro-roll bar that amplifies every lean.
    // Deliberately weak: a stiff bar is the fastest way to kill articulation.
    for (const [a, b, k] of [[0, 1, tune.antiRollFront], [2, 3, tune.antiRollRear]]) {
      const wa = wheels[a], wb = wheels[b];
      if (!wa.contact && !wb.contact) continue;
      const transfer = springK * k * (wa.u - wb.u);
      wa.springForce = clamp(wa.springForce + transfer, 0, maxSpring);
      wb.springForce = clamp(wb.springForce - transfer, 0, maxSpring);
    }

    // Apply the spring force and record the normal load for the tire model.
    for (const w of wheels) {
      if (!w.contact) { w.load = 0; continue; }
      force.copy(up).multiplyScalar(w.springForce);
      addForce(force, w.contactPoint);
      // The tire only presses on the ground along the normal.
      w.load = w.springForce * Math.max(0.25, w.normal.dot(up));
    }
  }

  /* --------------------------------------------------------------- tires -- */

  function solveTires(dt) {
    let slipMax = 0;
    let bogged = 0;

    for (const w of wheels) {
      const drive = driveTorque[w.index];
      let torque = drive;

      if (w.contact && w.load > 0) {
        // Wheel heading, flattened into the contact plane. A wheel on a slope
        // makes force along the ground, not along the chassis.
        wFwd.copy(fwd).applyAxisAngle(up, w.steer);
        wFwd.addScaledVector(w.normal, -wFwd.dot(w.normal));
        if (wFwd.lengthSq() < 1e-6) wFwd.copy(fwd);
        wFwd.normalize();
        wRight.copy(w.normal).cross(wFwd).normalize();

        pointVelocity(w.contactPoint, pv);
        const vLong = pv.dot(wFwd);
        const vLat = pv.dot(wRight);
        const wheelSpeed = w.omega * tireR;

        const mu = peakFriction(w.surface, w.wetness, w.load, staticLoad, tune);
        tireForce(tire, w.load, mu, vLong, vLat, wheelSpeed, tune);

        w.long = tire.fx;
        w.lat = tire.fy;
        w.slip = tire.combined;
        w.slipRatio = tire.slipRatio;
        if (tire.combined > slipMax) slipMax = tire.combined;

        force.copy(wFwd).multiplyScalar(tire.fx).addScaledVector(wRight, tire.fy);
        point.copy(w.contactPoint).addScaledVector(w.normal, tune.tireForceHeight * tireR);
        addForce(force, point);

        // Reaction on the wheel, plus the ground trying to stop it turning.
        torque -= tire.fx * tireR;
        torque -= Math.sign(w.omega)
          * rollingResistance(w.surface, w.wetness, w.load, tireR, tune);

        // Bulldozing. A sunk tire has to push a wall of slop out of the way,
        // and unlike friction this does not care how much grip it has.
        if (w.sink > 0.005) {
          bogged += w.sink;
          const drag = tune.bogDrag * w.sink;
          tmp.copy(pv).setY(0);
          const sp = tmp.length();
          if (sp > 0.02) {
            force.copy(tmp).multiplyScalar(-drag * Math.min(sp, 6) / sp);
            addForce(force, w.contactPoint);
          }
        }
      } else {
        w.long = 0; w.lat = 0; w.slip = 0; w.slipRatio = 0;
        // A free wheel still has bearing drag, or it never stops spinning.
        torque -= w.omega * 0.6;
      }

      w.omega += (torque * dt) / wheelInertia;

      // Brakes last, and clamped so they can only ever reach zero. Letting a
      // brake torque overshoot flips the wheel's direction every step and the
      // truck buzzes apart.
      const bt = brakes[w.index];
      if (bt > 0) {
        const dOmega = (bt * dt) / wheelInertia;
        w.omega = Math.abs(w.omega) <= dOmega ? 0 : w.omega - Math.sign(w.omega) * dOmega;
      }
      omega[w.index] = w.omega;
    }

    drivetrain.applyDiffs(omega, dt);
    for (const w of wheels) {
      w.omega = omega[w.index];
      w.spin += w.omega * dt;
    }

    state.slipMax = slipMax;
    return bogged;
  }

  /* --------------------------------------------------------------- misc --- */

  function applyAero() {
    const v2 = linvel.lengthSq();
    if (v2 < 0.25) return;
    const v = Math.sqrt(v2);
    force.copy(linvel).multiplyScalar(-tune.aeroDrag * v);
    addForce(force, com);
  }

  function applyWinch(dt) {
    state.winchTension = 0;
    if (!state.winchAnchor) return;
    tmp.copy(state.winchAnchor).sub(pos);
    const dist = tmp.length();
    if (dist > tune.winchRange * 1.3) { state.winchAnchor = null; return; }
    if (input.winch > 0) {
      state.winchLength = Math.max(1.0, state.winchLength - tune.winchSpeed * dt);
    }
    const over = dist - state.winchLength;
    if (over <= 0) return;
    const pull = Math.min(tune.winchForce, over * tune.winchStiffness);
    state.winchTension = pull / tune.winchForce;
    force.copy(tmp).multiplyScalar(pull / Math.max(0.001, dist));
    // Pull on the bumper, not the centre - that is why a winch noses you down.
    point.set(0, vehicle.metrics.sillY * 0.4, vehicle.metrics.zFront)
      .applyQuaternion(q).add(pos);
    addForce(force, point);
  }

  /* ---------------------------------------------------------------- step -- */

  function step(dt) {
    readBody();
    body.resetForces(true);
    body.resetTorques(true);

    /* steering: rate-limited, and reduced with speed so a 35-degree lock does
     * not put you in a tree at 60 km/h. */
    const speedFrac = clamp01(Math.abs(state.forwardSpeed) / tune.steerSpeedFalloff);
    const authority = 1 - (1 - tune.steerMinFraction) * speedFrac;
    const target = input.steer * tune.maxSteerAngle * authority;
    const rate = Math.abs(input.steer) > 0.02 ? tune.steerRate : tune.steerReturnRate;
    steerAngle = approach(steerAngle, target, rate, dt);

    // Ackermann: both front wheels must be tangent to circles about the same
    // centre, so the inside wheel - on the smaller radius - turns further.
    // Without it the truck scrubs its way round every hairpin.
    const mag = Math.abs(steerAngle);
    let innerA = mag, outerA = mag;
    if (mag > 1e-4) {
      const radius = wheelbase / Math.tan(mag);
      innerA = Math.atan(wheelbase / Math.max(0.35, radius - track / 2));
      outerA = Math.atan(wheelbase / (radius + track / 2));
    }
    const sgn = Math.sign(steerAngle) || 1;
    // Positive steer turns right, so the right wheel (index 1) is the inner one.
    const leftA = sgn * (steerAngle > 0 ? outerA : innerA);
    const rightA = sgn * (steerAngle > 0 ? innerA : outerA);
    wheels[0].steer = THREE.MathUtils.lerp(steerAngle, leftA, tune.ackermann);
    wheels[1].steer = THREE.MathUtils.lerp(steerAngle, rightA, tune.ackermann);

    solveSuspension(dt);

    const dtOut = drivetrain.update(dt, input.throttle, omega);
    for (let i = 0; i < 4; i++) driveTorque[i] = dtOut[i];
    drivetrain.brakeTorques(brakes, input.brake, input.handbrake);

    const bogged = solveTires(dt);

    applyAero();
    applyWinch(dt);

    world.step();

    /* ------------------------------------------------------ telemetry --- */
    readBody();
    state.forwardSpeed = linvel.dot(fwd);
    state.speed = linvel.length();
    state.reverse = state.forwardSpeed < -0.4;
    state.altitude = pos.y;
    euler.setFromQuaternion(q, 'YXZ');
    state.heading = euler.y;
    state.pitch = euler.x;
    state.roll = euler.z;

    let contacts = 0;
    let wet = 0;
    let surf = SURFACE.DIRT;
    let best = -1;
    for (const w of wheels) {
      if (!w.contact) continue;
      contacts++;
      wet += w.wetness;
      if (w.load > best) { best = w.load; surf = w.surface; }
    }
    state.airborne = 4 - contacts;
    state.wetness = contacts ? wet / contacts : 0;
    state.surface = surf;

    // Impact: a step that ate a lot of speed was a collision, not braking.
    const dv = Math.abs(state.speed - lastSpeed);
    state.impact = dv > 1.6 ? Math.min(1.5, (dv - 1.6) * 0.5) : 0;
    lastSpeed = state.speed;
    state.odometer += Math.abs(state.forwardSpeed) * dt;

    // Stuck: throttle down, wheels turning, truck not moving.
    const spinning = Math.abs(wheels[2].omega + wheels[3].omega) * 0.5 * tireR;
    const trying = input.throttle > 0.35 && spinning > 1.2;
    if (trying && state.speed < tune.stuckSpeed) {
      state.stuck = clamp01(state.stuck + dt * tune.stuckRate);
    } else {
      state.stuck = clamp01(state.stuck - dt * tune.stuckRate * 0.8);
    }

    // Mud caking. Gated on SOFT AND WET ground, not on wheelspin alone -
    // spinning a tire on dry grass throws grass, and a truck that arrives at
    // the first mud hole already filthy has nothing left to show for it.
    const soft = clamp01(bogged * 3);
    const filth = soft * clamp01(0.22 + state.wetness * 1.4) * (0.4 + 0.6 * state.slipMax);
    state.mud = clamp01(state.mud
      + (filth * tune.mudCakeRate - tune.mudCleanRate * (1 - filth)) * dt);

    if (state.flipTimer > 0) state.flipTimer = Math.max(0, state.flipTimer - dt);
  }

  /* ------------------------------------------------------------- visual -- */

  function syncVisual() {
    vehicle.root.position.copy(pos);
    vehicle.root.quaternion.copy(q);
    vehicle.setSteering((wheels[0].steer + wheels[1].steer) * 0.5);
    for (const w of wheels) {
      vehicle.setWheelSpin(w.index, w.spin);
      vehicle.setTravel(w.index, w.travel);
    }
    vehicle.setMud(state.mud);
    vehicle.update();
  }

  /* -------------------------------------------------------------- verbs -- */

  /** Right the truck in place. Deliberately explicit - never automatic. */
  function flip() {
    if (state.flipTimer > 0) return false;
    state.flipTimer = tune.flipCooldown;
    const yaw = Math.atan2(fwd.x, fwd.z);
    const qq = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), yaw);
    body.setRotation({ x: qq.x, y: qq.y, z: qq.z, w: qq.w }, true);
    const h = terrain ? terrain.height(pos.x, pos.z) : 0;
    body.setTranslation({ x: pos.x, y: h + tune.flipLift + travel, z: pos.z }, true);
    body.setLinvel({ x: 0, y: 0, z: 0 }, true);
    body.setAngvel({ x: 0, y: 0, z: 0 }, true);
    for (const w of wheels) w.omega = 0;
    state.stuck = 0;
    return true;
  }

  /** Hook the winch to the ground straight ahead. */
  function winchAttach() {
    if (state.winchAnchor) { state.winchAnchor = null; return false; }
    for (let d = 6; d <= tune.winchRange; d += 2) {
      const x = pos.x + fwd.x * d;
      const z = pos.z + fwd.z * d;
      const y = terrain ? terrain.height(x, z) : 0;
      if (y > pos.y - 1.5) {
        state.winchAnchor = new THREE.Vector3(x, y + 0.4, z);
        state.winchLength = Math.hypot(x - pos.x, y + 0.4 - pos.y, z - pos.z);
        return true;
      }
    }
    // Nothing uphill: anchor at full range anyway, like a tree at the end of the line.
    const x = pos.x + fwd.x * tune.winchRange * 0.9;
    const z = pos.z + fwd.z * tune.winchRange * 0.9;
    const y = terrain ? terrain.height(x, z) : 0;
    state.winchAnchor = new THREE.Vector3(x, y + 0.6, z);
    state.winchLength = Math.hypot(x - pos.x, y + 0.6 - pos.y, z - pos.z);
    return true;
  }

  /** Drop the truck onto its own static ride height. Spawning any higher is a
   *  fall, and a fall means the first two seconds of the game are a bounce. */
  function settle() {
    readBody();
    const y = (terrain ? terrain.height(pos.x, pos.z) : 0) + rideHeight;
    body.setTranslation({ x: pos.x, y, z: pos.z }, true);
    body.setLinvel({ x: 0, y: 0, z: 0 }, true);
    body.setAngvel({ x: 0, y: 0, z: 0 }, true);
    for (const w of wheels) {
      w.travel = restTravel;
      w.u = sagU;
      w.omega = 0;
      w.sink = 0;
      w.spin = 0;
    }
    readBody();
    syncVisual();
  }

  function teleport(x, z, yaw = 0) {
    const qq = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 1, 0), yaw);
    body.setTranslation({ x, y: (terrain ? terrain.height(x, z) : 0) + rideHeight, z }, true);
    body.setRotation({ x: qq.x, y: qq.y, z: qq.z, w: qq.w }, true);
    drivetrain.reset();
    state.stuck = 0;
    state.winchAnchor = null;
    settle();
  }

  settle();

  return {
    body,
    collider,
    wheels,
    drivetrain,
    state,
    input,
    position: pos,
    quaternion: q,
    forward: fwd,
    get steerAngle() { return steerAngle; },

    /** Turn on Rapier raycasts once the scatter layer has colliders in the world. */
    setSolidQueries: (on) => probe.setSolidQueries(on),

    setInput(src) {
      input.throttle = clamp01(src.throttle ?? 0);
      input.brake = clamp01(src.brake ?? 0);
      input.handbrake = clamp01(src.handbrake ?? 0);
      input.steer = clamp(src.steer ?? 0, -1, 1);
      input.winch = clamp01(src.winch ?? 0);
    },

    step,
    syncVisual,
    settle,
    flip,
    winchAttach,
    teleport,

    /** Everything the HUD and the camera need, in one object, no allocation. */
    telemetry() {
      return {
        speed: state.speed,
        forwardSpeed: state.forwardSpeed,
        kph: Math.abs(state.forwardSpeed) * 3.6,
        reverse: state.reverse,
        rpm: drivetrain.state.rpm,
        gear: drivetrain.state.gear,
        gearName: drivetrain.gearName(),
        lowRange: drivetrain.state.lowRange,
        lock: drivetrain.lockName(),
        pitch: state.pitch,
        roll: state.roll,
        heading: state.heading,
        altitude: state.altitude,
        surface: state.surface,
        wetness: state.wetness,
        airborne: state.airborne,
        stuck: state.stuck,
        mud: state.mud,
        slip: state.slipMax,
        odometer: state.odometer,
        impact: state.impact,
        winch: !!state.winchAnchor,
        winchTension: state.winchTension,
        winchAnchor: state.winchAnchor,
        wheels,
        redline: tune.redlineRpm,
        nominalLoad: staticLoad,
      };
    },

    dispose() {
      world.removeCollider(collider, false);
      world.removeRigidBody(body);
    },
  };
}

export { REVERSE, NEUTRAL, FIXED_DT };
