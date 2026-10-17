# The rifle or bow in your hands on a hunt (hunt.gd gives it with the ticket): on foot, inside the farm.
#   right click (hold): aim (a rifle's scope, ~9x; the bow's pin sight)     left click: shoot (the bow: hold to draw,
#   let go to loose once it's drawn)     R: reload (a rifle's magazine from your rounds; the bow nocks its next arrow)
# Each shot is flown by Core (GeodriveHunt: its drop, its speed and energy at the animal, where it lands on it) and
# told: what was hit, where, how far, with how much energy, and whether it was legal. It's heard (the shot and its
# echoes off the hills, the bolt, a hit's "whop" coming back after the bullet's flight and the sound's way back at 343
# m/s; the sounds made once by GeodriveHunt.sound) and seen in your hands (the rifle or bow, its kick, the bolt worked).
extends Node

const SCOPE_FOV := 8.0	# degrees: a rifle scope at ~9x (the eyes' ~70 over 9)
const IRON_FOV := 50.0	# degrees: along plain sights (a little closer than the eyes)
const SIGHT_FOV := 30.0	# degrees: looking along the bow's sight
const DRAW_TIME := 0.8	# s to draw the bow
const BOLT_TIME := 1.0	# s to work the bolt after a shot
const RELOAD_TIME := 3.0	# s to fill a magazine
const NOCK_TIME := 2.5	# s to nock the next arrow
# the bow's pins below the 20 m one (m low at the distance: Core's flight of its arrow)
const PINS := [[20.0, 0.0], [30.0, 0.168], [40.0, 0.47]]

var hunt: Node
var main: Node
var load := {}	# (GeodriveHunt.loads()'s)
var loaded := 0	# in the magazine (or nocked)
var spare := 0	# the rest
var aiming := false
var _drawing := -1.0	# the bow: drawn for how long (-1: not)
var _busy_until := 0.0	# (working the bolt, reloading, nocking)
var _reloading := false	# (the busy time's a reload, not the bolt)
var _overlay: Control
var _ammo: Label
var _model: Node3D	# the rifle or bow in your hands (on the camera)
var _string: Node3D	# (the bow's string, drawn back)
var _kick := 0.0	# the shot's kick (1 at the shot, dying away)
var _trigger := false	# (held: an automatic fires on while it is)
var _next_round := 0.0	# (an automatic's next round, by its rate)
var _clock := 0.0	# (the game's time: _process's steps added up)
var shown_for_test := false	# (--hunttest's pictures: in hand though the mouse isn't captured)
var _rest := Vector3(0.21, -0.23, -0.44)	# where it's held, from the eyes
var _look := {}	# (a gun's look: its parts and its sight)
var _raised := 0.0	# (0 held low, 1 up to the eye along its sights)

const RATE := 22050	# the sounds' samples a second
static var _sounds := {}	# (made once: kind -> AudioStreamWAV)


func setup(given: Dictionary) -> void:
	load = given
	if load["arrow"]:
		loaded = 1
		spare = 11
	else:
		loaded = int(load["magazine"])
		spare = maxi(int(load.get("rounds", 20)) - loaded, 0)


func _ready() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	_overlay = Control.new()
	_overlay.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_overlay.draw.connect(_draw_overlay)
	layer.add_child(_overlay)
	_ammo = Label.new()
	_ammo.add_theme_font_size_override("font_size", 22)
	_ammo.add_theme_constant_override("outline_size", 6)
	_ammo.add_theme_color_override("font_outline_color", Color.BLACK)
	_ammo.position = Vector2(30.0, 30.0)
	layer.add_child(_ammo)
	_build_model()


func _exit_tree() -> void:
	if main and main.camera:
		main.camera.zoom_fov = 0.0
	if is_instance_valid(_model):
		_model.queue_free()


# A sound (made once), played now or after Delay s, Loud dB off full
func _play(kind: String, delay := 0.0, loud := 0.0) -> void:
	play_sound(self, main, kind, delay, loud)


