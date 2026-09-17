# main.gd - native entry point: the milestone-1 vertical slice.
#
# Boot order follows PLAN.md: the vehicle and only the camera-visible terrain
# cells are built behind a loading overlay, then the rest streams.
# Command-line options (after `--`):
#   --seed=N      world seed (default 1337, same as the browser reference)
#   --vehicle=N   0-3, catalog order (default 0)
#   --smoke       camera turns in place at spawn for a few seconds while the
#                 truck sits on the brake; print stats and quit (replay
#                 turn-in-place-v1). Exit 3 if the truck did not settle.
#   --roll        with --smoke: half-roll the truck onto its roof at t=1s;
#                 settling then means the chassis rests on the ground patch
#                 instead of falling through the world
#   --camera=chase     with --smoke: keep the chase camera on the truck instead
#   --orbit=DEG        with --smoke: chase camera orbited around the truck (90 = side, 180 = front)
#   --zoom=K           with --smoke: chase camera distance multiplier (0.4 = close-up)
#   --stream-workers=N terrain build threads (default: cores - 2, max 4)
#   --screenshot=PATH  with --smoke: save the last rendered frame as PNG
#   --capture=PATH     with --smoke: write a frame capture (bench/README.md format)
#   --replay=ID --trace=PATH  run a fixed driving replay headless and write its trace
extends Node3D

var seed := 1337
var vehicle_index := 0
var smoke := false
var roll_test := false
var _rolled := false
var smoke_chase := false
var smoke_orbit := 0.0
var smoke_zoom := 1.0
var stream_workers := 0
var screenshot := ""
var capture_path := ""
var capture: FrameCapture
var _worst_ms := 0.0

var field: TerrainField
var terrain: TerrainStreamer
var vegetation: VegetationStreamer
var vehicle: OffroadVehicle
var patch: GroundPatch
var visual: VehicleVisual
var input: VehicleInput
var hud: GameHud
var chase: ChaseCamera
var smoke_camera: Camera3D
var overlay: Label

var vehicles_data: Dictionary
var tune_data: Dictionary
var paused := false
var _hold := true         # auto-hold: parked until the first throttle
var _hold_timer := 0.0
var _frames := 0
var _elapsed := 0.0


