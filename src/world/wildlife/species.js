/**
 * species.js - the animal table. Every number that makes a deer a deer.
 *
 * WHY one table: the rig builder, the gait engine, the behaviour state machine
 * and the spawner all need to agree about the same animal. Splitting "how it
 * looks" from "how it moves" from "where it lives" guarantees they drift, so
 * they live together here and the modules read from it.
 *
 * Measurements are real. A red deer stag stands ~1.25 m at the shoulder and is
 * ~2.1 m nose to rump; a roe-sized fawn is a bit over half that. Getting the
 * absolute scale right matters more than any shape detail - an animal that is
 * 20% too big next to a 1.9 m tall truck reads as a fake immediately.
 *
 * SPINE stations run rump -> nose. `p` is [x, y, z] of the section centre,
 * `r` is [halfWidth, halfHeight]. LEG joints run [shoulder/hip, elbow/stifle,
 * carpus/hock, fetlock, hoof tip] as [y, z] pairs at a fixed x.
 */
import { BIOME } from '../contract.js';

/* --------------------------------------------------------------- deer ---- */

const DEER_STAG = {
  key: 'stag',
  label: 'red deer stag',
  kind: 'quadruped',
  coat: 0x7b5836,
  hard: 0x6d6049,
  spine: [
    { n: 'hips',  p: [0, 1.10, -0.60], r: [0.185, 0.215] },
    { n: 'spine', p: [0, 1.11, -0.24], r: [0.215, 0.250] },
    { n: 'chest', p: [0, 1.09,  0.16], r: [0.230, 0.290], shear: -0.05 },
    { n: 'neck0', p: [0, 1.14,  0.44], r: [0.170, 0.215] },
    { n: 'neck1', p: [0, 1.34,  0.60], r: [0.120, 0.145] },
    { n: 'neck2', p: [0, 1.55,  0.72], r: [0.098, 0.108] },
    { n: 'head',  p: [0, 1.67,  0.83], r: [0.083, 0.098] },
    { n: 'nose',  p: [0, 1.61,  1.06], r: [0.046, 0.052] },
  ],
  // The last spine entry is the muzzle tip and carries no bone of its own.
  neckFrom: 3,
  legs: {
    fore: {
      x: 0.185, bend: 1,
      joints: [[0.96, 0.24], [0.62, 0.18], [0.34, 0.235], [0.115, 0.255], [0, 0.29]],
      r: [0.082, 0.056, 0.036, 0.029, 0.034],
    },
    hind: {
      x: 0.170, bend: -1,
      joints: [[1.02, -0.52], [0.69, -0.38], [0.39, -0.545], [0.135, -0.50], [0, -0.465]],
      r: [0.105, 0.065, 0.040, 0.029, 0.034],
    },
  },
  tail: { joints: [[1.14, -0.66], [1.02, -0.74], [0.90, -0.78]], r: [0.045, 0.030, 0.014] },
  ear: { pos: [0.075, 1.70, 0.79], len: 0.185, w: 0.052, yaw: 1.05, pitch: 0.30 },
  eye: { pos: [0.077, 1.70, 0.94], r: 0.021 },
  antlers: true,
  markings: 'deer',
  shoulderH: 1.25,
  bodyLen: 2.1,
  radius: 0.55,
  behaviour: {
    speeds: { walk: 1.25, trot: 4.2, run: 11.5 },
    turnRate: 2.4, senseRadius: 46, alarmGain: 1.5, alarmCalm: 0.24,
    fleeAlarm: 0.55, alertAlarm: 0.2, holdGround: 0.0, grazeBias: 0.65,
    herd: { spacing: 4.2, separation: 2.2, alignment: 0.5, cohesion: 0.42 },
    stamina: 12,
  },
};