static func play_sound(owner: Node, game: Node, kind: String, delay := 0.0, loud := 0.0) -> void:
	if not _sounds.has(kind):
		var wav := AudioStreamWAV.new()
		wav.format = AudioStreamWAV.FORMAT_16_BITS
		wav.mix_rate = RATE
		wav.stereo = false
		wav.data = GeodriveHunt.sound(kind, RATE)
		_sounds[kind] = wav
	var level: float = game.settings.sound_levels.get("geo.Sound.Volume", 1.0) if game and game.settings else 1.0
	if level <= 0.0:
		return
	var player := AudioStreamPlayer.new()
	player.stream = _sounds[kind]
	player.volume_db = loud + linear_to_db(level)
	owner.add_child(player)
	player.finished.connect(player.queue_free)
	if delay > 0.0:
		owner.get_tree().create_timer(delay).timeout.connect(func(): if is_instance_valid(player): player.play())
	else:
		player.play()


# The rifle (a wooden stock, the action, a barrel and its scope) or the bow (its riser, limbs and string), plain shapes
# on the camera, lower right (the bow, held out on the left); no shadows
func _build_model() -> void:
	_model = Node3D.new()
	_model.name = "InHand"
	var steel := _material(Color(0.04, 0.04, 0.045), 0.35)
	if load.get("arrow", false):
		_rest = Vector3(-0.1, -0.12, -0.5)
		var dark := _material(Color(0.06, 0.06, 0.05), 0.5)
		_part(_model, BoxMesh.new(), Vector3(0.025, 0.24, 0.035), Vector3.ZERO, Vector3.ZERO, dark)	# (the riser and grip)
		for side in [1.0, -1.0]:
			_part(_model, BoxMesh.new(), Vector3(0.018, 0.3, 0.03), Vector3(0.0, side * 0.24, 0.05), Vector3(side * -0.35, 0.0, 0.0), dark)	# (a limb, swept back)
			_part(_model, CylinderMesh.new(), Vector3(0.05, 0.012, 0.05), Vector3(0.0, side * 0.37, 0.1), Vector3(0.0, 0.0, PI * 0.5), steel)	# (its cam)
		_string = Node3D.new()
		_model.add_child(_string)
		_part(_string, BoxMesh.new(), Vector3(0.003, 0.74, 0.003), Vector3(0.0, 0.0, 0.1), Vector3.ZERO, _material(Color(0.8, 0.8, 0.75), 0.8))
		_part(_string, CylinderMesh.new(), Vector3(0.008, 0.7, 0.008), Vector3(0.0, 0.0, -0.25), Vector3(PI * 0.5, 0.0, 0.0), _material(Color(0.15, 0.4, 0.15), 0.6))	# (the arrow)
		_model.rotation = Vector3(0.0, 0.0, 0.15)
	else:
		# (a gun: its look, its file's own or its kind's: shapes, made by part_model.gd)
		var look: Dictionary = load.get("look", {}) if load.get("look") is Dictionary else {}
		_look = look if not look.get("parts", []).is_empty() else PartModel.default_look(String(load.get("model", "rifle")))
		_model.add_child(PartModel.build(_look))
		if load.get("model", "rifle") == "smg":
			_rest = Vector3(0.17, -0.2, -0.38)
		_model.rotation = Vector3(0.0, 0.06, 0.0)
	_model.position = _rest
	if main and main.camera:
		main.camera.add_child(_model)


