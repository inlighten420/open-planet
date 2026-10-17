# Flying: the airports (from the map: geotile airports -> Cache/regions/*.airports.json), their runways, taxiways and
# aprons drawn on the ground, and the aircraft (GeodriveAircraft: Core's flight model, GeoFlight/Flight.h) to fly from
# them. The menu's Fly page asks for a flight (an aircraft and an airport); the world is built round the airport, then
# the aircraft is put at the start of its longest runway, lined up.
#   W S / up down: pitch (S pulls the nose up)   A D / left right: roll   Q E: rudder (and the nose wheel)
#   Shift / Ctrl: throttle up / down (past 100%: the afterburner, the fighter)   G: gear   R / T: flaps down / up
#   Space: brakes   Ctrl at idle on the ground: reverse thrust (the airliners)   B: speed brake   C: cockpit or chase
#   Enter: out (on the ground, stopped)
#   Pad: left stick pitch and roll, triggers throttle, shoulders rudder, A brakes, X gear, B flaps, Y out
extends Node

const WALKER := preload("res://walker.gd")	# (its gamepad(): a real pad, never the wheel)

const KINDS := ["Fighter jet", "Airliner", "Regional jet"]
const ABOUT := [
	"An F-16's numbers: 12 t, afterburner, 9 g, rolls 240 degrees a second, supersonic",
	"An A320's numbers: 64 t, two engines, flaps, it won't stall or bank past 67 degrees",
	"An Embraer E195's numbers (George to Cape Town's jet): 42 t, two engines, six flap settings",
]
const THROTTLE_RATE := 0.5	# a second, full travel in 2 s
const STAND_REACH := 8.0	# m: on foot this near an airport's plane stand, E chooses a plane
const STAND_RUNWAY := 1000.0	# m: airports with a runway this long have one (the airliner needs ~2 km, the fighter ~1)
const RESPAWN_AFTER := 3.0

var main: Node
var world: Node
var clearings: Array = []	# (no plants on its runways, taxiways, aprons: main.update_plant_clearings)
var airports: Array = []	# from the files: {name, icao, iata, lat, lon, runways: [{ref, width, surface, line}], taxiways, aprons}
var aircraft: Node3D = null	# GeodriveAircraft, when there is one
var flying := false	# the player is in it
var kind := 2
var throttle := 0.0	# 0-1, past 1 the afterburner (to 1.05)
var reverse := 0.0	# reverse thrust asked for (Ctrl at idle, on the ground)
var flaps := 0
var gear_down := true
var speed_brake := false
var start := {}	# where it was put: {ground: Vector3 (Godot), heading, airport}
var pending := {}	# a flight asked for: {kind, airport}, until the ground there is in
var crashed_for := 0.0	# (a wait before it goes back to the runway: only a flight asked for elsewhere now)
var _told_crash := false
var force_controls := {}	# (a test's: these controls instead of the player's)
var _runways: Node3D
var _drift := Vector3.ZERO	# (an airport drawn over several frames: how far Godot's 0 has moved since it was begun)
var _area_number := 0	# (a new area: an airport still being drawn for the old one is dropped)
var _built := {}	# airport index -> true (drawn)
var _stands: Array = []	# the plane stands drawn: [{node, airport}] (their nodes under _runways, moving with it)
var _chooser: PanelContainer	# E at a stand: which plane
var _chooser_airport := {}
var _hint := 0.0	# (the "E: choose a plane" line, now and then)
var _last := Vector3.ZERO
var velocity := Vector3.ZERO	# m/s (Godot), for the world's focus
var _test_time := 0.0	# --planetest, --cruisetest
var _cruise_from := Vector3.ZERO
var _cruise_km := 0.0
var _cruise_alt := 3048.0	# m (10,000 ft)
var _test_mark := 0.0


func _ready() -> void:
	_runways = Node3D.new()
	_runways.name = "Runways"
	add_child(_runways)
	_load_airports()
	for id in Input.get_connected_joypads():
		print("Geodrive: controller %d: %s (%s)" % [id, Input.get_joy_name(id), "a gamepad" if id == WALKER.gamepad() else "not read as a gamepad"])


func _load_airports() -> void:
	airports.clear()	# (again after a region's made: all of them read afresh)
	var folder := GameSettings.cache_dir().path_join("regions")
	for file in DirAccess.get_files_at(folder):
		if not file.ends_with(".airports.json"):
			continue
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(folder.path_join(file)))
		if parsed is Dictionary:
			airports.append_array(parsed.get("airports", []))
	print("Geodrive: %d airports" % airports.size())
	# no trees or bushes on them: the runways' strips 75 m each side, taxiways 20, aprons 10; the world leaves them out
	# as it builds each piece, as it does the roads (so they're never there to clear)
	clearings = []
	for airport in airports:
		for apron in airport["aprons"]:
			clearings.append([10.0, true, _lat_lons(apron)])
		for taxiway in airport["taxiways"]:
			clearings.append([20.0, false, _lat_lons(taxiway["line"])])
		for runway in airport["runways"]:
			clearings.append([75.0, false, _lat_lons(runway["line"])])
	main.update_plant_clearings()


func _lat_lons(line: Array) -> PackedFloat64Array:
	var out := PackedFloat64Array()
	for p in line:
		out.append(p[0])
		out.append(p[1])
	return out


# --- finding airports ---

static func runway_length(runway: Dictionary) -> float:
	var line: Array = runway["line"]
	var metres := 0.0
	for i in line.size() - 1:
		metres += _metres(line[i][0], line[i][1], line[i + 1][0], line[i + 1][1])
	return metres


