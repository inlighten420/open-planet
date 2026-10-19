# Every camera, as in the Unreal game: chase, cockpit (with or without the steering wheel, for players with a real one),
# bonnet; looking round (Z / X left and right, V back, the mouse); the free camera (F); the overhead map (M); and on
# foot, out of the walker's eyes (walker.gd).
class_name CarCamera
extends Camera3D

enum View { CHASE, COCKPIT, COCKPIT_NO_WHEEL, BONNET }
const VIEW_NAMES := ["Chase", "Cockpit", "Cockpit (no wheel)", "Bonnet"]

# the chase camera: an arm 6.5 m long behind the car, 1.5 m up, swinging after the car's heading (lag speed 3)
const CHASE_LENGTH := 6.5
const CHASE_HEIGHT := 1.5
const CHASE_LAG := 3.0
# the cockpit and bonnet cameras follow the car's turning with a little lag (speed 15)
const FRONT_LAG := 15.0
const COCKPIT_FOV := 80.0
# looking round: how fast the view turns to where you look, and how fast the mouse's look comes back to the road
const LOOK_SPEED := 8.0
const MOUSE_RETURN := 1.0
const MOUSE_DEGREES := 0.15
# the free camera (m/s) and the map (m above the car)
const FREE_SPEED := 40.0
const FREE_FAST := 150.0
const MAP_MIN := 30.0
const MAP_MAX := 25000.0

var cockpit_fov := COCKPIT_FOV	# (the settings set these)
var zoom_fov := 0.0	# on foot looking through a scope or sight (hunt.gd): the view's narrow angle (0: none); always from the eyes
var view_far := 30000.0
var car: RigidBody3D = null
var walker: CharacterBody3D = null	# on foot: the camera is their eyes (walker.gd)
var walker_behind := false	# on foot, C: from behind them instead
var aircraft: Node3D = null	# flying: the camera follows it (aviation.gd)
var plane_cockpit := false	# flying, C: from the pilot's seat instead of behind
const ALL_LAYERS := 0xFFFFF
var hidden_layers := 0	# render layers kept out of the view (settings, the GPU profile: see the world's layers)
var body_eye := 0.0	# m: the driver's eyes higher (or lower) in the cockpit than the car's design puts them (their height)
var view := View.CHASE
var free := false
var map := false
var map_height := 400.0
var seat := Vector2.ZERO	# cockpit seat offset (m): forward, up
var wheel_look := Vector2.ZERO	# the steering wheel's look buttons, set every frame: right, up (-1 to 1)
var wheel_look_back := false
var keys := true	# (false while the menu or the world map has the keys: online they don't pause the game)

var _arm := Basis()	# the lagging arm (chase) or eyes (front cameras)
var _look_yaw := 0.0
var _look_pitch := 0.0
var _mouse_yaw := 0.0
var _free_yaw := 0.0
var _free_pitch := 0.0
var _started := false
var _reach := 1.0	# share of the chase arm out (pulled in by what's behind the car)


func _ready() -> void:
	far = view_far


func set_view(new_view: int) -> void:
	view = new_view as View
	if car:
		car.set_view(view)


func cycle_view() -> void:
	set_view((view + 1) % View.size())


var speed_scale := 1.0	# (the free camera's speed, less while the world has jobs queued: it can't build as fast as the camera flies)


func start_free() -> void:
	free = true
	map = false
	_free_yaw = rotation.y
	_free_pitch = rotation.x
	if car:
		car.set_view(View.CHASE)


func stop_free() -> void:
	free = false
	set_view(view)


func toggle_map() -> void:
	map = not map
	if car:
		car.set_view(View.CHASE if map else view)


func mouse_moved(relative: Vector2) -> void:
	if free:
		_free_yaw -= deg_to_rad(relative.x * MOUSE_DEGREES)
		_free_pitch = clamp(_free_pitch - deg_to_rad(relative.y * MOUSE_DEGREES), -1.55, 1.55)
	elif walker and not map:
		walker.mouse_moved(relative * (zoom_fov / cockpit_fov if zoom_fov > 0.0 else 1.0))	# (through a scope, as steady as the view's narrow)
	elif not map:
		_mouse_yaw = clamp(_mouse_yaw - deg_to_rad(relative.x * MOUSE_DEGREES), -PI, PI)


func zoom_map(steps: int) -> void:
	map_height = clamp(map_height * pow(1.25, steps), MAP_MIN, MAP_MAX)


