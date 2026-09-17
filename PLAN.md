# Ridgeline Offroad — Build Plan

Open-world offroad 4x4 sim. Forest, mud, mountains, wildlife.

**Runtime:** a native desktop game for **Linux, Windows and macOS**. Godot 4.7
with its native renderer and built-in Jolt Physics, plus a C++20 GDExtension
(`worldcore`) for the world model, streaming scheduler and vehicle controller.
No browser, WebView, Electron or Tauri anywhere.

**Current focus: Fedora** (`x86_64` RPM). Windows and macOS are not being built
or packaged yet, but every change must keep them buildable — see *Portability
rules*.

There are intentionally no purchased or downloaded 3D models. Vehicles, trees,
rocks, terrain and eventual wildlife stay procedural; a native asset baker turns
the procedural definitions into LOD meshes at build time.

Conventions: `+X` east, `+Y` up, `+Z` north/forward. 1 unit = 1 metre.

> **The browser build is gone (removed 2026-09-17).** It was a Three.js + Rapier
> + Vite reference. Everything already ported was verified against it, and its
> answers are frozen as test data (see *Frozen oracle data*). Its source is kept
> in git only: `git show a59773d:src/<path>` — the vehicle builders
> (`src/vehicles/`), scatter/flora (`src/world/scatter/`), sky
> (`src/world/sky/`), wildlife parts (`src/world/wildlife/`) and HUD/camera
> (`src/game/`) are still to be ported from there.

---

# STATUS — read this first

**Last updated:** 2026-09-17

## Milestones

| # | Deliverable | State | What is done | What is left |
|---|---|---|---|---|
| 0 | Benchmarks, capture format, skeletons | **partial** | `bench/` format + `FrameCapture`; reference-iGPU manifest; native 1080p captures committed (`bench/captures/fedora-iris-xe/`) | Discrete-GPU control machine manifest + captures. Browser-side baselines are no longer possible (build removed); native captures are the baseline. |
| 1 | Native vertical slice | **done** (placeholder truck) | Window, keyboard/gamepad input, HUD, Jolt `OffroadVehicle` at 60 Hz, analytic terrain, decal blob shadow, chase camera, release export launches directly | Truck body is placeholder boxes until milestone 4 |
| 2 | Streaming scheduler + worker terrain | **done** | C++ `StreamScheduler` + `TerrainStreamer`: frustum/turn-margin/prefetch selection, worker builds, generation ids, attach/upload/retire budgets, p95 work gate, no full ring at boot | Unexplained frame stalls on the first run after an export (see Open issues) |
| 3 | Static per-cell vegetation, LOD bake, local collision | **partial** | Placement port (exact vs oracle), immutable per-cell rule; flora prototypes ported and baked at build time (`worldcore_bake` → `generated/flora.bin`); `VegetationStreamer`: one multimesh per cell + species + LOD + variant with exact AABBs, static shaders, no shadows; chassis heightfield patch (`GroundPatch`): 16 m Jolt heightfield under the truck from the analytic field, recentred on a 0.5 m lattice, fixes rolled-truck fall-through (gated by `--smoke --roll`; refill ≤ 0.3 ms warm, replays unchanged per `tools/parity.sh`) | Local collision set (trunks/boulders/logs); `obstaclesNear` query; density presets |
| 4 | Vehicle bake + physics parity, camera modes, settings | **partial** | Tire/drivetrain/controller exact ports; Jolt replay parity; chase + close camera; telemetry; **procedural vehicle baker**: C++ mesh kernel + ported body/chassis/wheel/interior/accessory builders, 3 LODs, statics merged by material layer, rigged axles/wheels/links/coilovers/steering wheel (`VehicleMeshLibrary`, `vehicle_visual.gd`) | Cockpit/bonnet/orbit/cinematic cameras; settings; lights and mud driven by gameplay (shader inputs exist); surface detail (normal maps) |
| 5 | Sky/shaders, audio, FX, presets, stress tests | **not started** | Placeholder `ProceduralSkyMaterial`, static terrain shader | Everything |
| 6 | Release export + Fedora RPM | **partial** | `ridgeline-offroad-0.1.0-1` and `-2` RPMs built (`build/rpms/`); `%check` ran all test suites + packaged smoke test; release 2 adds runtime `Requires` (Vulkan, Wayland/X11, audio) and Godot licence notices; payload verified: no web/Node artefacts | Install → launch → upgrade 1→2 → uninstall on the local machine (needs `sudo`, run by the user); release 3 with vegetation; real licence |
| 7 | Windows + macOS builds and packaging | **later** (code kept portable) | Portable threads/CMake, per-platform `.gdextension` entries, Windows Desktop and macOS export presets (`.pck` exports validated) | Build `worldcore` with MSVC/clang-cl and Apple clang, install export templates, package (installer / signed + notarised `.app`), captures on each platform |