const DEER_DOE = {
  ...DEER_STAG,
  key: 'doe',
  label: 'red deer hind',
  coat: 0x86643f,
  spine: [
    { n: 'hips',  p: [0, 1.01, -0.54], r: [0.165, 0.195] },
    { n: 'spine', p: [0, 1.02, -0.22], r: [0.190, 0.222] },
    { n: 'chest', p: [0, 1.00,  0.14], r: [0.200, 0.252], shear: -0.04 },
    { n: 'neck0', p: [0, 1.05,  0.39], r: [0.145, 0.180] },
    { n: 'neck1', p: [0, 1.23,  0.54], r: [0.100, 0.118] },
    { n: 'neck2', p: [0, 1.42,  0.65], r: [0.082, 0.090] },
    { n: 'head',  p: [0, 1.53,  0.75], r: [0.070, 0.084] },
    { n: 'nose',  p: [0, 1.48,  0.95], r: [0.039, 0.044] },
  ],
  legs: {
    fore: {
      x: 0.162, bend: 1,
      joints: [[0.88, 0.21], [0.57, 0.16], [0.31, 0.205], [0.105, 0.225], [0, 0.255]],
      r: [0.070, 0.048, 0.030, 0.024, 0.029],
    },
    hind: {
      x: 0.150, bend: -1,
      joints: [[0.94, -0.47], [0.63, -0.34], [0.355, -0.495], [0.123, -0.455], [0, -0.42]],
      r: [0.090, 0.055, 0.034, 0.024, 0.029],
    },
  },
  tail: { joints: [[1.04, -0.60], [0.94, -0.67], [0.84, -0.70]], r: [0.038, 0.026, 0.012] },
  ear: { pos: [0.065, 1.56, 0.71], len: 0.175, w: 0.050, yaw: 1.05, pitch: 0.30 },
  eye: { pos: [0.066, 1.56, 0.85], r: 0.019 },
  antlers: false,
  shoulderH: 1.13,
  bodyLen: 1.9,
  radius: 0.5,
  behaviour: {
    ...DEER_STAG.behaviour,
    speeds: { walk: 1.2, trot: 4.0, run: 11.0 },
    alarmGain: 1.9, fleeAlarm: 0.45,
  },
};

const DEER_FAWN = {
  ...DEER_DOE,
  key: 'fawn',
  label: 'red deer calf',
  coat: 0x9a7448,
  markings: 'fawn',
  spine: [
    { n: 'hips',  p: [0, 0.60, -0.30] , r: [0.098, 0.112] },
    { n: 'spine', p: [0, 0.61, -0.12], r: [0.110, 0.128] },
    { n: 'chest', p: [0, 0.60,  0.08], r: [0.115, 0.142] },
    { n: 'neck0', p: [0, 0.63,  0.21], r: [0.085, 0.100] },
    { n: 'neck1', p: [0, 0.73,  0.29], r: [0.058, 0.066] },
    { n: 'neck2', p: [0, 0.84,  0.35], r: [0.049, 0.054] },
    { n: 'head',  p: [0, 0.91,  0.41], r: [0.046, 0.054] },
    { n: 'nose',  p: [0, 0.88,  0.53], r: [0.026, 0.030] },
  ],
  legs: {
    fore: {
      x: 0.095, bend: 1,
      joints: [[0.52, 0.12], [0.34, 0.09], [0.185, 0.12], [0.062, 0.132], [0, 0.15]],
      r: [0.042, 0.028, 0.018, 0.014, 0.017],
    },
    hind: {
      x: 0.088, bend: -1,
      joints: [[0.56, -0.27], [0.375, -0.195], [0.21, -0.285], [0.073, -0.262], [0, -0.242]],
      r: [0.053, 0.032, 0.020, 0.014, 0.017],
    },
  },
  tail: { joints: [[0.62, -0.34], [0.56, -0.38], [0.50, -0.40]], r: [0.022, 0.015, 0.007] },
  ear: { pos: [0.040, 0.93, 0.39], len: 0.105, w: 0.032, yaw: 1.05, pitch: 0.30 },
  eye: { pos: [0.042, 0.93, 0.47], r: 0.013 },
  shoulderH: 0.67,
  bodyLen: 1.05,
  radius: 0.3,
  behaviour: {
    ...DEER_DOE.behaviour,
    speeds: { walk: 1.1, trot: 3.6, run: 9.0 },
    alarmGain: 2.4, fleeAlarm: 0.32,
    herd: { spacing: 2.0, separation: 2.0, alignment: 0.7, cohesion: 1.5 },
  },
};

/* --------------------------------------------------------------- wolf ---- */