func _material(colour: Color, rough: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.roughness = rough
	return m


func _part(parent: Node3D, mesh: PrimitiveMesh, size: Vector3, at: Vector3, turn: Vector3, material: Material) -> void:
	if mesh is BoxMesh:
		mesh.size = size
	elif mesh is CylinderMesh:
		mesh.top_radius = size.x * 0.5
		mesh.bottom_radius = size.z * 0.5
		mesh.height = size.y
		mesh.radial_segments = 10
		mesh.rings = 1
	var part := MeshInstance3D.new()
	part.mesh = mesh
	part.material_override = material
	part.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	part.position = at
	part.rotation = turn
	parent.add_child(part)


# The rifle or bow: seen when on foot in your own eyes, not aiming (the scope's or sight's view is drawn instead); its
# kick back and up, the bolt worked (the rifle rolled a little), the bow's string drawn back
func _move_model(delta: float, on: bool) -> void:
	if not is_instance_valid(_model):
		return
	var sights: bool = aiming and not load["arrow"]
	# (a scope: the gun seen coming up, then its view through the glass once it's at the eye)
	_model.visible = (on or shown_for_test) and (not aiming or (sights and not _through_scope())) and not main.camera.free and not main.camera.walker_behind
	_raised = move_toward(_raised, 1.0 if sights else 0.0, delta / 0.15)	# (up to the eye in ~0.15 s)
	_kick = maxf(_kick - delta * 4.0, 0.0)
	var k := _kick * _kick
	var working := 0.0
	if not load["arrow"] and _now() < _busy_until and (_reloading or _action() == "bolt"):
		var left := (_busy_until - _now()) / (_reload_time() if _reloading else BOLT_TIME)
		working = sin(clampf(1.0 - left, 0.0, 1.0) * PI)
	# (raised: its sight straight ahead of the eye, eye_relief in front of it, the gun along the look)
	var up: Vector3 = Vector3(0.0, 0.0, -float(_look.get("eye_relief", 0.1))) - PartModel.sight_of(_look) if not _look.is_empty() else _rest
	var held := _rest.lerp(up, _raised)
	_model.position = held + Vector3(0.0, k * 0.03, k * 0.09) * (1.0 - _raised * 0.6) + Vector3(-0.04, -0.03, 0.0) * working
	_model.rotation.x = k * 0.25 * (1.0 - _raised * 0.5)
	_model.rotation.y = 0.06 * (1.0 - _raised) if not load["arrow"] else _model.rotation.y
	if not load["arrow"]:
		_model.rotation.z = working * 0.35
	if _string:
		_string.position.z = minf(maxf(_drawing, 0.0) / DRAW_TIME, 1.0) * 0.45
		_string.visible = true
		_string.get_child(1).visible = loaded > 0


# In use: on foot, inside the farm being hunted, not building, no menu up
func usable() -> bool:
	return main.walker != null and not main.walker.down_wanted and hunt.hunting >= 0 and not (main.builder and main.builder.active) and not main.menu.is_open() \
		and (Input.mouse_mode == Input.MOUSE_MODE_CAPTURED or main.testing)	# (a test's: no mouse to capture without a window)


# The game's own time (s): the bolt, a reload, an automatic's rate go by it, not the wall's clock
func _now() -> float:
	return _clock


func _process(delta: float) -> void:
	_clock += delta
	var on := usable()
	if not on:
		aiming = false
		_drawing = -1.0
	main.camera.zoom_fov = (SIGHT_FOV if load["arrow"] else (SCOPE_FOV if _through_scope() else IRON_FOV)) if aiming else 0.0
	# (an automatic: rounds on while the trigger's held, at its rate)
	if _trigger and on and _action() == "auto" and _now() >= _next_round:
		_fire()
	elif not on:
		_trigger = false
	if _drawing >= 0.0:
		_drawing += delta
	# the bow nocks its next arrow by itself
	if load["arrow"] and loaded == 0 and spare > 0 and _now() >= _busy_until:
		loaded = 1
		spare -= 1
	_ammo.visible = main.walker != null and hunt.ticket >= 0
	_ammo.text = "%s   %s" % [load["name"], ("arrow nocked" if loaded > 0 else "no arrow") + ", %d more" % spare if load["arrow"] else "%d in the magazine, %d more" % [loaded, spare]]
	if _now() < _busy_until:
		_ammo.text += "   (%s)" % ("nocking" if load["arrow"] else ("reloading" if _reloading else ("working the bolt" if _action() == "bolt" else "")))
	_overlay.visible = on
	_overlay.queue_redraw()
	_move_model(delta, on)


func _unhandled_input(event: InputEvent) -> void:
	if not usable():
		return
	if event is InputEventMouseButton:
		match event.button_index:
			MOUSE_BUTTON_RIGHT:
				aiming = event.pressed
				get_viewport().set_input_as_handled()
			MOUSE_BUTTON_LEFT:
				if load["arrow"]:
					if event.pressed and loaded > 0 and _now() >= _busy_until:
						_drawing = 0.0
					elif not event.pressed and _drawing >= 0.0:
						if _drawing >= DRAW_TIME:
							_fire()
						else:
							main.hud.set_status("Let down (draw it fully: hold the button)", 1.5)
						_drawing = -1.0
				else:
					_trigger = event.pressed
					if event.pressed:
						_fire()
				get_viewport().set_input_as_handled()
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_R:
		_reload()
		get_viewport().set_input_as_handled()


func _reload() -> void:
	if load["arrow"] or _now() < _busy_until:
		return
	var room := int(load["magazine"]) - loaded
	var take := mini(room, spare)
	if take <= 0:
		main.hud.set_status("The magazine's full" if room <= 0 else "No rounds left", 1.5)
		return
	loaded += take
	spare -= take
	_busy_until = _now() + _reload_time()
	_reloading = true
	_play("reload")


# Looking through its scope now (aiming, the gun up at the eye)
func _through_scope() -> bool:
	return aiming and not load["arrow"] and load.get("sight", "scope") == "scope" and _raised > 0.95


# How it's worked: bolt, semi, auto (a bow: its own)
func _action() -> String:
	return "bow" if load["arrow"] else String(load.get("action", "bolt"))


func _reload_time() -> float:
	return float(load.get("reload_time", RELOAD_TIME))


func _fire() -> void:
	if _now() < _busy_until or _now() < _next_round:
		return
	if loaded <= 0:
		_trigger = false
		main.hud.set_status("Empty: R to reload" if spare > 0 else "No rounds left", 1.5)
		return
	var camera: Camera3D = main.camera
	var eye := camera.global_position
	var aim := -camera.global_transform.basis.z
	var result: Dictionary = hunt.shoot(load["id"], eye, aim)
	# (stopped on someone: whose body, and where on it)
	if main.net and result.has("collider_id") and int(result["collider_id"]) != 0:
		var body := instance_from_id(int(result["collider_id"]))
		var id: int = main.net.person_of(body) if body else -1
		if id >= 0:
			var up: float = ((result["point"] as Vector3).y - (body as Node3D).global_position.y) / (body.height_cm / 100.0)
			result["person"] = id
			result["name"] = main.net._people[id]["name"]
			result["zone"] = "head" if up > 0.87 else ("chest" if up > 0.6 else ("belly" if up > 0.5 else "legs"))
			main.net.hit_person(id, result["zone"], result["distance"], result["energy"])
	if main.net:
		main.net.fired(eye, _sound())
	loaded -= 1
	# (the next: the bow nocked, the bolt worked; a semi's next pull; an automatic's next round by its rate)
	match _action():
		"bow":
			_busy_until = _now() + NOCK_TIME
		"bolt":
			_busy_until = _now() + BOLT_TIME
		"semi":
			_next_round = _now() + 0.12
		"auto":
			# (from when it was due, held on: its true rate, not slowed to the frames)
			var due := _next_round if _now() - _next_round < 0.1 else _now()
			_next_round = due + 60.0 / maxf(float(load.get("rate", 600.0)), 60.0)
	_reloading = false
	_effects(result)
	main.hud.set_status(report(result), 6.0)
	print("Geodrive hunt: " + report(result))


# A shot seen and heard: the kick, the report, the bolt, a hit coming back
func _effects(result: Dictionary) -> void:
	_kick = (1.0 if not load["arrow"] else 0.3) * minf(float(load.get("kick", 1.0)) + 0.3, 1.0)
	# the shot heard (the bolt worked straight after), and a hit's "whop" coming back: the bullet's flight there (at
	# about its mean speed) and the sound's way back
	_play(_sound())
	if _action() == "bolt":
		_play("bolt", 0.25, -8.0)
	if result.get("hit", false) and result.has("distance"):
		var distance: float = result["distance"]
		var flight: float = distance / maxf((float(load["muzzle"]) + float(result.get("speed", load["muzzle"]))) * 0.5, 1.0)
		var loud := clampf(-20.0 * log(maxf(distance, 10.0) / 30.0) / log(10.0), -40.0, 0.0) - (0.0 if load["arrow"] else 4.0)
		_play("arrow_hit" if load["arrow"] else "hit", flight + distance / 343.0, loud)
	# (a rifle's kick: the muzzle jumps, as much as the gun's; an automatic's a little to the side too)
	if not load["arrow"] and main.walker:
		var kick := float(load.get("kick", 1.0))
		main.walker.mouse_moved(Vector2(randf_range(-6.0, 6.0) * kick if _action() == "auto" else 0.0, (-60.0 if not aiming else -12.0) * kick))


# Its report: a bow's, a pistol calibre's (a submachine gun's), a rifle's
func _sound() -> String:
	return "bow" if load["arrow"] else ("smg" if load.get("model", "rifle") == "smg" else "rifle")


static func report(result: Dictionary) -> String:
	if result.is_empty():
		return "Not here: hunt inside the farm"
	if result.has("person"):
		return "You shot %s: %s, %d m, %d ft-lb" % [result["name"], result["zone"], roundi(result["distance"]), roundi(result["energy"])]
	if not result.get("hit", false):
		return "Missed" + (" (into the ground at %d m)" % roundi(result["distance"]) if result.has("distance") else "")
	var line := "Hit: %s, %s, %d m, %d ft-lb" % [result["name"], result["zone"], roundi(result["distance"]), roundi(result["energy"])]
	if result.get("already_dead", false):
		line += " (already down)"
	elif result.get("down", false):
		line += ": down"
	elif result.get("dies", false):
		line += ": hit hard" + (", it ran" if result.get("flees", true) else "")
	else:
		line += ": wounded" + (", it ran" if result.get("flees", true) else "")
	if not result.get("legal", true):
		line += "   ILLEGAL: " + str(result["why"])
	return line


# The scope (black round its glass, a duplex crosshair), the bow's sight (a peep ring, its pins at 20, 30 and 40 m),
# or, not aiming, a dot
func _draw_overlay() -> void:
	var size := _overlay.size
	var centre := size * 0.5
	if not aiming or (not load["arrow"] and load.get("sight", "scope") == "scope" and not _through_scope()):
		if aiming:
			return	# (the gun coming up to the eye: nothing drawn over it yet)
		_overlay.draw_circle(centre, 2.0, Color(1, 1, 1, 0.8))
		if _drawing >= 0.0:
			_overlay.draw_arc(centre, 14.0, -PI * 0.5, -PI * 0.5 + TAU * minf(_drawing / DRAW_TIME, 1.0), 32, Color(1, 1, 1, 0.8), 2.0)
		return
	var black := Color(0, 0, 0, 1)
	if load["arrow"]:
		var ring := minf(size.x, size.y) * 0.3
		_overlay.draw_arc(centre, ring, 0.0, TAU, 64, Color(0, 0, 0, 0.85), 6.0)
		var fov := deg_to_rad(SIGHT_FOV)
		var colours := [Color(0.3, 1.0, 0.3), Color(1.0, 0.9, 0.2), Color(1.0, 0.35, 0.3)]
		for k in PINS.size():
			var below: float = atan(PINS[k][1] / PINS[k][0]) / fov * size.y
			_overlay.draw_circle(centre + Vector2(0.0, below), 3.0, colours[k])
			_overlay.draw_string(ThemeDB.fallback_font, centre + Vector2(10.0, below + 5.0), "%d" % roundi(PINS[k][0]), HORIZONTAL_ALIGNMENT_LEFT, -1, 14, colours[k])
		if _drawing >= 0.0:
			_overlay.draw_arc(centre, ring + 10.0, -PI * 0.5, -PI * 0.5 + TAU * minf(_drawing / DRAW_TIME, 1.0), 48, Color(1, 1, 1, 0.8), 3.0)
		return
	if load.get("sight", "scope") == "iron":
		return	# (plain sights: the gun's own, up to the eye)
	var radius := minf(size.x, size.y) * 0.46
	var outer := size.length()
	_overlay.draw_arc(centre, radius + outer * 0.5, 0.0, TAU, 96, black, outer)
	_overlay.draw_rect(Rect2(0.0, 0.0, centre.x - radius, size.y), black)
	_overlay.draw_rect(Rect2(centre.x + radius, 0.0, size.x - centre.x - radius, size.y), black)
	# (a duplex: thick posts from the edge, fine wires through the middle)
	var thin := 1.2
	var thick := 5.0
	var gap := radius * 0.35
	_overlay.draw_line(centre + Vector2(-radius, 0.0), centre + Vector2(-gap, 0.0), black, thick)
	_overlay.draw_line(centre + Vector2(gap, 0.0), centre + Vector2(radius, 0.0), black, thick)
	_overlay.draw_line(centre + Vector2(0.0, gap), centre + Vector2(0.0, radius), black, thick)
	_overlay.draw_line(centre + Vector2(-gap, 0.0), centre + Vector2(gap, 0.0), black, thin)
	_overlay.draw_line(centre + Vector2(0.0, -radius), centre + Vector2(0.0, gap), black, thin)
