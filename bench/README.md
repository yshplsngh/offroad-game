# Benchmarks

PLAN.md rule 5: **no performance claim without a capture.** Every capture is
taken at 1920×1080, render scale 1.0, on one of the two recorded machines, with
the replay named in the file.

```
bench/
  machines/     one ridgeline-machine/1 manifest per benchmark machine
  captures/     <machine>/<build>-<replay>-<YYYYMMDD>.json, ridgeline-capture/1
```

## Machines

| Role | Manifest | Status |
|---|---|---|
| `reference-igpu` | `machines/fedora-iris-xe.json`: i5-1334U, Iris Xe, Mesa 26.1 | recorded |
| `discrete-control` | — | **needed**: run `tools/machine-manifest.sh discrete-control` on a discrete-GPU machine |

Record a manifest on a machine before capturing on it:

```bash
tools/machine-manifest.sh reference-igpu > bench/machines/<name>.json
```

Capture on AC power, with the power profile shown in the manifest, and nothing
else heavy running. A compile in the background invalidates a capture.

## Replays

| Id | What it exercises | Native | Browser |
|---|---|---|---|
| `turn-in-place-v1` | Spawn (seed 1337), camera at 6 m, yaw 0.9 rad/s for 6 s: streaming in and retiring cells while turning | `--smoke --capture=` | none (browser build removed before it was captured) |
| `drive-straight-v1`, `crawl-turn-v1`, `shift-brake-v1` | Driving replays (`native/godot/data/replays.json`) | `tools/parity.sh` | frozen traces in `replays/reference/` |

Replays are fixed inputs. Changing one means a new id (`-v2`); old captures
are never compared against a changed replay.

## Native capture

```bash
build/ridgeline-offroad/ridgeline-offroad \
  --resolution 1920x1080 -- --smoke --capture=bench/captures/<machine>/native-turn-in-place-v1-$(date +%Y%m%d).json
```

## `ridgeline-capture/1` format

One JSON document:

| Field | Meaning |
|---|---|
| `format` | `"ridgeline-capture/1"` |
| `replay`, `seed` | replay id and world seed |
| `build` | `native` or `browser` |
| `engine`, `renderer`, `adapter`, `driver` | engine version, rendering method, GPU name and API version reported by the engine |
| `resolution`, `render_scale` | window size and 3D render scale |
| `captured_at` | UTC timestamp |
| `boot` | `{ms, cells}`: time and cell count for the boot-visible set |
| `summary` | statistics over frames after the first 10 (warm-up) |
| `summary.frame_ms`, `cpu_ms`, `gpu_ms`, `draw_calls`, `primitives` | `{mean, p50, p95, p99, max}` |
| `summary.stalls_over_33ms` | frames longer than 33 ms (the p99 hitch gate) |
| `summary.stream_ms`, `frames_with_missing_cells` | main-thread terrain attach/retire time; frames where a visible cell had no mesh |
| `summary.peak_static_mb`, `peak_video_mb` | peak CPU static and video memory |
| `columns`, `frames` | raw per-frame rows: `dt_ms, cpu_ms, gpu_ms, draws, primitives, cells, missing, stream_ms` |

`cpu_ms` and `gpu_ms` come from the renderer's measured viewport time, not from
frame delta, so waiting for vsync is not counted as work. Headless runs report
0 for GPU, draw and primitive columns and are not performance captures.