## Evidence

### Exact ports (`ctest --test-dir native/build/core`, also run by the RPM `%check`)
| Test | Covers | Result |
|---|---|---|
| `golden_terrain` | hash2, mulberry32, noise, `field.js` terms, normals, classification, colours, `findSpawn`, chunk mesher, 8-thread cache stress | 34k checks, worst error 5e-7 m |
| `golden_vehicle` | tire curve/friction/rolling resistance, drivetrain sessions for all 4 rigs, C++ `Tune`/`SURFACE` equal the data files | 74k checks, worst relative error 2e-12 |
| `golden_controller` | every force, wheel and telemetry value per step, 3 replays × 1200 steps | 549k checks at 1e-9 relative |
| `stream_scheduler` | boot = visible set only, bounded residency while turning, stale work discarded, no holes when stopped, budgets, starvation fallback, generations, capped retirement, hole-free LOD churn | 21 checks |
| `golden_scatter` | placement vs the frozen oracle: every tree/snag/deadfall/boulder per chunk (kind, position, height, radius) for 2 seeds and all rings; native rule = reference ring 0; thread-safe; density | 19.8k checks, 3,935 obstacles, worst error 2.6e-13 |
| `golden_flora` | prototype triangle total equals the reference's `prototypeTris` (10,116; depends on every random draw); native set differs only in far pine/birch; bake round-trips | 198 checks |
| `vegetation_batches` | instance counts per LOD rule, every transformed prototype vertex inside its batch AABB | 720 checks |
| `meshgen` | triangulation with holes (area exact, CCW, T-junctions split), watertight outward solids: extrude, rounded box, cylinder, cone, sphere, torus, tube, rod, lathe; mirroring | 68 checks |
| `ground_texture` | baked grass detail tiles without a seam, mean multiplier 1.0, valid normal map, deterministic, round-trip | 8 checks |
| `vehicle_mesh` | all 4 rigs x 3 LODs build, LOD detail ordering, triangle budgets, finite geometry inside the vehicle's dimensions, all material layers present, bake round-trip | 175 checks |

The reference amplifies round-off exponentially through wheel spin (5e-13 at
step 1 → 1e-5 by step 37), so `golden_controller` free-runs 12 steps and then
resynchronises the model's `IntegratedState` (also the save/load snapshot).

### Driving parity, Jolt vs the frozen Rapier reference (`tools/parity.sh`)
Gate = native deviation ≤ floor + 3 × the reference's **own** divergence under a
±1 µm / ±1 mm spawn nudge (`bench/replays/tolerances.json`). Fixed tolerances do
not work: a 1 µm nudge sends the reference 136 m off course on `shift-brake-v1`.
All 3 replays pass; on the calm `crawl-turn-v1` native stays within 0.30 m,
1.5 km/h and 1.7° over 24 s. New replays cannot be gated any more (no reference
to record them from) — extend with native-vs-native regression traces instead.

