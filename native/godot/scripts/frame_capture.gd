# frame_capture.gd - per-frame performance capture in the bench/README.md format.
#
# PLAN.md rule 5: no performance claim without a capture. CPU and GPU time come
# from the renderer's own measurement of the viewport, not from delta, so a
# vsync wait is not counted as work.
class_name FrameCapture
extends RefCounted

const FORMAT := "ridgeline-capture/1"

var replay: String
var seed: int
var boot_ms := 0.0
var boot_cells := 0

var _rid: RID
var _frames: Array[PackedFloat32Array] = []  # [dt_ms, cpu_ms, gpu_ms, draws, prims, cells, missing, stream_ms]
var _peak_static_mb := 0.0
var _peak_video_mb := 0.0


func _init(viewport: Viewport, p_replay: String, p_seed: int) -> void:
	replay = p_replay
	seed = p_seed
	_rid = viewport.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(_rid, true)


func record(delta: float, cells: int, missing: int, stream_ms := 0.0) -> void:
	var row := PackedFloat32Array([
		delta * 1000.0,
		RenderingServer.viewport_get_measured_render_time_cpu(_rid)
			+ RenderingServer.get_frame_setup_time_cpu(),
		RenderingServer.viewport_get_measured_render_time_gpu(_rid),
		Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
		Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),
		cells,
		missing,
		stream_ms,
	])
	_frames.append(row)
	_peak_static_mb = maxf(_peak_static_mb, Performance.get_monitor(Performance.MEMORY_STATIC) / 1048576.0)
	_peak_video_mb = maxf(_peak_video_mb, Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0)


static func _percentiles(values: Array) -> Dictionary:
	if values.is_empty():
		return {}
	var v := values.duplicate()
	v.sort()
	var pick := func(q: float) -> float: return v[mini(v.size() - 1, int(floor(q * (v.size() - 1))))]
	var sum := 0.0
	for x in v:
		sum += x
	return {mean = sum / v.size(), p50 = pick.call(0.5), p95 = pick.call(0.95), p99 = pick.call(0.99), max = v[-1]}


func save(path: String) -> void:
	# The first frames include shader compilation and boot; they are kept in the
	# raw rows but excluded from the summary so they cannot hide or fake a stall.
	var warm := _frames.slice(mini(10, _frames.size()))
	var cols := {dt = [], cpu = [], gpu = [], draws = [], prims = [], stream = []}
	var missing_frames := 0
	var stalls := 0
	for r in warm:
		cols.dt.append(r[0]); cols.cpu.append(r[1]); cols.gpu.append(r[2])
		cols.draws.append(r[3]); cols.prims.append(r[4]); cols.stream.append(r[7])
		if r[6] > 0:
			missing_frames += 1
		if r[0] > 33.0:
			stalls += 1
	var doc := {
		format = FORMAT,
		replay = replay,
		seed = seed,
		build = "native",
		engine = Engine.get_version_info().string,
		renderer = ProjectSettings.get_setting("rendering/renderer/rendering_method"),
		adapter = RenderingServer.get_video_adapter_name(),
		driver = RenderingServer.get_video_adapter_api_version(),
		resolution = [DisplayServer.window_get_size().x, DisplayServer.window_get_size().y],
		render_scale = ProjectSettings.get_setting("rendering/scaling_3d/scale"),
		captured_at = Time.get_datetime_string_from_system(true) + "Z",
		boot = {ms = boot_ms, cells = boot_cells},
		summary = {
			frames = warm.size(),
			frame_ms = _percentiles(cols.dt),
			cpu_ms = _percentiles(cols.cpu),
			gpu_ms = _percentiles(cols.gpu),
			draw_calls = _percentiles(cols.draws),
			primitives = _percentiles(cols.prims),
			stream_ms = _percentiles(cols.stream),
			frames_with_missing_cells = missing_frames,
			stalls_over_33ms = stalls,
			peak_static_mb = _peak_static_mb,
			peak_video_mb = _peak_video_mb,
		},
		columns = ["dt_ms", "cpu_ms", "gpu_ms", "draws", "primitives", "cells", "missing", "stream_ms"],
		frames = _frames.map(func(r): return Array(r)),
	}
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		push_error("capture: cannot write %s" % path)
		return
	f.store_string(JSON.stringify(doc, "", false))
	print("ridgeline: capture written %s (%d frames, p95 %.1f ms, p99 %.1f ms)" % [
		path, warm.size(), doc.summary.frame_ms.get("p95", 0.0), doc.summary.frame_ms.get("p99", 0.0)])
