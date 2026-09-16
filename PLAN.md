# Ridgeline Offroad — Build Plan

Open-world offroad 4x4 sim. Forest, mud, mountains, wildlife.

**Current reference build:** Three.js + Vite + Rapier3D (WASM), vanilla JS
modules. It is a playable behavioural reference, not the shipping runtime.

**Shipping target:** a native Linux desktop game, packaged as a Fedora
`x86_64` **RPM**. Use Godot 4 with its native renderer and built-in Jolt
Physics, plus a C++20 GDExtension for the world streamer, procedural mesh
baker, and custom vehicle controller. Do **not** wrap the current site in
Electron, Tauri, or another WebView: that would still leave the game running
through browser JavaScript and WebGL.

There are intentionally no purchased or downloaded 3D models. Vehicles,
trees, rocks, terrain, and eventual wildlife remain procedural; the native
asset baker turns stable procedural definitions into low-LOD native meshes at
build time, rather than rebuilding them in the player’s first frame.

Conventions: `+X` east, `+Y` up, `+Z` north/forward. 1 unit = 1 metre.
The integration seam is [`src/world/contract.js`](src/world/contract.js) — surface
table, biome bands, provider interfaces. Modules agree there or they don't merge.

> **Migration rule:** retain this web project unchanged as a visual and driving
> oracle until native parity is measured. Port rules, seed behaviour, tuning
> data, and tests—not browser/Three.js implementation details. Nothing in the
> browser runtime is part of the RPM deliverable.

---

## Phase 1 — Vehicles ✅ COMPLETE

Built first, on purpose: the car is what the camera stares at all game.

| Module | What it does | Status |
|---|---|---|
| `vehicles/procgen.js` | Canvas-drawn normal/roughness/mask maps, value-noise fBm | ✅ |
| `vehicles/materials.js` | Paint w/ clearcoat, rubber, cast iron, zinc, glass, lenses, **mud shader** | ✅ |
| `vehicles/parts.js` | roundedBox, plate, tube, rod, lathe, coil spring, safe geometry merge | ✅ |
| `vehicles/wheel.js` | Mud-terrain tire with **real tread-block geometry**, beadlock rim, vented rotor + caliper | ✅ |
| `vehicles/chassis.js` | Ladder frame, live axles, 4-link + panhard, coilovers, drivetrain, skids, sliders | ✅ |
| `vehicles/body.js` | Panels w/ real door cut-outs + shut lines, axle-centred arches, greenhouse, lights | ✅ |
| `vehicles/interior.js` | Bucket seats + harness, dash, gauges, **steering wheel that turns**, shifters, pedals | ✅ |
| `vehicles/accessories.js` | Roll cage, winch bumper, snorkel, light bar, roof rack + cargo, spare, hi-lift | ✅ |
| `vehicles/vehicle.js` | Assembler + runtime rig: steering, wheel spin, suspension travel, lights, mud | ✅ |
| `vehicles/catalog.js` | 4 rigs: Ridgeback 90 / 110, Sierra HD pickup, Timberwolf | ✅ |
| `showroom/main.js` | Turntable inspector: paint, flex, mud, lights, LOD, x-ray, night, deep links | ✅ |

**Physics-facing API is frozen** — the controller only ever calls:
`setSteering(rad)`, `setWheelSpin(i, rad)`, `setTravel(i, m)`, `update()`,
`setLights({})`, `setMud(0..1)`, `getChassisBox()`.

Solid-axle articulation is genuine: both wheels parent to one axle group, so
compressing one side droops the other, rolls the housing, and drags every
control arm and coilover with it.

### Known polish left on vehicles (low priority, not blocking)
- Mud reads slightly flat/uniform on large panels — wants streaking + wetness.
- No LOD switching yet at distance (builders take `detail`, nothing picks it).
- Tube buggy / no-body style not built (4 rigs all share the ladder-frame body).
- Door panels don't open.

---

## Phase 2 — World  (IN PROGRESS — integration layer landed)

The providers now exist and are wired together. `npm run dev` is a **playable
game**: terrain streams, a truck spawns on it, and it drives.

### A. Terrain — `src/world/terrain/` ✅

| Module | What it does | Status |
|---|---|---|
| `terrain/noise.js` | Perlin gradient noise, fBm, ridged multifractal | ✅ |
| `terrain/field.js` | The analytic world model `y = f(x, z)` — continental roll, warped forest hills, massif-masked mountains, bog basins, carved river valleys — plus surface/biome/colour classification | ✅ |
| `terrain/index.js` | **TerrainProvider.** 128 m chunks, 3 LOD rings (2/4/8 m), skirts, analytic normals, budgeted meshing, eviction, `findSpawn()` | ✅ |
| `terrain/material.js` | Ground shader: macro breakup noise, slope rock overlay, wetness darkening, distance fade | ✅ |