/** Lower, longer and level-backed. The stance is the whole read at distance. */
const WOLF = {
  key: 'wolf',
  label: 'grey wolf',
  kind: 'quadruped',
  coat: 0x6b6a66,
  hard: 0x3a352f,
  spine: [
    { n: 'hips',  p: [0, 0.71, -0.46], r: [0.130, 0.150] },
    { n: 'spine', p: [0, 0.73, -0.18], r: [0.145, 0.165] },
    { n: 'chest', p: [0, 0.73,  0.14], r: [0.155, 0.200] },
    { n: 'neck0', p: [0, 0.75,  0.36], r: [0.145, 0.165] },
    { n: 'neck1', p: [0, 0.79,  0.52], r: [0.110, 0.125] },
    { n: 'neck2', p: [0, 0.82,  0.66], r: [0.088, 0.098] },
    { n: 'head',  p: [0, 0.83,  0.77], r: [0.078, 0.088] },
    { n: 'nose',  p: [0, 0.79,  0.98], r: [0.038, 0.040] },
  ],
  neckFrom: 3,
  legs: {
    fore: {
      x: 0.140, bend: 1,
      joints: [[0.62, 0.19], [0.40, 0.14], [0.21, 0.175], [0.075, 0.195], [0, 0.23]],
      r: [0.068, 0.046, 0.032, 0.028, 0.033],
    },
    hind: {
      x: 0.130, bend: -1,
      joints: [[0.66, -0.40], [0.44, -0.29], [0.245, -0.42], [0.085, -0.385], [0, -0.35]],
      r: [0.080, 0.052, 0.034, 0.028, 0.033],
    },
  },
  tail: { joints: [[0.72, -0.54], [0.60, -0.70], [0.46, -0.82]], r: [0.055, 0.052, 0.028] },
  ear: { pos: [0.058, 0.88, 0.74], len: 0.095, w: 0.046, yaw: 0.35, pitch: 0.05 },
  eye: { pos: [0.060, 0.86, 0.86], r: 0.016 },
  antlers: false,
  markings: 'wolf',
  shoulderH: 0.78,
  bodyLen: 1.6,
  radius: 0.45,
  behaviour: {
    speeds: { walk: 1.5, trot: 4.6, run: 13.0 },
    turnRate: 3.0, senseRadius: 70, alarmGain: 0.6, alarmCalm: 0.45,
    fleeAlarm: 0.8, alertAlarm: 0.35, holdGround: 0.3, grazeBias: 0.0,
    herd: { spacing: 6.0, separation: 2.6, alignment: 0.9, cohesion: 0.7 },
    predator: true, huntRange: 90, strikeRange: 2.5, stamina: 20,
  },
};

/* --------------------------------------------------------------- boar ---- */

/** Front-heavy wedge: high shoulders, low rump, head carried below the back. */
const BOAR = {
  key: 'boar',
  label: 'wild boar',
  kind: 'quadruped',
  coat: 0x3d3229,
  hard: 0xcfc3a6,
  spine: [
    { n: 'hips',  p: [0, 0.60, -0.42], r: [0.140, 0.150], flat: 0.15 },
    { n: 'spine', p: [0, 0.64, -0.16], r: [0.175, 0.195], flat: 0.2 },
    { n: 'chest', p: [0, 0.68,  0.12], r: [0.200, 0.245], flat: 0.15 },
    { n: 'neck0', p: [0, 0.66,  0.34], r: [0.185, 0.215] },
    { n: 'neck1', p: [0, 0.62,  0.48], r: [0.150, 0.170] },
    { n: 'neck2', p: [0, 0.58,  0.60], r: [0.115, 0.125] },
    { n: 'head',  p: [0, 0.55,  0.72], r: [0.095, 0.100] },
    { n: 'nose',  p: [0, 0.49,  0.94], r: [0.052, 0.050] },
  ],
  neckFrom: 3,
  legs: {
    fore: {
      x: 0.135, bend: 1,
      joints: [[0.50, 0.17], [0.33, 0.13], [0.175, 0.16], [0.065, 0.175], [0, 0.20]],
      r: [0.078, 0.052, 0.034, 0.028, 0.034],
    },
    hind: {
      x: 0.125, bend: -1,
      joints: [[0.50, -0.38], [0.335, -0.28], [0.19, -0.40], [0.07, -0.365], [0, -0.335]],
      r: [0.085, 0.054, 0.034, 0.028, 0.034],
    },
  },
  tail: { joints: [[0.60, -0.48], [0.50, -0.55], [0.40, -0.58]], r: [0.022, 0.016, 0.010] },
  ear: { pos: [0.070, 0.64, 0.69], len: 0.085, w: 0.050, yaw: 0.45, pitch: 0.15 },
  eye: { pos: [0.072, 0.60, 0.79], r: 0.014 },
  antlers: false,
  tusks: true,
  mane: true,
  markings: 'boar',
  shoulderH: 0.72,
  bodyLen: 1.5,
  radius: 0.42,
  behaviour: {
    speeds: { walk: 1.0, trot: 3.2, run: 10.0 },
    turnRate: 2.6, senseRadius: 30, alarmGain: 0.9, alarmCalm: 0.35,
    // Boar are short-sighted and bad-tempered: they need a lot of alarm before
    // they run, and below that they square up instead.
    fleeAlarm: 0.78, alertAlarm: 0.3, holdGround: 0.75, grazeBias: 0.9,
    herd: { spacing: 2.6, separation: 2.4, alignment: 0.4, cohesion: 0.6 },
    roots: true, stamina: 8,
  },
};

/* --------------------------------------------------------------- hare ---- */

