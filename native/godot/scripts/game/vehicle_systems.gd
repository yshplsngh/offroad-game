# vehicle_systems.gd - fording, damage and fuel (REALISM.md R3).
#
# Pure game layer over the frozen physics: consequences arrive as input
# scaling (a flooded or empty or beaten engine pulls less), HUD state and
# audio character - never as edits to the force math.
#
#   Fording  the analytic world has no separate water plane, so depth is
#            estimated: on a water surface, the water level is the lowest
#            non-water ground on a ring around the truck (the bank the river
#            fills to). Past intake height the engine floods: throttle dies
#            until the truck is towed/serviced or dries out for a while.
#   Damage   telemetry `impact` spikes accumulate damage; damage caps the
#            throttle (drivetrain efficiency loss) and rattles the engine.
#   Fuel     drains with rpm x throttle; empty means the engine dies.
#   Service  recovery (R) is the field service: repaired, refuelled, dried.
class_name VehicleSystems
extends Node

const RING := 8              # depth probe ring samples
const RING_RADIUS := 7.0     # metres
const PROBE_EVERY := 0.25    # seconds between depth probes
const FLOOD_DRY_TIME := 12.0 # seconds out of deep water before a flooded engine dries
const FULL_TANK_MINUTES := 28.0  # flat-out driving on one tank

var field: TerrainField
var fuel := 1.0
var damage := 0.0
var depth := 0.0             # estimated water depth under the truck
var submersion := 0.0        # 0..1 of intake height
var flooded := false
var _intake := 1.0
var _dry_timer := 0.0
var _probe_timer := 0.0
var _last_impact := 0.0


func configure(spec: Dictionary, p_field: TerrainField) -> void:
	field = p_field
	# Intake sits above the sill, roughly bonnet height; low sports rigs
	# flood far sooner than snorkel-ready trucks.
	_intake = float(spec.physics.sillY) + 0.45
	depth = 0.0
	submersion = 0.0
	flooded = false
	_last_impact = 0.0


## Field service on recovery: repaired, refuelled, engine dried.
func service() -> void:
	fuel = 1.0
	damage = 0.0
	flooded = false
	_dry_timer = 0.0


## The fraction of the player's throttle the engine can actually deliver.
func throttle_scale() -> float:
	if flooded or fuel <= 0.0:
		return 0.0
	return 1.0 - 0.5 * damage


func running() -> bool:
	return not flooded and fuel > 0.0


## Call once per physics tick with live telemetry and the applied throttle.
func update(tel: Dictionary, throttle: float, pos: Vector3, delta: float) -> void:
	# --- fording depth (probed at PROBE_EVERY, cheap analytic samples) ---
	_probe_timer -= delta
	if _probe_timer <= 0.0:
		_probe_timer = PROBE_EVERY
		if int(tel.surface) == 6:
			var bed := field.height(pos.x, pos.z)
			var bank := INF
			for i in RING:
				var a := TAU * i / RING
				var sx := pos.x + cos(a) * RING_RADIUS
				var sz := pos.z + sin(a) * RING_RADIUS
				var s := field.sample(sx, sz)
				if int(s.surface) != 6:
					bank = minf(bank, float(s.height))
			# Mid-river (no bank on the ring): assume wheel-deep water.
			depth = clampf((bank - bed) if bank < INF else _intake * 0.6, 0.0, 3.0)
		else:
			depth = 0.0
	submersion = clampf(depth / _intake, 0.0, 1.0)

	# --- flooding ---
	if depth >= _intake:
		flooded = true
		_dry_timer = 0.0
	elif flooded:
		_dry_timer += delta
		if _dry_timer > FLOOD_DRY_TIME:
			flooded = false

	# --- damage from impact spikes ---
	var impact := float(tel.impact)
	if impact > _last_impact + 4.0:  # a fresh hit, not the decaying tail
		damage = minf(damage + (impact - _last_impact) * 0.012, 1.0)
	_last_impact = impact

	# --- fuel ---
	if running():
		var burn := (0.10 + 0.90 * throttle * clampf(float(tel.rpm) / 5000.0, 0.0, 1.0)) \
				/ (FULL_TANK_MINUTES * 60.0)
		fuel = maxf(fuel - burn * delta, 0.0)
