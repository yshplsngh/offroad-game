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
#   --drive            with --smoke: floor it and turn (diff locked) instead of braking
#   --zoom=K           with --smoke: chase camera distance multiplier (0.4 = close-up)
#   --stream-workers=N terrain build threads (default: cores - 2, max 4)
#   --screenshot=PATH  with --smoke: save the last rendered frame as PNG
#   --capture=PATH     with --smoke: write a frame capture (bench/README.md format)
#   --replay=ID --trace=PATH  run a fixed driving replay headless and write its trace
extends Node3D

const SETTINGS_PATH := "user://settings.cfg"

var seed := 1337
var vehicle_index := 0
var smoke := false
var roll_test := false
var _rolled := false
var smoke_chase := false
var smoke_orbit := 0.0
var smoke_zoom := 1.0
var smoke_drive := false
var _drive_ticks := 0
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
var engine_audio: EngineAudio
var tire_audio: TireAudio
var wheel_fx: WheelFX
var ruts: RutTrail
var sky: SkyCycle
var systems: VehicleSystems
var cable: WinchCable
var lights_on := false
var _was_flooded := false
var _was_empty := false
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
var _reverse_timer := 0.0
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
		elif arg == "--drive":
			smoke_drive = true
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
		var game_tune := "--game-tune" in OS.get_cmdline_user_args()
		add_child(ReplayRunner.new(replay_id,
				trace_path if trace_path != "" else "user://replay.json", game_tune))
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
	# Keep receiving input while the tree is paused: without this, pausing
	# disables the input node too and nothing can ever unpause the game.
	input.process_mode = Node.PROCESS_MODE_ALWAYS
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
	# Mouse look in play: captured mouse orbits/elevates the chase camera.
	# Pause (P/Esc) releases the cursor.
	if not smoke and DisplayServer.get_name() != "headless":
		chase.mouse_look = true
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	# Procedural engine note (R1) + surface rolling noise (R2); no device headless.
	if DisplayServer.get_name() != "headless":
		engine_audio = EngineAudio.new()
		engine_audio.name = "EngineAudio"
		add_child(engine_audio)
		tire_audio = TireAudio.new()
		tire_audio.name = "TireAudio"
		add_child(tire_audio)

	var cf := ConfigFile.new()
	if cf.load(SETTINGS_PATH) == OK:
		chase.sensitivity = clampf(float(cf.get_value("input", "mouse_sensitivity", 1.0)), 0.2, 3.0)
		if engine_audio:
			engine_audio.volume = clampf(float(cf.get_value("audio", "engine_volume", 0.7)), 0.0, 1.0)
			tire_audio.volume = engine_audio.volume * 0.9
	hud.set_sensitivity(chase.sensitivity)
	hud.set_volume(engine_audio.volume if engine_audio else 0.7)
	var names := PackedStringArray()
	for v in vehicles_data.vehicles:
		names.append(v.name)
	hud.set_vehicles(names)
	hud.sensitivity_changed.connect(_on_sensitivity_changed)
	hud.resume_pressed.connect(func() -> void: _set_paused(false))
	hud.menu_toggled.connect(func() -> void: _set_paused(not paused))
	hud.camera_selected.connect(func(m: String) -> void: chase.mode = m)
	hud.vehicle_selected.connect(_on_menu_vehicle)
	hud.lights_toggled.connect(func(on: bool) -> void: lights_on = on)
	hud.volume_changed.connect(func(v: float) -> void:
		if engine_audio:
			engine_audio.volume = v
			tire_audio.volume = v * 0.9
		_save_setting("audio", "engine_volume", v))
	hud.time_changed.connect(func(h: float) -> void: sky.set_time(h / 24.0))
	hud.recover_pressed.connect(func() -> void:
		if vehicle.flip():
			systems.service()
			hud.say("recovered & serviced - resume to see it", 1.5)
		else:
			hud.say("recovery cooling down", 1.5))
	hud.quit_pressed.connect(func() -> void: get_tree().quit())

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
	# Place the rig now: swapping vehicles from the pause menu happens with
	# _process stopped, and the new visual would sit at the origin until resume.
	visual.global_transform = vehicle.global_transform
	# Wheel dust/mud/spray emitters at the new hubs (REALISM.md R1).
	if wheel_fx == null:
		wheel_fx = WheelFX.new()
		wheel_fx.name = "WheelFX"
		add_child(wheel_fx)
	wheel_fx.setup(vehicle, spec)
	# Visual-only wheel ruts in soft ground (REALISM.md R2).
	if ruts == null:
		ruts = RutTrail.new()
		ruts.name = "RutTrail"
		add_child(ruts)
	ruts.setup(vehicle, spec, field)
	# Fording/damage/fuel systems + the rendered winch rope (REALISM.md R3).
	if systems == null:
		systems = VehicleSystems.new()
		systems.name = "Systems"
		add_child(systems)
	systems.configure(spec, field)
	if cable == null:
		cable = WinchCable.new()
		cable.name = "WinchCable"
		add_child(cable)
	cable.setup(vehicle, spec)
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
	sun.light_energy = 1.35
	sun.shadow_enabled = false  # no real-time shadows in the default game
	add_child(sun)

	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color(0.32, 0.5, 0.78)
	sky_mat.sky_horizon_color = Color(0.72, 0.78, 0.84)
	sky_mat.ground_horizon_color = Color(0.5, 0.52, 0.5)
	var sky_res := Sky.new()
	sky_res.sky_material = sky_mat
	sky_res.process_mode = Sky.PROCESS_MODE_QUALITY  # sky lighting updates only when the sky changes

	var env := Environment.new()
	env.background_mode = Environment.BG_SKY
	env.sky = sky_res
	env.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.fog_enabled = true
	env.fog_light_color = Color(0.68, 0.74, 0.8)
	# R2 shading pass: the old 0.0011 density greyed the whole midground out.
	env.fog_density = 0.0007
	env.fog_sky_affect = 0.3
	env.adjustment_enabled = true
	env.adjustment_contrast = 1.06
	env.adjustment_saturation = 1.15

	var we := WorldEnvironment.new()
	we.environment = env
	add_child(we)

	# Time of day (REALISM.md R2): sun per frame, sky pushed low-frequency.
	sky = SkyCycle.new()
	sky.name = "SkyCycle"
	sky.setup(sun, sky_mat, env)
	add_child(sky)