static func _metres(lat_a: float, lon_a: float, lat_b: float, lon_b: float) -> float:
	var y := (lat_b - lat_a) * 111320.0
	var x := (lon_b - lon_a) * 111320.0 * cos(deg_to_rad(lat_a))
	return sqrt(x * x + y * y)


static func longest(airport: Dictionary) -> Dictionary:
	var best := {}
	for runway in airport["runways"]:
		if best.is_empty() or runway_length(runway) > runway_length(best):
			best = runway
	return best


static func label(airport: Dictionary) -> String:
	var name: String = airport["name"] if airport["name"] != "" else "Airstrip"
	var code: String = airport["icao"] if airport["icao"] != "" else airport["iata"]
	return name + ("  (%s)" % code if code != "" else "")


# The nearest airports with a runway long enough (m), nearest first: [{airport, km}]
func nearest(lat: float, lon: float, count: int, min_runway: float = 800.0) -> Array:
	var found: Array = []
	for airport in airports:
		if runway_length(longest(airport)) < min_runway:
			continue
		found.append({"airport": airport, "km": _metres(lat, lon, airport["lat"], airport["lon"]) / 1000.0})
	found.sort_custom(func(a, b): return a["km"] < b["km"])
	return found.slice(0, count)


# By name or code (George, FACT, CPT)
func search(text: String, count: int = 12) -> Array:
	var wanted := text.strip_edges().to_lower()
	var found: Array = []
	if wanted == "":
		return found
	for airport in airports:
		var name: String = String(airport["name"]).to_lower()
		if wanted in name or wanted == String(airport["icao"]).to_lower() or wanted == String(airport["iata"]).to_lower():
			found.append({"airport": airport, "km": -1.0})
	found.sort_custom(func(a, b): return runway_length(longest(a["airport"])) > runway_length(longest(b["airport"])))
	return found.slice(0, count)


# --- a flight ---

# From the menu: this aircraft at this airport (the world is built round it first)
func request(new_kind: int, airport: Dictionary) -> void:
	kind = new_kind
	var runway := longest(airport)
	var line: Array = runway["line"]
	pending = {"kind": new_kind, "airport": airport, "runway": runway}
	if world.contains_lat_lon(line[0][0], line[0][1]):
		# already built: straight there, the world turning to the runway meanwhile (the car stays where it is)
		pending["at"] = world.from_lat_lon(line[0][0], line[0][1])
		main.hud.set_status("To %s..." % label(airport), 5.0)
	else:
		# beyond what's built: a new area round the airport (the car goes to a road by the runway)
		main.teleport(line[0][0], line[0][1], label(airport))


func _start_pending() -> bool:
	if pending.is_empty() or main.waiting != "" or not world.is_ready():
		return false
	var runway: Dictionary = pending["runway"]
	var line: Array = runway["line"]
	var first: Vector3 = world.from_lat_lon(line[0][0], line[0][1])
	var second: Vector3 = world.from_lat_lon(line[1][0], line[1][1])
	var along := Vector3(second.x - first.x, 0.0, second.z - first.z).normalized()
	var at := first + along * 60.0	# (a little in from the end, on the centreline)
	# (ready once there's solid ground under all its wheels and some way ahead: rays find the runway's collision there)
	var space: PhysicsDirectSpaceState3D = main.get_world_3d().direct_space_state
	var side := Vector3(-along.z, 0.0, along.x)
	for spot in [at, at + along * 20.0, at + side * 5.0, at - side * 5.0, at + along * 150.0]:
		if space.intersect_ray(PhysicsRayQueryParameters3D.create(spot + Vector3.UP * 50.0, spot + Vector3.DOWN * 50.0)).is_empty():
			return false
	_draw_airport(pending["airport"])
	var heading := fposmod(rad_to_deg(atan2(along.x, -along.z)), 360.0)
	start = {"ground": _ground_under(at), "heading": heading, "airport": pending["airport"]}
	pending = {}
	_spawn()
	return true


func _spawn() -> void:
	if aircraft == null or aircraft.get_kind() != kind:
		if aircraft:
			aircraft.queue_free()
		aircraft = ClassDB.instantiate("GeodriveAircraft")
		aircraft.name = "Aircraft"
		aircraft.setup(kind)
		var body := MeshInstance3D.new()
		body.mesh = ClassDB.class_call_static("GeodriveWorld", "make_aircraft_mesh", kind, false)
		body.material_override = _paint()
		aircraft.add_child(body)
		var gear := MeshInstance3D.new()
		gear.name = "Gear"
		gear.mesh = ClassDB.class_call_static("GeodriveWorld", "make_aircraft_mesh", kind, true)
		gear.material_override = _paint()
		aircraft.add_child(gear)
		main.add_child(aircraft)
	aircraft.place(start["ground"], start["heading"], 0.0)
	throttle = 0.0
	flaps = 2 if kind != 0 else 0	# (take-off flaps)
	gear_down = true
	speed_brake = false
	crashed_for = 0.0
	_last = aircraft.global_position
	_told_crash = false
	if main.cruise_test:
		# airborne at 10,000 ft over the airport, 300 kt, towards George (about 100 degrees from Cape Town)
		aircraft.place(start["ground"] + Vector3.UP * (_cruise_alt - start["ground"].y), 100.0, 155.0)
		gear_down = false
		flaps = 0
		_cruise_from = aircraft.global_position
	board()
	main.hud.set_status("%s at %s: Shift for power, S to lift the nose at %d kt" % [KINDS[kind], label(start["airport"]), [140, 145, 135][kind]], 6.0)


