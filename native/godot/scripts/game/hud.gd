# hud.gd - minimal native HUD: speed, gear, range, lock, rpm, surface, alerts,
# and the pause menu (resume button + mouse sensitivity slider).
class_name GameHud
extends CanvasLayer

signal sensitivity_changed(value: float)
signal volume_changed(value: float)
signal time_changed(hours: float)
signal lights_toggled(on: bool)
signal resume_pressed
signal menu_toggled
signal vehicle_selected(index: int)
signal camera_selected(mode: String)
signal recover_pressed
signal quit_pressed

const SURFACE_NAMES := ["rock", "gravel", "dirt", "grass", "loam", "mud", "water", "snow"]

var gauges: Label
var alert: Label
var stats: Label
var help: Label
var pause_menu: PanelContainer
var menu_button: Button
var _sens_slider: HSlider
var _sens_value: Label
var _vehicle_pick: OptionButton
var _camera_pick: OptionButton
var _vol_slider: HSlider
var _vol_value: Label
var _time_slider: HSlider
var _time_value: Label
var _lights_check: CheckBox
var _stats_check: CheckBox
var _help_check: CheckBox
var _alert_timer := 0.0


func _ready() -> void:
	gauges = _label(Vector2(24, -128), 22, Control.PRESET_BOTTOM_LEFT)
	alert = _label(Vector2(0, 64), 20, Control.PRESET_CENTER_TOP)
	stats = _label(Vector2(-560, 12), 13, Control.PRESET_TOP_RIGHT)
	stats.visible = false
	help = _label(Vector2(64, 20), 15, Control.PRESET_TOP_LEFT)  # right of the hamburger
	help.text = "W/S drive-brake (hold S at a stop to reverse)  A/D steer  Space handbrake\nQ/E gears  L range  X diff lock  R recover  H lights  mouse look around\nF winch hook  G reel in  C camera  ` stats  / help"
	help.visible = false
	_build_menu_button()
	_build_pause_menu()


func _build_menu_button() -> void:
	# Hamburger toggle, top-left. Drawn with rects - the default font has no
	# reliable three-lines glyph. Clickable whenever the cursor is visible.
	menu_button = Button.new()
	menu_button.process_mode = Node.PROCESS_MODE_ALWAYS
	menu_button.custom_minimum_size = Vector2(40, 34)
	menu_button.focus_mode = Control.FOCUS_NONE
	menu_button.set_anchors_preset(Control.PRESET_TOP_LEFT)
	menu_button.position = Vector2(12, 12)
	menu_button.pressed.connect(func() -> void: menu_toggled.emit())
	var lines := VBoxContainer.new()
	lines.set_anchors_preset(Control.PRESET_CENTER)
	lines.mouse_filter = Control.MOUSE_FILTER_IGNORE
	lines.add_theme_constant_override("separation", 4)
	for i in 3:
		var bar := ColorRect.new()
		bar.color = Color(0.92, 0.92, 0.92)
		bar.custom_minimum_size = Vector2(18, 2)
		bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
		lines.add_child(bar)
	menu_button.add_child(lines)
	add_child(menu_button)


