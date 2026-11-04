# rut_trail.gd - visual-only wheel ruts in soft ground (REALISM.md R2).
#
# One MultiMesh ring buffer of dark quads conformed to the terrain, dropped
# behind each wheel every RUT_STEP metres on soft surfaces. Purely visual:
# the analytic terrain never deforms (PLAN contract), and instances are
# written only when dropped - no per-frame updates, one draw call, no
# shadows. The ring overwrites the oldest rut.
class_name RutTrail
extends MultiMeshInstance3D

const MAX_RUTS := 1024
const RUT_STEP := 0.55
const SOFT := [2, 4, 5, 7]  # dirt, loam, mud, snow

var vehicle: OffroadVehicle
var field: TerrainField
var _hubs: Array[Vector3] = []
var _last: Array[Vector3] = []
var _next := 0


func setup(p_vehicle: OffroadVehicle, spec: Dictionary, p_field: TerrainField) -> void:
	vehicle = p_vehicle
	field = p_field
	_hubs.clear()
	_last.clear()
	var track: float = spec.axle.track
	var wheelbase: float = spec.frame.wheelbase
	for i in 4:
		var side := -1.0 if i % 2 == 0 else 1.0
		var fz := 1.0 if i < 2 else -1.0
		_hubs.append(Vector3(side * track / 2.0, 0.0, fz * wheelbase / 2.0))
		_last.append(Vector3(1e9, 0, 1e9))

	if multimesh == null:
		var quad := PlaneMesh.new()
		quad.size = Vector2(0.34, 0.62)
		var m := StandardMaterial3D.new()
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.albedo_color = Color(0.10, 0.085, 0.06, 0.40)
		quad.material = m
		multimesh = MultiMesh.new()
		multimesh.transform_format = MultiMesh.TRANSFORM_3D
		multimesh.mesh = quad
		multimesh.instance_count = MAX_RUTS
		var zero := Transform3D(Basis().scaled(Vector3.ZERO), Vector3.ZERO)
		for i in MAX_RUTS:
			multimesh.set_instance_transform(i, zero)
		cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		# The ring follows the truck anywhere; one generous fixed AABB beats
		# recomputing bounds on every drop.
		custom_aabb = AABB(Vector3(-8192, -512, -8192), Vector3(16384, 1024, 16384))


func _process(_delta: float) -> void:
	if vehicle == null:
		return
	var xf := vehicle.global_transform
	for i in 4:
		var w := vehicle.wheel(i)
		if not bool(w.contact) or not int(w.surface) in SOFT:
			continue
		var world := xf * _hubs[i]
		if world.distance_to(_last[i]) < RUT_STEP:
			continue
		_last[i] = world
		var n := field.normal(world.x, world.z)
		var fwd := xf.basis.z
		fwd = (fwd - n * fwd.dot(n)).normalized()
		var right := n.cross(fwd).normalized()
		var pos := Vector3(world.x, field.height(world.x, world.z), world.z) + n * 0.02
		multimesh.set_instance_transform(_next % MAX_RUTS, Transform3D(Basis(right, n, fwd), pos))
		_next += 1