func update(delta: float) -> void:
	if free:
		_update_free(delta)
		return
	if aircraft and not map:
		var body := aircraft.global_transform
		if plane_cockpit:
			_arm = body.basis.orthonormalized()
			global_transform = Transform3D(_arm, body * aircraft.get_eye())
			fov = cockpit_fov
			near = 0.1
		else:
			# behind and a little above, swinging after it (never its roll)
			var back := body.basis.z.normalized()
			var level := Vector3(back.x, 0.0, back.z)
			var wanted := Basis.looking_at(-back, Vector3.UP) if abs(back.y) < 0.98 else Basis.looking_at(-level.normalized(), Vector3.UP)
			_arm = wanted if not _started else _arm.slerp(wanted, clampf(4.0 * delta, 0.0, 1.0))
			var reach: float = aircraft.get_chase_distance()
			global_transform = Transform3D(_arm, body.origin + _arm.z * reach + Vector3.UP * reach * 0.22)
			look_at(body.origin + Vector3.UP * reach * 0.08, Vector3.UP)
			fov = 70.0
			near = 0.5
		far = view_far
		cull_mask = ALL_LAYERS & ~hidden_layers
		_started = true
		return
	if walker and not map:
		var look: Basis = walker.look_basis()
		var eyes: Vector3 = walker.eyes()
		if walker_behind and zoom_fov <= 0.0:
			# behind them, over the shoulder, pulled in where something's in the way
			var wanted := eyes + look.z * 3.0 + Vector3.UP * 0.4
			var ray := PhysicsRayQueryParameters3D.create(eyes, wanted, 0xFFFFFFFF, [walker.get_rid()])
			var hit := get_world_3d().direct_space_state.intersect_ray(ray)
			if hit:
				wanted = eyes.lerp(hit["position"], 0.85)
			global_transform = Transform3D(look, wanted)
			cull_mask = ALL_LAYERS & ~hidden_layers
		else:
			global_transform = Transform3D(look, eyes)
			cull_mask = ALL_LAYERS & ~2 & ~hidden_layers	# (their own head and neck, on layer 2, not in the way)
		fov = zoom_fov if zoom_fov > 0.0 else cockpit_fov
		near = 0.05
		far = view_far
		return
	cull_mask = ALL_LAYERS & ~hidden_layers
	if car == null:
		return
	if map:
		_update_map()
		return
	far = view_far

	# looking round: Z / X turn the view to the side, V looks back, the mouse adds to it and drifts back to the road
	# (and the wheel's buttons: up to 90 degrees to the side, 30 up or down; let go and the view goes back to the road)
	var side := clampf(wheel_look.x + float(_held("look_right")) - float(_held("look_left")), -1.0, 1.0)
	var target := -side * PI * 0.5
	if _held("look_back") or wheel_look_back:
		target = PI
	_look_pitch = lerp(_look_pitch, clampf(wheel_look.y, -1.0, 1.0) * deg_to_rad(30.0), 1.0 - exp(-LOOK_SPEED * delta))
	_mouse_yaw = lerp(_mouse_yaw, 0.0, 1.0 - exp(-MOUSE_RETURN * delta))
	_look_yaw = target if target == PI else lerp(_look_yaw, target, 1.0 - exp(-LOOK_SPEED * delta))
	var look := Basis(Vector3.UP, _look_yaw + _mouse_yaw) * Basis(Vector3.RIGHT, _look_pitch)

	var body := car.global_transform
	if view == View.CHASE:
		# the arm follows the car's heading and pitch, never its roll
		var forward := -body.basis.z
		var level := Vector3(forward.x, 0.0, forward.z)
		if level.length() < 0.01:
			level = Vector3.FORWARD
		var wanted := Basis.looking_at(forward.normalized(), Vector3.UP) if abs(forward.y) < 0.99 else Basis.looking_at(level.normalized(), Vector3.UP)
		_arm = wanted if not _started else _arm.slerp(wanted, clamp(CHASE_LAG * delta, 0.0, 1.0))
		var arm := (_arm * look).orthonormalized()
		# pulled in where the ground, a building or a bank is between the car and the camera (at once), let out again
		# gently: never looking out from inside a hill
		var pivot := body.origin + Vector3.UP * 1.0
		var wanted_at := body.origin + arm.z * CHASE_LENGTH + Vector3.UP * CHASE_HEIGHT
		var reach := 1.0
		var ray := PhysicsRayQueryParameters3D.create(pivot, wanted_at + (wanted_at - pivot).normalized() * 0.3, 0xFFFFFFFF, [car.get_rid()])
		var hit := get_world_3d().direct_space_state.intersect_ray(ray)
		if hit:
			reach = clampf((pivot.distance_to(hit["position"]) - 0.3) / pivot.distance_to(wanted_at), 0.15, 1.0)
		_reach = reach if reach < _reach or not _started else lerpf(_reach, reach, clampf(2.0 * delta, 0.0, 1.0))
		global_transform = Transform3D(arm, pivot.lerp(wanted_at, _reach))
		fov = 90.0
		near = 0.1
	else:
		var eyes: Vector3 = car.get_bonnet_camera() if view == View.BONNET else car.get_cockpit_camera() + Vector3(0.0, seat.y + body_eye, -seat.x)
		if view != View.BONNET:
			eyes.y = minf(eyes.y, car.get_cockpit_ceiling())	# (a tall driver's seat drops: their head under the roof)
		var wanted := body.basis.orthonormalized()
		_arm = wanted if not _started else _arm.slerp(wanted, clamp(FRONT_LAG * delta, 0.0, 1.0))
		global_transform = Transform3D((_arm * look).orthonormalized(), body * eyes)
		fov = 90.0 if view == View.BONNET else cockpit_fov
		near = 0.03
	_started = true


func _update_free(delta: float) -> void:
	var move := Vector3.ZERO
	move.z -= float(_held("forward")) - float(_held("back"))
	move.x += float(_held("right")) - float(_held("left"))
	move.y += float(_held("gear_up")) - float(_held("gear_down"))
	var speed := (FREE_FAST if _held("run") else FREE_SPEED) * speed_scale
	rotation = Vector3(_free_pitch, _free_yaw, 0.0)
	position += global_transform.basis * move * speed * delta
	fov = 80.0
	near = 0.1
	far = view_far
	cull_mask = ALL_LAYERS & ~hidden_layers	# (the road editor hides the ground, buildings and the rest one at a time)


func _update_map() -> void:
	# straight down, north at the top
	var at := aircraft.global_position if aircraft else (walker.global_position if walker else car.global_position)
	global_transform = Transform3D(Basis.looking_at(Vector3.DOWN, Vector3.FORWARD), at + Vector3.UP * map_height)
	fov = 60.0
	near = 1.0
	far = map_height + 5000.0


func view_name() -> String:
	return VIEW_NAMES[view]


func _held(action: String) -> bool:
	return keys and Controls.down(action)