const HARE = {
  key: 'hare',
  label: 'mountain hare',
  kind: 'quadruped',
  coat: 0x8a7658,
  hard: 0x3a332a,
  spine: [
    { n: 'hips',  p: [0, 0.175, -0.13], r: [0.060, 0.072] },
    { n: 'spine', p: [0, 0.180, -0.05], r: [0.062, 0.070] },
    { n: 'chest', p: [0, 0.165,  0.04], r: [0.055, 0.062] },
    { n: 'neck0', p: [0, 0.170,  0.10], r: [0.046, 0.050] },
    { n: 'neck1', p: [0, 0.185,  0.14], r: [0.038, 0.042] },
    { n: 'neck2', p: [0, 0.200,  0.175], r: [0.034, 0.038] },
    { n: 'head',  p: [0, 0.205,  0.21], r: [0.032, 0.036] },
    { n: 'nose',  p: [0, 0.190,  0.285], r: [0.016, 0.018] },
  ],
  neckFrom: 3,
  legs: {
    fore: {
      x: 0.048, bend: 1,
      joints: [[0.135, 0.055], [0.090, 0.040], [0.048, 0.052], [0.018, 0.058], [0, 0.072]],
      r: [0.022, 0.015, 0.010, 0.009, 0.011],
    },
    hind: {
      x: 0.052, bend: -1,
      joints: [[0.160, -0.115], [0.105, -0.055], [0.055, -0.145], [0.020, -0.115], [0, -0.075]],
      r: [0.036, 0.022, 0.013, 0.010, 0.014],
    },
  },
  tail: { joints: [[0.175, -0.155], [0.165, -0.185], [0.160, -0.20]], r: [0.026, 0.024, 0.014] },
  ear: { pos: [0.022, 0.225, 0.195], len: 0.115, w: 0.026, yaw: 0.16, pitch: -0.12 },
  eye: { pos: [0.030, 0.215, 0.245], r: 0.009 },
  antlers: false,
  markings: 'hare',
  shoulderH: 0.20,
  bodyLen: 0.5,
  radius: 0.16,
  behaviour: {
    speeds: { walk: 0.8, trot: 3.0, run: 13.0 },
    turnRate: 6.0, senseRadius: 26, alarmGain: 4.0, alarmCalm: 0.5,
    fleeAlarm: 0.22, alertAlarm: 0.08, holdGround: 0.0, grazeBias: 0.8,
    herd: { spacing: 8.0, separation: 1.2, alignment: 0.05, cohesion: 0.05 },
    burst: true, stamina: 4,
  },
};

/* --------------------------------------------------------------- crow ---- */

const CROW = {
  key: 'crow',
  label: 'carrion crow',
  kind: 'bird',
  coat: 0x191a20,
  hard: 0x25262c,
  bodyLen: 0.48,
  wingSpan: 0.95,
  radius: 0.25,
  behaviour: {
    speeds: { walk: 0.5, trot: 1.2, run: 13.0 },
    turnRate: 2.6, senseRadius: 34, alarmGain: 5.0, alarmCalm: 0.22,
    fleeAlarm: 0.18, alertAlarm: 0.06,
    flock: { spacing: 2.6, separation: 3.0, alignment: 1.4, cohesion: 0.9 },
    cruiseAlt: 26, stamina: 25,
  },
};

export const SPECIES = {
  stag: DEER_STAG, doe: DEER_DOE, fawn: DEER_FAWN,
  wolf: WOLF, boar: BOAR, hare: HARE, crow: CROW,
};

/**
 * Spawn groups per chunk, by biome. These are expectation values - the spawner
 * rolls against them per chunk, so a number below 1 means "sometimes".
 *
 * Deer want the meadow/pine edge, wolves follow the deer, boar root in the wet
 * loam of the pine belt, hares take the open meadow, crows go where trees are.
 * Nothing lives above the treeline in any numbers, which is itself a way of
 * making altitude feel real.
 */
export const DENSITY = {
  [BIOME.RIVERBED.id]: { herd: 0.25, boar: 0.30, hare: 0.45, crows: 0.55, wolves: 0.05 },
  [BIOME.MUDFLAT.id]:  { herd: 0.20, boar: 0.55, hare: 0.30, crows: 0.45, wolves: 0.05 },
  [BIOME.MEADOW.id]:   { herd: 0.85, boar: 0.20, hare: 0.85, crows: 0.50, wolves: 0.14 },
  [BIOME.PINE.id]:     { herd: 0.55, boar: 0.60, hare: 0.40, crows: 0.70, wolves: 0.22 },
  [BIOME.SCREE.id]:    { herd: 0.12, boar: 0.05, hare: 0.35, crows: 0.30, wolves: 0.08 },
  [BIOME.ALPINE.id]:   { herd: 0.04, boar: 0.00, hare: 0.15, crows: 0.18, wolves: 0.03 },
};

/** A herd is a stag, a few hinds and sometimes a calf - not N identical deer. */
export const HERD_COMPOSITION = [
  { key: 'stag', min: 1, max: 1 },
  { key: 'doe',  min: 2, max: 4 },
  { key: 'fawn', min: 0, max: 2 },
];