func _build_ui() -> void:
	hud = GameHud.new()
	add_child(hud)
	overlay = Label.new()
	overlay.text = "Ridgeline Offroad\nloading visible terrain..."
	overlay.set_anchors_preset(Control.PRESET_CENTER)
	hud.add_child(overlay)


func _on_action(name: String) -> void:
	# While paused only pause/click (and the overlays) act; gears, recovery and
	# the rest stay frozen with the game.
	if paused and name not in ["pause", "click", "help", "debug"]:
		return
	match name:
		"click":
			# Left click toggles the menu. While driving the cursor is captured
			# for mouse look, so there is nothing to aim at the hamburger with -
			# and no other play action uses the button - so any click opens the
			# menu. With the cursor free, a click off the panel resumes (panel
			# clicks are consumed by the GUI and never get here).
			if chase == null or not chase.mouse_look:
				return
			if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
				_set_paused(true)
			elif paused:
				_set_paused(false)
			else:
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
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
			if vehicle.flip():
				systems.service()  # recovery is the field service (R3)
				hud.say("recovered & serviced", 1.2)
			else:
				hud.say("recovery cooling down", 1.2)
		"winch":
			hud.say("winch anchored - hold G to reel in" if vehicle.winch_attach() else "winch released")
		"camera":
			hud.say("camera: %s" % chase.cycle_mode(), 0.9)
		"lights":
			lights_on = not lights_on
			hud.say("lights on" if lights_on else "lights off", 0.9)
		"nextVehicle":
			var p := vehicle.global_position
			var h: float = vehicle.telemetry().heading
			_spawn_vehicle(vehicle_index + 1, p.x, p.z, h)
			hud.say(GameData.vehicle(vehicles_data, vehicle_index).name, 2.0)
		"pause":
			_set_paused(not paused)
		"debug":
			hud.stats.visible = not hud.stats.visible
		"help":
			hud.help.visible = not hud.help.visible


func _set_paused(p: bool) -> void:
	paused = p
	get_tree().paused = p
	if chase and chase.mouse_look:
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if p else Input.MOUSE_MODE_CAPTURED
	if p:
		# The menu always opens showing reality.
		hud.sync_menu(vehicle_index, chase.mode, lights_on, sky.hours() if sky else 9.0)
	hud.pause_menu.visible = p
	hud.say("paused" if p else "resumed")


func _on_menu_vehicle(index: int) -> void:
	if index == vehicle_index:
		return
	var p := vehicle.global_position
	var h: float = vehicle.telemetry().heading
	_spawn_vehicle(index, p.x, p.z, h)
	hud.say(GameData.vehicle(vehicles_data, vehicle_index).name, 2.0)


func _on_sensitivity_changed(v: float) -> void:
	chase.sensitivity = v
	_save_setting("input", "mouse_sensitivity", v)


func _save_setting(section: String, key: String, value: Variant) -> void:
	var cf := ConfigFile.new()
	cf.load(SETTINGS_PATH)  # keep any other sections; a missing file is fine
	cf.set_value(section, key, value)
	cf.save(SETTINGS_PATH)


