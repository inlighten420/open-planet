# On foot: the player out of the car, walking the ground, roads and pavements (anywhere outdoors; buildings are solid).
# How a person of the player's height walks comes from Core (GeodriveWorld.body_measures / walk: GeoPeople/Body.h);
# this is the body in Godot's world: a capsule their size that slides along what it touches, steps up kerbs and steps
# (up to a fifth of their height) and falls off ledges. The camera (car_camera.gd) looks out of its eyes, which bob a
# little with each step. They have a body (simple solid shapes, as the trees: sized from their height by the usual
# proportions), whose arms and legs swing as they walk and run; their own eyes don't see their head.
#   W S A D / arrows / left stick: walk     Shift / stick click: run     Ctrl / right shoulder: sprint
#   Space / A: jump     mouse / right stick: look
# The speeds are real people's (Core): walking ~5 km/h, running ~12, sprinting ~25, a little more for the taller.
class_name Walker
extends CharacterBody3D

const MOUSE_DEGREES := 0.15
const STICK_TURN := 2.5	# radians a second, the right stick full over

var height_cm := 175.0
var keys := true	# (false while the menu has them)
var pitch := 0.0	# looking up (+) or down (radians)
var measures: Dictionary = {}
var _bob := 0.0	# the step's phase
var ground_speed := 0.0	# m/s over the ground, as they really moved (the limbs swing by it)
var world: Node = null	# (GeodriveWorld: whether the ground under them is solid yet)
var held := false	# the ground under them isn't solid yet (still loading, or let go while the free camera was away): held still till it is
var _solid_check := 0.0
const HEAD_LAYER := 2	# (the head and neck: the camera leaves this layer out when it's their eyes)
var _parts := {}	# the body's joints (Node3D pivots)
# Someone else's body in a multiplayer game (net.gd moves it where they are): no physics of its own, its limbs swinging
# by the speed they're going; still solid, so a shot stops on it (and you bump into them)
var puppet := false
var down := 0.0	# 0 standing, 1 lying where they fell (shot)
var down_wanted := false
var _rifle: Node3D	# (a rifle carried, when they have one: someone else's)


func _init(height: float = 175.0) -> void:
	height_cm = height
	measures = ClassDB.class_call_static("GeodriveWorld", "body_measures", height_cm)
	var capsule := CapsuleShape3D.new()
	capsule.radius = measures["radius"]
	capsule.height = measures["height"]
	var shape := CollisionShape3D.new()
	shape.shape = capsule
	shape.position = Vector3(0.0, capsule.height * 0.5, 0.0)	# (the body's origin is at its feet)
	add_child(shape)
	collision_layer = 2	# (people: their own layer, so an aircraft's crash checks look past them; they still meet the world, layer 1)
	_build_body()
	floor_max_angle = deg_to_rad(46.0)
	floor_snap_length = float(measures["step_up"]) + 0.1
	floor_constant_speed = true
	safe_margin = 0.02


# The eyes, in the world (bobbing a little as they walk; a little in front of the body's middle, as eyes are)
func eyes() -> Vector3:
	var h := float(measures["height"])
	var bob := absf(sin(_bob)) * 0.02 * h / 1.75
	# (down: the eyes near the ground)
	return global_position + Vector3.UP * lerpf(float(measures["eye_standing"]) + bob, 0.25, down) - global_transform.basis.z * 0.06 * h * (1.0 - down)


func look_basis() -> Basis:
	return Basis(Vector3.UP, rotation.y) * Basis(Vector3.RIGHT, pitch)


func mouse_moved(relative: Vector2) -> void:
	rotation.y -= deg_to_rad(relative.x * MOUSE_DEGREES)
	pitch = clampf(pitch - deg_to_rad(relative.y * MOUSE_DEGREES), -1.5, 1.5)


# The gamepad to read (its sticks and buttons): the first a known gamepad, never a wheel or pedals (a wheel's axes read
# as a stick would push the player about); -1 if there's none
static func gamepad() -> int:
	for id in Input.get_connected_joypads():
		var name := Input.get_joy_name(id).to_lower()
		var wheel := false
		for word in ["wheel", "moza", "fanatec", "thrustmaster", "logitech g", "simucube", "pedal", "base", "racing"]:
			wheel = wheel or word in name
		if Input.is_joy_known(id) and not wheel:
			return id
	return -1