# --- the plane stands: one at each airport with a runway long enough, by its longest runway's start, on the side
# towards the airport's middle (its terminals, mostly); on foot there, E and you choose a plane, and you're in it at
# that runway's start ---

# Where an airport's stand is (lat, lon; none: no runway long enough): 80 m in along its longest runway from its
# start, 30 m off its edge on the side towards the airport's middle
static func stand_lat_lon(airport: Dictionary) -> PackedFloat64Array:
	var runway := longest(airport)
	if runway.is_empty() or runway_length(runway) < STAND_RUNWAY:
		return PackedFloat64Array()
	var line: Array = runway["line"]
	var lat0: float = line[0][0]
	var lon0: float = line[0][1]
	var east := 111320.0 * cos(deg_to_rad(lat0))	# (m a degree)
	var along := Vector2((line[1][1] - lon0) * east, (line[1][0] - lat0) * 111320.0).normalized()	# (east, north)
	var side := Vector2(-along.y, along.x)
	var middle := Vector2((airport["lon"] - lon0) * east, (airport["lat"] - lat0) * 111320.0)
	if side.dot(middle) < 0.0:
		side = -side
	var width: float = runway["width"] if runway["width"] > 0.0 else 45.0
	var offset := along * 80.0 + side * (width * 0.5 + 30.0)
	return PackedFloat64Array([lat0 + offset.y / 111320.0, lon0 + offset.x / east])


func _add_stand(airport: Dictionary) -> void:
	var spot := stand_lat_lon(airport)
	if spot.is_empty():
		return
	var line: Array = longest(airport)["line"]
	var first: Vector3 = world.from_lat_lon(line[0][0], line[0][1])
	var second: Vector3 = world.from_lat_lon(line[1][0], line[1][1])
	var along := Vector3(second.x - first.x, 0.0, second.z - first.z).normalized()
	var at := _ground_under(world.from_lat_lon(spot[0], spot[1]))
	# (a post 6 m high with a sign board on it, and a ring on the ground round it: lit, seen from far off)
	var stand := Node3D.new()
	stand.name = "PlaneStand"
	var lit := StandardMaterial3D.new()
	lit.albedo_color = Color(1.0, 0.75, 0.1)
	lit.emission_enabled = true
	lit.emission = Color(1.0, 0.65, 0.05)
	lit.emission_energy_multiplier = 1.5
	var post := MeshInstance3D.new()
	var pole := BoxMesh.new()
	pole.size = Vector3(0.25, 6.0, 0.25)
	post.mesh = pole
	post.position = Vector3(0.0, 3.0, 0.0)
	post.material_override = lit
	stand.add_child(post)
	var board_node := MeshInstance3D.new()
	var sign := BoxMesh.new()
	sign.size = Vector3(2.4, 1.2, 0.12)
	board_node.mesh = sign
	board_node.position = Vector3(0.0, 5.6, 0.0)
	board_node.material_override = lit
	stand.add_child(board_node)
	var text := Label3D.new()
	text.text = "PLANES
(E)"
	text.font_size = 72
	text.pixel_size = 0.006
	text.modulate = Color(0.05, 0.05, 0.05)
	text.position = Vector3(0.0, 5.6, 0.07)
	text.billboard = BaseMaterial3D.BILLBOARD_FIXED_Y
	stand.add_child(text)
	var ring := MeshInstance3D.new()
	var disc := TorusMesh.new()
	disc.inner_radius = STAND_REACH - 0.4
	disc.outer_radius = STAND_REACH
	disc.rings = 48
	ring.mesh = disc
	ring.scale = Vector3(1.0, 0.1, 1.0)
	ring.position = Vector3(0.0, 0.1, 0.0)
	ring.material_override = lit
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	stand.add_child(ring)
	_runways.add_child(stand)
	stand.global_position = at
	stand.look_at(at + along, Vector3.UP)
	_stands.append({"node": stand, "airport": airport})


# The stand within reach of a point (on foot), or {}
func stand_near(at: Vector3) -> Dictionary:
	for stand in _stands:
		if is_instance_valid(stand["node"]) and Vector2(at.x - stand["node"].global_position.x, at.z - stand["node"].global_position.z).length() < STAND_REACH:
			return stand
	return {}


func chooser_open() -> bool:
	return _chooser != null and _chooser.visible


# E at a stand: the three planes, each with what it is; one chosen and you're in it at the runway's start
func open_chooser(airport: Dictionary) -> void:
	if _chooser == null:
		var layer := CanvasLayer.new()
		add_child(layer)
		_chooser = PanelContainer.new()
		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.07, 0.08, 0.09, 0.92)
		style.set_corner_radius_all(10)
		style.set_content_margin_all(18)
		_chooser.add_theme_stylebox_override("panel", style)
		layer.add_child(_chooser)
	for child in _chooser.get_children():
		child.queue_free()
	_chooser_airport = airport
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 10)
	_chooser.add_child(rows)
	var title := Label.new()
	title.text = "%s: which plane?" % label(airport)
	title.add_theme_font_size_override("font_size", 24)
	rows.add_child(title)
	for k in KINDS.size():
		var button := Button.new()
		button.text = "%s