Normals are **analytic, not computed from the mesh** — that is what keeps LOD
seams from lighting differently. Skirts, not stitching: a crack you cannot see
beats six index-buffer variants per chunk.

### B. Vegetation & scatter — `src/world/scatter/` ✅

| Module | What it does | Status |
|---|---|---|
| `scatter/geo.js` | Shape vocabulary: needle whorls, ribbons, leaflets, blobs, deformers, `assemble()` to one baked geometry with `aFlex` | ✅ |
| `scatter/flora.js` | Pine, birch, snag, deadfall, boulder, shrub, fern, grass — 3 detail levels each | ✅ |
| `scatter/material.js` | Instanced foliage material, wind in the vertex shader | ✅ |
| `scatter/index.js` | **ScatterProvider.** One-sample-one-decision placement, instance pools with free lists, LOD by ring, `obstaclesNear()`, collider ring that follows the truck | ✅ |

Placement is **derived, never stored** — `mulberry32` seeded on the chunk
coordinate, so a chunk that streams out and back comes back identical and the
physics layer can ask about chunks it cannot see. Densities are stems per **m²**,
multiplied by cell area, so re-spacing the grid never changes how thick the
forest is.

### C. Wildlife — `src/world/wildlife/` 🔨 half-built

| Module | What it does | Status |
|---|---|---|
| `wildlife/pelt.js` | Canvas hair strokes → fur/feather normal maps, coat materials | ✅ |
| `wildlife/shapes.js` | `loft()`, bend, leaflet — the animal geometry vocabulary | ✅ |
| `wildlife/rig.js` | Bone hierarchy, skinning by distance-to-segment, merge | ✅ |
| `wildlife/species.js` | Procedural specs: deer stag/doe, wolf, boar, hare, crow | ✅ |
| `wildlife/index.js` | **WildlifeProvider** — assembly, gait, AI, chunk spawn/despawn | ❌ not written |

### D. Vehicle physics — `src/physics/` ✅

| Module | What it does | Status |
|---|---|---|
| `physics/tune.js` | Every feel number a designer touches, in one table | ✅ |
| `physics/tire.js` | Normalised Pacejka-ish curve, combined slip friction circle, rolling resistance | ✅ |
| `physics/drivetrain.js` | Torque curve, impulse clutch, gearbox, low range, open vs locked diffs | ✅ |
| `physics/ground.js` | Heightfield fixed-point solve + Rapier fallback for scatter colliders | ✅ |
| `physics/controller.js` | **The driving model.** Rapier body, per-wheel raycast suspension, weight transfer, anti-roll, mud sink + bulldozing, aero, winch, recovery flip, stuck meter, telemetry | ✅ |

Order matters and is load-bearing: all four loads are solved **before** any tire
force, then the drivetrain splits torque, then wheels integrate, then the diffs
equalise speeds. Reorder any of it and the locker becomes a no-op.

### E. Atmosphere & FX — `src/world/sky/` ⚠️

| Module | What it does | Status |
|---|---|---|
| `sky/atmosphere.js` | Preetham scattering (GLSL + JS twins), ephemeris, grade table by sun altitude | ✅ |
| `sky/index.js` | **AtmosphereProvider.** Dome, sun/moon, stars, hemisphere + bounce fill, sun-matched fog, texel-snapped shadow camera, day-night clock | ✅ |
| `sky/index.js` → `refreshEnvironment()` | PMREM IBL prefiltered from the sky | ⚠️ **broken — see below** |
| `world/fx/` | Mud spray, dust plumes, tire tracks, rain/snow, water surface | ❌ empty |
| `src/audio/` | Engine, tire, wind | ❌ empty |

### F. Game layer — `src/game/`, `src/main.js` ✅

| Module | What it does | Status |
|---|---|---|
| `game/camera.js` | Chase / close / cockpit / bonnet / orbit / cinematic, loose heading, no inherited roll | ✅ |
| `game/input.js` | Keyboard + gamepad; continuous axes vs edge-triggered actions | ✅ |
| `game/hud.js` | Speed, gear, range, lock, tach, artificial horizon, compass tape, per-wheel load+slip, surface, clock, alerts, help | ✅ |
| `main.js` | input → fixed 60 Hz physics → visual sync → streaming → camera → render | ✅ |
| `index.html` | Real game entry with boot overlay and a startup-error trap | ✅ |
| `tools/probe.mjs` | Headless driving harness — drives the real game over CDP with real key events, dumps telemetry | ✅ |

---

## ⚠️ OPEN REGRESSION — fix this first

