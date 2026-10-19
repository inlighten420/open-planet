class_name Controls
extends RefCounted

# Every key and mouse button the player can reassign (Settings > Controls > Keyboard and mouse). Each action has two
# slots (a key or a mouse button each); a key is stored by its place on the keyboard (the defaults are QWERTY places,
# so WASD is the same four keys on AZERTY or Dvorak) and shown by what's printed on it. Mouse buttons are stored as minus their button number. Saved in the settings file as Key_<action>.
# Not reassignable: Esc (the menu), the keys that close the phone and the map, F1 and F3, the builder's number keys.

const MOUSE_LEFT := -MOUSE_BUTTON_LEFT
const MOUSE_RIGHT := -MOUSE_BUTTON_RIGHT

# [action, what it does, group, first key, second key] (0: none)
const ACTIONS := [
	["forward", "Forward / accelerate (flying: nose down)", "Move", KEY_W, KEY_UP],
	["back", "Back / brake and reverse (flying: nose up)", "Move", KEY_S, KEY_DOWN],
	["left", "Left / steer left (flying: roll left)", "Move", KEY_A, KEY_LEFT],
	["right", "Right / steer right (flying: roll right)", "Move", KEY_D, KEY_RIGHT],
	["run", "Run (flying: throttle up, free camera: faster)", "Move", KEY_SHIFT, 0],
	["sprint", "Sprint (flying: throttle down and reverse thrust)", "Move", KEY_CTRL, 0],
	["jump", "Jump / handbrake / wheel brakes", "Move", KEY_SPACE, 0],
	["gear_up", "Gear up (flying: yaw right, free camera: up)", "Car", KEY_E, 0],
	["gear_down", "Gear down (flying: yaw left, free camera: down)", "Car", KEY_Q, 0],
	["auto_gear", "Automatic gears", "Car", KEY_G, 0],
	["diff_lock", "Diff lock", "Car", KEY_L, 0],
	["engine", "Engine on / off", "Car", KEY_I, 0],
	["view", "Change view", "Car", KEY_C, 0],
	["look_left", "Look left", "Car", KEY_Z, 0],
	["look_right", "Look right", "Car", KEY_X, 0],
	["look_back", "Look back", "Car", KEY_V, 0],
	["recover", "Recover the car: back on the nearest road", "Car", KEY_R, 0],
	["reset_car", "Reset the car where it is", "Car", KEY_BACKSPACE, 0],
	["report_spot", "Report a spot on the road", "Car", KEY_B, 0],
	["enter", "Get out of the car or vehicle, or back in", "On foot", KEY_ENTER, 0],
	["use", "Use (tickets, airport stands, building, golf ball)", "On foot", KEY_E, 0],
	["build", "Build", "On foot", KEY_B, 0],
	["park_car", "Park the car here", "On foot", KEY_V, 0],
	["fly_gear", "Landing gear", "Flying", KEY_G, 0],
	["flaps_down", "Flaps down (crashed: back on its wheels)", "Flying", KEY_R, 0],
	["flaps_up", "Flaps up", "Flying", KEY_T, 0],
	["speed_brake", "Speed brake", "Flying", KEY_B, 0],
	["shoot", "Shoot", "Weapon", MOUSE_LEFT, 0],
	["aim", "Aim", "Weapon", MOUSE_RIGHT, 0],
	["reload", "Reload", "Weapon", KEY_R, 0],
	["map", "Map", "Anywhere", KEY_M, 0],
	["phone", "Phone", "Anywhere", KEY_TAB, 0],
	["free_cam", "Free camera (again: teleport to where it is)", "Anywhere", KEY_F, 0],
	["free_cam_cancel", "Free camera: back to where you were", "Anywhere", KEY_BACKSPACE, 0],
	["road_edit", "Road editor: fix the world (in the free camera)", "Anywhere", KEY_F2, 0],
	["reload_world", "Reload the world here (when it lags, or looks wrong)", "Anywhere", KEY_F5, 0],
	["send", "Send what you made last to us (a picture, recording or road edit, on GitHub)", "Anywhere", KEY_F6, 0],
	["hide_hud", "Hide / show the on-screen text", "Anywhere", KEY_H, 0],
	["screenshot", "Save a picture of the game", "Anywhere", KEY_F12, 0],
	["record", "Debug recording: start / stop and save", "Anywhere", KEY_F4, 0],
]

static var _slots := {}	# action -> [first, second]; empty until first use


static func _load() -> void:
	if not _slots.is_empty():
		return
	for entry in ACTIONS:
		var saved := UnrealSettings.get_value("Geodrive", "Key_" + entry[0], "")
		var codes := [entry[3], entry[4]]
		if saved != "":
			var parts := saved.split(",")
			for i in mini(parts.size(), 2):
				codes[i] = parts[i].to_int()
		_slots[entry[0]] = codes


static func slots(action: String) -> Array:
	_load()
	return _slots.get(action, [0, 0])


static func _code_down(code: int) -> bool:
	if code > 0:
		return Input.is_physical_key_pressed(code)
	return code < 0 and Input.is_mouse_button_pressed(-code)


# Is this action's key or button held now?
static func down(action: String) -> bool:
	for code in slots(action):
		if _code_down(code):
			return true
	return false


# +1 for the positive action held, -1 for the negative
static func axis(negative: String, positive: String) -> float:
	return float(down(positive)) - float(down(negative))


static func code_of(event: InputEvent) -> int:
	if event is InputEventKey:
		return event.physical_keycode if event.physical_keycode != 0 else event.keycode
	if event is InputEventMouseButton:
		return -event.button_index
	return 0


# Does this key or mouse button event belong to the action? (pressed or not: the caller checks)
static func hit(event: InputEvent, action: String) -> bool:
	var code := code_of(event)
	return code != 0 and slots(action).has(code)


# Every action bound to this key code (the ladder in main.gd works from them)
static func actions_for(key: int) -> Array:
	_load()
	var found := []
	for action in _slots:
		if key != 0 and _slots[action].has(key):
			found.append(action)
	return found


static func code_name(code: int) -> String:
	if code == 0:
		return "-"
	if code < 0:
		return ["", "Mouse left", "Mouse right", "Mouse middle", "Wheel up", "Wheel down", "", "", "Mouse side 1", "Mouse side 2"][-code] if -code < 10 else "Mouse %d" % -code
	# (a key is stored by its place; its name is what's printed on it on this keyboard)
	var printed := KEY_NONE if DisplayServer.get_name() == "headless" else DisplayServer.keyboard_get_keycode_from_physical(code as Key)
	return OS.get_keycode_string(printed if printed != KEY_NONE else code)


# The action's keys as a short text for hints: "W / Up"
static func text(action: String) -> String:
	var names := PackedStringArray()
	for code in slots(action):
		if code != 0:
			names.append(code_name(code))
	return " / ".join(names) if names.size() > 0 else "(not set)"


static func set_slot(action: String, slot: int, code: int) -> void:
	var codes: Array = slots(action)
	codes[slot] = code
	_save(action)


static func reset(action: String) -> void:
	for entry in ACTIONS:
		if entry[0] == action:
			_slots[action] = [entry[3], entry[4]]
			_save(action)


static func reset_all() -> void:
	for entry in ACTIONS:
		reset(entry[0])


static func _save(action: String) -> void:
	var codes: Array = _slots[action]
	UnrealSettings.set_value("Geodrive", "Key_" + action, "%d,%d" % [codes[0], codes[1]])