### Streaming on the reference iGPU (Iris Xe, 1920×1080, render scale 1.0, `turn-in-place-v1`)
| Capture | frame p95 / p99 / max | frames > 33 ms | frames with a missing cell | GPU p99 | stream work max |
|---|---|---|---|---|---|
| GDScript stand-in (main-thread builds, 720p) | — / — / 23–26 ms | — | — | — | — |
| native, warm run (`…-20260917-warm.json`) | 16.7 / 18.1 / 27.5 ms | 0 | 0 | 7.5 ms | 1.4 ms |
| native, first run after export (`…-20260917.json`) | 30.5 / 42.1 / 57.1 ms | 8 | 1 | 6.7 ms | 2.3 ms |
| native + vegetation, warm (`…-20260917-vegetation.json`) | 16.7 / 17.1 / 19.6 ms | 0 | 0 | 8.8 ms | 1.3 ms (terrain only) |
| + detailed truck, chase camera (`native-chase-idle-20260917-vehicle.json`) | 18.1 / 18.1 / 24.2 ms | 0 | 0 | 8.6 ms | — |
| + grass texture, chase camera (`native-chase-idle-20260917-grass.json`) | 17.0 / 18.1 / 23.4 ms | 0 | 0 | 8.5 ms | — |
| + round-shouldered mud-terrain tyres (`native-chase-idle-20260917-tyres.json`) | 17.2 / 18.1 / 33.3 ms | 0 | 0 | 9.3 ms (p95) | — |

Boot: 50 visible cells in ~25 ms on 4 workers (the GDScript stand-in: 222 ms).
Terrain only: draw calls ~48–70, ~90k primitives, 67 MB video memory. With
vegetation: ~363 draw calls (p95), ~625k primitives, 68 MB; vegetation boot adds
19 cells; ~500 multimesh batches / ~3.5k stems resident while turning.

## Open issues
- **Frame stalls on the first run after an export** (8–10 frames > 33 ms), with
  stream work ≤ 2.3 ms and GPU ≤ 8 ms in those frames — not the streamer, not the
  GPU. Suspected cold shader/pipeline cache or system load; **not proven**.
  Worker count (1 vs 4) made no measurable difference.
- **Sierra HD cannot hold still on full brake** in low first at idle (~0.5 m/s;
  Ridgeback ~0.09 m/s). Inherited; brake torque vs `clutchCreep` tuning item.
- `Performance.TIME_PROCESS` includes the vsync wait (reads 16–70 ms while the
  GPU does ~4 ms); the streamer's gate uses measured work instead. Do not
  reintroduce it into any frame-cost budget.
- ~~Terrain shading reads washed out, with an unexplained grey patch near
  spawn.~~ R2 shading pass (fog 0.0007, contrast/saturation grading, stronger
  wetness darkening) fixed the wash; the "patch" was rock/scree reading flat
  under it plus a WheelFX mis-emission at locked brakes (see REALISM.md R2).
  Verify on the Fedora reference iGPU before closing for good.
- Vegetation draw calls (~360) are dominated by far cells with several variants
  each; if a capture ever shows CPU render cost, merge far-LOD variants per cell.
- Grass detail texture: procedural tufts + normal map baked by `worldcore_bake`
  (`generated/ground.bin`, 512 px, 4 m world-space tile), one albedo + one
  normal read on vegetated ground, faded out by 140 m. Cost +0.7–2 ms GPU median
  at 1080p on Iris Xe; a second repeat-breaking read cost another ~1.5 ms and was
  dropped. Rock, scree, mud and snow do not have detail textures yet.
- Vehicle near LOD is ~89–95k triangles (chassis ~49–51k, each wheel ~13–15k);
  ~310 draw calls total in the chase view at 1080p. Tyres have a round shoulder
  profile (lathed arc) and deep staggered, shoulder and wrap-around sidewall blocks
  at every LOD. Bake is 26.1 MB
  (`generated/vehicles.bin`, palette-indexed looks).
- The reference's side-panel extrusion used `rotateY(+90deg)`, which mirrors the
  panel profile front-to-back; the native builder maps profile z to +z so cowl,
  doors and hood line up (`side_panel()` in `vehicle_mesh.cpp`).