func _physics_process(delta: float) -> void:
	if puppet:
		return	# (someone else's: net.gd places it; Godot turns this back on as it enters the tree)
	var forward := 0.0
	var right := 0.0
	var run := false
	var sprint := false
	var jump := false
	if keys:
		forward = Controls.axis("back", "forward")
		right = Controls.axis("left", "right")
		var pad := gamepad()
		var stick := Vector2(Input.get_joy_axis(pad, JOY_AXIS_LEFT_X), Input.get_joy_axis(pad, JOY_AXIS_LEFT_Y)) if pad >= 0 else Vector2.ZERO
		if stick.length() > 0.15:
			right = stick.x
			forward = -stick.y
		var look := Vector2(Input.get_joy_axis(pad, JOY_AXIS_RIGHT_X), Input.get_joy_axis(pad, JOY_AXIS_RIGHT_Y)) if pad >= 0 else Vector2.ZERO
		if look.length() > 0.15:
			rotation.y -= look.x * STICK_TURN * delta
			pitch = clampf(pitch - look.y * STICK_TURN * delta, -1.5, 1.5)
		run = Controls.down("run") or (pad >= 0 and Input.is_joy_button_pressed(pad, JOY_BUTTON_LEFT_STICK))
		sprint = Controls.down("sprint") or (pad >= 0 and Input.is_joy_button_pressed(pad, JOY_BUTTON_RIGHT_SHOULDER))
		jump = Controls.down("jump") or (pad >= 0 and Input.is_joy_button_pressed(pad, JOY_BUTTON_A))
	steer(forward, right, run, jump, delta, sprint)


# One step of walking (the keys', or a test's): the velocity from Core, up any kerb in the way, then moved
func steer(forward: float, right: float, run: bool, jump: bool, delta: float, sprint: bool = false) -> void:
	if down_wanted:
		forward = 0.0
		right = 0.0
		jump = false
	# (asked four times a second: the ground's tiles looked through)
	_solid_check -= delta
	if world and _solid_check <= 0.0:
		_solid_check = 0.25
		held = not world.is_solid_at(global_position)
	if held:
		velocity = Vector3.ZERO
		return
	var on_floor := is_on_floor()
	velocity = ClassDB.class_call_static("GeodriveWorld", "walk", height_cm, forward, right, run, sprint, jump, rotation.y, on_floor, velocity, delta)
	var level := Vector3(velocity.x, 0.0, velocity.z)
	if on_floor and level.length() > 0.01:
		# (looking a few cm ahead the way they mean to go, however slow: a step in the way stops them dead, and from a
		# standstill they'd never get fast enough to try it, stuck at the foot of every flight)
		_step_up(level.normalized() * (level.length() * delta + 0.06))
		_bob += ground_speed * delta / float(measures["step_length"]) * PI
	else:
		_bob = lerpf(_bob, round(_bob / PI) * PI, 1.0 - exp(-6.0 * delta))	# (settling when they stop)
	var before := global_position
	move_and_slide()
	# (how fast they really went over the ground: Godot's velocity after sliding reads lower than that)
	ground_speed = Vector2(global_position.x - before.x, global_position.z - before.z).length() / maxf(delta, 0.0001)


# A kerb or a step in the way (something steep, no higher than they can step): up onto it; floor snapping brings them
# down onto its top
func _step_up(motion: Vector3) -> void:
	# (tried from 3 cm up: the floor under their feet, or a step's edge they stand on, isn't in the way; on a flight's
	# step, that edge was found first and taken for a slope, and the next step never)
	var hit := KinematicCollision3D.new()
	if not test_move(global_transform.translated(Vector3.UP * 0.03), motion, hit) or hit.get_normal().y > 0.7:
		return
	# (the least lift that clears it, as a foot finds a step: lifted the most they can at once, a 25 cm stair's next
	# step was in the way, and they stopped at the foot of every flight)
	var most := float(measures["step_up"])
	for height in [0.1, 0.18, 0.26, 0.34, most]:
		if height > most:
			continue
		var up: Vector3 = Vector3.UP * float(height)
		if test_move(global_transform, up):
			return	# (no head room)
		if not test_move(global_transform.translated(up), motion):
			global_position += up
			return
	# (higher than they can step: a wall)


