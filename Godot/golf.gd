# Golf on the real courses: a course's holes as OpenStreetMap has them (geotile games: tee to pin, par, its greens and
# bunkers), played on the real ground. The ball flies as a real one does: 45.9 g, 42.7 mm, its drag and the lift of
# its backspin (both by its spin rate), landing, bouncing and rolling as the surface it's on lets it (a green, the
# fairway, the rough, a bunker) and down the slope of the real ground; holed if it drops in the cup (108 mm) slowly
# enough. The clubs are an average golfer's (ball speed, launch, spin: a driver carries ~200 m, a 7 iron ~130 m).
#   Walk to your ball; E by it: address it (the view behind it, the pin ahead). Mouse: aim; wheel: the club; hold the
#   left button: the swing's power (up, then back down), let go: the shot. Right click: step away. T (walking): to
#   your ball (the walk skipped). F1: stop playing.
# A stroke a shot; out of the course (past its edge): a stroke more, from where you hit it. Par and score on screen.
extends Node

const MASS := 0.04593	# kg
const RADIUS := 0.021335	# m
const CUP := 0.054	# m: the cup's radius
const CUP_SPEED := 1.6	# m/s: faster than this it lips out
const AIR := 1.225	# kg/m^3
const GRAVITY := 9.81
# [name, ball speed m/s, launch deg, spin rpm] at full power (an average club golfer's)
const CLUBS := [["Driver", 62.0, 13.0, 2900.0], ["3 wood", 58.0, 13.0, 3800.0], ["5 iron", 51.0, 16.0, 5500.0], ["7 iron", 47.0, 19.0, 6800.0],
	["9 iron", 42.0, 24.0, 8200.0], ["Wedge", 37.0, 30.0, 9200.0], ["Sand wedge", 33.0, 33.0, 9800.0], ["Putter", 9.0, 0.0, 0.0]]
# each surface: [bounce (of the speed into it), kept along it on landing, rolling slows (m/s^2)]
const SURFACES := {"green": [0.25, 0.6, 0.55], "fairway": [0.35, 0.7, 1.4], "rough": [0.2, 0.4, 3.5], "bunker": [0.05, 0.1, 8.0]}

var main: Node
var active := false
var reserve := {}	# (the course's game)
var course := {}	# its file: {name, holes [{ref, par, line}], greens, bunkers}
var hole := 0
var strokes: Array = []	# per hole
var state := "walk"	# walk, address, flight
var club := 0
var aim := 0.0	# rad: the shot's heading (about up)
var power := 0.0
var _charging := false
var _charge_up := true
var _root: Node3D	# (all of it, moved with Godot's 0)
var _ball: MeshInstance3D
var _ball_mark: Label3D
var _flag: Node3D
var _camera: Camera3D
var _ball_at := Vector3.ZERO	# (in _root's frame)
var _velocity := Vector3.ZERO
var _spin := 0.0	# rad/s: backspin
var _rolling := false
var _hit_from := Vector3.ZERO	# (where the shot was played: out of the course, from here again)
var _greens: Array = []	# PackedVector2Array outlines (x, z in _root's frame)
var _bunkers: Array = []
var _bounds := Rect2()	# the course's, with a margin (x, z)
var _line: Array = []	# the hole's tee to pin (Vector3s in _root's frame)
var _still := 0.0
var closest := INF	# (the shot's nearest to the cup: the tests)
var holed := false	# (this hole's done: the next one's set up in a moment)


func _ready() -> void:
	_root = Node3D.new()
	_root.name = "Golf"
	main.add_child.call_deferred(_root)


func shift(offset: Vector3) -> void:
	_root.position += offset


# --- a round ---