- Native flora deviates from the reference on purpose in one place: far pine is
  a cone and far birch a crown cluster (the reference's flat opaque cards fogged
  into pale rectangles). `FloraStyle::Reference` keeps the original for tests.
- `tests/golden/controller.json` is 12 MB.
- Spec license is the placeholder `LicenseRef-Proprietary`.

## Next actions, in order
1. **RPM install test on the local machine** (user runs the `sudo dnf` steps):
   install release 1, launch, upgrade to release 2, launch, uninstall.
2. ~~Milestone 3 vegetation rendering~~ ✅ placement, flora bake and per-cell
   multimesh streaming are done (see Evidence).
3. **Local collision set** around the truck: trunk/boulder/log colliders.
   ~~Chassis heightfield patch~~ ✅ `GroundPatch` (fixes the fall-through bug;
   see milestone 3).
4. ~~Milestone 4 — vehicle baker~~ ✅ built (see milestone table). Next on the
   vehicle: headlights/brake lights and mud from gameplay, cockpit and other
   camera modes, surface detail.
5. Remaining camera modes, settings/graphics presets, discrete-GPU captures.
6. Milestone 5: native sky shader, audio/FX within budgets, memory pressure and
   crash/recovery tests; tune the performance preset from captures.
7. Only after milestone 6: water, wildlife, FX, audio, objectives, damage/fuel,
   save/load (start from `IntegratedState`) — each with its own budget.
8. Milestone 7, when Fedora ships: Windows and macOS builds, packaging and captures.

---

# Portability rules (Linux, Windows, macOS)

Fedora is the focus, but nothing may lock the game to Linux:
- **C++:** standard C++20 only, and only features all three standard libraries
  ship: libstdc++, MSVC STL and **Apple libc++** (no `std::jthread`,
  `std::stop_token`, `<execution>` parallel algorithms, or other partially
  supported features). No POSIX, Win32 or Cocoa headers in `worldcore`; engine
  services (files, threads visible to Godot, time) go through Godot or the STL.
- **Build:** CMake options must be compiler-neutral: GCC/Clang flags inside
  `if(NOT MSVC)`, ELF linker flags inside `if(UNIX AND NOT APPLE)`. Library
  names come from godot-cpp's `GODOTCPP_SUFFIX` and are listed for every platform
  in `native/godot/worldcore.gdextension`.
- **Godot:** no OS-specific paths (`user://`/`res://` only), no platform
  singletons without a fallback; renderer choices must exist on Vulkan and Metal.
- **Packaging** lives per platform under `packaging/<platform>/`; only
  `packaging/fedora/` exists today. Nothing in `native/` may depend on it.
- **Dev tools** (`tools/`) are POSIX shell + Python 3 + Node built-ins; they are
  not shipped and are expected to run on Linux/macOS (Windows via Git Bash/WSL).

---

# Project layout

```
PLAN.md                     this file
native/worldcore/           C++20 library + GDExtension
  include/worldcore/        field, noise, hash, chunk_mesh, stream, tire,
                            drivetrain, vehicle, tune, vmath, world
  src/                      field, chunk_mesh, stream, scatter, flora, vegetation,
                            drivetrain, vehicle
  gdextension/              TerrainField, TerrainStreamer, VegetationStreamer,
                            OffroadVehicle
  tools/bake.cpp            worldcore_bake: build-time flora prototype bake
  tests/                    golden/ (frozen oracle data) + test_*.cpp
native/godot/               Godot 4.7 project: Mobile renderer, Jolt, 60 Hz
  data/                     world/tune/vehicles/replays JSON — source of truth
  scripts/                  main, game/ (input, visual, camera, HUD),
                            replay_runner, frame_capture, game_data
  shaders/                  static terrain and foliage shaders
  generated/                build output of worldcore_bake (gitignored)
packaging/fedora/           spec, desktop entry, icon, launcher, build-rpm.sh
bench/                      capture format, machine manifests, captures,
                            replays/ (tolerances + frozen reference traces)
tools/                      parity.sh, compare-replay.mjs, machine-manifest.sh
```

