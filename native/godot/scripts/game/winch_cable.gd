# winch_cable.gd - the winch rope, rendered (REALISM.md R3).
#
# A sagging polyline from the front bumper to the model's anchor while the
# winch is attached: slack rope hangs, a loaded rope pulls straight. One
# ImmediateMesh line strip rebuilt only while winching, unshaded, no shadows.
class_name WinchCable
extends MeshInstance3D

const SEGMENTS := 14

var vehicle: OffroadVehicle
var _mesh := ImmediateMesh.new()
var _attach := Vector3(0, 0.7, 2.6)  # bumper hook, vehicle-local


func setup(p_vehicle: OffroadVehicle, spec: Dictionary) -> void:
	vehicle = p_vehicle
	_attach = Vector3(0, float(spec.physics.sillY) * 0.9, float(spec.frame.wheelbase) / 2.0 + 0.7)
	if mesh == null:
		mesh = _mesh
		var m := StandardMaterial3D.new()
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.albedo_color = Color(0.14, 0.13, 0.12)
		material_override = m
		cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF


func _process(_delta: float) -> void:
	if vehicle == null:
		return
	var t := vehicle.telemetry()
	if not bool(t.winch):
		if visible:
			visible = false
		return
	visible = true
	var from := vehicle.global_transform * _attach
	var to := vehicle.winch_anchor()
	# Slack rope sags, tension pulls it straight.
	var sag := clampf(1.0 - float(t.winch_tension), 0.0, 1.0) * from.distance_to(to) * 0.06
	_mesh.clear_surfaces()
	_mesh.surface_begin(Mesh.PRIMITIVE_LINE_STRIP)
	for i in SEGMENTS + 1:
		var f := float(i) / SEGMENTS
		var p := from.lerp(to, f)
		p.y -= sag * 4.0 * f * (1.0 - f)  # parabolic hang
		_mesh.surface_add_vertex(p)
	_mesh.surface_end()
