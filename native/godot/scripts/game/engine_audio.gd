# engine_audio.gd - procedural V8-ish engine note (REALISM.md R1).
#
# Additive synthesis into an AudioStreamGenerator: the firing-order
# fundamental (four power strokes per rev for a V8: rpm / 15 Hz) plus a few
# harmonics and a sub-harmonic rumble, with throttle-scaled intake noise.
# Phase-continuous across buffer fills; rpm and level are smoothed against
# zipper noise. Synthesis only - no recordings (PLAN.md: everything
# procedural). Fill cost is measured (fill worst ms in the stats overlay).
class_name EngineAudio
extends AudioStreamPlayer

const MIX_RATE := 16000.0

## User setting from the menu (0-1), persisted in user://settings.cfg.
var volume := 0.7
var fill_worst_ms := 0.0

var _playback: AudioStreamGeneratorPlayback
var _phase := 0.0
var _rpm := 800.0
var _level := 0.0
var _noise := 0.0
var _rng := RandomNumberGenerator.new()


func _init() -> void:
	var gen := AudioStreamGenerator.new()
	gen.mix_rate = MIX_RATE
	gen.buffer_length = 0.1
	stream = gen


func _ready() -> void:
	play()
	_playback = get_stream_playback()


## Called once per rendered frame with live telemetry.
func update(rpm: float, throttle: float, delta: float) -> void:
	if _playback == null:
		return
	_rpm += (maxf(rpm, 600.0) - _rpm) * minf(delta * 8.0, 1.0)
	var target := (0.16 + 0.22 * throttle + 0.10 * clampf((_rpm - 900.0) / 4200.0, 0.0, 1.0)) * volume
	_level += (target - _level) * minf(delta * 10.0, 1.0)
	var frames := _playback.get_frames_available()
	if frames <= 0:
		return
	var t0 := Time.get_ticks_usec()
	var inc := (_rpm / 15.0) * TAU / MIX_RATE  # V8 firing fundamental
	var buf := PackedVector2Array()
	buf.resize(frames)
	for i in frames:
		_phase = fmod(_phase + inc, TAU)
		var s := sin(_phase) * 0.55
		s += sin(_phase * 2.0) * 0.24
		s += sin(_phase * 3.0) * 0.13
		s += sin(_phase * 0.5) * 0.18  # sub-harmonic rumble
		_noise = _noise * 0.92 + (_rng.randf() * 2.0 - 1.0) * 0.08
		s += _noise * (0.15 + 0.45 * throttle)
		var v := clampf(s * _level, -1.0, 1.0)
		buf[i] = Vector2(v, v)
	_playback.push_buffer(buf)
	fill_worst_ms = maxf(fill_worst_ms, (Time.get_ticks_usec() - t0) / 1000.0)