%s" % [KINDS[k], ABOUT[k]]
		button.custom_minimum_size = Vector2(620.0, 64.0)
		button.focus_mode = Control.FOCUS_NONE
		button.pressed.connect(_choose.bind(k))
		rows.add_child(button)
	var note := Label.new()
	note.text = "Runway %.1f km        Esc: not now" % (runway_length(longest(airport)) / 1000.0)
	note.modulate = Color(1, 1, 1, 0.7)
	rows.add_child(note)
	_chooser.visible = true
	_chooser.reset_size()
	_chooser.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func close_chooser() -> void:
	if chooser_open():
		_chooser.visible = false
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _choose(k: int) -> void:
	close_chooser()
	request(k, _chooser_airport)


func _unhandled_input(event: InputEvent) -> void:
	if chooser_open() and event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		close_chooser()
		get_viewport().set_input_as_handled()


func _paint() -> StandardMaterial3D:
	var paint := StandardMaterial3D.new()
	paint.vertex_color_use_as_albedo = true
	paint.roughness = 0.45
	paint.metallic = 0.1
	return paint


# The player in (from the walker, or put there)
func board() -> void:
	main._remove_walker()	# (from a stand, or back in at its door)
	flying = true
	main.camera.aircraft = aircraft
	main.hud.set_on_foot(true)


# Out, beside the aircraft's door (on the ground, stopped)
func can_leave() -> bool:
	var r: Dictionary = aircraft.readout()
	return aircraft.is_crashed() or (r["on_ground"] and r["ground_speed"] < 3.0)	# (a wreck: out of it wherever it is)


# Crashed: back on its wheels where it is (on the ground under it, facing the way it was), to try again from there
func recover() -> void:
	if aircraft == null or not aircraft.is_crashed():
		return
	var heading: float = aircraft.readout()["heading"]
	aircraft.place(_flattest_near(aircraft.global_position, heading) + Vector3.UP * 0.3, heading, 0.0)
	_told_crash = false
	gear_down = true
	_last = aircraft.global_position
	main.hud.set_status("Back on its wheels. Throttle up to go again", 3.0)
	print("Geodrive: the %s put back on its wheels where it crashed" % KINDS[kind].to_lower())


func leave() -> void:
	flying = false
	main.camera.aircraft = null


# Done flying (a car chosen, a drive somewhere): the aircraft gone, nothing waiting to put it back
func stop() -> void:
	flying = false
	pending = {}
	crashed_for = 0.0
	main.camera.aircraft = null
	if aircraft:
		aircraft.queue_free()
		aircraft = null


func near_door(at: Vector3) -> bool:
	return aircraft != null and at.distance_to(aircraft.global_position) < aircraft.get_chase_distance() * 0.45 + 4.0


func door() -> Vector3:
	# the left side, a little ahead of the middle (the front door; the fighter's ladder)
	var basis := aircraft.global_transform.basis
	var side: float = [3.0, 4.5, 3.8][kind]
	var ahead: float = [2.0, 8.0, 9.0][kind]
	var out: Vector3 = -basis.x * side - basis.z * ahead
	return aircraft.global_position + out


# A new area (a new frame): the airports drawn again as they come in
func reset_area() -> void:
	for child in _runways.get_children():
		child.queue_free()
	_runways.position = Vector3.ZERO
	_built = {}
	_stands = []
	_area_number += 1


func shift(offset: Vector3) -> void:
	if pending.has("at"):
		pending["at"] += offset
	if aircraft:
		aircraft.shift(offset)
	_runways.position += offset
	if not start.is_empty():
		start["ground"] += offset
	_cruise_from += offset
	_last += offset


# --- each step ---

func physics(delta: float) -> void:
	if not pending.is_empty():
		_start_pending()
	# (on foot at a plane stand: what E does there, now and then)
	_hint -= delta
	if _hint <= 0.0 and main.walker and not flying and not chooser_open():
		_hint = 1.0
		var stand := stand_near(main.walker.global_position)
		if not stand.is_empty():
			main.hud.set_status("%s: E to choose a plane" % label(stand["airport"]), 1.2)
	if aircraft == null:
		return
	if crashed_for > 0.0:
		crashed_for -= delta
		if crashed_for <= 0.0 and pending.is_empty():
			# back to the runway as at the start: once its ground is solid again (flown away, it may have been let go:
			# put back at once, the aircraft fell through it), the area built again round it if it's far
			request(kind, start["airport"])
		return
	var controls := (force_controls if not force_controls.is_empty() else _cruise_controls(delta) if main.cruise_test else _test_controls(delta) if main.plane_test else _controls(delta)) if flying else {"throttle": 0.0, "brakes": 1.0, "gear": true}
	aircraft.fly(delta, controls)
	velocity = (aircraft.global_position - _last) / maxf(delta, 0.0001)
	_last = aircraft.global_position
	var gear := aircraft.get_node_or_null("Gear")
	if gear:
		gear.visible = aircraft.get_gear() > 0.5
	# (well under the ground, however it got there (its ground let go under it): back on top of it, where it is)
	if not aircraft.is_crashed() and aircraft.global_position.y < world.ground_height(aircraft.global_position) - 30.0:
		var heading: float = aircraft.readout()["heading"]
		aircraft.place(_ground_under(aircraft.global_position), heading, 0.0)
		_last = aircraft.global_position
		main.hud.set_status("Fell through the ground: put back on it", 3.0)
		print("Geodrive: the %s fell through the ground; put back on it where it was" % KINDS[kind].to_lower())
		return
	# crashed: the wreck stays where it is. R puts it back on its wheels there; Enter, out of it (and on foot, a car
	# can be had there: Menu > Settings > Game, or the car key)
	if aircraft.is_crashed() and not _told_crash:
		_told_crash = true
		var r: Dictionary = aircraft.readout()
		main.hud.set_status("Crashed: %s.  R: back on its wheels here and try again.  Enter: get out" % r["crash"], 8.0)
		print("Geodrive: the %s crashed: %s, at %d kt, %d ft" % [KINDS[kind].to_lower(), r["crash"], roundi(r["airspeed"]), roundi(r["altitude"])])