Adding the PMREM sky environment map turned the whole scene **black**: the sky
renders white, terrain and trees render as silhouettes, the lights appear dead.
No shader errors and no console output, so it is renderer **state**, not a
compile failure.

Suspects, in order:
1. `PMREMGenerator.compileEquirectangularShader()` was deprecated/removed in
   recent three — drop the call, `fromScene()` does not need it.
2. `pmrem.fromScene()` not restoring render-target / tone-mapping state before
   the shadow pass, leaving the whole world fully shadowed.
3. The dome material's `depthTest: false` + `gl_Position.z = gl_Position.w` may
   not survive being rendered by the PMREM cube camera.

Bisect by commenting out the two `sky.refreshEnvironment(...)` calls in
`src/main.js`. If the scene comes back, the fault is inside
`refreshEnvironment()` in `src/world/sky/index.js`.

It is worth fixing rather than deleting: without an IBL the truck's clearcoat,
glass and chrome have nothing to reflect and read as black plastic.

---

# ⚡ PERFORMANCE IS THE PRIORITY

This is a big open world on a modest GPU. From here on, **frame time wins every
argument** — if a feature and 60 fps disagree, the feature loses. The JavaScript
build is a reference only; optimising it is useful while porting, but it must
not defer the native RPM migration.

## Non-negotiable shipping rules

1. **Never load, generate, upload, or draw the full map.** Terrain collision is
   analytic and deterministic, so it can answer a wheel query everywhere
   without a resident mesh. GPU terrain, vegetation, and decorations exist
   only for chunks in the camera frustum or a very small turn/prediction margin.
2. **No real-time shadows in the default game.** The vehicle gets one cheap
   projected blob/contact shadow. Trees, terrain, and props neither cast nor
   receive dynamic shadows. A screenshot-only `Ultra` preset may add a
   short-range vehicle shadow later, but that preset may never become default.
3. **No tree or grass movement.** Foliage transforms and vertices are static
   after a chunk upload. Do not run wind code in a vertex shader or update an
   instance buffer every frame. Keep the existing `aFlex` concept only as
   optional baked metadata; it has no runtime use in the base build.
4. **All heavy work has a budget.** Worker generation, main-thread scene
   attachment, GPU uploads, collider changes, and destruction all have a
   measured per-frame budget. When the frame is over budget, world work waits.
5. **No performance claims without a capture.** Every performance change is
   judged on 1080p at render scale 1.0 on the recorded reference iGPU and a
   discrete-GPU control machine. Track CPU frame time, GPU frame time, p95/p99,
   resident-chunk memory, draw calls, triangles, and streaming stalls.

## Native runtime decision

| Area | Shipping decision | Why it meets the requirement |
|---|---|---|
| Application | Godot 4 native Linux export | Runs as a native desktop executable, not inside a browser or WebView. |
| Rendering | Native Vulkan Mobile renderer for the performance preset; Forward+ only for a verified higher-quality preset; OpenGL Compatibility as fallback | Avoids browser/WebGL overhead and gives a controlled low-cost baseline. |
| Performance-critical code | C++20 `worldcore` GDExtension, built with CMake/Ninja | Terrain/scatter generation, scheduling, asset baking, and raycast vehicle maths leave JavaScript and run as native code. |
| Physics | Godot's built-in Jolt Physics (Godot 4.4+) | Replaces Rapier WASM with native physics; keep the vehicle’s four raycast suspension and tire model as custom C++ logic. |
| Authoring/UI | Godot scenes, shaders, input, HUD, settings | Retains an editor and native input/audio/windowing without paying for a browser shell. |
| Distribution | `rpmbuild` Fedora RPM, initially `x86_64` | Installs a desktop launcher, executable, packed game data, and native extension in conventional Linux locations. |

