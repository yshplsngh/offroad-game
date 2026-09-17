# wheel_fx.gd - dust, mud and spray from the wheels (REALISM.md R1).
#
# One GPUParticles3D per wheel at the hub, emitting only on contact and only
# when the wheel is working: slipping, or rolling fast on a loose surface.
# Colour and kick come from the wheel's surface id. Budgets (PLAN.md rules):
# 4 draw calls, 48 particles each, unlit billboards, no shadows.
class_name WheelFX
extends Node3D

## Loose surfaces kick up debris just from rolling speed (id order in PLAN.md).
const LOOSE := [1, 2, 4, 5, 7]  # gravel, dirt, loam, mud, snow
const COLORS := {
	0: Color(0.62, 0.60, 0.55, 0.50),  # rock dust
	1: Color(0.62, 0.58, 0.50, 0.55),  # gravel
	2: Color(0.52, 0.44, 0.34, 0.60),  # dirt
	3: Color(0.50, 0.50, 0.40, 0.40),  # grass clippings
	4: Color(0.42, 0.36, 0.26, 0.60),  # loam
	5: Color(0.30, 0.24, 0.17, 0.80),  # mud chunks
	6: Color(0.70, 0.78, 0.85, 0.50),  # water spray
	7: Color(0.92, 0.94, 0.97, 0.60),  # snow
}

var vehicle: OffroadVehicle
var _emitters: Array[GPUParticles3D] = []
var _mats: Array[ParticleProcessMaterial] = []
var _hubs: Array[Vector3] = []
var _radius := 0.4


func setup(p_vehicle: OffroadVehicle, spec: Dictionary) -> void:
	vehicle = p_vehicle
	for e in _emitters:
		e.queue_free()
	_emitters.clear()
	_mats.clear()
	_hubs.clear()
	var track: float = spec.axle.track
	var wheelbase: float = spec.frame.wheelbase
	var r: float = spec.physics.wheelRadius
	_radius = r
	for i in 4:  # FL FR RL RR; front axle at +z (PLAN.md coordinates)
		var side := -1.0 if i % 2 == 0 else 1.0
		var fz := 1.0 if i < 2 else -1.0
		_hubs.append(Vector3(side * track / 2.0, r * 0.3, fz * wheelbase / 2.0))
		var mat := ParticleProcessMaterial.new()
		mat.direction = Vector3(0, 1, 0)
		mat.spread = 40.0
		mat.initial_velocity_min = 1.2
		mat.initial_velocity_max = 3.0
		mat.gravity = Vector3(0, -7.0, 0)
		mat.scale_min = 0.5
		mat.scale_max = 1.4
		mat.damping_min = 0.5
		mat.damping_max = 1.5
		var quad := QuadMesh.new()
		quad.size = Vector2(0.22, 0.22)
		var m := StandardMaterial3D.new()
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.billboard_mode = BaseMaterial3D.BILLBOARD_PARTICLES
		m.vertex_color_use_as_albedo = true
		quad.material = m
		var p := GPUParticles3D.new()
		p.process_material = mat
		p.draw_pass_1 = quad
		p.amount = 48
		p.lifetime = 0.9
		p.emitting = false
		p.local_coords = false
		p.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(p)
		_emitters.append(p)
		_mats.append(mat)


func _process(_delta: float) -> void:
	if vehicle == null:
		return
	var xf := vehicle.global_transform
	var speed := vehicle.linear_velocity.length()
	for i in 4:
		var w := vehicle.wheel(i)
		var p := _emitters[i]
		p.global_position = xf * _hubs[i]
		var surf := int(w.surface)
		# Slip alone is not enough: locked brakes at a standstill read slip ~1
		# while nothing moves. Debris needs real relative motion - a spinning
		# wheel (burnout) or a body actually travelling (skid).
		var kick := maxf(absf(float(w.omega)) * _radius, speed)
		var working: bool = (float(w.slip) > 0.35 and kick > 1.5) or (speed > 4.0 and surf in LOOSE)
		var on: bool = bool(w.contact) and working
		p.emitting = on
		if on:
			_mats[i].color = COLORS.get(surf, COLORS[2])
			_mats[i].initial_velocity_max = 2.0 + minf(speed * 0.25 + float(w.slip) * 3.0, 6.0)