func start(game: Dictionary) -> bool:
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(String(game.get("golf", {}).get("file", ""))))
	if not (parsed is Dictionary) or parsed.get("holes", []).is_empty():
		main.hud.set_status("This course's holes couldn't be read", 4.0)
		return false
	stop(false)
	reserve = game
	course = parsed
	active = true
	hole = 0
	strokes = []
	var world: Node = main.world
	_greens = _outlines(course.get("greens", []))
	_bunkers = _outlines(course.get("bunkers", []))
	_bounds = Rect2()
	for h in course["holes"]:
		for p in h["line"]:
			var at := _local(p)
			_bounds = Rect2(Vector2(at.x, at.z), Vector2.ZERO) if _bounds.size == Vector2.ZERO and _bounds.position == Vector2.ZERO else _bounds.expand(Vector2(at.x, at.z))
	_bounds = _bounds.grow(120.0)
	_ball = MeshInstance3D.new()
	var round := SphereMesh.new()
	round.radius = RADIUS
	round.height = RADIUS * 2.0
	_ball.mesh = round
	var white := StandardMaterial3D.new()
	white.albedo_color = Color(0.97, 0.97, 0.97)
	_ball.material_override = white
	_root.add_child(_ball)
	_ball_mark = Label3D.new()
	_ball_mark.text = "Your ball\n▼"
	_ball_mark.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_ball_mark.no_depth_test = true
	_ball_mark.fixed_size = true
	_ball_mark.pixel_size = 0.0011
	_ball_mark.font_size = 26
	_ball_mark.outline_size = 8
	_ball_mark.modulate = Color(1.0, 1.0, 1.0)
	_root.add_child(_ball_mark)
	_camera = Camera3D.new()
	_camera.fov = 60.0
	_camera.far = 20000.0
	_root.add_child(_camera)
	_start_hole(0)
	if world:
		print("Geodrive golf: %s, %d holes, par %d" % [course["name"], course["holes"].size(), _par_total()])
	return true


func stop(tell: bool = true) -> void:
	if not active:
		return
	active = false
	_leave_address()
	for child in _root.get_children():
		child.queue_free()
	main.hud.set_game_line("")
	if tell:
		main.hud.set_status("Stopped playing %s" % course.get("name", "golf"), 3.0)


func _start_hole(index: int) -> void:
	hole = index
	holed = false
	strokes.resize(hole + 1)
	strokes[hole] = 0
	var h: Dictionary = course["holes"][hole]
	_line = []
	for p in h["line"]:
		_line.append(_local(p))
	_ball_at = _on_ground(_line[0])
	_hit_from = _ball_at
	_velocity = Vector3.ZERO
	_rolling = false
	state = "walk"
	club = 0 if _par(h) >= 4 else 3
	aim = _heading_to(_line[1] if _line.size() > 1 else _line[-1])
	_place_flag(_line[-1])
	# (you at the tee, by your ball)
	var walker: Node3D = main.walker
	if walker:
		walker.global_position = _root.to_global(_ball_at + Vector3(1.2, 0.3, 0.0))
	main.hud.set_status("Hole %s, par %d, %d m. Walk to your ball, E to address it" % [h.get("ref", str(hole + 1)), _par(h), roundi(_hole_length())], 6.0)
	_update_ball()


func _place_flag(at: Vector3) -> void:
	if is_instance_valid(_flag):
		_flag.queue_free()
	_flag = Node3D.new()
	_root.add_child(_flag)
	_flag.position = _on_ground(at) - Vector3.UP * RADIUS
	var pole := MeshInstance3D.new()
	var stick := CylinderMesh.new()
	stick.top_radius = 0.0125
	stick.bottom_radius = 0.0125
	stick.height = 2.1
	pole.mesh = stick
	pole.position = Vector3.UP * 1.05
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.95, 0.95, 0.9)
	pole.material_override = paint
	_flag.add_child(pole)
	var cloth := MeshInstance3D.new()
	var sheet := BoxMesh.new()
	sheet.size = Vector3(0.5, 0.35, 0.01)
	cloth.mesh = sheet
	cloth.position = Vector3(0.25, 1.9, 0.0)
	var red := StandardMaterial3D.new()
	red.albedo_color = Color(0.9, 0.12, 0.1)
	cloth.material_override = red
	_flag.add_child(cloth)
	var cup := MeshInstance3D.new()
	var hole_mesh := CylinderMesh.new()
	hole_mesh.top_radius = CUP
	hole_mesh.bottom_radius = CUP
	hole_mesh.height = 0.01
	cup.mesh = hole_mesh
	var black := StandardMaterial3D.new()
	black.albedo_color = Color(0.02, 0.02, 0.02)
	cup.material_override = black
	cup.position = Vector3.UP * 0.006
	_flag.add_child(cup)


# --- input ---

func _unhandled_input(event: InputEvent) -> void:
	if not active or main.walker == null or main.menu.is_open() or main.phone.is_open():
		return
	if state == "walk":
		if event is InputEventKey and event.pressed and not event.echo:
			if event.keycode == KEY_E and _near_ball() and not holed:
				get_viewport().set_input_as_handled()
				_address()
			elif event.keycode == KEY_T:
				get_viewport().set_input_as_handled()
				main.walker.global_position = _root.to_global(_ball_at + Vector3(1.2, 0.3, 0.0))
				main.hud.set_status("By your ball: E to address it", 2.0)
		return
	if state != "address":
		return
	get_viewport().set_input_as_handled()
	if event is InputEventMouseMotion:
		aim -= event.relative.x * 0.002
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_UP:
		club = (club + CLUBS.size() - 1) % CLUBS.size()
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
		club = (club + 1) % CLUBS.size()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_charging = true
			_charge_up = true
			power = 0.0
		elif _charging:
			_charging = false
			hit(club, power, aim)
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_RIGHT:
		_leave_address()
	elif event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		_leave_address()