# --- the body ---

# Made of a few solid shapes, from the height H by the usual proportions: hips at 0.53 H, shoulders 0.82 H, knees
# 0.285 H, elbows 0.63 H, the head 0.13 H tall; each limb hangs from its joint (a pivot), so turning the pivot swings it
func _build_body() -> void:
	var h := float(measures["height"])
	var skin := _paint(Color(0.72, 0.52, 0.4))
	var shirt := _paint(Color(0.16, 0.3, 0.5))
	var trousers := _paint(Color(0.13, 0.15, 0.2))
	var shoes := _paint(Color(0.05, 0.05, 0.05))
	var root := Node3D.new()
	root.name = "Body"
	add_child(root)
	# the hips (they lean forward running), the torso, neck and head on them
	var hips := _pivot(root, "hips", Vector3(0.0, 0.53 * h, 0.0))
	_box(hips, Vector3(0.2 * h, 0.08 * h, 0.11 * h), Vector3(0.0, 0.02 * h, 0.0), trousers)
	_box(hips, Vector3(0.21 * h, 0.24 * h, 0.12 * h), Vector3(0.0, 0.17 * h, 0.0), shirt)
	_capsule(hips, 0.03 * h, 0.06 * h, Vector3(0.0, 0.32 * h, 0.0), skin, HEAD_LAYER)
	var head := MeshInstance3D.new()
	var ball := SphereMesh.new()
	ball.radius = 0.062 * h
	ball.height = 0.133 * h
	ball.radial_segments = 10
	ball.rings = 6
	head.mesh = ball
	head.material_override = skin
	head.position = Vector3(0.0, 0.405 * h, 0.0)
	head.layers = HEAD_LAYER
	hips.add_child(head)
	# arms: from the shoulders, the upper arm 0.19 H, the forearm and hand 0.24 H
	for side in [-1.0, 1.0]:
		var part := "l" if side < 0.0 else "r"
		var shoulder := _pivot(hips, part + "_arm", Vector3(side * 0.125 * h, 0.27 * h, 0.0))
		_capsule(shoulder, 0.028 * h, 0.19 * h, Vector3(0.0, -0.095 * h, 0.0), shirt)
		var elbow := _pivot(shoulder, part + "_elbow", Vector3(0.0, -0.19 * h, 0.0))
		_capsule(elbow, 0.024 * h, 0.24 * h, Vector3(0.0, -0.12 * h, 0.0), skin)
		# legs: from the hips, the thigh 0.245 H, the shin 0.245 H, the foot 0.15 H long
		var hip := _pivot(root, part + "_leg", Vector3(side * 0.055 * h, 0.53 * h, 0.0))
		_capsule(hip, 0.04 * h, 0.245 * h, Vector3(0.0, -0.1225 * h, 0.0), trousers)
		var knee := _pivot(hip, part + "_knee", Vector3(0.0, -0.245 * h, 0.0))
		_capsule(knee, 0.034 * h, 0.245 * h, Vector3(0.0, -0.1225 * h, 0.0), trousers)
		var ankle := _pivot(knee, part + "_ankle", Vector3(0.0, -0.245 * h, 0.0))
		_box(ankle, Vector3(0.055 * h, 0.035 * h, 0.15 * h), Vector3(0.0, -0.022 * h, -0.04 * h), shoes)


func _paint(colour: Color) -> StandardMaterial3D:
	var paint := StandardMaterial3D.new()
	paint.albedo_color = colour
	paint.roughness = 0.85
	return paint


func _pivot(parent: Node3D, part: String, at: Vector3) -> Node3D:
	var joint := Node3D.new()
	joint.name = part
	joint.position = at
	parent.add_child(joint)
	_parts[part] = joint
	return joint