## Frozen oracle data (recorded from the browser reference before removal)
| File | Recorded from |
|---|---|
| `native/worldcore/tests/golden/terrain.json` | noise, field, classification, `findSpawn` for 3 seeds |
| `native/worldcore/tests/golden/physics.json` | tire model, drivetrain sessions for 4 rigs |
| `native/worldcore/tests/golden/controller.json` | controller forces/telemetry against recorded Rapier body states |
| `native/worldcore/tests/golden/scatter.json` | trees/snags/deadfall/boulders per chunk (positions, radii) for 2 seeds; ground cover not recorded |
| `bench/replays/reference/*.json` | 3 driving replays + 4 perturbed runs each (chaos envelope) |
| `native/godot/data/*.json` | contract SURFACE/BIOME, TUNE, vehicle catalog + body metrics, replay inputs |

## Frozen contracts — change only with every consumer
1. **Coordinates:** +X east, +Y up, +Z forward/north. Vehicle origin at ground
   level, centred between the axles; hub at `(±track/2, tireRadius, ±wheelbase/2)`.
   Wheels ordered **FL, FR, RL, RR**.
2. **World:** `CHUNK = 128`, surface ids 0–7 (rock, gravel, dirt, grass, loam,
   mud, water, snow), biome ids 0–5 (riverbed, mudflat, meadow, pine, scree,
   alpine), `hash2`, `mulberry32` bit-identical to the reference.
3. **Vehicle rig (for the baker):** steering angle, per-wheel spin and suspension
   travel, lights, mud amount.
4. **Data drift:** `native/godot/data/{world,tune}.json` must equal the C++
   defaults (`golden_vehicle` fails otherwise).

---

# ⚡ PERFORMANCE IS THE PRIORITY

Big open world on a modest GPU. **Frame time wins every argument** — if a
feature and 60 fps disagree, the feature loses.

## Non-negotiable shipping rules

1. **Never load, generate, upload, or draw the full map.** Terrain collision is
   analytic and deterministic, so it answers a wheel query everywhere without a
   resident mesh. GPU terrain, vegetation and decorations exist only for cells in
   the camera frustum or a small turn/prediction margin. ✅ terrain
2. **No real-time shadows in the default game.** The vehicle gets one cheap
   projected blob shadow. Trees, terrain and props neither cast nor receive
   dynamic shadows. A screenshot-only `Ultra` preset may add a short-range
   vehicle shadow later, never default. ✅
3. **No tree or grass movement.** Foliage transforms and vertices are static
   after upload: no wind in a vertex shader, no per-frame instance updates.
4. **All heavy work has a budget.** Worker generation, main-thread attachment,
   GPU uploads, collider changes and destruction all have a measured per-frame
   budget. When the frame is over budget, world work waits. ✅ terrain
5. **No performance claims without a capture.** Judged on 1080p at render scale
   1.0 on the recorded reference iGPU and a discrete-GPU control machine. Track
   CPU and GPU frame time, p95/p99, resident memory, draw calls, triangles and
   streaming stalls.

## Native runtime decision

| Area | Decision | Why |
|---|---|---|
| Application | Godot 4 native exports: Linux now, Windows and macOS later | Native desktop executables, no browser or WebView |
| Rendering | Mobile renderer (Vulkan; Metal on macOS) for the performance preset; Forward+ only for a verified higher preset; Compatibility as fallback | Controlled low-cost baseline |
| Performance-critical code | C++20 `worldcore` GDExtension, CMake/Ninja | World generation, scheduling, baking and vehicle maths run as native code |
| Physics | Godot's built-in Jolt | Native rigid bodies; the four-raycast suspension and tire model stay custom C++ |
| Authoring/UI | Godot scenes, shaders, input, HUD, settings | Editor plus native input/audio/windowing |
| Distribution | Fedora RPM (`x86_64`) first; Windows installer and macOS `.app` later | Launcher, executable, packed data and extension in each platform's standard locations |

