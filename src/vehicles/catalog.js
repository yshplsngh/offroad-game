/**
 * catalog.js - vehicle specifications.
 *
 * A spec is pure data: every builder reads from it, nothing is hardcoded in
 * geometry. Changing `wheelbase` or `tire.diameter` re-proportions the whole
 * vehicle - arches, doors, greenhouse and suspension all follow.
 *
 * Dimensions are metres. Tire sizes are quoted in inches in the comments
 * because that is how anyone actually talks about them.
 */

const IN = 0.0254;

/** Fill in the defaults so a spec only has to state what makes it different. */
function defineVehicle(spec) {
  const tireR = spec.tire.diameter / 2;
  const railY = spec.frame.railY ?? tireR + 0.1;
  return {
    rhd: false,
    ...spec,
    frame: {
      frameWidth: 0.98, railHeight: 0.16, ...spec.frame, railY,
    },
    axle: { tubeR: 0.055, diffOffset: 0.18, ...spec.axle },
    tire: { spokes: 6, rimColor: 0x2b2e33, beadlock: true, ...spec.tire },
    suspension: { travel: 0.26, shockColor: 0xc23a2a, ...spec.suspension },
    body: {
      style: 'wagon', doors: 2, sideHeight: 0.60, glassHeight: 0.46,
      hoodDrop: 0.10, cowlSetback: 0.30, archClearance: 0.075,
      windshieldRake: 0.34, headlampR: 0.1, glassTint: 0.12, steeringR: 0.185,
      matte: false, ...spec.body,
    },
    features: {
      cage: true, winch: true, snorkel: false, lightBar: true, roofRack: true,
      spare: true, flares: true, sliders: true, rearBumper: true, kit: true,
      ...spec.features,
    },
    // Physics numbers - consumed by the vehicle controller, not the modeller.
    perf: {
      mass: 2100, power: 230, torque: 420, topSpeed: 38,
      lowRangeRatio: 2.72, gearRatios: [3.6, 2.1, 1.4, 1.0, 0.78],
      finalDrive: 4.56, brakeTorque: 3600, ...spec.perf,
    },
  };
}

export const VEHICLES = [
  defineVehicle({
    id: 'ridgeback90',
    name: 'Ridgeback 90',
    tagline: 'Short-wheelbase trail weapon. Breaks over anything.',
    frame: { wheelbase: 2.36, frontOverhang: 0.72, rearOverhang: 0.78, frameWidth: 0.98 },
    axle: { track: 1.58 },
    tire: { diameter: 35 * IN, width: 12.5 * IN, rimInch: 17, spokes: 6, rimColor: 0x31353b },
    suspension: { travel: 0.28, shockColor: 0xc23a2a },
    body: { style: 'wagon', doors: 2, width: 1.82, color: 0x3f5f4a },
    features: { snorkel: true },
    perf: { mass: 1980, power: 215 },
  }),

  defineVehicle({
    id: 'ridgeback110',
    name: 'Ridgeback 110',
    tagline: 'Long-wheelbase overlander. Carries the camp and the crew.',
    frame: { wheelbase: 2.79, frontOverhang: 0.74, rearOverhang: 1.05, frameWidth: 1.0 },
    axle: { track: 1.62 },
    tire: { diameter: 35 * IN, width: 12.5 * IN, rimInch: 17, spokes: 8, rimColor: 0x8c8f94 },
    suspension: { travel: 0.26, shockColor: 0xe0a02a },
    body: { style: 'wagon', doors: 4, width: 1.86, sideHeight: 0.62, color: 0xb8a37c },
    features: { snorkel: true },
    perf: { mass: 2450, power: 240, torque: 470 },
  }),

  defineVehicle({
    id: 'sierraHD',
    name: 'Sierra HD',
    tagline: 'Crew-cab one-ton on 37s. Torque first, finesse later.',
    frame: { wheelbase: 3.35, frontOverhang: 0.86, rearOverhang: 1.18, frameWidth: 1.06, railHeight: 0.19 },
    axle: { track: 1.74, tubeR: 0.062, diffOffset: 0.2 },
    tire: { diameter: 37 * IN, width: 13.5 * IN, rimInch: 17, spokes: 8, rimColor: 0x24272c },
    suspension: { travel: 0.24, shockColor: 0x2f6fb0 },
    body: {
      style: 'pickup', doors: 4, width: 1.98, sideHeight: 0.66, glassHeight: 0.44,
      hoodDrop: 0.06, cowlSetback: 0.44, windshieldRake: 0.42, color: 0x8d1f1f,
    },
    features: { snorkel: true, roofRack: false },
    perf: { mass: 3200, power: 330, torque: 900, topSpeed: 40, finalDrive: 4.1 },
  }),

  defineVehicle({
    id: 'timberwolf',
    name: 'Timberwolf',
    tagline: 'Light, cheap, unkillable. The one you actually wheel.',
    frame: { wheelbase: 2.57, frontOverhang: 0.66, rearOverhang: 0.82, frameWidth: 0.92, railHeight: 0.14 },
    axle: { track: 1.5, tubeR: 0.05, diffOffset: 0.16 },
    tire: { diameter: 33 * IN, width: 11.5 * IN, rimInch: 15, spokes: 5, rimColor: 0xd8d4c8, beadlock: false },
    suspension: { travel: 0.24, shockColor: 0x3f8f4f },
    body: {
      style: 'wagon', doors: 4, width: 1.72, sideHeight: 0.56, glassHeight: 0.5,
      cowlSetback: 0.26, windshieldRake: 0.3, color: 0x2f6f8f, matte: true,
    },
    features: { snorkel: false, roofRack: true, lightBar: true },
    perf: { mass: 1620, power: 190, torque: 320, topSpeed: 36 },
  }),
];

export const VEHICLES_BY_ID = Object.fromEntries(VEHICLES.map((v) => [v.id, v]));

/** Colours offered in the showroom's paint picker. */
export const PAINT_SWATCHES = [
  { name: 'Moss',        color: 0x3f5f4a },
  { name: 'Desert Tan',  color: 0xb8a37c },
  { name: 'Oxide Red',   color: 0x8d1f1f },
  { name: 'Glacier',     color: 0x2f6f8f },
  { name: 'Bone',        color: 0xd9d5c9 },
  { name: 'Slate',       color: 0x45494f },
  { name: 'Ember',       color: 0xd2761f },
  { name: 'Midnight',    color: 0x181b20 },
];
