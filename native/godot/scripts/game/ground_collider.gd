# ground_collider.gd - a small solid heightfield patch that follows the vehicle.
#
# The wheels ray-cast the analytic terrain directly, but the chassis box had
# nothing to hit: a hard landing or a roll dropped the truck through the world.
# This patch gives the body real ground under and ahead of it, sampled from the
# same field in C++ (TerrainField.height_grid) and re-centred as the truck moves.
class_name GroundCollider
extends StaticBody3D

const SAMPLES := 33       # per side: 64 m at 2 m spacing
const SPACING := 2.0
const RECENTRE := 12.0    # metres the look-ahead point may drift before re-sampling
const LOOK_AHEAD := 0.25  # seconds of travel to lead the patch by

var field: TerrainField
var _shape := HeightMapShape3D.new()
var _centre := Vector2(INF, INF)


func _init(p_field: TerrainField) -> void:
	field = p_field
	name = "GroundCollider"
	_shape.map_width = SAMPLES
	_shape.map_depth = SAMPLES
	var col := CollisionShape3D.new()
	col.shape = _shape
	col.scale = Vector3(SPACING, 1.0, SPACING)
	add_child(col)


## Call every physics tick with the body that needs ground under it.
func follow(body: RigidBody3D) -> void:
	var p := body.global_position + body.linear_velocity * LOOK_AHEAD
	var ahead := Vector2(p.x, p.z)
	if ahead.distance_to(_centre) < RECENTRE:
		return
	_centre = (ahead / SPACING).round() * SPACING
	var half := (SAMPLES - 1) * 0.5 * SPACING
	_shape.map_data = field.height_grid(_centre.x - half, _centre.y - half, SAMPLES, SAMPLES, SPACING)
	global_position = Vector3(_centre.x, 0.0, _centre.y)