func _physics_process(_delta: float) -> void:
	if vehicle == null:
		return
	if smoke:
		# Foot on the brake: in low first the truck creeps at idle, like the reference.
		if smoke_drive:
			_drive_ticks += 1
			if _drive_ticks == 1:
				vehicle.cycle_lock()
				vehicle.cycle_lock()
			vehicle.set_input(1, 0, 0, 0.0 if _drive_ticks < 120 else 0.7, 0)
		else:
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
	# Automatic reverse (game layer): holding S near standstill shifts down to
	# R and backs up, like every offroad game; W then brakes, and W from a
	# reverse stop shifts back up to first. Q/E manual shifting is untouched -
	# this only auto-shifts within ~walking pace of a stop.
	var tel := vehicle.telemetry()
	var gear := int(tel.gear)
	var eff_throttle := input.throttle
	var eff_brake := input.brake
	if gear == -1:
		eff_throttle = input.brake  # S backs up in R
		eff_brake = input.throttle  # W brakes
		_reverse_timer = 0.0
		if input.throttle > 0.0 and absf(tel.forward_speed) < 0.4:
			vehicle.shift_up()      # W from a reverse stop: R -> N (-> 1 below)
			eff_throttle = 0.0
			eff_brake = 1.0
	elif gear == 0 and input.throttle > 0.0:
		vehicle.shift_up()          # N with throttle: into first
	elif input.brake > 0.0 and absf(tel.forward_speed) < 0.4:
		_reverse_timer += get_physics_process_delta_time()
		if _reverse_timer > 0.25:
			vehicle.shift_down()    # one step per tick: 1 -> N -> R
	else:
		_reverse_timer = 0.0
	# Auto-hold (game layer, the physics stays the exact port): with clutchCreep
	# an automatic never stands still - at spawn the truck wandered off on its
	# own with the tires turning and jittering forever at idle. Holding the
	# brake makes the model clamp wheel omega to zero, so the tires actually
	# stop. Throttle (or winching) releases it instantly; off throttle it
	# re-engages after dawdling below walking pace - the threshold must sit
	# above the ~1-1.7 m/s the idle creep sustains on its own, or a truck that
	# has driven once never parks again.
	if eff_throttle > 0.0 or input.winch > 0.0:
		_hold = false
		_hold_timer = 0.0
	elif not _hold:
		if absf(tel.speed) < 2.0:
			_hold_timer += get_physics_process_delta_time()
			if _hold_timer > 1.2:
				_hold = true
		else:
			_hold_timer = 0.0
	# Vehicle systems (R3): a flooded, empty or beaten engine delivers less -
	# consequences arrive through the inputs, never by editing the physics.
	systems.update(tel, eff_throttle, vehicle.global_position, get_physics_process_delta_time())
	if systems.flooded != _was_flooded:
		_was_flooded = systems.flooded
		if systems.flooded:
			hud.say("ENGINE FLOODED - winch out (F/G) and let it dry, or R to service", 3.5)
		else:
			hud.say("engine dried out", 1.5)
	if (systems.fuel <= 0.0) != _was_empty:
		_was_empty = systems.fuel <= 0.0
		if _was_empty:
			hud.say("OUT OF FUEL - R to service", 3.5)
	vehicle.set_input(eff_throttle * systems.throttle_scale(),
			maxf(eff_brake, 1.0 if _hold else 0.0),
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
	# Realism drive (REALISM.md R1+R2): lights answer the pedals (and the
	# night), the body carries the mud it drove through, the engine answers
	# rpm/throttle, the tires answer the surface.
	var tel := vehicle.telemetry()
	var head := lights_on or (sky != null and sky.night > 0.45)
	visual.set_lights(head, input.brake > 0.0 or _hold, int(tel.gear) == -1,
			head and bool(tel.low_range))
	visual.update_dirt(float(tel.mud), int(tel.surface), delta)
	if engine_audio:
		# R3 conditions: water muffles then chokes the engine, damage rattles
		# it, no fuel or a flooded intake kills it; the winch rope creaks.
		var sputter := clampf((systems.submersion - 0.6) / 0.4, 0.0, 1.0)
		engine_audio.update(float(tel.rpm), input.throttle, delta,
				systems.submersion, sputter, systems.damage, systems.running())
		tire_audio.update(float(tel.speed), float(tel.slip), int(tel.surface), delta,
				float(tel.winch_tension) if tel.winch else 0.0)
	tel["fuel"] = systems.fuel
	tel["damage"] = systems.damage
	tel["depth"] = systems.depth
	hud.update_hud(tel, delta, stats_text)

	if smoke and _elapsed > (8.0 if roll_test else 6.0):
		var t := vehicle.telemetry()
		# < 1 m/s: the heavy trucks inherit a slow brake-held creep from the reference.
		# Rolled, the wheels point anywhere (airborne stays set) - resting on the
		# ground patch near the analytic surface is what passes. --drive is a
		# screenshots-in-motion run: it never settles by design.
		var p := vehicle.global_position
		var above: bool = p.y > field.height(p.x, p.z) - 1.0
		var settled: bool = smoke_drive or (absf(t.speed) < 1.0 and (t.airborne == 0 or roll_test) and above)
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