The RPM installs the exported executable and `.pck` under
`/usr/libexec/ridgeline-offroad/`, `libworldcore` beside the executable, the
launcher under `/usr/share/applications/`, icons under
`/usr/share/icons/hicolor/`, and licences/credits under
`/usr/share/licenses/ridgeline-offroad/`. Built offline from Fedora packages
(`godot`, `godot-runner` as the release template) with Fedora compiler flags;
never bundles Node, a browser or `node_modules`.

## Visible-world streaming design ✅ implemented (`worldcore/stream.*`)

```text
camera frustum + small turn margin
        │
        ├─ visible now ─────► highest-priority worker jobs ─► capped attaches/uploads
        ├─ prefetch ────────► far edge +1 cell, turn margin, velocity wedge; cancelled if the view changes
        └─ behind / outside ─► no job, no mesh, no GPU memory

analytic height / surface function ─► wheel and spawn queries anywhere
near-vehicle obstacle set         ─► only the small collision radius gets colliders (milestone 3)
```

- 128 m cells are seed/ownership boundaries only, not residency.
- Score: visible first, then depth along the view, distance, alignment.
- Workers build raw arrays (and Godot packed arrays); only the engine thread
  creates RenderingServer meshes/instances. Generation ids drop stale results.
- Separate budgets: attaches per frame, upload bytes, retirements per frame. No
  attaching while the rolling p95 of **measured work** exceeds 16.7 ms, except
  one cell per frame once a visible cell has been missing for 1 s.
- Retirement after 1.5 s unwanted, capped per frame, never removing the last mesh
  covering a visible cell.
- Boot builds only the camera-visible set behind the loading overlay.

## Static vegetation, terrain and vehicle plan

### Vegetation and props (milestone 3)
- Port the deterministic placement rules, species table, per-m² densities and
  seed hashes (`git show a59773d:src/world/scatter/index.js`); placement is
  immutable per cell and built on the stream workers.
- Bake pine, birch, snag, deadfall, boulder, shrub, fern and grass prototypes
  (`src/world/scatter/flora.js`, `geo.js`) to low/medium/near LODs at build time.
- One `MultiMeshInstance3D` (or RenderingServer multimesh) per **visible cell +
  species + LOD + variant** with an exact cell AABB. A global forest MultiMesh is
  forbidden: its AABB defeats culling.
- Vertex colours, opaque/cutout materials; no per-instance animation, alpha
  blending, shadows or per-frame transform updates. Far vegetation becomes
  cards/clusters with no collider.
- Colliders independent of visibility: a small recycled radius of
  trunk/boulder/log colliders around the truck; analytic ground stays primary.

### Terrain and lighting
- ✅ Analytic height, normal, surface, biome, river and spawn in C++; three LODs
  with skirts; positions, normals, colours and a rock/wet/snow mask only.
- ✅ Static terrain shader; no time or wind uniforms.
- ✅ Directional sun without shadow maps; decal blob shadow aligned to the terrain.
- Sky: a single native sky shader or a low-frequency update; environment-map
  generation never in the frame loop.

### Vehicles without purchased models (milestone 4)
- Port the shared builders (body, chassis, wheels, accessories, interior) from
  `git show a59773d:src/vehicles/` into the native asset baker; emit three LOD
  mesh sets per rig at build time.
- Merge static surfaces by material; only wheels, axles/suspension, steering
  wheel, lamps and the blob shadow stay dynamic. Interior, bolts and real tread
  load only in the close/cockpit LOD.
- ✅ `SURFACE`/`TUNE` tables, tire curve, drivetrain/diff order, four suspension
  probes, winch, recovery and telemetry in C++ with replay parity.

## Milestone exit gates

