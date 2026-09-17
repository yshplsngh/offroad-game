# sky_cycle.gd - slow time-of-day drift (REALISM.md R2).
#
# The sun (no shadow maps - cheap) moves every frame; the sky material, fog
# and ambient push only every SKY_PUSH seconds so radiance regeneration stays
# out of the frame loop (PLAN: environment-map generation never per frame).
# Exposes `night` (0-1) for automatic headlights.
class_name SkyCycle
extends Node

const SKY_PUSH := 5.0
const DAY_SECONDS := 780.0  # a full day in 13 minutes

# Key palettes: night, dawn/dusk, day.
const SKY_TOP := [Color(0.02, 0.03, 0.07), Color(0.36, 0.30, 0.44), Color(0.32, 0.50, 0.78)]
const HORIZON := [Color(0.05, 0.06, 0.10), Color(0.86, 0.52, 0.36), Color(0.72, 0.78, 0.84)]
const GROUND := [Color(0.03, 0.03, 0.04), Color(0.30, 0.24, 0.20), Color(0.50, 0.52, 0.50)]
const FOG := [Color(0.03, 0.04, 0.07), Color(0.55, 0.44, 0.40), Color(0.66, 0.73, 0.80)]
const SUN := [Color(0.60, 0.70, 1.00), Color(1.00, 0.62, 0.38), Color(1.00, 0.96, 0.88)]

## 0 = midnight, 0.5 = noon. Starts mid-morning.
var time_frac := 0.38
var night := 0.0

var sun: DirectionalLight3D
var sky_mat: ProceduralSkyMaterial
var env: Environment
var _push_timer := SKY_PUSH  # push on the first frame


func setup(p_sun: DirectionalLight3D, p_sky_mat: ProceduralSkyMaterial, p_env: Environment) -> void:
	sun = p_sun
	sky_mat = p_sky_mat
	env = p_env


## Menu sets the clock; the next frame pushes the whole sky.
func set_time(frac: float) -> void:
	time_frac = fposmod(frac, 1.0)
	_push_timer = SKY_PUSH


func hours() -> float:
	return time_frac * 24.0


func _process(delta: float) -> void:
	if sun == null:
		return
	time_frac = fposmod(time_frac + delta / DAY_SECONDS, 1.0)
	var elev := sin((time_frac - 0.25) * TAU)  # -1 midnight .. +1 noon
	night = clampf(0.25 - elev * 2.2, 0.0, 1.0)

	# Sun every frame: direction, energy, colour. A dim blue "moon" floor keeps
	# the night readable.
	var azimuth := time_frac * 360.0 + 90.0
	sun.rotation_degrees = Vector3(-maxf(elev * 62.0, 4.0), azimuth, 0.0)
	sun.light_energy = lerpf(0.06, 1.35, clampf(elev * 1.4, 0.0, 1.0))
	sun.light_color = _key(SUN, elev)

	_push_timer += delta
	if _push_timer < SKY_PUSH:
		return
	_push_timer = 0.0
	sky_mat.sky_top_color = _key(SKY_TOP, elev)
	sky_mat.sky_horizon_color = _key(HORIZON, elev)
	sky_mat.ground_horizon_color = _key(HORIZON, elev) * 0.8
	sky_mat.ground_bottom_color = _key(GROUND, elev)
	env.fog_light_color = _key(FOG, elev)
	env.ambient_light_energy = lerpf(0.30, 0.90, clampf(elev * 1.5 + 0.15, 0.0, 1.0))


## Blend the night/dawn/day keys around the horizon crossing.
func _key(keys: Array, elev: float) -> Color:
	var dawn := clampf((elev + 0.12) / 0.35, 0.0, 1.0)
	var day := clampf((elev - 0.12) / 0.50, 0.0, 1.0)
	return (keys[0] as Color).lerp(keys[1], dawn).lerp(keys[2], day)
