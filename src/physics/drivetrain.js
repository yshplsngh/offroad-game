/**
 * drivetrain.js - engine, clutch, gearbox, transfer case, differentials.
 *
 * WHY MODEL THE DIFFS AT ALL: because the diff lock button is the single most
 * important control on a crawler, and a button is only interesting if the thing
 * it switches off genuinely hurts. An OPEN differential splits torque EQUALLY
 * between its two outputs and lets their speeds differ. That is the whole
 * trick: if one tire is on wet clay and can only hold 200 N m before it spins,
 * the tire on dry rock also receives 200 N m, no matter how much grip it has.
 * You do not get the sum of your traction, you get twice your worst. Lock it
 * and the constraint flips - speeds are forced equal, torque splits however
 * grip demands, and the good tire can take everything the engine has.
 *
 * That behaviour is not scripted here. It emerges from two lines: equal torque
 * to both outputs (open) versus equal speed on both outputs (locked).
 *
 * WHY AN IMPULSE CLUTCH: an explicit spring-damper clutch with a stiffness high
 * enough to feel locked blows up at 60 Hz. Instead we solve directly for the
 * torque that would synchronise engine and driveline within this one step, then
 * clamp it to the clutch's friction capacity. Below capacity it slips like a
 * real clutch, at capacity it is rigid, and it can never overshoot.
 *
 * Low range matters more than gears 2-5 ever will: 2.72:1 on top of a 3.6:1
 * first and a 4.56:1 final is 44.7:1 overall, which is the difference between
 * "blip it over the ledge" and "idle over the ledge".
 */

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const clamp01 = (v) => clamp(v, 0, 1);
const smoothstep = (a, b, x) => { const t = clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); };

const RPM_TO_RAD = Math.PI / 30;
const RAD_TO_RPM = 30 / Math.PI;

/** Gear index convention: -1 reverse, 0 neutral, 1..N the forward gears. */
export const REVERSE = -1;
export const NEUTRAL = 0;

/** Diff lock states, in the order the X key cycles them. */
export const LOCK_OPEN = 0;
export const LOCK_CENTRE = 1;
export const LOCK_FULL = 2;
export const LOCK_NAMES = ['OPEN', 'CENTRE', 'FULL'];

/**
 * Normalised engine torque, 0..1 of peak, as a function of rpm.
 *
 * Shaped like a big-bore diesel-ish petrol truck motor: fat from just off idle,
 * peak a little under half of redline, and a long soft fall after it so revving
 * it out is a waste of time. That shape is what makes low range feel correct -
 * you use gearing for torque multiplication, not the tachometer.
 */
function torqueFraction(rpm, tune) {
  if (rpm <= 0) return 0;
  const x = rpm / tune.peakTorqueRpm;
  let f = x < 1
    ? 1 - 0.55 * Math.pow(1 - x, 1.6)
    : 1 - 0.38 * Math.pow(x - 1, 1.7);
  // Fuel cut at the limiter, faded over 220 rpm so it burbles instead of spiking.
  f *= 1 - smoothstep(tune.redlineRpm, tune.redlineRpm + 220, rpm);
  return clamp(f, 0, 1);
}

/**
 * @param {object} spec    A catalog spec (uses spec.perf).
 * @param {object} tune    TUNE.
 * @param {number} wheelInertia  Per-wheel rotational inertia, kg m^2.
 */