| # | Exit gate | Met? |
|---|---|---|
| 0 | Baseline captures and an empty native RPM install/launch test committed | partial — iGPU captures only; no install test yet |
| 1 | Native executable launches directly and handles the fixed 60 Hz vehicle step | ✅ |
| 2 | Turning never causes an all-ring load; stale jobs discarded; no visible holes during the replay | ✅ (warm capture: 0 frames with a missing cell) |
| 3 | No tree wind or tree shadows; forest behind the camera has no resident batch or draw call | — |
| 4 | Vehicle replay within handling tolerance; draw calls dominated by cells, not truck parts | physics ✅, bake — |
| 5 | Performance preset ≥ 60 FPS at 1080p / scale 1.0 on the reference iGPU, no streaming hitch > 33 ms at p99 | — |
| 6 | `rpmbuild -ba packaging/fedora/ridgeline-offroad.spec` produces an installable `x86_64.rpm`; install, launch, uninstall, upgrade tested on clean Fedora | — |

---

# Game layer backlog

Realism roadmap: see [REALISM.md](REALISM.md) (research, tranches R1-R4,
budgets). R1 landed: pedal-answering lights (`H` + menu), mud/dirt
accumulation on the body, procedural engine audio (menu volume, persisted),
speed-driven FOV, per-wheel dust/mud/spray particles. R2 landed: terrain
shading pass (fog/grading/wetness), `SkyCycle` time-of-day with auto
headlights and a menu clock, `TireAudio` surface noise, `RutTrail`
visual-only wheel ruts.

- [x] Chase and close cameras; HUD speed/gear/range/lock/rpm/surface/winch/stuck
- [x] Keyboard + gamepad bindings (same as the reference)
- [x] Auto-hold (game layer): brake held for the model off-throttle near
      standstill, released on throttle/winch - without it clutchCreep drives
      the parked truck away with the tires turning and jittering forever
      (inherited from the reference). Re-engage threshold must stay above the
      ~1-1.7 m/s the creep sustains, or a truck that has driven never parks.
- [x] Automatic reverse (game layer): holding S near standstill shifts to R
      and S becomes reverse throttle; W brakes, and from a reverse stop
      shifts back to first. Q/E manual shifting untouched.
- [x] Mouse look on the chase camera: captured mouse orbits/elevates, eases
      back behind the truck when the mouse rests and the truck is moving;
      pause releases the cursor.
- [ ] Cockpit, bonnet, orbit, cinematic cameras
- [ ] Trail objectives, waypoints, recovery points, free-roam map
- [ ] Damage model, fuel, water fording depth
- [ ] Save/load (start from `worldcore::IntegratedState`), settings, graphics
      presets — started: pause menu with a mouse-sensitivity slider persisted
      to `user://settings.cfg` (`[input] mouse_sensitivity`)

**Controls:** `WASD` drive (hold `S` at a stop to reverse) · mouse look ·
`Space` handbrake · `Q/E` gears · `L` range · `X` diff lock · `R` recover ·
`F` winch hook · `G` reel in · `C` camera · `V` next vehicle ·
`P`/`Esc` pause · `/` help · `` ` `` stats

---

# Commands

```bash
# C++ core + tests (no Godot needed)
cmake -S native/worldcore -B native/build/core -G Ninja && cmake --build native/build/core
ctest --test-dir native/build/core

# GDExtension (debug + release land in native/godot/bin/)
cmake -S native/worldcore -B native/build/gde -G Ninja -DWORLDCORE_GODOT=ON -DWORLDCORE_TESTS=OFF
cmake -S native/worldcore -B native/build/gde-release -G Ninja -DWORLDCORE_GODOT=ON -DWORLDCORE_TESTS=OFF -DGODOTCPP_TARGET=template_release
cmake --build native/build/gde --target worldcore && cmake --build native/build/gde-release --target worldcore