func _box(parent: Node3D, size: Vector3, at: Vector3, paint: Material) -> void:
	var box := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	box.mesh = mesh
	box.material_override = paint
	box.position = at
	parent.add_child(box)


func _capsule(parent: Node3D, radius: float, length: float, at: Vector3, paint: Material, layer: int = 1) -> void:
	var limb := MeshInstance3D.new()
	var mesh := CapsuleMesh.new()
	mesh.radius = radius
	mesh.height = maxf(length, radius * 2.0)
	mesh.radial_segments = 8
	mesh.rings = 2
	limb.mesh = mesh
	limb.material_override = paint
	limb.position = at
	limb.layers = layer
	parent.add_child(limb)


# The limbs swing with the steps: legs forward and back (more, running), knees bending as each leg comes through,
# the arms against the legs, elbows bent more running, the body leaning into a run; in the air, knees up a little
func _process(delta: float) -> void:
	if _parts.is_empty():
		return
	# (down: the whole body over onto its back over a second; up again when they're up)
	down = move_toward(down, 1.0 if down_wanted else 0.0, delta * 1.5)
	var body: Node3D = get_node_or_null("Body")
	if body:
		body.rotation.x = down * PI * 0.5
		body.position.y = down * 0.15
	if puppet:
		_bob += ground_speed * delta / float(measures["step_length"]) * PI
	var level := ground_speed
	var walk := float(measures["walk_speed"])
	var moving := clampf(level / walk, 0.0, 1.0)
	var run := float(measures["run_speed"])
	var running := clampf((level - walk) / (run - walk), 0.0, 1.0)
	var sprinting := clampf((level - run) / (float(measures["sprint_speed"]) - run), 0.0, 1.0)
	var swing := deg_to_rad(lerpf(26.0, 42.0, running) + 12.0 * sprinting) * moving
	var phase := _bob
	var air := 0.0 if puppet or is_on_floor() else 1.0
	var follow := 1.0 - exp(-14.0 * delta)
	for side in [["l", 1.0], ["r", -1.0]]:
		var part: String = side[0]
		var way: float = side[1]
		var leg := way * swing * sin(phase) + air * (0.35 if way > 0.0 else -0.15)
		var knee := -swing * lerpf(1.1, 1.7, running) * maxf(0.0, way * cos(phase)) - air * 0.6
		var arm := -way * swing * 0.75 * sin(phase)
		var elbow := deg_to_rad(lerpf(12.0, 80.0, running))
		_turn(part + "_leg", leg, follow)
		_turn(part + "_knee", knee, follow)
		_turn(part + "_ankle", -(leg + knee) * 0.8, follow)
		_turn(part + "_arm", arm, follow)
		_turn(part + "_elbow", elbow, follow)
	_turn("hips", -deg_to_rad(6.0 * running + 8.0 * sprinting), follow)


# Someone else's: no physics of its own (net.gd places it), not pushed by anything
func make_puppet() -> void:
	puppet = true
	set_physics_process(false)
	collision_mask = 0


# A rifle carried in the right hand, along the forearm (someone else's, when they have one)
func carry(rifle: bool) -> void:
	if rifle == (_rifle != null):
		return
	if _rifle:
		_rifle.queue_free()
		_rifle = null
		return
	var h := float(measures["height"])
	_rifle = Node3D.new()
	_rifle.name = "Rifle"
	var elbow: Node3D = _parts["r_elbow"]
	elbow.add_child(_rifle)
	_rifle.position = Vector3(0.0, -0.22 * h, -0.05)
	var wood := _paint(Color(0.28, 0.15, 0.07))
	var steel := _paint(Color(0.04, 0.04, 0.045))
	_box(_rifle, Vector3(0.04, 0.06, 0.75), Vector3(0.0, 0.0, -0.15), wood)
	_box(_rifle, Vector3(0.02, 0.02, 0.45), Vector3(0.0, 0.02, -0.7), steel)
	_box(_rifle, Vector3(0.04, 0.04, 0.3), Vector3(0.0, 0.07, -0.25), steel)


func _turn(part: String, angle: float, follow: float) -> void:
	var joint: Node3D = _parts[part]
	joint.rotation.x = lerp_angle(joint.rotation.x, angle, follow)
