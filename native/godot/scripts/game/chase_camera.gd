# chase_camera.gd - follows position tightly and heading loosely, never rolls.
# Parameters from the browser reference's src/game/camera.js (git a59773d).
class_name ChaseCamera
extends Camera3D

const MODES := {
	chase = {dist = 7.4, height = 2.1, lead = 3.2, lambda = 5.5, fov = 48.0},
	close = {dist = 5.0, height = 1.5, lead = 2.2, lambda = 7.0, fov = 52.0},
}

var target: Node3D
var field: TerrainField
var mode := "chase"
## Extra yaw around the truck (radians): 0 behind, PI/2 side, PI in front. For screenshots.
var orbit := 0.0
## Distance multiplier (screenshots / close-ups).
var zoom := 1.0
var _yaw := 0.0


func cycle_mode() -> String:
	mode = "close" if mode == "chase" else "chase"
	return mode


func snap() -> void:
	if target:
		_yaw = _heading()
		_update(1.0, true)


func _heading() -> float:
	var f := target.global_basis.z
	return atan2(f.x, f.z)


func _process(delta: float) -> void:
	if target:
		_update(delta, false)


func _update(delta: float, instant: bool) -> void:
	var cfg: Dictionary = MODES[mode]
	var dist: float = cfg.dist * zoom
	var height: float = cfg.height * lerpf(0.35, 1.0, clampf(zoom, 0.0, 1.0))
	var lead: float = cfg.lead
	var lambda: float = cfg.lambda
	var k := 1.0 if instant else 1.0 - exp(-lambda * delta)
	var h := _heading()
	_yaw += wrapf(h - _yaw, -PI, PI) * k
	var back := Vector3(sin(_yaw + orbit), 0, cos(_yaw + orbit))
	var p := target.global_position
	var want := p - back * dist + Vector3(0, height, 0)
	if field:
		want.y = maxf(want.y, field.height(want.x, want.z) + 0.8)
	global_position = want if instant else global_position.lerp(want, 1.0 - exp(-12.0 * delta))
	fov = cfg.fov
	look_at(p + back * lead + Vector3(0, 1.0, 0), Vector3.UP)
