# replay_runner.gd - drive OffroadVehicle through a fixed replay and write a
# ridgeline-replay-trace/1 file, gated by tools/parity.sh against the frozen
# browser-reference traces in bench/replays/reference/.
#
# Runs headless with --fixed-fps 60 so every frame is exactly one physics step:
#   godot --headless --fixed-fps 60 --path native/godot -- --replay=drive-straight-v1 --trace=out.json
class_name ReplayRunner
extends Node

const FIXED_DT := 1.0 / 60.0

var replay_id: String
var trace_path: String
var replay: Dictionary
var trace_every := 6
var total_steps := 0
var vehicle: OffroadVehicle
var field: TerrainField
var spawn: Vector3
var frames: Array = []


func _init(p_replay_id: String, p_trace_path: String) -> void:
	replay_id = p_replay_id
	trace_path = p_trace_path


func _ready() -> void:
	var replays := GameData.load_json("replays")
	replay = replays.get("replays", {}).get(replay_id, {})
	if replay.is_empty():
		_quit(2, "unknown replay %s" % replay_id)
		return
	trace_every = int(replays.get("traceEvery", 6))
	total_steps = roundi(float(replay.seconds) / FIXED_DT)
	if Engine.physics_ticks_per_second != 60:
		_quit(2, "physics must tick at 60 Hz")
		return

	field = TerrainField.new()
	field.seed = int(replay.seed)
	spawn = field.find_spawn(0.0, 0.0)

	var spec := GameData.vehicle(GameData.load_json("vehicles"), int(replay.vehicle))
	vehicle = OffroadVehicle.new()
	vehicle.name = "Vehicle"
	add_child(vehicle)
	if not vehicle.configure(spec, GameData.load_json("tune"), field):
		_quit(2, "vehicle configure failed")
		return
	vehicle.spawn(spawn.x, spawn.z, float(replay.heading))
	# Same physics world as the game: the chassis ground patch rides along. The
	# chassis never touches ground in the frozen replays, so traces are
	# unchanged - tools/parity.sh is the proof.
	var patch := GroundPatch.new()
	patch.name = "GroundPatch"
	add_child(patch)
	patch.configure(field, vehicle)
	set_physics_process_priority(0)


## Axes and one-shot actions for step i: a segment holds from `from` seconds; its
## actions fire once on the segment's first step (the reference harness's rule).
func _inputs_at(i: int) -> Dictionary:
	var t := i * FIXED_DT
	var seg: Dictionary = replay.segments[0]
	for s in replay.segments:
		if t >= float(s.from) - 1e-9:
			seg = s
	var axes := {throttle = 0.0, brake = 0.0, handbrake = 0.0, steer = 0.0, winch = 0.0}
	axes.merge(seg.get("axes", {}), true)
	var first := roundi(float(seg.from) / FIXED_DT) == i
	return {axes = axes, actions = seg.get("actions", []) if first else []}


func _physics_process(_delta: float) -> void:
	if vehicle == null:
		return
	var done := vehicle.get_steps()
	if done > 0 and ((done - 1) % trace_every == 0 or done == total_steps) and (frames.is_empty() or frames[-1].step != done - 1):
		_record(done - 1)
	if done >= total_steps:
		_write()
		return

	var inp := _inputs_at(done)
	for action in inp.actions:
		match action:
			"gearUp": vehicle.shift_up()
			"gearDown": vehicle.shift_down()
			"lock": vehicle.cycle_lock()
			"range": vehicle.toggle_range()
	var a: Dictionary = inp.axes
	vehicle.set_input(a.throttle, a.brake, a.handbrake, a.steer, a.winch)


func _record(step: int) -> void:
	var t := vehicle.telemetry()
	var p := vehicle.global_position
	var wheels := []
	for k in 4:
		var w := vehicle.wheel(k)
		wheels.append({travel = w.travel, load = w.load, omega = w.omega, contact = w.contact, surface = w.surface})
	frames.append({
		step = step, t = (step + 1) * FIXED_DT, pos = [p.x, p.y, p.z],
		speed = t.speed, forwardSpeed = t.forward_speed, heading = t.heading, pitch = t.pitch, roll = t.roll,
		rpm = t.rpm, gear = t.gear, airborne = t.airborne, stuck = t.stuck, slip = t.slip, wheels = wheels,
	})


func _write() -> void:
	var doc := {
		format = "ridgeline-replay-trace/1", build = "native", replay = replay_id,
		vehicle = GameData.vehicle(GameData.load_json("vehicles"), int(replay.vehicle)).id,
		seed = int(replay.seed), spawn = [spawn.x, spawn.y, spawn.z], fixedDt = FIXED_DT,
		engine = Engine.get_version_info().string,
		physics = ProjectSettings.get_setting("physics/3d/physics_engine"),
		frames = frames,
	}
	var f := FileAccess.open(trace_path, FileAccess.WRITE)
	if f == null:
		_quit(2, "cannot write %s" % trace_path)
		return
	f.store_string(JSON.stringify(doc))
	f.close()
	var last: Dictionary = frames[-1]
	print("ridgeline: replay %s wrote %s (%d samples) end pos (%.2f, %.2f, %.2f) %.1f km/h gear %d rpm %.0f" % [
		replay_id, trace_path, frames.size(), last.pos[0], last.pos[1], last.pos[2],
		last.forwardSpeed * 3.6, last.gear, last.rpm])
	_quit(0, "")


func _quit(code: int, msg: String) -> void:
	if msg != "":
		printerr("ridgeline: replay: ", msg)
	vehicle = null
	get_tree().quit(code)