func _controls(delta: float) -> Dictionary:
	var keys: bool = main._keys_drive() and not main.camera.free and not main.camera.map
	var pitch := 0.0
	var roll := 0.0
	var yaw := 0.0
	var brakes := 0.0
	if keys:
		pitch = float(Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN)) - float(Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP))
		roll = float(Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT)) - float(Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT))
		yaw = float(Input.is_key_pressed(KEY_E)) - float(Input.is_key_pressed(KEY_Q))
		var pad: int = WALKER.gamepad()
		brakes = float(Input.is_key_pressed(KEY_SPACE) or (pad >= 0 and Input.is_joy_button_pressed(pad, JOY_BUTTON_A)))
		var stick := Vector2(Input.get_joy_axis(pad, JOY_AXIS_LEFT_X), Input.get_joy_axis(pad, JOY_AXIS_LEFT_Y)) if pad >= 0 else Vector2.ZERO
		if stick.length() > 0.12:
			roll = stick.x
			pitch = stick.y	# (pulled back: nose up)
		var up := float(Input.is_key_pressed(KEY_SHIFT))
		var down := float(Input.is_key_pressed(KEY_CTRL))
		if pad >= 0:
			yaw += float(Input.is_joy_button_pressed(pad, JOY_BUTTON_RIGHT_SHOULDER)) - float(Input.is_joy_button_pressed(pad, JOY_BUTTON_LEFT_SHOULDER))
			up += Input.get_joy_axis(pad, JOY_AXIS_TRIGGER_RIGHT)
			down += Input.get_joy_axis(pad, JOY_AXIS_TRIGGER_LEFT)
		var top := 1.05 if kind == 0 else 1.0
		# (Ctrl at idle on the ground: reverse thrust, the airliners'; let go and it's off)
		var r: Dictionary = aircraft.readout()
		reverse = 1.0 if down > 0.5 and throttle <= 0.0 and r["on_ground"] and kind != 0 else 0.0
		throttle = clampf(throttle + (up - down) * THROTTLE_RATE * delta, 0.0, top)
		# (the afterburner's detent: the throttle stops at 100% a moment before it goes past)
		if kind == 0 and throttle > 1.0 and up < 0.5:
			throttle = maxf(throttle, 1.0)
	return {"pitch": pitch, "roll": roll, "yaw": yaw, "throttle": minf(throttle, 1.0), "afterburner": throttle > 1.0,
		"flaps": flaps, "gear": gear_down, "brakes": brakes, "speed_brake": speed_brake, "reverse": reverse}


