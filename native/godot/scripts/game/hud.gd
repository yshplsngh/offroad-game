# hud.gd - minimal native HUD: speed, gear, range, lock, rpm, surface, alerts.
class_name GameHud
extends CanvasLayer

const SURFACE_NAMES := ["rock", "gravel", "dirt", "grass", "loam", "mud", "water", "snow"]

var gauges: Label
var alert: Label
var stats: Label
var help: Label
var _alert_timer := 0.0


func _ready() -> void:
	gauges = _label(Vector2(24, -128), 22, Control.PRESET_BOTTOM_LEFT)
	alert = _label(Vector2(0, 64), 20, Control.PRESET_CENTER_TOP)
	stats = _label(Vector2(-560, 12), 13, Control.PRESET_TOP_RIGHT)
	stats.visible = false
	help = _label(Vector2(24, 24), 15, Control.PRESET_TOP_LEFT)
	help.text = "W/S drive-brake (hold S at a stop to reverse)  A/D steer  Space handbrake\nQ/E gears  L range  X diff lock  R recover  mouse look around\nF winch hook  G reel in  C camera  ` stats  / help"
	help.visible = false


func _label(offset: Vector2, size: int, preset: int) -> Label:
	var l := Label.new()
	l.set_anchors_preset(preset)
	l.position += offset
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	l.add_theme_constant_override("outline_size", 6)
	add_child(l)
	return l


func say(text: String, seconds := 2.0) -> void:
	alert.text = text
	_alert_timer = seconds


func update_hud(t: Dictionary, delta: float, stats_text: String) -> void:
	gauges.text = "%3d km/h   %s%s   %s   %s\n%4d rpm   %s%s" % [
		roundi(t.kph), t.gear_name, " LOW" if t.low_range else "", "lock " + t.lock,
		"%.0f%% stuck" % (t.stuck * 100.0) if t.stuck > 0.05 else "",
		roundi(t.rpm), SURFACE_NAMES[clampi(t.surface, 0, 7)],
		"   winch %.0f%%" % (t.winch_tension * 100.0) if t.winch else ""]
	_alert_timer -= delta
	alert.visible = _alert_timer > 0.0
	if stats.visible:
		stats.text = stats_text