Godot includes Jolt as a 3D physics option from 4.4, and its Linux export
produces an optimised executable plus packed project data; those are the right
inputs to the RPM packaging stage. [Godot: Jolt Physics](https://docs.godotengine.org/en/stable/tutorials/physics/using_jolt_physics.html)
and [Godot: Linux export](https://docs.godotengine.org/en/stable/tutorials/export/exporting_for_linux.html).

### Native project boundary

```text
games/offroad/
  src/                         # current web reference — retained during parity work
  native/
    godot/                     # scenes, UI, materials, input maps, export presets
    worldcore/                 # C++20 GDExtension: rules, workers, streaming, vehicle
    tools/                     # native procedural asset baker and replay benchmark
    generated/                 # build-only LOD meshes/material data; never hand-edited
  packaging/fedora/
    ridgeline-offroad.spec     # RPM source-of-truth
    ridgeline-offroad.desktop
    icons/
```

`world/contract.js`, `terrain/field.js`, `physics/tune.js`, vehicle catalog
data, and fixed input/replay traces are the migration source of truth. Port
their constants and algorithms into neutral data/C++ tests first; do not call
the JavaScript files from the native build and do not carry Three.js mesh or
material APIs across the boundary.

## Visible-world streaming design

The old square ring around the truck is explicitly retired. It loads much more
world than the player can see and it gives background work the same priority as
the road ahead.

```text
camera frustum + small turn margin
        │
        ├─ visible now ─────► highest-priority native worker jobs ─► capped GPU uploads
        ├─ forward prefetch ─► low-priority jobs; discard if view changes
        └─ behind / outside ─► no visual mesh, no foliage instances, no GPU memory

analytic height / surface function ─► wheel and spawn queries anywhere
near-vehicle obstacle set         ─► only the small collision radius gets colliders
```

- Use 128 m logical cells only as deterministic seed/ownership boundaries.
  They do not imply residency. Every frame, calculate a 2D camera-frustum set
  expanded by one cell at the far edge and a short velocity/look-direction
  prefetch wedge. A cell behind the camera is not built merely because it is
  close to the truck.
- Give each candidate a score: inside-frustum first, screen depth next,
  distance next, then forward velocity/look alignment. Cancel queued work that
  loses relevance before it reaches the GPU.
- Build raw vertex/index/instance arrays on native worker threads. Only create
  Godot rendering resources and physics bodies on the appropriate engine
  thread. Use generation IDs so a result for an evicted cell is dropped rather
  than briefly appearing in the wrong place.
- Keep separate budgets for CPU mesh attachment, GPU upload bytes, vegetation
  instances, and collision changes. Do not attach new world resources while
  the rolling p95 frame cost is above the 16.7 ms 60-FPS budget.
- Evict outside the visible/prefetch envelope with an age hysteresis so a small
  camera wobble does not thrash memory. Dispose GPU and physics resources on a
  capped retirement queue, never in one large stop-the-world sweep.
- At boot, construct the active vehicle and only the first camera-visible
  terrain/prop cells. Show a native loading overlay until that small set is
  ready, then let the rest stream. Startup must never synchronously generate
  the surrounding square ring or the entire map.

## Static vegetation, terrain, and vehicle plan

### Vegetation and props

- Port the existing deterministic one-sample placement rules, species table,
  density units, LOD rings, and seed hashes to `worldcore`; retain their world
  appearance but make placement results immutable per cell.
- Bake the existing procedural pine, birch, snag, rock, shrub, fern, and grass
  prototypes into native meshes in the package build. Generate low, medium,
  and near LODs once—not while driving.
- Make one `MultiMeshInstance3D` per **visible cell + species + LOD + variant**
  with an exact cell-local AABB. A global forest MultiMesh is forbidden because
  its huge AABB defeats frustum culling. MultiMesh supports a custom AABB, so
  setting it avoids runtime AABB calculation and lets the engine cull batches
  at cell granularity. [Godot MultiMesh reference](https://docs.godotengine.org/en/stable/classes/class_multimesh.html).
- Use vertex colours and simple opaque/cutout materials. No per-instance
  animation, alpha blending, dynamic shadow flags, or per-frame transform
  updates. Far vegetation becomes cards/cluster meshes and is never given a
  collider.
- Build colliders independently of visibility: retain only a small radius of
  trunk/boulder/log colliders around the truck and recycle them as it moves.
  The analytic ground remains the primary contact source.

### Terrain and lighting

- Port the analytic height, normal, surface, biome, river, and spawn functions
  exactly to C++. Mesh only the visible cells in three LODs; preserve skirts
  until a profiled native alternative beats them. Carry only positions,
  normals, vertex colours, and compact surface/wetness attributes.
- The terrain and foliage shaders are static. Remove the current foliage time
  and wind uniforms, shader injection, and `tickFoliage()` equivalent.
- Keep a directional sun for diffuse light but disable shadow-map allocation,
  shadow camera updates, `castShadow`, and `receiveShadow` across world and
  vehicle content. Implement the vehicle blob shadow as a terrain-normal
  aligned transparent/decal quad with depth fade.
- Keep atmosphere/sky as a single native sky shader or a low-frequency update.
  Environment-map generation must not run in the frame loop. Fixing the
  current PMREM regression is optional reference-build work, not a blocker for
  the native lighting path.

### Vehicles without purchased models

- The current four rigs are already complete procedural specifications. Port
  the shared builders (body, chassis, wheels, accessories, interior) into the
  native asset baker, then emit three LOD mesh sets per rig at build time.
- Merge every static vehicle surface by material in the baked result. At runtime
  retain only wheels, axles/suspension, steering wheel, lamps, and the blob
  shadow as moving nodes. The high-detail interior, bolt geometry, and real
  tread never load outside the close/cockpit LOD.
- Port the existing `SURFACE` and `TUNE` tables, Pacejka-ish tire curve,
  drivetrain/differential order, four suspension probes, winch, recovery, and
  telemetry to the C++ vehicle controller. A parity replay must agree with the
  browser reference within recorded handling tolerances before artistic tuning.

## Migration milestones and acceptance gates

| Order | Deliverable | Exit gate |
|---|---|---|
| 0 | Establish two reproducible benchmark machines, fixed camera/driving replays, a frame-time capture format, and current browser baselines. Create the `native/` and RPM skeletons without deleting the reference. | Baseline captures and an empty native RPM install/launch test are committed. |
| 1 | Native vertical slice: Godot window, input/HUD, one generated vehicle, analytic terrain, Jolt chassis, blob shadow, no trees. | Native executable launches directly; it is not served by Vite and it handles the fixed 60 Hz vehicle step. |
| 2 | `worldcore` streaming scheduler and worker terrain generation. Attach only frustum/prefetch cells, with cancellation, upload budgets, and retirement budgets. | Turning the camera never causes an all-ring load; stale cell jobs are discarded; no visible terrain holes during the replay. |
| 3 | Static per-cell MultiMesh vegetation/rocks, procedural LOD baking, and local recycled collision set. | Tree wind and all dynamic tree shadows are absent; forest behind the camera consumes no resident visual batch or draw call. |
| 4 | Vehicle asset bake/LOD merge and C++ physics/drivetrain parity. Port camera modes, controls, telemetry, and settings. | Chosen vehicle replay stays within the documented handling tolerance and draw calls are dominated by cells, not hundreds of truck parts. |
| 5 | Native shaders/sky, audio/FX only within explicit budgets, graphics presets, memory pressure tests, and crash/recovery tests. | Performance preset sustains >=60 FPS at 1080p / render scale 1.0 on the reference iGPU with no streaming hitch above 33 ms at p99. |
| 6 | Release export and Fedora packaging. | `rpmbuild -ba packaging/fedora/ridgeline-offroad.spec` creates an installable `x86_64.rpm`; install, launch, uninstall, and upgrade are tested in a clean Fedora environment. |

The RPM will install the exported native executable and `.pck` game data under
`/usr/libexec/ridgeline-offroad/`, `libworldcore.so` beside that executable,
the launcher under `/usr/share/applications/`, icons under
`/usr/share/icons/hicolor/`, and licensing/credits under
`/usr/share/licenses/ridgeline-offroad/`. The spec must use Fedora compiler
flags and RPM dependency metadata; it must not bundle Node, a browser, Vite,
or `node_modules`.

## Transitional browser-reference work

Do this only when it helps create the baseline or de-risks a direct port. It
is **not** a substitute for milestones 0–6 above. The web build should default
to shadows off and remain a fast oracle while native work is underway.

### Cut: real-time shadows

Shadow mapping is the single most expensive thing in the frame. It costs a full
extra scene render every frame, forces `castShadow` bookkeeping on every tree
and every vehicle part, and a 2048² map is a large allocation on an iGPU.

- Default `shadows` to **off**, not on. Keep the `?shadows=1` switch for
  screenshots and for anyone with a discrete card.
- Delete the `castShadow` / `receiveShadow` traversal in `src/main.js` and the
  `shadow: true` flags in the scatter species table.
- Replace with a **blob shadow**: one dark, soft-edged quad projected on the
  ground under the truck, oriented to the terrain normal. It costs one draw
  call, it grounds the vehicle — which is the entire job a contact shadow does —
  and nobody misses tree shadows at 30 fps.
- The sun stays a `DirectionalLight`; only `castShadow` goes. Removing it also
  retires the texel-snapping and bias tuning in `sky/index.js`.

### Cut: tree wind animation

The wind sway in `scatter/material.js` looks good and is not worth its price.
It runs per vertex on **every foliage vertex in the world**, every frame, and
the forest is the largest vertex count in the scene by a wide margin. It also
forces the vertex shader off the fast path and defeats any static batching.

- Remove the `uTime` / `uWind` displacement from the foliage vertex shader and
  the `tickFoliage()` call from the scatter `update()`.
- **Keep `aFlex` baked into the geometry.** It costs one float per vertex,
  nothing per frame, and it is what makes re-enabling wind a one-line change if
  a stronger machine ever justifies it.
- Static foliage can then be merged and uploaded once, which is worth far more
  than the sway was.

### The rule: load and draw only what the player can see

Right now the world streams in a **square ring around the truck**, and the
scatter pools set `frustumCulled = false`. That means the game builds, uploads
and submits the forest *behind* the camera every single frame. Fix it in three
places:

1. **Cull by frustum, which means batching by chunk.** One global
   `InstancedMesh` per species cannot be culled — it spans the whole ring, so
   three has to draw it. Move to **per-chunk instanced batches** (one per chunk
   per species-detail that actually has instances there), each with a real
   bounding box. Three then culls them for free. This trades a higher draw-call
   count for a much lower vertex count; cap it by only doing per-chunk batching
   for the near rings and keeping one merged far-ring batch.
2. **Prioritise the build queue by where the camera is looking**, not just by
   distance. `rebuildQueue()` in both `terrain/index.js` and `scatter/index.js`
   sorts on `dx² + dz²`. Add a dot product against the camera forward vector so
   chunks ahead of the player resolve first and chunks behind resolve last, or
   not at all until you turn around.
3. **Never build a frame's worth of world in one frame.** The budget mechanism
   already exists (`budget` chunks per `update()`); keep it, and make the budget
   adaptive — if the last frame took longer than ~14 ms, build nothing this
   frame.

### Other cheap wins, in order of payoff

- **Merge the vehicle.** ~370 draw calls and most of them are the truck: every
  part is a separate mesh with its own cloned material, which was right for the
  showroom and wrong for the game. Merge static parts by material and keep only
  the wheels, axles and steering wheel as separate movable nodes.
- **Vehicle LOD.** The builders already take `detail`; nothing picks it. At
  distance the truck does not need its interior, cage bolts or tread blocks.
- **Cap the pixel ratio.** Already capped at 1.5 — consider 1.0 as the default
  on integrated graphics and let a setting raise it.
- **Drop the terrain LOD0 ring to 32 segments** if the near ring proves to be
  the cost. 2 m ground resolution is nice, 4 m is fine under tires.
- **Measure before cutting anything else.** `` ` `` toggles the stats line
  (fps / draws / triangles / chunks). Nothing above should be done on a hunch —
  the anti-roll bug looked like a performance problem and was not.

---

## Phase 3 — Game layer

- [x] Camera rig: chase, close, cockpit, bonnet, orbit, cinematic
- [x] HUD: speed, gear, range/lock, compass, tilt/pitch meter, winch, stuck
- [ ] Trail objectives, waypoints, recovery points, free-roam map
- [ ] Damage model, fuel, water fording depth
- [ ] Save/load, settings, graphics presets for the iGPU

---

## Run the current reference build

These commands exist only for visual/handling parity and benchmark capture.
They are **not** the future shipping application.

```
npm run dev        # the game
npm run showroom   # vehicle showroom — the Phase 1 deliverable
```

After milestones 0–6 are implemented, the release path will be native export
followed by RPM packaging, conceptually:

```bash
godot --headless --path native/godot --export-release "Linux/x86_64" build/ridgeline-offroad
rpmbuild -ba packaging/fedora/ridgeline-offroad.spec
```

Those commands are planned, not runnable yet: the Godot project and RPM spec
will be created in step 2 of the ordered next actions.

**Game URL params:** `seed`, `v` (vehicle 0-3), `hour`, `view` (terrain chunks),
`trees` (scatter chunks), `density`, `shadows=0`, `dpr`.

**Controls:** `WASD` drive · `Space` handbrake · `Q/E` gears · `L` range ·
`X` diff lock · `R` recover · `F/G` winch · `C` camera · drag to look ·
`H/J` lights · `T/Y` time · `B` wash · `V` next vehicle · `P` pause ·
`/` help · `` ` `` stats

Showroom deep links, e.g. `showroom.html?v=2&cam=215&el=12&dist=9&artic=.9&mud=.7&spin=0`

---

# RESUME HERE  (read this first if the session was lost)

**Last updated:** 2026-09-17 · Native RPM migration selected; performance-first
streaming and static-world rules are now the shipping plan.

## Where the work stands
Phase 1 (vehicles) is done and visually verified.
Phase 2 tracks **A (terrain), B (scatter), D (physics), E (sky)** are landed and
wired together by `src/main.js`; the game boots, streams terrain, spawns a truck
on flat ground and drives it. Verified headlessly by screenshot **and** by a
telemetry probe that sends real key events (`tools/probe.mjs`).
Track **C (wildlife)** has all its parts built but nothing assembles them yet.
One open regression: the PMREM environment map (see above).

The native project and RPM spec do not exist yet. The browser source therefore
remains the behavioural baseline until milestones 0–4 pass; adding wildlife,
FX, new rendering features, or full-world content before then is out of scope.

## Project state
```
games/offroad/
  PLAN.md                  <- this file
  index.html               game entry - boot overlay + startup error trap
  showroom.html            vehicle inspector (Phase 1 deliverable)
  tools/probe.mjs          headless driving harness over CDP
  src/main.js              DONE - the integration layer
  src/vehicles/            DONE - 10 modules, do not rewrite
  src/showroom/main.js     DONE
  src/world/contract.js    FROZEN interface - terrain/scatter/wildlife/sky agree here
  src/world/terrain/       DONE - field, noise, index (provider), material
  src/world/scatter/       DONE - geo, flora, material, index (provider)
  src/world/sky/           DONE - atmosphere, index (provider); env map broken
  src/world/wildlife/      pelt/rig/shapes/species DONE - index.js MISSING
  src/world/fx/            EMPTY
  src/audio/               EMPTY
  src/physics/             DONE - tune, tire, drivetrain, ground, controller
  src/game/                DONE - camera, input, hud
  native/                  PLANNED - Godot project + C++20 worldcore, not created yet
  packaging/fedora/        PLANNED - RPM spec/desktop metadata, not created yet
```

## Frozen contracts — do not change without updating every consumer
1. **Vehicle API** (`src/vehicles/vehicle.js`): `setSteering(rad)`,
   `setWheelSpin(i, rad)`, `setTravel(i, metres)`, `update()`,
   `setLights({head,aux,brake,indicator})`, `setMud(0..1)`, `setPaint(c)`,
   `getChassisBox()`, `wheels[]` ordered **FL, FR, RL, RR**.
2. **World providers** (`src/world/contract.js`): `CHUNK=128`, `SURFACE` table,
   `BIOME` bands, `TerrainProvider.height/sample/biomeAt/update`,
   `ScatterProvider.obstaclesNear`, `hash2`, `mulberry32`.
3. **Coordinates:** +X east, +Y up, +Z forward/north. Vehicle origin is at
   **ground level, centred between the axles** — so a hub sits at
   `(±track/2, tireRadius, ±wheelbase/2)`.

## Bugs already found and fixed — do not reintroduce
- `mergeGeometries` returns **null** when a batch mixes indexed and non-indexed
  geometry. `ExtrudeGeometry` is non-indexed; Cylinder/Lathe/Sphere/Torus are
  indexed. **Always use `mergeParts()` from `parts.js`**, never
  `mergeGeometries` directly.
- Wheel arches must be arcs centred on the **axle**, not the sill, or tires sit
  in caves. See `archOutline()` in `body.js`.
- A `PlaneGeometry` is already upright facing +Z — a windshield needs only the
  rake rotation, not an extra `-PI/2`.
- The mud shader must sample **vehicle-local** space (via the root's inverse
  world matrix uniform `uMudInv`), not object space — every part is modelled
  around its own origin, so object space caked the whole truck uniformly.
- `CatmullRomCurve3` default `'catmullrom'` overshoots on tight corners and
  pushed cage bars through the bodywork. `tube()` uses `'centripetal'`.
- Door cut-outs must clear the wheel arches (`m.archHalf`) or ExtrudeGeometry
  triangulation collapses.
- Avoid `transmission` on glass: it forces an extra full-scene pass per frame
  and blows out white against a bright environment. Tinted alpha instead.

### Found during Phase 2 integration
- **`SKY_GLSL` emitted invalid GLSL.** `${2.0}` stringifies to `"2"`, and GLSL
  will not assign an int to a float. Every interpolated constant now goes
  through `glslFloat()`. This had silently broken the sky dome since it was
  written — the dome never once compiled.
- **Shader injection point matters.** `roughnessFactor` is declared by
  `<roughnessmap_fragment>`; touching it at `<color_fragment>` will not compile.
- **`PCFSoftShadowMap` has been removed** from this version of three. Use
  `PCFShadowMap`.
- **Fog must not be brightness-corrected.** Fog is applied *before* tone mapping,
  exactly like the dome, so raw sky radiance is already the right number. The
  correction factor washed the horizon to white.
- **Anti-roll bars were inverted.** The compressed side must *gain* force. Wrong
  sign turns a sway bar into a pro-roll bar that amplifies every lean.
- **Tire basis must use the same steer sign as the visual rig** —
  `applyAxisAngle(up, +steer)` matches `wheel.rotation.y = steer`. Note that a
  **positive** angle points the wheel toward +X, so positive steer turns
  **right**, despite what the `setSteering` docstring says.
- **Ackermann comes from the turn radius**, not from hand-rolled algebra:
  `δ_inner = atan(L / (R − t/2))`, `δ_outer = atan(L / (R + t/2))`.
- **Never spawn the truck in the air.** It costs two seconds of bouncing.
  `controller.settle()` places it at its own static ride height,
  `travel × (1 − 2·staticSag)` above the ground.
- **Mud must be gated on soft AND wet ground.** Wheelspin alone caked the truck
  on dry grass, so it arrived at the first real mud hole already filthy.
- **Scatter density is per m², never per cell.** Per-cell probabilities mean
  re-spacing the placement grid silently changes how thick the forest is.
- **SwiftShader is not a performance signal.** Headless software rendering runs
  at seconds per frame, which starves the fixed-step accumulator and makes the
  game appear to run in slow motion. The physics was correct the whole time.

## How to verify visually without a GUI

```bash
npm run build
./node_modules/.bin/vite preview --port 5191 --host 127.0.0.1 &

# screenshot
google-chrome --headless=new --no-sandbox --enable-unsafe-swiftshader \
  --use-angle=swiftshader --hide-scrollbars --window-size=1400,880 \
  --virtual-time-budget=60000 --screenshot=out.png \
  "http://127.0.0.1:5191/index.html?hour=9&view=4&trees=3&dpr=1"

# browser console (shader errors show up here and nowhere else)
... --enable-logging=stderr --log-level=0 2>&1 | grep -iE "CONSOLE|ERROR: 0:"
```

Both `index.html` and `showroom.html` print startup errors onto the loading
overlay, so `--dump-dom | grep -A14 'STARTUP ERROR'` gives you the stack trace.

### Driving telemetry, headless

`tools/probe.mjs` drives the real game over the DevTools protocol with real key
events and prints per-second telemetry — speed, rpm, gear, per-wheel load, slip,
travel, contact, surface. This is how the suspension and drivetrain were
validated; a screenshot cannot tell you the anti-roll sign is backwards.

```bash
google-chrome --headless=new --no-sandbox --enable-unsafe-swiftshader \
  --use-angle=swiftshader --window-size=640,400 \
  --remote-debugging-port=9222 --user-data-dir=/tmp/rl-chrome about:blank &

node tools/probe.mjs "http://127.0.0.1:5191/index.html?shadows=0&view=3&trees=3&dpr=0.4" \
  --keys=KeyW --seconds=8
```

Keep the window small and shadows off, or SwiftShader gets slow enough that the
page stops answering CDP between frames and the probe looks like a hang.

---

## Next actions, in order

**The native migration is the work.** Browser-only polish is allowed only where
it validates a rule or improves the reference capture; it cannot add a feature
that must be ported twice.

1. **Record the baseline.** Capture current browser frame data, draw/triangle
   counts, memory, startup time, and fixed driving replays on the two specified
   machines. Add a written machine manifest beside the captures.
2. **Create the native foundation.** Add the Godot project, C++20
   `worldcore` GDExtension, neutral data files for surface/biome/tune/vehicle
   specs, and an empty Fedora spec. Prove that a release-exported native binary
   is launched directly—not through Vite—and that a minimal RPM installs it.
3. **Port the analytic terrain and custom raycast vehicle controller first.**
   Run its 60 Hz step against replay traces before adding a forest or sky.
4. **Implement the camera-directed streaming scheduler and worker hand-off.**
   Enforce cancellation, build/upload/retirement budgets, and no full ring at
   boot before porting decorative content.
5. **Bake and instance static terrain/vegetation/prop LODs per visible cell.**
   Ship no tree wind, world shadow map, or uncullable global instance batch.
6. **Bake and merge vehicle LODs.** Port only dynamic suspension/wheel/lamp
   nodes and verify driving parity and frame captures.
7. **Add native UI, camera modes, graphics settings, and the blob shadow;**
   then fix/replace sky lighting only inside the measured budget.
8. **Tune the native performance preset on real GPUs.** Set view distance,
   density, LOD thresholds, render scale, and upload budgets from captures;
   do not copy the web defaults blindly.
9. **Build, test, and inspect the RPM in a clean Fedora environment.** Verify
   clean install, desktop launch, upgrade, uninstall, dependency closure, and
   that no web runtime or Node artefact is included.
10. Only after milestone 6 passes: water, wildlife, FX, audio, objectives,
    damage/fuel, and save/load—each with a separate budget and regression run.

### Deferred reference-only follow-ups

- Mud reads flat on large panels; reproduce any improvement in the native
  baked-material path rather than adding browser-only shader complexity.
- A near web terrain chunk is ~4 ms to build; the native worker split is now a
  mandatory migration requirement, not an optional optimisation.
- `main.js` caps physics catch-up at 5 steps, which degrades to slow motion on a
  slow browser. Retain it as an oracle characteristic; define explicit native
  time-debt handling only after replay profiling.
