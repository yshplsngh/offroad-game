# vehicle_input.gd - keyboard + gamepad -> continuous axes and one-shot actions.
# Bindings follow the browser reference's src/game/input.js (git a59773d).
class_name VehicleInput
extends Node

signal action(name: String)

const KEY_ACTIONS := {
	KEY_E: "gearUp", KEY_Q: "gearDown", KEY_X: "lock", KEY_L: "range",
	KEY_C: "camera", KEY_R: "recover", KEY_F: "winch", KEY_P: "pause",
	KEY_ESCAPE: "pause", KEY_QUOTELEFT: "debug", KEY_SLASH: "help", KEY_V: "nextVehicle",
}
const PAD_ACTIONS := {
	JOY_BUTTON_A: "recover", JOY_BUTTON_B: "lock", JOY_BUTTON_X: "winch", JOY_BUTTON_Y: "camera",
	JOY_BUTTON_LEFT_SHOULDER: "gearDown", JOY_BUTTON_RIGHT_SHOULDER: "gearUp",
	JOY_BUTTON_START: "pause", JOY_BUTTON_LEFT_STICK: "range",
}
const DEADZONE := 0.12

var throttle := 0.0
var brake := 0.0
var handbrake := 0.0
var steer := 0.0
var winch := 0.0


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		var name: String = KEY_ACTIONS.get(event.physical_keycode, "")
		if name != "":
			action.emit(name)
	elif event is InputEventJoypadButton and event.pressed:
		var name: String = PAD_ACTIONS.get(event.button_index, "")
		if name != "":
			action.emit(name)


func poll() -> void:
	var k := func(a: Key, b: Key) -> float:
		return 1.0 if Input.is_physical_key_pressed(a) or Input.is_physical_key_pressed(b) else 0.0
	throttle = k.call(KEY_W, KEY_UP)
	brake = k.call(KEY_S, KEY_DOWN)
	# The controller's positive steer points the wheels toward +X. The truck faces
	# +Z, and in Godot's right-handed Y-up space a camera behind it sees +X on the
	# LEFT of the screen, so screen-right input is negative steer.
	steer = clampf(k.call(KEY_A, KEY_LEFT) - k.call(KEY_D, KEY_RIGHT), -1.0, 1.0)
	handbrake = 1.0 if Input.is_physical_key_pressed(KEY_SPACE) else 0.0
	winch = 1.0 if Input.is_physical_key_pressed(KEY_G) else 0.0

	# First connected pad overrides the keyboard while it is being used.
	var pads := Input.get_connected_joypads()
	if pads.is_empty():
		return
	var pad: int = pads[0]
	var ps := Input.get_joy_axis(pad, JOY_AXIS_LEFT_X)
	ps = 0.0 if absf(ps) < DEADZONE else signf(ps) * (absf(ps) - DEADZONE) / (1.0 - DEADZONE)
	var pt := Input.get_joy_axis(pad, JOY_AXIS_TRIGGER_RIGHT)
	var pb := Input.get_joy_axis(pad, JOY_AXIS_TRIGGER_LEFT)
	if absf(ps) > 0.0 or pt > 0.02 or pb > 0.02:
		steer = -ps  # stick right = screen right = negative steer (see above)
		throttle = pt
		brake = pb
	if Input.is_joy_button_pressed(pad, JOY_BUTTON_B):
		handbrake = 1.0