func _near_ball() -> bool:
	var walker: Node3D = main.walker
	return walker and walker.global_position.distance_to(_root.to_global(_ball_at)) < 3.0


func _address() -> void:
	state = "address"
	main.walker.set_physics_process(false)
	_camera.current = true
	# (the club for how far it is: the longest that doesn't go past, on the green the putter)
	var left := Vector2(_line[-1].x - _ball_at.x, _line[-1].z - _ball_at.z).length()
	var here := _surface(_ball_at)
	club = CLUBS.size() - 1 if here == "green" or (here == "fairway" and left < 20.0) else CLUBS.size() - 2	# (the putter; else the shortest...)
	if club != CLUBS.size() - 1:
		for c in range(CLUBS.size() - 1):
			if carry_of(c) <= left + 15.0:
				club = c	# (...or the longest that doesn't go past)
				break
	aim = _heading_to(_line[-1]) if left < 260.0 or _line.size() < 3 else _heading_to(_line[1])


func _leave_address() -> void:
	if state == "address" or state == "flight":
		state = "walk"
	_charging = false
	if is_instance_valid(_camera):
		_camera.current = false
	if main.walker:
		main.walker.set_physics_process(true)
		main.camera.current = true


# --- the shot ---

# A club at Power (0-1) on Heading: the ball's off (a stroke)
func hit(which: int, with_power: float, heading: float) -> void:
	var c: Array = CLUBS[which]
	var speed: float = c[1] * clampf(with_power, 0.05, 1.0)
	var launch := deg_to_rad(c[2])
	var forward := Vector3(-sin(heading), 0.0, -cos(heading))
	_velocity = (forward * cos(launch) + Vector3.UP * sin(launch)) * speed
	_spin = float(c[3]) * TAU / 60.0 * clampf(with_power, 0.3, 1.0)
	_rolling = which == CLUBS.size() - 1	# (a putt rolls from the start)
	_hit_from = _ball_at
	closest = INF
	strokes[hole] += 1
	state = "flight"
	_still = 0.0
	main.hud.set_status("%s, %d%%" % [c[0], roundi(with_power * 100.0)], 1.5)


# How far a club carries at full power on flat ground (its flight alone): for choosing it
func carry_of(which: int) -> float:
	var c: Array = CLUBS[which]
	var launch := deg_to_rad(c[2])
	var v := Vector3(0.0, sin(launch), -cos(launch)) * float(c[1])
	var spin: float = float(c[3]) * TAU / 60.0
	var p := Vector3.ZERO
	var dt := 1.0 / 240.0
	for i in 4000:
		var step := _air(v, spin, dt)
		v = step[0]
		spin = step[1]
		p += v * dt
		if p.y < 0.0 and v.y < 0.0:
			break
	return Vector2(p.x, p.z).length()


# The air on the ball for Dt: gravity, drag and backspin's lift (Cd and Cl by its spin ratio); its spin wearing off
static func _air(v: Vector3, spin: float, dt: float) -> Array:
	var speed := v.length()
	if speed < 0.01:
		return [v + Vector3.DOWN * GRAVITY * dt, spin]
	var area := PI * RADIUS * RADIUS
	var ratio := spin * RADIUS / speed
	var drag := 0.24 + 0.18 * ratio
	var lift := clampf(0.54 * pow(ratio, 0.4), 0.0, 0.35) if spin > 0.0 else 0.0
	var right := Vector3(v.x, 0.0, v.z).normalized().cross(Vector3.UP)
	var up := right.cross(v).normalized() if right.length() > 0.1 else Vector3.ZERO
	var push := 0.5 * AIR * area * speed * speed / MASS
	var a := Vector3.DOWN * GRAVITY - v / speed * drag * push + up * lift * push
	return [v + a * dt, spin * (1.0 - 0.04 * dt)]


func _physics_process(delta: float) -> void:
	if not active:
		return
	if state == "address" and _charging:
		power += (delta / 1.2) * (1.0 if _charge_up else -1.0)
		if power >= 1.0:
			power = 1.0
			_charge_up = false
		elif power <= 0.0:
			power = 0.0
			_charge_up = true
	if state == "flight":
		var steps := 8
		for i in steps:
			_fly(delta / steps)
			if state != "flight":
				break
	_update_ball()
	_line_on_screen()


