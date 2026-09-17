# tire_audio.gd - surface-keyed rolling/skid noise (REALISM.md R2).
#
# Same AudioStreamGenerator technique as the engine: one-pole filtered noise
# whose cutoff, gain and "pop" burst rate come from the surface under the
# wheels - gravel crackles, mud squelches low, water hisses. Level follows
# speed, with a skid layer when the tires are sliding at speed. Synthesis
# only, phase-free, smoothed.
class_name TireAudio
extends AudioStreamPlayer

const MIX_RATE := 16000.0
## surface id -> [lowpass keep (0-1, higher = darker), gain, pop rate/sample]
const SURF := {
	0: [0.90, 0.50, 0.020],  # rock: dry rattle
	1: [0.88, 0.95, 0.060],  # gravel: bright crackle
	2: [0.93, 0.70, 0.015],  # dirt
	3: [0.94, 0.45, 0.000],  # grass: soft swish
	4: [0.96, 0.80, 0.008],  # loam
	5: [0.975, 1.10, 0.004], # mud: low squelch
	6: [0.55, 0.95, 0.000],  # water: hiss
	7: [0.93, 0.70, 0.010],  # snow: soft crunch
}

var volume := 0.7
var fill_worst_ms := 0.0

var _playback: AudioStreamGeneratorPlayback
var _level := 0.0
var _lp := 0.94
var _gain := 0.5
var _pop_rate := 0.0
var _noise := 0.0
var _pop := 0.0
var _rng := RandomNumberGenerator.new()


func _init() -> void:
	var gen := AudioStreamGenerator.new()
	gen.mix_rate = MIX_RATE
	gen.buffer_length = 0.1
	stream = gen


func _ready() -> void:
	play()
	_playback = get_stream_playback()


## `strain` is winch tension (0-1): the rope creaks and snaps under load.
func update(speed: float, slip: float, surface: int, delta: float, strain := 0.0) -> void:
	if _playback == null:
		return
	var p: Array = SURF.get(surface, SURF[2])
	var k := minf(delta * 6.0, 1.0)
	_lp += (float(p[0]) - _lp) * k
	_gain += (float(p[1]) - _gain) * k
	_pop_rate = float(p[2]) + strain * 0.004  # creak snaps under load
	var skid := 0.35 if slip > 0.5 and speed > 2.0 else 0.0
	var target := (clampf(speed / 22.0, 0.0, 1.0) * 0.40 + skid + strain * 0.30) \
			* maxf(_gain, 0.6 if strain > 0.05 else _gain) * volume
	_level += (target - _level) * minf(delta * 8.0, 1.0)
	var frames := _playback.get_frames_available()
	if frames <= 0 or _level < 0.003:
		return
	var t0 := Time.get_ticks_usec()
	var buf := PackedVector2Array()
	buf.resize(frames)
	for i in frames:
		if _pop_rate > 0.0 and _rng.randf() < _pop_rate:
			_pop = 1.0
		_pop *= 0.95
		_noise = _noise * _lp + (_rng.randf() * 2.0 - 1.0) * (1.0 - _lp) * 2.2
		var v := clampf(_noise * (0.55 + _pop * 1.6) * _level, -1.0, 1.0)
		buf[i] = Vector2(v, v)
	_playback.push_buffer(buf)
	fill_worst_ms = maxf(fill_worst_ms, (Time.get_ticks_usec() - t0) / 1000.0)
