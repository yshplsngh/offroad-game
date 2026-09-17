# vehicle_visual.gd - the procedural truck from the build-time bake, rigged.
#
# Static parts are one merged mesh per LOD (a few surfaces, split by material
# layer). Moving parts follow the physics each frame: axles rise and roll with
# suspension travel, front wheels steer, tires spin, links and coilovers aim at
# the axles, the steering wheel turns. Three LODs switch by camera distance with
# engine visibility ranges, so there is no per-frame LOD script cost.
# Also owns the blob shadow (PLAN.md: no shadow maps).
class_name VehicleVisual
extends Node3D

## Render layer for vehicle meshes; the blob shadow decal skips it.
const VEHICLE_LAYER := 1 << 1
## [begin, end] metres from the camera for LOD 2 (near), 1 (mid), 0 (far).
const LOD_RANGES := [[45.0, 0.0], [14.0, 45.0], [0.0, 14.0]]

static var _library: VehicleMeshLibrary
static var _materials := {}

var vehicle: OffroadVehicle
var field: TerrainField
var spec: Dictionary
var rig: Dictionary
var shadow: Decal
var paint_material: ShaderMaterial
var lamp_material: ShaderMaterial

var _axles: Array[Node3D] = []        # front, rear
var _wheel_pivots: Array[Node3D] = [] # FL FR RL RR (steer)
var _wheel_spinners: Array[Node3D] = []
var _links: Array = []                # {node, pivot, target, axle}
var _shocks: Array = []               # {body, shaft, spring, mount, anchor, axle}
var _steering: Node3D


static func library() -> VehicleMeshLibrary:
	if _library == null:
		_library = VehicleMeshLibrary.new()
		if not _library.load("res://generated/vehicles.bin"):
			_library = null
	return _library


static func _material(key: String) -> ShaderMaterial:
	if not _materials.has(key):
		var m := ShaderMaterial.new()
		match key:
			"opaque", "paint":
				m.shader = load("res://shaders/vehicle.gdshader")
			"glass":
				m.shader = load("res://shaders/vehicle_glass.gdshader")
			"lamp":
				m.shader = load("res://shaders/vehicle_lamp.gdshader")
		_materials[key] = m
	return _materials[key]


func setup(p_vehicle: OffroadVehicle, p_spec: Dictionary, p_field: TerrainField) -> bool:
	vehicle = p_vehicle
	spec = p_spec
	field = p_field
	var lib := library()
	if lib == null:
		return false
	rig = lib.get_rig(spec.id)

	# Per-vehicle paint and lamps; opaque and glass materials are shared.
	paint_material = _material("paint").duplicate()
	paint_material.set_shader_parameter("is_paint", true)
	paint_material.set_shader_parameter("paint_color", Color.html(spec.body.color))
	lamp_material = _material("lamp").duplicate()

	var root := Node3D.new()
	root.name = "Rig"
	add_child(root)

	var axle_y: float = rig.axle_y
	var track: float = rig.track
	for a in 2:
		var axle := Node3D.new()
		axle.name = "Axle%d" % a
		axle.position = Vector3(0, axle_y, rig.axle_z[a])
		root.add_child(axle)
		_axles.append(axle)
	for i in 4:
		var side := -1.0 if i % 2 == 0 else 1.0
		var pivot := Node3D.new()
		pivot.position = Vector3(side * track / 2.0, 0, 0)
		_axles[0 if i < 2 else 1].add_child(pivot)
		var spinner := Node3D.new()
		pivot.add_child(spinner)
		_wheel_pivots.append(pivot)
		_wheel_spinners.append(spinner)
	for l in rig.links:
		var n := Node3D.new()
		root.add_child(n)
		_links.append({node = n, pivot = l.pivot, target = l.target, axle = l.axle, part = l.part})
	for s in rig.shocks:
		var body := Node3D.new()
		var shaft := Node3D.new()
		var spring := Node3D.new()
		root.add_child(body)
		root.add_child(shaft)
		root.add_child(spring)
		_shocks.append({body = body, shaft = shaft, spring = spring, mount = s.mount, anchor = s.anchor, axle = s.axle})
	_steering = Node3D.new()
	_steering.position = rig.steering_pos
	_steering.rotation.x = rig.steering_tilt
	root.add_child(_steering)

	for lod in 3:
		var parts: Dictionary = lib.build_parts(spec.id, lod)
		var range: Array = LOD_RANGES[lod]
		_add_mesh(root, parts.chassis, range)
		_add_mesh(_axles[0], parts.axle_front, range)
		_add_mesh(_axles[1], parts.axle_rear, range)
		for i in 4:
			var left := i % 2 == 0
			_add_mesh(_wheel_pivots[i], parts.brake_left if left else parts.brake_right, range)
			_add_mesh(_wheel_spinners[i], parts.wheel_left if left else parts.wheel_right, range)
		for l in _links:
			var mesh: ArrayMesh = [parts.link_lower, parts.link_upper, parts.link_panhard][l.part]
			_add_mesh(l.node, mesh, range)
		for s in _shocks:
			_add_mesh(s.body, parts.shock_body, range)
			_add_mesh(s.shaft, parts.shock_shaft, range)
			_add_mesh(s.spring, parts.spring, range)
		if lod > 0:
			_add_mesh(_steering, parts.steering_wheel, range)

	_build_shadow()
	return true