func _ready() -> void:
	var replay_id := ""
	var trace_path := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--replay="):
			replay_id = arg.get_slice("=", 1)
		elif arg.begins_with("--trace="):
			trace_path = arg.get_slice("=", 1)
		elif arg.begins_with("--seed="):
			seed = int(arg.get_slice("=", 1))
		elif arg.begins_with("--vehicle="):
			vehicle_index = int(arg.get_slice("=", 1))
		elif arg == "--smoke":
			smoke = true
		elif arg == "--roll":
			roll_test = true
		elif arg.begins_with("--stream-workers="):
			stream_workers = int(arg.get_slice("=", 1))
		elif arg == "--camera=chase":
			smoke_chase = true
		elif arg.begins_with("--zoom="):
			smoke_zoom = float(arg.get_slice("=", 1))
		elif arg.begins_with("--orbit="):
			smoke_chase = true
			smoke_orbit = deg_to_rad(float(arg.get_slice("=", 1)))
		elif arg.begins_with("--screenshot="):
			screenshot = arg.get_slice("=", 1)
		elif arg.begins_with("--capture="):
			capture_path = arg.get_slice("=", 1)

	if not ClassDB.class_exists("TerrainField") or not ClassDB.class_exists("OffroadVehicle"):
		_fail("worldcore GDExtension did not load (TerrainField/OffroadVehicle missing)")
		return

	if replay_id != "":
		add_child(ReplayRunner.new(replay_id, trace_path if trace_path != "" else "user://replay.json"))
		return

	field = TerrainField.new()
	field.seed = seed
	vehicles_data = GameData.load_json("vehicles")
	tune_data = GameData.load_json("tune")

	_build_environment()
	_build_ui()

	var spawn := field.find_spawn(0.0, 0.0)
	_spawn_vehicle(vehicle_index, spawn.x, spawn.z, 0.0)

	input = VehicleInput.new()
	input.name = "Input"
	add_child(input)
	input.action.connect(_on_action)

	chase = ChaseCamera.new()
	chase.far = 1400.0
	chase.near = 0.1
	chase.target = vehicle
	chase.field = field
	chase.orbit = smoke_orbit
	chase.zoom = smoke_zoom
	add_child(chase)
	chase.snap()

	if smoke and not smoke_chase:
		smoke_camera = Camera3D.new()
		smoke_camera.far = 1400.0
		smoke_camera.fov = 70.0
		add_child(smoke_camera)
		smoke_camera.position = spawn + Vector3(0, 6, 0)
		smoke_camera.rotation = Vector3(-0.18, 0, 0)
		smoke_camera.make_current()

	# Native streaming (worldcore StreamScheduler): frustum cells on worker
	# threads, budgeted attaches and retirements on this thread.
	terrain = TerrainStreamer.new()
	terrain.name = "Terrain"
	terrain.terrain = field
	var ground := ShaderMaterial.new()
	ground.shader = load("res://shaders/terrain.gdshader")
	if not GroundTextures.apply(ground):
		_fail("ground textures missing (res://generated/ground.bin): run the worldcore_bake build step")
		return
	terrain.material = ground
	terrain.set_camera(_active_camera())
	if stream_workers > 0:
		terrain.workers = stream_workers
	add_child(terrain)

	# Static vegetation per visible cell: baked prototypes, one multimesh per
	# cell + species + LOD + variant, no wind, no shadows.
	vegetation = VegetationStreamer.new()
	vegetation.name = "Vegetation"
	vegetation.terrain = field
	var solid := ShaderMaterial.new()
	solid.shader = load("res://shaders/foliage_solid.gdshader")
	var leaf := ShaderMaterial.new()
	leaf.shader = load("res://shaders/foliage_leaf.gdshader")
	vegetation.solid_material = solid
	vegetation.leaf_material = leaf
	vegetation.set_camera(_active_camera())
	if stream_workers > 0:
		vegetation.workers = stream_workers
	add_child(vegetation)

	var t0 := Time.get_ticks_usec()
	var boot_cells := terrain.prime()
	var boot_veg := vegetation.prime()
	var boot_ms := (Time.get_ticks_usec() - t0) / 1000.0
	if boot_veg < 0:
		_fail("vegetation prototypes missing (res://generated/flora.bin): run the worldcore_bake build step")
		return
	overlay.visible = false
	if capture_path != "":
		capture = FrameCapture.new(get_viewport(), "turn-in-place-v1", seed)
		capture.boot_ms = boot_ms
		capture.boot_cells = boot_cells
	hud.say("%s - press / for controls" % GameData.vehicle(vehicles_data, vehicle_index).name, 4.0)
	print("ridgeline: seed=%d spawn=%s boot_cells=%d boot_vegetation_cells=%d boot_ms=%.1f renderer=%s" % [
		seed, spawn, boot_cells, boot_veg, boot_ms,
		ProjectSettings.get_setting("rendering/renderer/rendering_method")])


func _active_camera() -> Camera3D:
	return smoke_camera if smoke_camera != null else chase


func _spawn_vehicle(index: int, x: float, z: float, heading: float) -> void:
	if vehicle != null:
		vehicle.queue_free()
		visual.queue_free()
		if visual.shadow:
			visual.shadow.queue_free()
	vehicle_index = posmod(index, vehicles_data.vehicles.size())
	var spec := GameData.vehicle(vehicles_data, vehicle_index)
	vehicle = OffroadVehicle.new()
	vehicle.name = "Vehicle"
	add_child(vehicle)
	vehicle.configure(spec, tune_data, field)
	vehicle.spawn(x, z, heading)
	# Chassis heightfield patch: the only ground geometry in the physics world.
	# Wheels stay on the analytic queries; this catches a rolled or bottoming
	# chassis that previously fell through.
	if patch == null:
		patch = GroundPatch.new()
		patch.name = "GroundPatch"
		add_child(patch)
	patch.configure(field, vehicle)
	_hold = true  # a fresh truck spawns parked
	_hold_timer = 0.0
	visual = VehicleVisual.new()
	visual.name = "VehicleVisual"
	add_child(visual)
	if not visual.setup(vehicle, spec, field):
		_fail("vehicle meshes missing (res://generated/vehicles.bin): run the worldcore_bake build step")
	if chase:
		chase.target = vehicle