# --planetest: full power (the afterburner, the fighter), the nose up at the real rotation speed to the climb's
# attitude, the gear up at 50 ft, flaps up at 1000 ft; the instruments printed every 5 s, a picture at 20 and 45 s
func _test_controls(delta: float) -> Dictionary:
	_test_time += delta
	var r: Dictionary = aircraft.readout()
	var rotate: float = [140.0, 145.0, 135.0][kind]
	var attitude: float = [11.0, 15.0, 14.0][kind]
	var pitch := 0.0
	if r["airspeed"] >= rotate:
		pitch = 1.0 if r["pitch"] < attitude else 0.0
		if not r["on_ground"]:
			pitch = clampf((attitude - r["pitch"]) * 0.08, -0.4, 0.4)
	if r["height"] > 15.0:
		gear_down = false
	if r["height"] > 300.0:
		flaps = 0
	if _test_time >= _test_mark:
		_test_mark += 5.0
		print("Geodrive plane test at %.0f s: %s" % [_test_time, instruments().replace("
", " | ")])
	if main.shot_path != "" and (absf(_test_time - 20.0) < delta * 0.6 or absf(_test_time - 45.0) < delta * 0.6):
		main._save(main.shot_path.get_basename() + "_%ds.png" % roundi(_test_time))
	if _test_time > 70.0 or aircraft.is_crashed():
		if aircraft.is_crashed():
			print("Geodrive plane test: crashed: ", aircraft.readout()["crash"])
		main.get_tree().quit()
	return {"pitch": pitch, "roll": 0.0, "yaw": 0.0, "throttle": 1.0, "afterburner": kind == 0, "flaps": flaps, "gear": gear_down,
		"brakes": 0.0, "speed_brake": false}


# --cruisetest: level at 10,000 ft, wings level, 300 kt, for 4 minutes: is the world built ahead in time, and how
# smoothly does it run (the distance, the height over the ground, the world 10 km ahead, every 10 s)
func _cruise_controls(delta: float) -> Dictionary:
	_test_time += delta
	var r: Dictionary = aircraft.readout()
	var altitude := aircraft.global_position.y
	var pitch := clampf((_cruise_alt - altitude) * 0.004 - r["climb"] * 0.00012, -0.5, 0.5)
	var roll := clampf(-r["bank"] * 0.05, -0.5, 0.5)
	var power := clampf(0.82 + (300.0 - r["airspeed"]) * 0.01, 0.4, 1.0)
	if _test_time >= _test_mark:
		_test_mark += 10.0
		var ahead := aircraft.global_position - aircraft.global_transform.basis.z * 10000.0
		var at: PackedFloat64Array = world.to_lat_lon(ahead)
		print("Geodrive cruise test at %.0f s: %.1f km flown, %d kt, %s ft above the ground, world 10 km ahead: %s; %s" % [_test_time,
			Vector2(aircraft.global_position.x - _cruise_from.x, aircraft.global_position.z - _cruise_from.z).length() / 1000.0 + _cruise_km,
			roundi(r["airspeed"]), str(roundi(r["height"])) if r["height"] >= 0.0 else "no ground under it", "yes" if world.contains_lat_lon(at[0], at[1]) else "NO", world.get_status()])
	for at in [30.0, 120.0]:
		if main.shot_path != "" and absf(_test_time - at) < delta * 0.6:
			main._save(main.shot_path.get_basename() + "_%ds.png" % roundi(at))
	if _test_time > 240.0 or aircraft.is_crashed():
		if aircraft.is_crashed():
			print("Geodrive cruise test: crashed: ", r["crash"])
		main.get_tree().quit()
	return {"pitch": pitch, "roll": roll, "yaw": 0.0, "throttle": power, "flaps": 0, "gear": false, "brakes": 0.0, "speed_brake": false}


# A key while flying (main passes them on); true if it was taken
func key(code: Key) -> bool:
	if aircraft.is_crashed() and code == KEY_R:
		recover()
		return true
	var names: PackedStringArray = aircraft.get_flap_names()
	match code:
		KEY_G:
			gear_down = not gear_down
			main.hud.set_status("Gear " + ("down" if gear_down else "up"), 1.5)
		KEY_R:
			flaps = mini(flaps + 1, names.size() - 1)
			main.hud.set_status("Flaps " + names[flaps], 1.5)
		KEY_T:
			flaps = maxi(flaps - 1, 0)
			main.hud.set_status("Flaps " + names[flaps], 1.5)
		KEY_B:
			speed_brake = not speed_brake
			main.hud.set_status("Speed brake " + ("out" if speed_brake else "in"), 1.5)
		KEY_C:
			main.camera.plane_cockpit = not main.camera.plane_cockpit
			main.hud.set_status("Cockpit" if main.camera.plane_cockpit else "Chase", 1.5)
		_:
			return false
	return true


func pad(button: JoyButton) -> bool:
	match button:
		JOY_BUTTON_X:
			return key(KEY_G)
		JOY_BUTTON_B:
			var names: PackedStringArray = aircraft.get_flap_names()
			flaps = (flaps + 1) % names.size()
			main.hud.set_status("Flaps " + names[flaps], 1.5)
			return true
	return false


# The instruments, for the HUD
func instruments() -> String:
	if aircraft == null:
		return ""
	var r: Dictionary = aircraft.readout()
	var names: PackedStringArray = aircraft.get_flap_names()
	var lines := PackedStringArray()
	# (the airspeed in knots, as pilots fly by; and how fast it's going over the ground, in km/h)
	lines.append("%d kt   %d km/h   %s ft   %+d ft/min" % [roundi(r["airspeed"]), roundi(r["ground_speed"] * 1.852), _thousands(roundi(r["altitude"])), roundi(r["climb"] / 10.0) * 10])
	var power := "%d%%" % roundi(r["engine"] * 100.0) + (" AFTERBURNER" if r["afterburner"] else "")
	lines.append("heading %03d   power %s   %.1f g%s" % [roundi(r["heading"]) % 360, power, r["g"], ("   Mach %.2f" % r["mach"]) if r["mach"] > 0.6 else ""])
	var bits := PackedStringArray()
	if names.size() > 1:
		bits.append("flaps " + names[clampi(flaps, 0, names.size() - 1)])
	bits.append("gear " + ("down" if r["gear"] > 0.99 else ("up" if r["gear"] < 0.01 else "moving")))
	if speed_brake:
		bits.append("SPEED BRAKE")
	if r["spoilers"]:
		bits.append("SPOILERS")
	if r["reverse"]:
		bits.append("REVERSE")
	# (over the ground, below 2,500 ft as a radio altimeter shows it; from the terrain where there's no collision to measure)
	var height: float = r["height"] if r["height"] >= 0.0 else (aircraft.global_position.y - world.ground_height(aircraft.global_position)) * 3.28084
	if height < 2500.0 and not r["on_ground"]:
		bits.append("%d ft above the ground" % roundi(height))
	lines.append("   ".join(bits))
	# what to do next, on the runway: power, then the nose up at the rotation speed
	if r["on_ground"] and not r["crashed"]:
		var rotate: float = [140.0, 145.0, 135.0][kind]
		if throttle < 0.9 and r["airspeed"] < 40.0:
			lines.append("Hold Shift for power")
		elif r["airspeed"] >= rotate - 5.0:
			lines.append("Hold S (or Down) to lift off")
		elif r["airspeed"] >= 40.0 and throttle < 0.1:
			lines.append("Space: brakes" + ("   hold Ctrl: reverse" if kind != 0 else "   B: speed brake"))
		elif r["airspeed"] >= 40.0:
			lines.append("Lift off at %d kt: hold S then" % roundi(rotate))
	if r["stall"]:
		lines.append("STALL")
	return "\n".join(lines)


static func _thousands(n: int) -> String:
	var s := str(absi(n))
	var out := ""
	while s.length() > 3:
		out = "," + s.substr(s.length() - 3) + out
		s = s.substr(0, s.length() - 3)
	return ("-" if n < 0 else "") + s + out


# The flattest ground within 40 m that the aircraft fits on (its nose to tail and wingtip to wingtip, roughly: put down
# level on a slope its high end is in the ground, a crash before it moves): the nearest of the flattest, its ground
func _flattest_near(at: Vector3, heading: float) -> Vector3:
	var reach := clampf(aircraft.get_chase_distance() * 0.4, 6.0, 30.0)	# (about half its length or span)
	var ahead := Vector3(sin(deg_to_rad(heading)), 0.0, -cos(deg_to_rad(heading)))
	var side := Vector3(-ahead.z, 0.0, ahead.x)
	var best := at
	var best_score := INF
	for ring: float in [0.0, 10.0, 20.0, 30.0, 40.0]:
		for k in (1 if ring == 0.0 else 8):
			var spot: Vector3 = at + Vector3(cos(k * TAU / 8.0), 0.0, sin(k * TAU / 8.0)) * ring
			var middle: float = world.ground_height(spot)
			var spread := 0.0
			for d in [ahead, -ahead, side, -side]:
				spread = maxf(spread, absf(world.ground_height(spot + d * reach) - middle))
			var score: float = spread + ring * 0.01	# (a little flatter's worth a little further)
			if score < best_score:
				best_score = score
				best = spot
	return _ground_under(best)


func _ground_under(at: Vector3) -> Vector3:
	var space: PhysicsDirectSpaceState3D = main.get_world_3d().direct_space_state
	var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(at + Vector3.UP * 50.0, at + Vector3.DOWN * 50.0))
	return hit["position"] if hit else Vector3(at.x, world.ground_height(at), at.z)


# --- the airports drawn: runways (asphalt, white edges, dashed centreline, the piano keys at each end), taxiways
# (with a yellow centreline) and aprons, laid on the ground ---

func draw_near(at: Vector3, km: float) -> void:
	var here: PackedFloat64Array = world.to_lat_lon(at)
	for i in airports.size():
		if _built.has(i):
			continue
		var airport: Dictionary = airports[i]
		if _metres(here[0], here[1], airport["lat"], airport["lon"]) < km * 1000.0 and world.contains_lat_lon(airport["lat"], airport["lon"]):
			_draw_airport(airport)


func _draw_airport(airport: Dictionary) -> void:
	var index := airports.find(airport)
	if _built.has(index):
		return
	_built[index] = true
	# (a few of its pieces a frame, ~2 ms: all at once a big airport (Cape Town's) took 50 ms, a jolt as it came in.
	# Each piece in the frame it was begun in, so a move of Godot's 0 meanwhile doesn't tear it)
	var area := _area_number
	var base := _runways.position
	var turn := {"start": Time.get_ticks_usec()}
	var pause := func() -> bool:
		if Time.get_ticks_usec() - turn["start"] > 2000:
			await get_tree().process_frame
			turn["start"] = Time.get_ticks_usec()
		_drift = _runways.position - base
		return area == _area_number
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	var asphalt := Color(0.09, 0.09, 0.095)
	var concrete := Color(0.3, 0.3, 0.29)
	var white := Color(0.85, 0.85, 0.85)
	var yellow := Color(0.8, 0.6, 0.05)
	for apron in airport["aprons"]:
		if not await pause.call():
			return
		_area(tool, apron, concrete, 0.18)
	for taxiway in airport["taxiways"]:
		if not await pause.call():
			return
		var width: float = taxiway["width"] if taxiway["width"] > 0.0 else 23.0
		_strip(tool, taxiway["line"], width, asphalt, 0.2)
		_strip(tool, taxiway["line"], 0.3, yellow, 0.23)
	for runway in airport["runways"]:
		if not await pause.call():
			return
		var width: float = runway["width"] if runway["width"] > 0.0 else 45.0
		var line: Array = runway["line"]
		_strip(tool, line, width, asphalt, 0.25)
		_strip(tool, line, width - 2.0, white, 0.27, 0.0, -1.0, true)	# (the edge lines: drawn as the strip's two edges)
		_strip(tool, line, width - 3.8, asphalt, 0.28)
		_strip(tool, line, 0.9, white, 0.3, 30.0, 20.0)	# (the centreline: 30 m dashes, 20 m gaps)
		_piano_keys(tool, line, width, white, false)
		_piano_keys(tool, line, width, white, true)
	if not await pause.call():
		return
	_add_stand(airport)
	var node := MeshInstance3D.new()
	node.mesh = tool.commit()
	var paint := StandardMaterial3D.new()
	paint.vertex_color_use_as_albedo = true
	paint.roughness = 0.9
	paint.cull_mode = BaseMaterial3D.CULL_DISABLED	# (seen from above either way)
	node.material_override = paint
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_runways.add_child(node)
	node.position = -base	# (where Godot's 0 was when it was begun)


func _points(line: Array) -> PackedVector3Array:
	var out := PackedVector3Array()
	for p in line:
		out.append(world.from_lat_lon(p[0], p[1]))
	return out


# A whole strip along a line as one ribbon: every point's sides mitred between the pieces either side of it (no
# saw-teeth on bends), every 10 m on the ground
func _ribbon(tool: SurfaceTool, line: Array, width: float, colour: Color, lift: float) -> void:
	var points: Array[Vector3] = []
	for p in line:
		var at: Vector3 = world.from_lat_lon(p[0], p[1])
		if points.is_empty() or Vector2(at.x - points[-1].x, at.z - points[-1].z).length() > 0.5:
			points.append(at)
	if points.size() < 2:
		return
	# (every 10 m)
	var fine: Array[Vector3] = [points[0]]
	for i in points.size() - 1:
		var steps := maxi(1, ceili(Vector2(points[i + 1].x - points[i].x, points[i + 1].z - points[i].z).length() / 10.0))
		for s in steps:
			fine.append(points[i].lerp(points[i + 1], float(s + 1) / steps))
	var sides: Array[Vector3] = []
	for i in fine.size():
		var before := fine[maxi(i - 1, 0)]
		var after := fine[mini(i + 1, fine.size() - 1)]
		var dir := Vector3(after.x - before.x, 0.0, after.z - before.z).normalized()
		sides.append(Vector3(-dir.z, 0.0, dir.x) * width * 0.5)
	for i in fine.size() - 1:
		_quad(tool, fine[i] - sides[i], fine[i] + sides[i], fine[i + 1] + sides[i + 1], fine[i + 1] - sides[i + 1], colour, lift)


# A strip along a line (lat/lon points), Width wide, Lift over the ground, in dashes (Dash, Gap m) or whole
func _strip(tool: SurfaceTool, line: Array, width: float, colour: Color, lift: float, dash: float = 0.0, gap: float = 0.0, edges_only: bool = false) -> void:
	if dash <= 0.0 and not edges_only:
		_ribbon(tool, line, width, colour, lift)
		return
	var points: Array[Vector3] = []
	for p in line:
		points.append(world.from_lat_lon(p[0], p[1]))
	var along := 0.0
	for i in points.size() - 1:
		var a := points[i]
		var b := points[i + 1]
		var length := Vector2(b.x - a.x, b.z - a.z).length()
		if length < 0.5:
			continue
		var dir := Vector3(b.x - a.x, 0.0, b.z - a.z) / length
		var side := Vector3(-dir.z, 0.0, dir.x)
		var steps := maxi(1, ceili(length / 20.0))
		for s in steps:
			var t0 := float(s) / steps * length
			var t1 := float(s + 1) / steps * length
			if dash > 0.0:
				var phase := fmod(along + t0, dash + gap)
				if phase > dash:
					continue
				t1 = minf(t1, t0 + dash - phase)
			var p0 := a + dir * t0
			var p1 := a + dir * t1
			if edges_only:
				for edge in [-1.0, 1.0]:
					var o0: Vector3 = p0 + side * (edge * width * 0.5)
					var o1: Vector3 = p1 + side * (edge * width * 0.5)
					_quad(tool, o0 - side * 0.45, o0 + side * 0.45, o1 + side * 0.45, o1 - side * 0.45, colour, lift)
			else:
				_quad(tool, p0 - side * width * 0.5, p0 + side * width * 0.5, p1 + side * width * 0.5, p1 - side * width * 0.5, colour, lift)
		along += length


# The threshold's stripes ("piano keys") at the start (or the end) of a runway
func _piano_keys(tool: SurfaceTool, line: Array, width: float, colour: Color, at_end: bool) -> void:
	var a: Vector3 = world.from_lat_lon(line[-1][0] if at_end else line[0][0], line[-1][1] if at_end else line[0][1])
	var b: Vector3 = world.from_lat_lon(line[-2][0] if at_end else line[1][0], line[-2][1] if at_end else line[1][1])
	var dir := Vector3(b.x - a.x, 0.0, b.z - a.z).normalized()
	var side := Vector3(-dir.z, 0.0, dir.x)
	var stripes := 12
	var span := width - 6.0
	for k in stripes:
		var offset := -span * 0.5 + (k + 0.5) * span / stripes
		if absf(offset) < 2.5:
			continue	# (a gap at the middle)
		var c := a + dir * 6.0 + side * offset
		var half := span / stripes * 0.3
		_quad(tool, c - side * half, c + side * half, c + side * half + dir * 30.0, c - side * half + dir * 30.0, colour, 0.31)


# An apron: a polygon (lat/lon), filled
func _area(tool: SurfaceTool, ring: Array, colour: Color, lift: float) -> void:
	var flat := PackedVector2Array()
	var points: Array[Vector3] = []
	for p in ring:
		var at: Vector3 = world.from_lat_lon(p[0], p[1])
		points.append(at)
		flat.append(Vector2(at.x, at.z))
	if flat.size() > 3 and flat[0] == flat[flat.size() - 1]:
		flat.remove_at(flat.size() - 1)
		points.remove_at(points.size() - 1)
	var triangles := Geometry2D.triangulate_polygon(flat)
	for t in range(0, triangles.size(), 3):
		for k in [0, 2, 1]:
			var p := points[triangles[t + k]]
			tool.set_color(colour)
			tool.set_normal(Vector3.UP)
			tool.add_vertex(Vector3(p.x, world.ground_height(p) + lift, p.z) - _drift)


func _quad(tool: SurfaceTool, a: Vector3, b: Vector3, c: Vector3, d: Vector3, colour: Color, lift: float) -> void:
	var corners := [a, b, c, d]
	for i in 4:
		var p: Vector3 = corners[i]
		corners[i] = Vector3(p.x, world.ground_height(p) + lift, p.z)
	for i in [0, 2, 1, 0, 3, 2]:
		tool.set_color(colour)
		tool.set_normal(Vector3.UP)
		tool.add_vertex(corners[i] - _drift)