export function createDrivetrain(spec, tune, wheelInertia) {
  const perf = spec.perf;
  const forwardGears = perf.gearRatios.length;
  const peakTorque = perf.torque;
  const peakPower = perf.power * 1000;          // catalog quotes kW
  const idleW = tune.idleRpm * RPM_TO_RAD;
  const stallW = tune.stallRpm * RPM_TO_RAD;
  const limitW = (tune.redlineRpm + 400) * RPM_TO_RAD;
  const clutchCap = peakTorque * tune.clutchCapacity;

  const state = {
    omegaE: idleW,
    rpm: tune.idleRpm,
    gear: 1,
    lowRange: true,             // a crawler starts in low. It is the point.
    diffLock: LOCK_OPEN,
    shiftTimer: 0,
    engineTorque: 0,
    clutchTorque: 0,
    propTorque: 0,
    running: true,
  };

  /** Signed overall ratio from engine to wheel. 0 means no drive path. */
  function ratio() {
    if (state.gear === NEUTRAL || !state.running) return 0;
    const g = state.gear === REVERSE
      ? -perf.gearRatios[0] * tune.reverseRatio
      : perf.gearRatios[state.gear - 1];
    return g * perf.finalDrive * (state.lowRange ? perf.lowRangeRatio : 1);
  }

  function shift(to) {
    const g = clamp(to, REVERSE, forwardGears);
    if (g === state.gear) return false;
    state.gear = g;
    state.shiftTimer = tune.shiftTime;
    return true;
  }

  const driveTorque = [0, 0, 0, 0];

  return {
    state,
    ratio,
    shift,
    shiftUp: () => shift(state.gear + 1),
    shiftDown: () => shift(state.gear - 1),
    cycleLock: () => { state.diffLock = (state.diffLock + 1) % 3; return state.diffLock; },
    setLock: (v) => { state.diffLock = clamp(v | 0, 0, 2); },
    /** Range changes need the driveline near-stationary, same as the real lever. */
    toggleRange: (speed) => {
      if (Math.abs(speed) > 2.2) return false;
      state.lowRange = !state.lowRange;
      return true;
    },
    gearName: () => (state.gear === REVERSE ? 'R' : state.gear === NEUTRAL ? 'N' : String(state.gear)),

    /**
     * Advance the engine and clutch one step and split the result across the
     * four wheels.
     *
     * @param {number} dt
     * @param {number} throttle 0..1
     * @param {number[]} wheelOmega Current wheel angular velocities, rad/s.
     * @returns {number[]} Per-wheel drive torque, N m, in FL FR RL RR order.
     */
    update(dt, throttle, wheelOmega) {
      if (state.shiftTimer > 0) state.shiftTimer = Math.max(0, state.shiftTimer - dt);

      const r = ratio();
      // Full-time 4WD: the transmission output turns at the mean of what the
      // four wheels are doing, whatever the diffs are up to internally.
      const wOut = (wheelOmega[0] + wheelOmega[1] + wheelOmega[2] + wheelOmega[3]) * 0.25;
      const wIn = wOut * r;

      /* --- engine ------------------------------------------------------- */
      const rpm = state.omegaE * RAD_TO_RPM;
      let te = torqueFraction(rpm, tune) * peakTorque * throttle;
      // Power ceiling: torque x speed cannot exceed the rated output.
      if (state.omegaE > 1) te = Math.min(te, peakPower / state.omegaE);
      // Pumping and bearing losses. Off throttle this IS the engine braking,
      // and through 44:1 of low range it is most of how you get down a hill.
      te -= tune.engineFrictionA + tune.engineFrictionB * state.omegaE;
      // Idle governor. Without it the engine dies every time you stop.
      if (state.omegaE < idleW) te += (idleW - state.omegaE) * 3.2 * (1 - 0.6 * throttle);
      state.engineTorque = te;

      /* --- clutch ------------------------------------------------------- */
      let tc = 0;
      if (r !== 0 && state.shiftTimer <= 0) {
        // Capacity ramps in as the driveline comes up to idle speed, so pulling
        // away is a slip, not a bang. Anti-stall bleeds it off again as revs
        // fall towards stalling, which is what a driver's left foot does.
        const engage = clamp01((Math.abs(wIn) - idleW * 0.30) / (idleW * 0.85));
        const antiStall = smoothstep(stallW, idleW * 1.05, state.omegaE);
        const cap = clutchCap * Math.max(engage, tune.clutchCreep + 0.6 * throttle) * antiStall;

        // Reflect the driven wheels onto the engine side: omega_in = omega_out * r,
        // so their inertia divides by r^2. In low first that is almost nothing,
        // which is why the engine barely notices the wheels down there.
        const iDrive = Math.max(1e-4, (wheelInertia * 4) / (r * r));
        const tLock = (state.omegaE - wIn) / (dt * (1 / tune.engineInertia + 1 / iDrive));
        tc = clamp(tLock, -cap, cap);
      }
      state.clutchTorque = tc;

      state.omegaE += (te - tc) * dt / tune.engineInertia;
      state.omegaE = clamp(state.omegaE, stallW * 0.4, limitW);
      state.rpm = state.omegaE * RAD_TO_RPM;

      /* --- diffs: equal torque out of every open case ------------------- */
      const prop = tc * r * tune.drivelineEff;
      state.propTorque = prop;
      const quarter = prop * 0.25;
      driveTorque[0] = quarter; driveTorque[1] = quarter;
      driveTorque[2] = quarter; driveTorque[3] = quarter;
      return driveTorque;
    },

    /**
     * Second half of the differential: the SPEED side.
     *
     * Called after the wheels have integrated their own spin, because a locker
     * is a kinematic constraint between wheels, not a torque source. Equalising
     * the speeds of two equal-inertia wheels conserves angular momentum, so
     * this quietly performs the torque transfer an open diff cannot.
     */
    applyDiffs(wheelOmega, dt) {
      const k = clamp01(tune.lockerStrength);
      const centreLocked = state.lowRange || state.diffLock >= LOCK_CENTRE;
      const axlesLocked = state.diffLock >= LOCK_FULL;

      if (axlesLocked) {
        lockPair(wheelOmega, 0, 1, k);
        lockPair(wheelOmega, 2, 3, k);
      } else {
        // Even an open diff has internal friction. It is far too little to get
        // you out of anything, but it stops a lifted wheel chattering.
        preload(wheelOmega, 0, 1, tune.openDiffPreload, wheelInertia, dt);
        preload(wheelOmega, 2, 3, tune.openDiffPreload, wheelInertia, dt);
      }

      if (centreLocked) {
        const fa = (wheelOmega[0] + wheelOmega[1]) * 0.5;
        const ra = (wheelOmega[2] + wheelOmega[3]) * 0.5;
        const mean = (fa + ra) * 0.5;
        const df = (mean - fa) * k;
        const dr = (mean - ra) * k;
        wheelOmega[0] += df; wheelOmega[1] += df;
        wheelOmega[2] += dr; wheelOmega[3] += dr;
      } else {
        preload(wheelOmega, 0, 2, tune.openDiffPreload, wheelInertia, dt);
        preload(wheelOmega, 1, 3, tune.openDiffPreload, wheelInertia, dt);
      }
    },

    /** Per-wheel brake torque, N m, in FL FR RL RR order. */
    brakeTorques(out, brake, handbrake) {
      const base = perf.brakeTorque * tune.brakeScale;
      const f = base * tune.brakeBiasFront * 0.5 * brake;
      const r = base * (1 - tune.brakeBiasFront) * 0.5 * brake;
      const hb = base * (1 - tune.brakeBiasFront) * 0.5 * tune.handbrakeFraction * handbrake;
      out[0] = f; out[1] = f;
      out[2] = Math.max(r, hb); out[3] = Math.max(r, hb);
      return out;
    },

    lockName: () => LOCK_NAMES[state.diffLock],
    reset() {
      state.omegaE = idleW;
      state.rpm = tune.idleRpm;
      state.shiftTimer = 0;
      state.clutchTorque = 0;
      state.propTorque = 0;
    },
  };
}

/** Force two wheels to the same speed. This is what a locker physically does. */
function lockPair(w, a, b, k) {
  const mean = (w[a] + w[b]) * 0.5;
  w[a] += (mean - w[a]) * k;
  w[b] += (mean - w[b]) * k;
}

/** A trickle of coupling torque, capped at the diff's internal friction. */
function preload(w, a, b, maxTorque, inertia, dt) {
  const t = clamp((w[b] - w[a]) * 25, -maxTorque, maxTorque);
  const dw = t * dt / inertia;
  w[a] += dw; w[b] -= dw;
}