func _build_pause_menu() -> void:
	# Runs while the tree is paused, so it (and its children) must be ALWAYS.
	pause_menu = PanelContainer.new()
	pause_menu.process_mode = Node.PROCESS_MODE_ALWAYS
	# Docked on the left under the hamburger, clear of the truck in the centre.
	pause_menu.set_anchors_preset(Control.PRESET_TOP_LEFT)
	pause_menu.position = Vector2(12, 56)
	pause_menu.visible = false
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(380, 0)
	box.add_theme_constant_override("separation", 10)
	pause_menu.add_child(box)

	var title := Label.new()
	title.text = "paused"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_font_size_override("font_size", 20)
	box.add_child(title)

	_vehicle_pick = OptionButton.new()
	_vehicle_pick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_vehicle_pick.item_selected.connect(func(i: int) -> void: vehicle_selected.emit(i))
	_menu_row(box, "vehicle").add_child(_vehicle_pick)

	_camera_pick = OptionButton.new()
	_camera_pick.add_item("chase")
	_camera_pick.add_item("close")
	_camera_pick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_camera_pick.item_selected.connect(
			func(i: int) -> void: camera_selected.emit("close" if i == 1 else "chase"))
	_menu_row(box, "camera").add_child(_camera_pick)

	var sens_row := _menu_row(box, "mouse sensitivity")
	_sens_slider = HSlider.new()
	_sens_slider.min_value = 0.2
	_sens_slider.max_value = 3.0
	_sens_slider.step = 0.05
	_sens_slider.value = 1.0
	_sens_slider.custom_minimum_size = Vector2(120, 0)
	_sens_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_sens_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_sens_slider.value_changed.connect(_on_sensitivity)
	sens_row.add_child(_sens_slider)
	_sens_value = Label.new()
	_sens_value.text = "1.00x"
	sens_row.add_child(_sens_value)

	var vol_row := _menu_row(box, "engine volume")
	_vol_slider = HSlider.new()
	_vol_slider.min_value = 0.0
	_vol_slider.max_value = 1.0
	_vol_slider.step = 0.05
	_vol_slider.value = 0.7
	_vol_slider.custom_minimum_size = Vector2(120, 0)
	_vol_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_vol_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_vol_slider.value_changed.connect(func(v: float) -> void:
		_vol_value.text = "%d%%" % roundi(v * 100.0)
		volume_changed.emit(v))
	vol_row.add_child(_vol_slider)
	_vol_value = Label.new()
	_vol_value.text = "70%"
	vol_row.add_child(_vol_value)

	var time_row := _menu_row(box, "time of day")
	_time_slider = HSlider.new()
	_time_slider.min_value = 0.0
	_time_slider.max_value = 24.0
	_time_slider.step = 0.25
	_time_slider.value = 9.0
	_time_slider.custom_minimum_size = Vector2(120, 0)
	_time_slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_time_slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_time_slider.value_changed.connect(func(h: float) -> void:
		_time_value.text = _clock(h)
		time_changed.emit(h))
	time_row.add_child(_time_slider)
	_time_value = Label.new()
	_time_value.text = "09:00"
	time_row.add_child(_time_value)

	_lights_check = CheckBox.new()
	_lights_check.text = "headlights  (H)"
	_lights_check.toggled.connect(func(on: bool) -> void: lights_toggled.emit(on))
	box.add_child(_lights_check)
	_stats_check = CheckBox.new()
	_stats_check.text = "stats overlay  (`)"
	_stats_check.toggled.connect(func(on: bool) -> void: stats.visible = on)
	box.add_child(_stats_check)
	_help_check = CheckBox.new()
	_help_check.text = "controls help  (/)"
	_help_check.toggled.connect(func(on: bool) -> void: help.visible = on)
	box.add_child(_help_check)

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	buttons.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_child(buttons)
	for entry in [["resume", func() -> void: resume_pressed.emit()],
			["recover truck", func() -> void: recover_pressed.emit()],
			["quit", func() -> void: quit_pressed.emit()]]:
		var b := Button.new()
		b.text = entry[0]
		b.pressed.connect(entry[1])
		buttons.add_child(b)
	add_child(pause_menu)


func _menu_row(box: VBoxContainer, text: String) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	var label := Label.new()
	label.text = text
	label.custom_minimum_size = Vector2(130, 0)
	row.add_child(label)
	box.add_child(row)
	return row


## Populate the vehicle picker (once, from the catalog).
func set_vehicles(names: PackedStringArray) -> void:
	_vehicle_pick.clear()
	for n in names:
		_vehicle_pick.add_item(n)


## Reflect a loaded volume setting without re-emitting volume_changed.
func set_volume(v: float) -> void:
	_vol_slider.set_value_no_signal(v)
	_vol_value.text = "%d%%" % roundi(v * 100.0)


static func _clock(h: float) -> String:
	return "%02d:%02d" % [int(h) % 24, roundi(fmod(h, 1.0) * 60.0)]


## Reflect current game state when the menu opens, without re-emitting signals.
func sync_menu(vehicle_index: int, camera_mode: String, lights_on: bool, hours: float) -> void:
	_vehicle_pick.select(vehicle_index)
	_camera_pick.select(1 if camera_mode == "close" else 0)
	_time_slider.set_value_no_signal(hours)
	_time_value.text = _clock(hours)
	_lights_check.set_pressed_no_signal(lights_on)
	_stats_check.set_pressed_no_signal(stats.visible)
	_help_check.set_pressed_no_signal(help.visible)


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
	# Vehicle systems (REALISM.md R3), injected by main when present.
	var systems := ""
	if t.has("fuel"):
		systems += "   fuel %d%%" % roundi(float(t.fuel) * 100.0)
	if float(t.get("damage", 0.0)) > 0.02:
		systems += "   dmg %d%%" % roundi(float(t.damage) * 100.0)
	if float(t.get("depth", 0.0)) > 0.15:
		systems += "   water %.1f m" % float(t.depth)
	gauges.text = "%3d km/h   %s%s   %s   %s\n%4d rpm   %s%s%s" % [
		roundi(t.kph), t.gear_name, " LOW" if t.low_range else "", "lock " + t.lock,
		"%.0f%% stuck" % (t.stuck * 100.0) if t.stuck > 0.05 else "",
		roundi(t.rpm), SURFACE_NAMES[clampi(t.surface, 0, 7)],
		"   winch %.0f%%" % (t.winch_tension * 100.0) if t.winch else "", systems]
	_alert_timer -= delta
	alert.visible = _alert_timer > 0.0
	if stats.visible:
		stats.text = stats_text