func _fail(msg: String) -> void:
	push_error(msg)
	printerr("ridgeline: FATAL ", msg)
	if smoke or DisplayServer.get_name() == "headless":
		get_tree().quit(2)
	else:
		var l := Label.new()
		l.text = "STARTUP ERROR\n" + msg
		add_child(l)


func _build_environment() -> void:
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(deg_to_rad(-38), deg_to_rad(35), 0)
	sun.light_energy = 1.25
	sun.shadow_enabled = false  # no real-time shadows in the default game
	add_child(sun)

	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.32, 0.5, 0.78)
	sky_mat.sky_horizon_color = Color(0.72, 0.78, 0.84)
	sky_mat.ground_horizon_color = Color(0.5, 0.52, 0.5)
	var sky := Sky.new()
	sky.sky_material = sky_mat
	sky.process_mode = Sky.PROCESS_MODE_QUALITY  # sky lighting updates only when the sky changes

	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.fog_enabled = true
	env.fog_light_color = Color(0.68, 0.74, 0.8)
	env.fog_density = 0.0011
	env.fog_sky_affect = 0.3

	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)


func _build_ui() -> void:
	hud = GameHud.new()
	add_child(hud)
	overlay = Label.new()
	overlay.text = "Ridgeline Offroad\nloading visible terrain..."
	overlay.set_anchors_preset(Control.PRESET_CENTER)
	hud.add_child(overlay)


func _on_action(name: String) -> void:
	match name:
		"gearUp":
			if vehicle.shift_up(): hud.say("gear %s" % vehicle.telemetry().gear_name, 0.8)
		"gearDown":
			if vehicle.shift_down(): hud.say("gear %s" % vehicle.telemetry().gear_name, 0.8)
		"lock":
			vehicle.cycle_lock()
			hud.say("diff lock: %s" % vehicle.telemetry().lock)
		"range":
			if vehicle.toggle_range():
				hud.say("LOW range" if vehicle.telemetry().low_range else "HIGH range")
			else:
				hud.say("slow down to change range", 1.2)
		"recover":
			hud.say("recovered" if vehicle.flip() else "recovery cooling down", 1.2)
		"winch":
			hud.say("winch anchored - hold G to reel in" if vehicle.winch_attach() else "winch released")
		"camera":
			hud.say("camera: %s" % chase.cycle_mode(), 0.9)
		"nextVehicle":
			var p := vehicle.global_position
			var h: float = vehicle.telemetry().heading
			_spawn_vehicle(vehicle_index + 1, p.x, p.z, h)
			hud.say(GameData.vehicle(vehicles_data, vehicle_index).name, 2.0)
		"pause":
			paused = not paused
			get_tree().paused = paused
			hud.say("paused" if paused else "resumed")
		"debug":
			hud.stats.visible = not hud.stats.visible
		"help":
			hud.help.visible = not hud.help.visible


func _physics_process(_delta: float) -> void:
	if vehicle == null:
		return
	if smoke:
		# Foot on the brake: in low first the truck creeps at idle, like the reference.
		vehicle.set_input(0, 1, 0, 0, 0)
		if roll_test and not _rolled and _elapsed > 1.0:
			# Half-roll with hangtime so the truck comes down on its roof/side:
			# only the chassis can catch it there, and without the ground patch
			# it fell through the world.
			_rolled = true
			vehicle.angular_velocity = Vector3(0, 0, 4.0)
			vehicle.linear_velocity = Vector3(0, 5.0, 0)
		return
	input.poll()
	# Auto-hold (game layer, the physics stays the exact port): with clutchCreep
	# an automatic never stands still - at spawn the truck wandered off on its
	# own with the tires turning and jittering forever at idle. Holding the
	# brake makes the model clamp wheel omega to zero, so the tires actually
	# stop. Throttle (or winching) releases it instantly; off throttle it
	# re-engages after dawdling below walking pace - the threshold must sit
	# above the ~1-1.7 m/s the idle creep sustains on its own, or a truck that
	# has driven once never parks again.
	if input.throttle > 0.0 or input.winch > 0.0:
		_hold = false
		_hold_timer = 0.0
	elif not _hold:
		if absf(vehicle.telemetry().speed) < 2.0:
			_hold_timer += get_physics_process_delta_time()
			if _hold_timer > 1.2:
				_hold = true
		else:
			_hold_timer = 0.0
	vehicle.set_input(input.throttle, maxf(input.brake, 1.0 if _hold else 0.0),
			input.handbrake, input.steer, input.winch)


