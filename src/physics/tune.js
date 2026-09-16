/**
 * tune.js - every number a designer is expected to reach for.
 *
 * The rule from PLAN.md is "tune driving feel via the SURFACE table, not the
 * tire model". This file is the other half of that rule: SURFACE says what the
 * ground is like, TUNE says what the truck is like. Nothing in the physics
 * modules hardcodes a feel number - they all read it from here, so a designer
 * can rebalance the whole rig from one screen without reading a solver.
 *
 * The target feel is a SIM-LEANING ROCK CRAWLER: slow, deliberate, technical.
 * That means soft long-travel springs, a low centre of mass, torque that
 * arrives through huge gearing rather than revs, and tires that genuinely give
 * up on mud. Getting stuck is a feature, so nothing here quietly rescues the
 * player - the winch and the flip key do that, explicitly.
 *
 * SI units throughout: metres, kilograms, seconds, newtons, radians.
 */

export const TUNE = {
  /* ------------------------------------------------------------- mass ---- */
  // Centre of mass, relative to the vehicle origin (ground level, centred
  // between the axles). Low and slightly rearward: low so the truck leans
  // instead of tripping over itself, rearward so the front unloads on a climb
  // and the rear digs in, which is what makes a climb feel earned.
  comAboveSill: 0.05,
  comRearBias: 0.05,          // fraction of wheelbase, + is rearward

  // A real body carries its mass at the ends (engine, fuel, spare), so yaw and
  // pitch inertia run higher than a uniform box while roll runs lower.
  inertiaRoll: 0.92,
  inertiaPitch: 1.18,
  inertiaYaw: 1.22,

  bodyLinearDamping: 0.02,
  bodyAngularDamping: 0.10,   // small: too much and rollovers stop reading as physical
  aeroDrag: 0.62 * 3.4 * 0.5 * 1.225,   // 0.5*rho*Cd*A, a brick with a roof rack

  /* ------------------------------------------------------- suspension ---- */
  // Static sag as a fraction of total travel. 0.32 leaves two thirds of the
  // travel for droop, which is where articulation comes from.
  staticSag: 0.32,
  dampBump: 0.30,             // fraction of critical
  dampRebound: 0.52,          // always higher than bump or the truck pogos
  bumpStopStart: 0.86,        // fraction of travel where the stop engages
  bumpStopRate: 26,           // multiplier on spring rate inside the stop
  maxSpringForceG: 6.0,       // clamp, in static-load multiples
  antiRollFront: 0.10,        // fraction of spring rate. Keep tiny: sway bars
  antiRollRear: 0.06,         // are the enemy of articulation.

  /* ------------------------------------------------------------- tire ---- */
  peakSlipRatio: 0.16,        // slip ratio at peak longitudinal grip
  peakSlipAngle: 0.17,        // radians, ~10 degrees
  slipFalloff: 0.030,         // how fast grip dies past the peak. Wheelspin tax.
  loadSensitivity: 0.12,      // mu loss per unit of load above nominal
  wetnessGripLoss: 0.35,      // mu multiplier lost at wetness = 1
  gripScale: 1.0,             // global grip trim - the first knob to touch
  slipRefSpeed: 1.6,          // m/s, keeps slip finite at walking pace
  tireForceHeight: 0.25,      // 0 applies tire force at the contact patch, 1 at the hub
  rollingResistScale: 1.0,    // multiplier on SURFACE.drag
  wheelInertiaFactor: 0.62,   // I = factor * m_wheel * r^2, > 0.5 for a heavy mud tire
  wheelMassFraction: 0.028,   // wheel+brake+hub mass as a fraction of vehicle mass

  /* -------------------------------------------------------------- mud ---- */
  // Sink is what separates mud from a merely slippery surface: the tire drops
  // into it, the belly gets closer to the ground, and the sunk tire has to
  // bulldoze a wall of slop ahead of it.
  sinkScale: 1.0,             // multiplier on SURFACE.sink
  sinkRate: 3.2,              // 1/s, how fast a tire settles into soft ground
  bogDrag: 5200,              // N per metre of sink per m/s of travel
  mudCakeRate: 0.55,          // how fast the bodywork gets filthy, 1/s
  mudCleanRate: 0.05,

  /* -------------------------------------------------------- drivetrain ---- */
  idleRpm: 780,
  stallRpm: 380,
  redlineRpm: 5000,
  peakTorqueRpm: 2700,
  engineInertia: 0.42,        // kg m^2, flywheel side
  engineFrictionA: 12,        // Nm constant drag
  engineFrictionB: 0.035,     // Nm per rad/s - this is the engine braking you feel
  clutchCapacity: 1.45,       // multiples of peak engine torque
  clutchCreep: 0.10,          // capacity at a standstill off-throttle: lets it crawl
  drivelineEff: 0.86,
  reverseRatio: 1.05,         // multiple of first gear
  shiftTime: 0.28,            // s of torque interruption
  brakeBiasFront: 0.62,
  // spec.perf.brakeTorque is the catalog's total figure and on its own only
  // reaches about 0.4 g, which never locks a wheel and so removes threshold
  // braking from the game. This scales it until the pedal can out-torque the
  // tires, which is how real brakes are sized.
  brakeScale: 2.4,
  handbrakeFraction: 1.3,     // of the rear share - must be able to lock them
  lockerStrength: 1.0,        // 1 = a true locker, < 1 = a limited slip
  openDiffPreload: 3.0,       // Nm of internal friction, stops idle wheel chatter

  /* ---------------------------------------------------------- steering ---- */
  maxSteerAngle: 0.62,        // rad at the road wheel, ~35 degrees
  steerSpeedFalloff: 18,      // m/s at which steering is fully reduced
  steerMinFraction: 0.34,     // steering left at high speed
  steerRate: 2.0,             // rad/s at the road wheel
  steerReturnRate: 2.6,       // rad/s self-centring
  ackermann: 0.85,            // 0 parallel steer, 1 full Ackermann

  /* ---------------------------------------------------------- recovery ---- */
  winchForce: 42000,          // N, a 9500 lb winch pulling on a double line
  winchSpeed: 0.55,           // m/s of cable under load
  winchRange: 28,             // m of cable
  winchStiffness: 90000,      // N/m once the cable is taut
  flipCooldown: 4.0,          // s
  flipLift: 0.45,             // m above the ground after righting

  /* ---------------------------------------------------------- stuckness ---- */
  stuckSpeed: 0.7,            // below this, with throttle, counts as stuck
  stuckRate: 0.55,            // how fast the stuck meter fills, 1/s
};

/** Clone the knobs so a caller can tune one truck without touching the rest. */
export function tuneCopy(over = {}) {
  return { ...TUNE, ...over };
}
