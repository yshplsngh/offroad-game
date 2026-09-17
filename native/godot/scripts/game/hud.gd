# hud.gd - minimal native HUD: speed, gear, range, lock, rpm, surface, alerts,
# and the pause menu (resume button + mouse sensitivity slider).
class_name GameHud
extends CanvasLayer

signal sensitivity_changed(value: float)
signal resume_pressed

const SURFACE_NAMES := ["rock", "gravel", "dirt", "grass", "loam", "mud", "water", "snow"]

var gauges: Label
var alert: Label
var stats: Label
var help: Label
var pause_menu: PanelContainer
var _sens_slider: HSlider
var _sens_value: Label
var _alert_timer := 0.0


func _ready() -> void:
	gauges = _label(Vector2(24, -128), 22, Control.PRESET_BOTTOM_LEFT)
	alert = _label(Vector2(0, 64), 20, Control.PRESET_CENTER_TOP)
	stats = _label(Vector2(-560, 12), 13, Control.PRESET_TOP_RIGHT)
	stats.visible = false
	help = _label(Vector2(24, 24), 15, Control.PRESET_TOP_LEFT)
	help.text = "W/S drive-brake (hold S at a stop to reverse)  A/D steer  Space handbrake\nQ/E gears  L range  X diff lock  R recover  mouse look around\nF winch hook  G reel in  C camera  ` stats  / help"
	help.visible = false
	_build_pause_menu()


func _build_pause_menu() -> void:
	# Runs while the tree is paused, so it (and its children) must be ALWAYS.
	pause_menu = PanelContainer.new()
	pause_menu.process_mode = Node.PROCESS_MODE_ALWAYS
	pause_menu.set_anchors_preset(Control.PRESET_CENTER)
	pause_menu.visible = false
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(340, 0)
	box.add_theme_constant_override("separation", 10)
	pause_menu.add_child(box)

	var title := Label.new()
	title.text = "paused"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 20)
	box.add_child(title)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	box.add_child(row)
	var sens_label := Label.new()
	sens_label.text = "mouse sensitivity"
	row.add_child(sens_label)
	_sens_slider = HSlider.new()
	_sens_slider.min_value = 0.2
	_sens_slider.max_value = 3.0
	_sens_slider.step = 0.05
	_sens_slider.value = 1.0
	_sens_slider.custom_minimum_size = Vector2(140, 0)
	_sens_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_sens_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_sens_slider.value_changed.connect(_on_sensitivity)
	row.add_child(_sens_slider)
	_sens_value = Label.new()
	_sens_value.text = "1.00x"
	row.add_child(_sens_value)

	var resume := Button.new()
	resume.text = "resume"
	resume.pressed.connect(func() -> void: resume_pressed.emit())
	box.add_child(resume)
	add_child(pause_menu)


func _on_sensitivity(v: float) -> void:
	_sens_value.text = "%.2fx" % v
	sensitivity_changed.emit(v)


## Reflect a loaded setting without re-emitting sensitivity_changed.
func set_sensitivity(v: float) -> void:
	_sens_slider.set_value_no_signal(v)
	_sens_value.text = "%.2fx" % v


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
	alert.visible = true  # show even while the tree is paused (update_hud is not running)
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
