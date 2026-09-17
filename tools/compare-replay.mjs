/**
 * compare-replay.mjs - native vs reference driving parity for one replay.
 *
 * WHY AN ENVELOPE, NOT FIXED TOLERANCES: the reference is chaotic. Wheel spin
 * amplifies round-off exponentially, so nudging the spawn by 1 um sends the
 * Rapier build itself tens of metres off course (and into a rollover) on some
 * replays, while others stay within centimetres. A fixed tolerance is either
 * meaningless on the first kind or toothless on the second.
 *
 * So the gate is measured: perturbed reference runs (frozen in
 * bench/replays/reference/ from the removed browser build) define, per sample and metric, how far the reference drifts from
 * ITSELF (running max over time). The native build may deviate from the
 * reference by at most  floor + factor x envelope.  On a calm replay that is
 * a tight bound; on a chaotic one it only says native is no more sensitive
 * than the reference already is. Floors and factor: bench/replays/tolerances.json.
 *
 * Usage:
 *   node tools/compare-replay.mjs <reference.json> <native.json> [perturbed.json ...] [--table]
 * Exit code 1 when any gate fails.
 */
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const files = process.argv.slice(2).filter((a) => !a.startsWith('--'));
const TABLE = process.argv.includes('--table');
if (files.length < 2) {
  console.error('usage: node tools/compare-replay.mjs <reference.json> <native.json> [perturbed.json ...] [--table]');
  process.exit(2);
}

const load = (p) => JSON.parse(readFileSync(p, 'utf8'));
const [ref, nat, ...perturbed] = files.map(load);
const tolerances = load(join(ROOT, 'bench/replays/tolerances.json'));
const tol = { ...tolerances.default, ...(tolerances.replays?.[ref.replay] ?? {}) };

for (const other of [nat, ...perturbed]) {
  if (other.replay !== ref.replay || other.vehicle !== ref.vehicle || other.seed !== ref.seed) {
    console.error(`not the same replay: ${ref.replay}/${ref.vehicle}/${ref.seed} vs ${other.replay}/${other.vehicle}/${other.seed}`);
    process.exit(2);
  }
}

const angle = (a, b) => Math.abs(Math.atan2(Math.sin(a - b), Math.cos(a - b))) * 180 / Math.PI;
const METRICS = {
  pos: (a, b) => Math.hypot(a.pos[0] - b.pos[0], a.pos[1] - b.pos[1], a.pos[2] - b.pos[2]),
  height: (a, b) => Math.abs(a.pos[1] - b.pos[1]),
  speedKph: (a, b) => Math.abs(a.forwardSpeed - b.forwardSpeed) * 3.6,
  headingDeg: (a, b) => angle(a.heading, b.heading),
  rollDeg: (a, b) => angle(a.roll, b.roll),
  pitchDeg: (a, b) => angle(a.pitch, b.pitch),
  rpm: (a, b) => Math.abs(a.rpm - b.rpm),
};

const index = (trace) => new Map(trace.frames.map((f) => [f.step, f]));
const natBy = index(nat);
const pertBy = perturbed.map(index);

const envelope = Object.fromEntries(Object.keys(METRICS).map((m) => [m, 0]));
const worst = Object.fromEntries(Object.keys(METRICS).map((m) => [m, { excess: -Infinity }]));
const largest = Object.fromEntries(Object.keys(METRICS).map((m) => [m, { dev: -Infinity }]));
const rows = [];
let gearMismatch = 0;

for (const r of ref.frames) {
  const n = natBy.get(r.step);
  if (!n) continue;
  const row = { t: r.t, ref: r, nat: n };
  for (const [m, dist] of Object.entries(METRICS)) {
    for (const p of pertBy) {
      const q = p.get(r.step);
      if (q) envelope[m] = Math.max(envelope[m], dist(r, q));
    }
    const dev = dist(r, n);
    const allowed = tol.floor[m] + tol.factor * envelope[m];
    row[m] = { dev, env: envelope[m], allowed };
    if (dev - allowed > worst[m].excess) worst[m] = { excess: dev - allowed, dev, env: envelope[m], allowed, t: r.t };
    if (dev > largest[m].dev) largest[m] = { dev, env: envelope[m], allowed, t: r.t };
  }
  if (r.gear !== n.gear) gearMismatch++;
  rows.push(row);
}

let failed = 0;
console.log(`${ref.replay} (${ref.vehicle}, seed ${ref.seed}): ${rows.length} samples, envelope from ${perturbed.length} perturbed reference run(s)`);
if (!perturbed.length) console.log('  (no perturbed runs: envelope is 0, gates are the floors alone)');
for (const m of Object.keys(METRICS)) {
  const w = worst[m];
  const l = largest[m];
  const ok = w.excess <= 0;
  if (!ok) failed++;
  const fmt = (x) => x.toFixed(2).padStart(7);
  console.log(`  ${ok ? 'pass' : 'FAIL'}  ${m.padEnd(10)} largest ${fmt(l.dev)} at t=${l.t.toFixed(1).padStart(4)}s (allowed ${fmt(l.allowed)}, self-env ${fmt(l.env)})`
    + ` | tightest margin ${fmt(-w.excess)} at t=${w.t.toFixed(1).padStart(4)}s`);
}
const gearOk = gearMismatch <= tol.gearMismatchSamples;
if (!gearOk) failed++;
console.log(`  ${gearOk ? 'pass' : 'FAIL'}  gear       ${gearMismatch} mismatched samples (max ${tol.gearMismatchSamples})`);

if (TABLE) {
  console.log('\n     t   dPos  envPos   dKph  envKph  dRoll envRoll  refY    natY');
  for (const row of rows.filter((_, i) => i % 10 === 0)) {
    console.log(`${row.t.toFixed(1).padStart(6)} ${row.pos.dev.toFixed(2).padStart(6)} ${row.pos.env.toFixed(2).padStart(7)} `
      + `${row.speedKph.dev.toFixed(1).padStart(6)} ${row.speedKph.env.toFixed(1).padStart(7)} `
      + `${row.rollDeg.dev.toFixed(1).padStart(6)} ${row.rollDeg.env.toFixed(1).padStart(7)} `
      + `${row.ref.pos[1].toFixed(2).padStart(7)} ${row.nat.pos[1].toFixed(2).padStart(7)}`);
  }
}
process.exit(failed ? 1 : 0);