func _fly(dt: float) -> void:
	var ground := _ground_y(_ball_at)
	if not _rolling:
		var step := _air(_velocity, _spin, dt)
		_velocity = step[0]
		_spin = step[1]
		_ball_at += _velocity * dt
		if _ball_at.y - RADIUS <= ground:
			_ball_at.y = ground + RADIUS
			_land()
	else:
		var surface: Array = SURFACES[_surface(_ball_at)]
		var slope := _slope(_ball_at)
		var along := Vector3(_velocity.x, 0.0, _velocity.z)
		var speed := along.length()
		var downhill := Vector3(-slope.x, 0.0, -slope.y) * GRAVITY
		if speed > 0.0:
			along -= along / speed * minf(surface[2] * dt, speed)
		along += downhill * dt
		_velocity = along
		_ball_at += along * dt
		_ball_at.y = _ground_y(_ball_at) + RADIUS
		if along.length() < 0.05 and downhill.length() < surface[2]:
			_velocity = Vector3.ZERO
			_still += dt
			if _still > 0.3:
				_stopped()
		else:
			_still = 0.0
	# in the cup?
	var pin: Vector3 = _line[-1]
	closest = minf(closest, Vector2(_ball_at.x - pin.x, _ball_at.z - pin.z).length())
	if Vector2(_ball_at.x - pin.x, _ball_at.z - pin.z).length() < CUP and _velocity.length() < CUP_SPEED and _ball_at.y - RADIUS <= _ground_y(_ball_at) + 0.05:
		_holed()


func _land() -> void:
	var surface: Array = SURFACES[_surface(_ball_at)]
	var into := -_velocity.y
	_velocity.x *= surface[1]
	_velocity.z *= surface[1]
	_velocity.y = into * surface[0]
	_spin *= 0.5
	if _velocity.y < 0.6:
		_velocity.y = 0.0
		_rolling = true


func _stopped() -> void:
	state = "walk"
	_camera.current = false
	main.walker.set_physics_process(true)
	main.camera.current = true
	if not _bounds.has_point(Vector2(_ball_at.x, _ball_at.z)):
		strokes[hole] += 1
		_ball_at = _hit_from
		main.hud.set_status("Out of the course: a stroke more, from where you hit it", 4.0)
		return
	var left := Vector2(_line[-1].x - _ball_at.x, _line[-1].z - _ball_at.z).length()
	var where := _surface(_ball_at)
	main.hud.set_status("On the %s, %s to the pin. Walk to it (T: skip the walk)" % [where, ("%.1f m" % left) if left < 30.0 else ("%d m" % roundi(left))], 4.0)


func _holed() -> void:
	state = "walk"
	holed = true
	_velocity = Vector3.ZERO
	var h: Dictionary = course["holes"][hole]
	var par := _par(h)
	var n: int = strokes[hole]
	var word: String = {-3: "Albatross", -2: "Eagle", -1: "Birdie", 0: "Par", 1: "Bogey", 2: "Double bogey"}.get(n - par, "%+d" % (n - par))
	if n == 1:
		word = "Hole in one!"
	main.hud.show_banner("%s: %d on a par %d" % [word, n, par], n <= par, 3.0)
	if _camera.current:
		_camera.current = false
		main.camera.current = true
	main.walker.set_physics_process(true)
	if hole + 1 < course["holes"].size():
		get_tree().create_timer(3.0).timeout.connect(func(): if active: _start_hole(hole + 1))
	else:
		var total := 0
		for s in strokes:
			total += int(s)
		var to_par := total - _par_total()
		main.hud.show_banner("%s: %d (%s)" % [course["name"], total, "level par" if to_par == 0 else "%+d" % to_par], to_par <= 0, 10.0)
		print("Geodrive golf: round over: %d (%+d)" % [total, to_par])
		get_tree().create_timer(10.0).timeout.connect(func(): stop(false))


# --- what's on screen ---