func _process(delta: float) -> void:
	if terrain == null:
		return
	_frames += 1
	_elapsed += delta

	if smoke_camera != null:
		smoke_camera.rotation.y += delta * 0.9  # must stream cells in, then retire them

	var frame_ms := delta * 1000.0
	if _frames > 10:
		_worst_ms = maxf(_worst_ms, frame_ms)
	terrain.set_prefetch_velocity(vehicle.linear_velocity)
	vegetation.set_prefetch_velocity(vehicle.linear_velocity)
	var ts := terrain.stats()
	var vs := vegetation.stats()
	var live: int = ts.get("live", 0)
	var missing: int = ts.get("missing_visible", 0)
	if capture != null:
		capture.record(delta, live, missing, ts.get("attach_ms", 0.0))

	var stats_text := ""
	if hud.stats.visible or smoke:
		var p := vehicle.global_position
		var s := field.sample(p.x, p.z)
		var ps := patch.stats()
		stats_text = "fps %d | draws %d | cells %d (+%d missing, %d queued) | built %d retired %d stale %d | p95 %.1f ms attach %.2f ms | veg %d cells %d batches %d stems | patch %d refills %.2f ms | biome %d" % [
			Engine.get_frames_per_second(),
			RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME),
			live, missing, ts.queued, ts.jobs_built, ts.retired, ts.cancelled + ts.dropped_stale,
			ts.rolling_p95_ms, ts.attach_ms, vs.live_cells, vs.live_batches, vs.live_instances,
			ps.refills, ps.worst_refill_ms, s.biome]
	hud.update_hud(vehicle.telemetry(), delta, stats_text)

	if smoke and _elapsed > (8.0 if roll_test else 6.0):
		var t := vehicle.telemetry()
		# < 1 m/s: the heavy trucks inherit a slow brake-held creep from the reference.
		# Rolled, the wheels point anywhere (airborne stays set) - resting on the
		# ground patch near the analytic surface is what passes.
		var p := vehicle.global_position
		var above: bool = p.y > field.height(p.x, p.z) - 1.0
		var settled: bool = absf(t.speed) < 1.0 and (t.airborne == 0 or roll_test) and above
		var ps := patch.stats()
		print("ridgeline: smoke %s frames=%d avg_fps=%.1f worst_ms=%.1f cells=%d built=%d retired=%d stale=%d missing=%d worst_attach_ms=%.2f p95_cost_ms=%.1f deferred=%d veg_cells=%d veg_batches=%d veg_instances=%d veg_missing=%d veg_worst_attach_ms=%.2f draws=%d vehicle_steps=%d vehicle_speed=%.3f airborne=%d pos_y=%.2f ground=%.2f patch_refills=%d patch_worst_ms=%.2f" % [
			"ok" if settled else "FAILED (vehicle did not settle)",
			_frames, _frames / _elapsed, _worst_ms, live, ts.jobs_built,
			ts.retired, ts.cancelled + ts.dropped_stale, missing, ts.worst_attach_ms,
			ts.rolling_p95_ms, ts.deferred_over_budget,
			vs.live_cells, vs.live_batches, vs.live_instances, vs.missing_visible, vs.worst_attach_ms,
			RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME),
			vehicle.get_steps(), t.speed, t.airborne,
			p.y, field.height(p.x, p.z), ps.refills, ps.worst_refill_ms])
		if capture != null:
			capture.save(capture_path)
		if screenshot != "" and DisplayServer.get_name() != "headless":
			get_viewport().get_texture().get_image().save_png(screenshot)
		get_tree().quit(0 if settled else 3)
		set_process(false)
