# Ridgeline Offroad — Realism Plan

Companion to [PLAN.md](PLAN.md). PLAN.md owns milestones, budgets and the
frozen contracts; this file owns the *realism* roadmap: what makes the sim
feel real, in what order, and inside which budgets. Update both when a
tranche lands.

## What research says sells offroad realism

Analysis of the genre leaders (SnowRunner/MudRunner, BeamNG) and driving
game-feel literature points at a consistent stack, in descending
value-for-cost:

1. **Consequence in the terrain.** Ground that visibly reacts: darker = wetter
   = more viscous, ruts where wheels churned, vehicles that carry the mud they
   drove through. SnowRunner's surfaces differ in base viscosity and darkness
   telegraphs risk before you commit — the *reading* of the terrain is the
   gameplay.
2. **Audio that tracks the machine.** Engine note driven by rpm/load (additive
   harmonics + noise is the standard real-time approach), surface-dependent
   tire/rolling noise, suspension thumps. Nothing else anchors speed and
   strain as cheaply.
3. **The vehicle shows its state.** Lights that answer the pedals, mud/dirt
   accumulation, visible suspension work (already rigged), body roll.
4. **Camera and FOV as speed cues.** FOV widening with speed, slight lag and
   lead — game-feel work ties perceived speed almost entirely to these.
5. **Wheel-driven FX.** Dust on dry loose surfaces, mud chunks under
   wheelspin, spray in water — keyed to slip, load and surface id.

Sources: [SnowRunner terrain physics](https://www.mudrunnermods.com/terrain-physics-snowrunner/),
[realistic mud projection mods](https://mod.io/g/snowrunner/m/realistic-mud-and-projection-on-vehicles),
[procedural engine audio](https://gamedev.net/forums/topic/713424-procedural-engine-audio/),
[engine sound generator survey](https://gameaudiotools.dev/tools/engine-sound-generator/),
[game feel survey](https://arxiv.org/pdf/2011.09201),
[sim realism guide](https://www.asetek.com/simsports/guides/most-realistic-racing-simulator-everything-you-need-to-know/).

## Hard constraints (from PLAN.md — non-negotiable)

- **The physics core is a frozen exact port** (golden tests + replay parity).
  Realism lands in the *game layer*: what you see, hear and feel — never by
  editing `worldcore` force math. Physics evolution would need new
  native-vs-native regression baselines first (PLAN.md "Driving parity").
- **Frame time wins every argument.** Every tranche item ships with a measured
  budget on the reference iGPU. No shadows, no per-frame foliage updates.
- **Everything stays procedural** — no purchased/downloaded assets, audio
  included: synthesis only.
- Platform-portable: Godot + STL only, no OS-specific paths.

## The model already exposes (unused until now)

| Signal | Source | Used by |
|---|---|---|
| `rpm`, `gear`, `reverse`, `throttle` | telemetry / input | engine audio, lights |
| `mud` (0–1), `surface`, `wetness` | telemetry / field.sample | dirt accumulation, FX, tire noise |
| per-wheel `slip`, `contact`, `load`, `surface` | `wheel(i)` | wheel FX, tire noise |
| `dirt` uniform | vehicle.gdshader | mud on the body (never driven before) |
| `head/tail/indicator/aux` | vehicle_lamp.gdshader | lights (never driven before) |
| suspension travel, steer, spin | wheel rig | already visual |
| `speed`, `forward_speed` | telemetry | FOV, rolling noise |

## Tranches

### R1 — the machine answers you (this tranche)
| Item | Design | Budget/Gate |
|---|---|---|
| **Lights** | Tail lamps lit on brake/auto-hold, reverse lamp (`indicator`) in R, headlights toggled from the menu + `H`; aux follows headlights in low range | free (shader params) |
| **Dirt accumulation** | `dirt` uniform integrates toward telemetry `mud` while churning; rinses in water, slowly sheds on dry ground | free (one param/frame) |
| **Procedural engine audio** | `AudioStreamGenerator` 22.05 kHz: firing-order fundamental (rpm/15 for a V8) + 4 harmonics + throttle-scaled intake noise, phase-continuous, volume/pitch smoothed; menu volume slider, persisted | ≤ 0.3 ms/frame fill, measured |
| **Speed FOV** | chase fov + up to +10° by ~90 km/h, smoothed | free |
| **Wheel dust/mud FX** | 4 GPUParticles3D at hub positions: emit on contact && (slip or loose surface at speed); color/size by surface class (dust / mud / spray) | ≤ 4 draw calls, ≤ 256 particles, no shadows; smoke stays 60 fps |

Gate: `--smoke` and `--smoke --roll` pass; replay parity untouched (audio/FX
never touch physics inputs).

### R2 — the world reads true
- Terrain shading pass: fix the washed-out look + grey spawn patch (open
  issue): tonemap/ambient rebalance, wetness darkening like SnowRunner's
  viscosity telegraphing (darker = wetter = riskier — data already in the
  colour mask).
- Native sky shader with a slow time-of-day drift + headlight relevance at
  dusk (milestone 5 item); environment updates never in the frame loop.
- Tire/rolling audio layer: surface-keyed noise (gravel crunch, mud squelch,
  wet hiss) from the same generator, per-surface filters.
- Wheel ruts *visual only*: decal ribbon behind wheels in soft ground,
  fixed-size ring buffer, no collision feedback (physics stays frozen).

### R3 — consequences (gameplay systems, PLAN backlog)
- Water fording depth: engine stall + hydrolock risk past intake height,
  audio muffling, spray FX (uses analytic river/water data).
- Damage model: impact telemetry already recorded → panel dirt/scratch masks,
  drivetrain efficiency loss; repair at recovery.
- Fuel: consumption from rpm × throttle; jerry-can accessory already baked.
- Winch realism: tree anchor points from the scatter set (milestone 3's
  `obstaclesNear`), cable sag render, strain audio.

### R4 — physics evolution (only after native baselines exist)
- Record native-vs-native regression traces (PLAN.md replay section), then:
  brake-hold vs clutchCreep tuning (Sierra HD open issue), tire relaxation
  length at crawl speeds (kills the low-speed omega jitter at its source),
  per-surface rolling resistance spread.
- Each change re-baselines the traces in the same commit.

## Non-goals

- Real mud *deformation* (SnowRunner-style SDF terrain edits): conflicts with
  the analytic-terrain contract that makes wheels, streaming and collision
  answerable anywhere. Ruts stay visual (R2).
- Licensed engine recordings, sample libraries: procedural only.
- VR, force feedback hardware: out of scope for the Fedora-first milestone.