func _update_ball() -> void:
	if not is_instance_valid(_ball):
		return
	_ball.position = _ball_at
	_ball_mark.position = _ball_at + Vector3.UP * 1.2
	_ball_mark.visible = state != "address" and not _near_ball()
	if state == "address":
		var back := Vector3(sin(aim), 0.0, cos(aim))
		var side := back.cross(Vector3.UP)	# (your side of the ball: the camera behind it a little the other way)
		_camera.position = _ball_at + back * 3.0 - side * 0.9 + Vector3.UP * 1.5
		_camera.look_at(_ball_at - back * 30.0 - side * 0.9 + Vector3.UP * 0.5)
		# (you side on to it, as a golfer stands: off the line of the shot, facing the ball)
		var stand := _root.to_global(_ball_at + side * 0.75)
		main.walker.global_position = Vector3(stand.x, main.world.ground_height(stand) + 0.02, stand.z)
		main.walker.rotation.y = atan2(side.x, side.z)
	elif state == "flight" and _camera.current:
		var along := Vector3(_velocity.x, 0.0, _velocity.z)
		var back := -along.normalized() if along.length() > 1.0 else Vector3(sin(aim), 0.0, cos(aim))
		var want := _ball_at + back * 9.0 + Vector3.UP * 3.5
		_camera.position = _camera.position.lerp(want, 0.08)
		_camera.look_at(_ball_at)


func _line_on_screen() -> void:
	if not active or _line.is_empty():
		return
	var h: Dictionary = course["holes"][hole]
	var left := Vector2(_line[-1].x - _ball_at.x, _line[-1].z - _ball_at.z).length()
	var text := "%s  Hole %s  Par %d  Stroke %d  %s to the pin" % [course["name"], h.get("ref", str(hole + 1)), _par(h), int(strokes[hole]) + (1 if state == "address" else 0),
		("%.1f m" % left) if left < 30.0 else ("%d m" % roundi(left))]
	if state == "address":
		var c: Array = CLUBS[club]
		text += "\n%s (%s)   Power %s %d%%   (wheel: club, hold left: power, right: step away)" % [c[0], ("%d m" % roundi(carry_of(club))) if club < CLUBS.size() - 1 else "putting",
			"|".repeat(roundi(power * 20.0)) + ".".repeat(20 - roundi(power * 20.0)), roundi(power * 100.0)]
	main.hud.set_game_line(text)


# --- the course on the ground ---

func _local(p: Array) -> Vector3:
	return _root.to_local(main.world.from_lat_lon(float(p[0]), float(p[1])))


func _outlines(list: Array) -> Array:
	var out := []
	for outline in list:
		var points := PackedVector2Array()
		for p in outline:
			var at := _local(p)
			points.append(Vector2(at.x, at.z))
		out.append(points)
	return out


func _surface(at: Vector3) -> String:
	var flat := Vector2(at.x, at.z)
	for outline in _greens:
		if Geometry2D.is_point_in_polygon(flat, outline):
			return "green"
	for outline in _bunkers:
		if Geometry2D.is_point_in_polygon(flat, outline):
			return "bunker"
	# the fairway: near the line from tee to pin (~20 m either side)
	for i in range(_line.size() - 1):
		var a := Vector2(_line[i].x, _line[i].z)
		var b := Vector2(_line[i + 1].x, _line[i + 1].z)
		if Geometry2D.get_closest_point_to_segment(flat, a, b).distance_to(flat) < 20.0:
			return "fairway"
	return "rough"


func _ground_y(at: Vector3) -> float:
	var g := _root.to_global(at)
	return _root.to_local(Vector3(g.x, main.world.ground_height(g), g.z)).y


func _on_ground(at: Vector3) -> Vector3:
	return Vector3(at.x, _ground_y(at) + RADIUS, at.z)


# The ground's slope at At: (dy/dx, dy/dz). The heights are the land's (30 m apart); a course's greens and fairways
# are graded flatter than that: a green at most 3%, a fairway 8% (the rough and bunkers as the land is)
func _slope(at: Vector3) -> Vector2:
	var e := 0.5
	var slope := Vector2((_ground_y(at + Vector3(e, 0, 0)) - _ground_y(at - Vector3(e, 0, 0))) / (2.0 * e),
		(_ground_y(at + Vector3(0, 0, e)) - _ground_y(at - Vector3(0, 0, e))) / (2.0 * e))
	var most: float = {"green": 0.03, "fairway": 0.08}.get(_surface(at), 1.0)
	return slope.limit_length(most)


func _heading_to(target: Vector3) -> float:
	var d := target - _ball_at
	return atan2(-d.x, -d.z)


func _hole_length() -> float:
	var total := 0.0
	for i in range(_line.size() - 1):
		total += Vector2(_line[i + 1].x - _line[i].x, _line[i + 1].z - _line[i].z).length()
	return total


static func _par(h: Dictionary) -> int:
	return int(h.get("par", 0)) if int(h.get("par", 0)) > 0 else 4


func _par_total() -> int:
	var total := 0
	for h in course["holes"]:
		total += _par(h)
	return total