# Play, smoke test, capture, parity
godot --path native/godot                                  # play
godot --headless --path native/godot -- --smoke            # exit 0 = boots, streams, truck settles
godot --headless --fixed-fps 60 --path native/godot -- --replay=crawl-turn-v1 --trace=out.json
godot --headless --path native/godot --export-release "Linux/x86_64" "$PWD/build/ridgeline-offroad/ridgeline-offroad"
build/ridgeline-offroad/ridgeline-offroad --resolution 1920x1080 -- --smoke --capture=bench/captures/<machine>/<name>.json
tools/parity.sh

# RPM
packaging/fedora/build-rpm.sh
```

Game options after `--`: `--seed=N`, `--vehicle=0..3`, `--smoke`,
`--roll` (with `--smoke`: rolled-truck ground-patch gate),
`--camera=chase`, `--screenshot=PATH`, `--capture=PATH`, `--stream-workers=N`,
`--replay=ID --trace=PATH`.

## Toolchain notes (this machine has no system compiler or Godot)
- User-local toolchain in `.toolchain/` (gitignored): pip `cmake`, `ninja`,
  `ziglang` (clang/libc++) wrappers in `.toolchain/bin/`, Godot 4.7.2 binary;
  export templates in `~/.local/share/godot/export_templates/4.7.2.stable`.
  On a normal Fedora setup: `sudo dnf install gcc-c++ cmake ninja-build godot godot-runner`.
- godot-cpp `godot-4.5-stable` in `native/third_party/godot-cpp` (gitignored).
- The extension links with `-z nodelete`: without it the editor segfaults at exit
  running exit handlers from the already-unloaded library.
- Keep parallel compile jobs low (`--define "_smp_build_ncpus 2"`) when memory is
  tight; `rpmbuild` runs were killed by the low-memory guard.
- `%global _debugsource_packages 0` does not disable debugsource (RPM checks that
  the macro is defined); use `%undefine`.

---

# Bugs already found and fixed — do not reintroduce

### Physics (now in C++)
- **Anti-roll bars:** the compressed side must *gain* force; the wrong sign makes
  a pro-roll bar.
- **Tire basis uses the same steer sign as the visual rig:** a positive angle
  points the wheel toward +X. Seen from the chase camera (looking along +Z in
  right-handed Y-up space) +X is on the screen's **left**, so input maps
  screen-right (`D`, right arrow, stick right) to **negative** steer
  (`scripts/game/vehicle_input.gd`). Physics and replays keep the +X convention.
- **Ackermann comes from the turn radius:** `δ_inner = atan(L / (R − t/2))`,
  `δ_outer = atan(L / (R + t/2))`.
- **Never spawn the truck in the air:** place it at its static ride height,
  `travel × (1 − 2·staticSag)` above the ground (`VehicleModel::spawn_pose`).
- **Mud is gated on soft AND wet ground**, not wheelspin alone.
- **The physics world has no ground mesh** — wheels ride the analytic field, so
  a rolled truck's chassis fell through the world. `GroundPatch` keeps a small
  heightfield under the truck; its heights sit 2 cm *below* the analytic
  surface so bilinear interpolation never pokes above the true ground into the
  chassis during normal driving. Wheels must never query the patch.
- **Scatter density is per m², never per cell.**
- Gravity is 9.81 (Godot's default 9.8 was changed in `project.godot`).
- JS `Math.round` rounds halves toward +∞ (`js_round`), and the terrain's
  low-frequency lattice is float32; both matter for bit-exact terrain.

### Vehicle builders (for the milestone-4 port)
- Wheel arches are arcs centred on the **axle**, not the sill.
- Door cut-outs must clear the arches (`archHalf`) or triangulation collapses.
- The mud mask samples **vehicle-local** space, not object space.
- Curved tubes use centripetal Catmull-Rom; the default overshoots through bodywork.
- Glass is tinted alpha, never a transmission/refraction pass.

### Engine / tooling
- Performance.TIME_PROCESS includes the vsync wait; never use it as work cost.
- Godot's Plane normals from `Camera3D.get_frustum()` point outward.
- Triangle winding: Three.js fronts are counter-clockwise, Godot's clockwise.