func _add_mesh(parent: Node3D, mesh: ArrayMesh, range: Array) -> void:
	var inst := MeshInstance3D.new()
	inst.mesh = mesh
	inst.layers = VEHICLE_LAYER
	inst.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	inst.visibility_range_begin = range[0]
	inst.visibility_range_end = range[1]
	var layers: Array = mesh.get_meta("layers", [])
	for s in layers.size():
		match int(layers[s]):
			0: inst.set_surface_override_material(s, _material("opaque"))
			1: inst.set_surface_override_material(s, paint_material)
			2: inst.set_surface_override_material(s, _material("glass"))
			3: inst.set_surface_override_material(s, lamp_material)
	parent.add_child(inst)


func _build_shadow() -> void:
	var phys: Dictionary = spec.physics
	var box: Dictionary = phys.chassisBox
	var half := Vector3(box.halfExtents[0], box.halfExtents[1], box.halfExtents[2])
	shadow = Decal.new()
	var tex := GradientTexture2D.new()
	tex.fill = GradientTexture2D.FILL_RADIAL
	tex.fill_from = Vector2(0.5, 0.5)
	tex.fill_to = Vector2(1.0, 0.5)
	var g := Gradient.new()
	g.set_color(0, Color(0, 0, 0, 0.72))
	g.set_color(1, Color(0, 0, 0, 0))
	tex.gradient = g
	shadow.texture_albedo = tex
	shadow.size = Vector3(half.x * 2.6, 3.0, half.z * 2.3)
	shadow.cull_mask = 0xFFFFF & ~VEHICLE_LAYER
	shadow.upper_fade = 0.2
	shadow.lower_fade = 0.2
	get_parent().add_child.call_deferred(shadow)


func set_paint(color: Color) -> void:
	paint_material.set_shader_parameter("paint_color", color)


func set_lights(head: bool, brake: bool, reverse: bool, aux: bool) -> void:
	lamp_material.set_shader_parameter("head", 3.5 if head else 0.0)
	lamp_material.set_shader_parameter("tail", 3.0 if brake else (0.8 if head else 0.0))
	lamp_material.set_shader_parameter("indicator", 4.0 if reverse else 0.0)
	lamp_material.set_shader_parameter("aux", 5.0 if aux else 0.0)


var _dirt := 0.0

## Carry the terrain (REALISM.md R1): build up while churning mud, rinse in
## water, shed very slowly on dry ground. Drives the shader `dirt` uniform.
func update_dirt(mud: float, surface: int, delta: float) -> void:
	if surface == 6:  # water rinses
		_dirt = maxf(_dirt - delta * 0.25, 0.0)
	elif mud > 0.01:
		_dirt = minf(_dirt + mud * delta * 0.35, 1.0)
	else:
		_dirt = maxf(_dirt - delta * 0.01, 0.0)
	paint_material.set_shader_parameter("dirt", _dirt)
	_material("opaque").set_shader_parameter("dirt", _dirt)


static func _aim(from: Vector3, to: Vector3, stretch: bool) -> Transform3D:
	var d := to - from
	var length := d.length()
	if length < 1e-5:
		return Transform3D(Basis(), from)
	var z := d / length
	var up := Vector3.UP if absf(z.y) < 0.98 else Vector3.FORWARD
	var x := up.cross(z).normalized()
	var y := z.cross(x)
	return Transform3D(Basis(x, y, z * (length if stretch else 1.0)), from)


func _process(_delta: float) -> void:
	if vehicle == null:
		return
	global_transform = vehicle.global_transform
	var t := vehicle.telemetry()

	var axle_y: float = rig.axle_y
	var track: float = rig.track
	var travel: float = rig.travel
	for a in 2:
		var left := vehicle.wheel(a * 2)
		var right := vehicle.wheel(a * 2 + 1)
		var cl := clampf(left.travel, -travel, travel)
		var cr := clampf(right.travel, -travel, travel)
		_axles[a].position.y = axle_y + (cl + cr) * 0.5
		_axles[a].rotation.z = asin(clampf((cr - cl) / track, -0.6, 0.6))
	for i in 4:
		var w := vehicle.wheel(i)
		_wheel_pivots[i].rotation.y = w.steer if i < 2 else 0.0
		_wheel_spinners[i].rotation.x = w.spin

	for l in _links:
		var target: Vector3 = _axles[l.axle].transform * (l.target as Vector3)
		l.node.transform = _aim(l.pivot, target, true)
	for s in _shocks:
		var anchor: Vector3 = _axles[s.axle].transform * (s.anchor as Vector3)
		var mount: Vector3 = s.mount
		# Body hangs from the mount pointing at the anchor (-Y along the shock).
		var down := _aim(mount, anchor, false)
		s.body.transform = Transform3D(Basis(down.basis.x, -down.basis.z, down.basis.y), mount)
		var up := _aim(anchor, mount, false)
		s.shaft.transform = Transform3D(Basis(up.basis.x, up.basis.z, -up.basis.y), anchor)
		var length := mount.distance_to(anchor)
		s.spring.transform = Transform3D(Basis(up.basis.x, up.basis.z * length * 0.82, -up.basis.y), anchor + (mount - anchor) * 0.08)

	for c in _steering.get_children():
		c.rotation.z = -float(t.steer_angle) * 4.2

	# Blob shadow: on the ground under the chassis, aligned to the terrain normal.
	if shadow != null and shadow.is_inside_tree():
		var p := vehicle.global_position
		var n := field.normal(p.x, p.z)
		var fwd := vehicle.global_basis.z
		fwd = (fwd - n * fwd.dot(n)).normalized()
		var right := n.cross(fwd).normalized()
		shadow.global_transform = Transform3D(Basis(right, n, fwd), Vector3(p.x, field.height(p.x, p.z) + 0.5, p.z))
		var clearance := p.y - field.height(p.x, p.z)
		shadow.modulate = Color(1, 1, 1, clampf(1.4 - clearance * 0.35, 0.0, 1.0))
