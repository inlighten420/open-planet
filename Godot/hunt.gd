# Hunting: game farms (a pack's reserves/*.json, packs.gd: a real farm's outline from the Surveyor General's cadastre, its gates, its
# water, its stock), their game fences, and the hunt itself: a ticket at the main gate's kiosk (a rifle or the bow,
# and its rounds: weapon.gd), the animals (Core's: GeodriveHunt), out on the farm whenever you're near it (seen from
# the road and the kiosk, as a real farm's are), hunted only inside with a ticket; what you've taken today, by the law
# (the district's seasons and bag limits). Inside a farm everything on it is drawn: its animals all over it, its
# bushes far out (vegetation.gd's wide ring).
#
# The fence: a game fence 2.4 m high along the outline, a post every 4 m and six strands of wire, following the
# ground; an opening at each gate (its two gateposts, the main gate's sign). Drawn once its farm is in the built area
# and you're within a few km; solid only along the stretch near you (23 km of fence would be ~5,700 boxes).
extends Node

const FENCE_HEIGHT := 2.4	# m (a game fence)
const POST_STEP := 4.0	# m between posts
const STRANDS := [0.15, 0.5, 0.9, 1.3, 1.8, 2.35]	# m: the wires' heights
const GATE_GAP := 6.0	# m: a gate's opening
const NEAR := 5000.0	# m: a farm's fence is drawn once you're this near it
const SOLID_REACH := 60.0	# m: the fence is solid this far round you
const KIOSK_REACH := 5.0	# m: on foot this near the ticket kiosk, E buys a ticket
const DRAWN_TO := 6000.0	# m: animals this near are drawn (the whole farm, from anywhere on it or near it)
const WEAPON := preload("res://weapon.gd")

var main: Node
var world: Node
var reserves: Array = []	# from the packs' reserves/*.json: {name, about, district, outline [[lat, lon]...], gates [...], water [...]}
var _root: Node3D	# the fences (moved with Godot's 0)
var _built := {}	# reserve index -> its fence's pieces [[a, b]] (in _root's frame), once drawn
var _solid: StaticBody3D
var _solid_near: Array = []	# (which pieces are solid now)
var _check := 0.0
var _solid_check := 0.0
var _kiosks: Array = []	# [{node, reserve}] (under _root)
var hunter: Node = null	# GeodriveHunt: the animals, while a hunt's on
var weapon: Node = null	# weapon.gd: what you were given at the kiosk (null: no ticket)
var living := -1	# the farm whose animals are out (you're near it), or -1
var hunting := -1	# the farm hunted now (its index: inside it with its ticket), or -1
var ticket := -1	# the farm the ticket's for (-1: none)
var taken := {}	# today: species id -> how many killed (the bag)
var _taken_day := ""
var _shots: Array = []	# this hunt's shots (GeodriveHunt.shoot's results)
var _chooser: PanelContainer
var _hint := 0.0
var _was_inside := false
var _last_shared := -100.0	# (when the host's animals last came: following them till they stop coming)
var rules: GeodriveGame = null	# (the game being played's rules: Core's GeoGame; null, it has none)
var _course: Array = []	# (a race's checkpoints, as played: [{node, at (Godot), radius}])
var _finished_told := false
const BEST_PATH := "user://race_times.json"	# (each race's best time, by its id)
var car_hits: Array = []	# (the car's hits so far: take_hits' dictionaries, for the tests)
var _car_box := Vector3.ZERO	# (the car's own box, found once a car: its size)
var _car_for := 0	# (which car that was)
var _booth_hint := 0.0
var _grace_left := 0.0	# (just up again: come for, not caught, this much longer)
const GRACE := 10.0	# s	# (in an area without its ticket: when next to say where its booth is)
var _down_left := 0.0	# caught (by something that goes for people): down this much longer (s of the game's time), then up by the car
const DOWN_FOR := 5.0	# s
# what an area's file may say in its "texts" (each a hunting farm's by default; %s: the area's name)
const TEXTS := {"sign": "TICKETS\n(E)", "booth": "%s\nHunting: tickets here", "title": "%s: a hunting ticket", "law": true,
	"note": "In through the gate to hunt; out through it again and the hunt's over.        Esc: not now",
	"hint": "Hunting tickets: E", "start": "Hunting on %s: %d animals. Right click to aim, left click to shoot, R to reload",
	"ticket": "Ticket for %s: %s. Walk in through the gate (beside the kiosk) and the hunt's on", "bag": "your bag"}


func _ready() -> void:
	_root = Node3D.new()
	_root.name = "Reserves"
	main.add_child(_root)
	_solid = StaticBody3D.new()
	_solid.name = "FenceSolid"
	_root.add_child(_solid)
	Packs.load_all()	# (the packs' creatures and weapons: before any farm's stocked)
	for path in Packs.files("reserves"):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
		if parsed is Dictionary:
			reserves.append(parsed)
	print("Geodrive: %d game farms (%s)" % [reserves.size(), ", ".join(reserves.map(func(r): return r["name"]))])
	if ClassDB.class_exists("GeodriveHunt"):
		hunter = ClassDB.instantiate("GeodriveHunt")
		hunter.name = "Hunt"
		main.add_child.call_deferred(hunter)


# Which farm a place is inside (its index), or -1
func reserve_at(lat: float, lon: float) -> int:
	for i in reserves.size():
		if _inside(reserves[i]["outline"], lat, lon):
			return i
	return -1


# Whether a place is inside an area (its own outline: areas can overlap)
func _in(index: int, here: PackedFloat64Array) -> bool:
	return index >= 0 and index < reserves.size() and _inside(reserves[index]["outline"], here[0], here[1])


static func _inside(outline: Array, lat: float, lon: float) -> bool:
	var inside := false
	var j := outline.size() - 1
	for i in outline.size():
		var a: Array = outline[i]
		var b: Array = outline[j]
		if (a[0] > lat) != (b[0] > lat) and lon < (b[1] - a[1]) * (lat - a[0]) / (b[0] - a[0]) + a[1]:
			inside = not inside
		j = i
	return inside


func _player() -> Node3D:
	if main.walker:
		return main.walker
	if main.aviation and main.aviation.flying and main.aviation.aircraft:
		return main.aviation.aircraft
	return main.car


# --- each step ---

func physics(delta: float) -> void:
	if world == null or not world.is_ready():
		return
	var player := _player()
	if player == null:
		return
	_check -= delta
	if _check <= 0.0:
		_check = 1.0
		var here: PackedFloat64Array = world.to_lat_lon(player.global_position)
		for i in reserves.size():
			if _built.has(i):
				continue
			var centre := _centre(reserves[i])
			if _metres(here[0], here[1], centre.x, centre.y) < _reach(reserves[i]) and world.contains_lat_lon(centre.x, centre.y):
				if reserves[i].get("fence", true):
					_draw_fence(i)
				else:
					_place_booths(i)
	_solid_check -= delta
	if _solid_check <= 0.0:
		_solid_check = 0.25
		_update_solid(_root.to_local(player.global_position))
	_hunt_step(delta, player)


# What an area's file says (its "texts"), or a hunting farm's words
static func _text(reserve: Dictionary, key: String):
	return reserve.get("texts", {}).get(key, TEXTS[key])


# How near an area its animals come out: NEAR, or its own size and a kilometre more (a town)
static func _reach(reserve: Dictionary) -> float:
	var centre := _centre(reserve)
	var widest := 0.0
	for p in reserve["outline"]:
		widest = maxf(widest, _metres(centre.x, centre.y, p[0], p[1]))
	return maxf(NEAR, widest + 1000.0)


static func _centre(reserve: Dictionary) -> Vector2:
	var sum := Vector2.ZERO
	for p in reserve["outline"]:
		sum += Vector2(p[0], p[1])
	return sum / reserve["outline"].size()


static func _metres(lat0: float, lon0: float, lat1: float, lon1: float) -> float:
	return Vector2((lon1 - lon0) * 111320.0 * cos(deg_to_rad(lat0)), (lat1 - lat0) * 110574.0).length()


# --- the fence ---

func _draw_fence(index: int) -> void:
	var reserve: Dictionary = reserves[index]
	var outline: Array = reserve["outline"]
	var gates: Array = []
	for gate in reserve["gates"]:
		gates.append(world.from_lat_lon(gate["lat"], gate["lon"]))
	# posts every POST_STEP along the outline (in Godot's frame now), each on the ground; none in a gate's opening
	var posts: Array = []	# [position, after a gap (no wire to the one before)]
	var gap := true
	for i in outline.size():
		var a: Vector3 = world.from_lat_lon(outline[i][0], outline[i][1])
		var b: Vector3 = world.from_lat_lon(outline[(i + 1) % outline.size()][0], outline[(i + 1) % outline.size()][1])
		var length := Vector2(b.x - a.x, b.z - a.z).length()
		var steps := maxi(1, ceili(length / POST_STEP))
		for s in steps:
			var p := a.lerp(b, float(s) / steps)
			var in_gate := false
			for g in gates:
				in_gate = in_gate or Vector2(p.x - g.x, p.z - g.z).length() < GATE_GAP * 0.5
			if in_gate:
				gap = true
				continue
			p.y = world.ground_height(p)
			posts.append([p, gap])
			gap = false
	# the wire: thin strips between neighbouring posts (both sides seen); the posts, one multimesh
	var tool := SurfaceTool.new()
	tool.begin(Mesh.PRIMITIVE_TRIANGLES)
	var pieces: Array = []
	for k in range(1, posts.size()):
		if posts[k][1]:
			continue
		var a: Vector3 = posts[k - 1][0]
		var b: Vector3 = posts[k][0]
		pieces.append([a, b])
		for h in STRANDS:
			var up := Vector3(0.0, 0.012, 0.0)
			var a0: Vector3 = a + Vector3.UP * h
			var b0: Vector3 = b + Vector3.UP * h
			for v in [a0 - up, b0 - up, b0 + up, a0 - up, b0 + up, a0 + up]:
				tool.add_vertex(v)
	var wire := MeshInstance3D.new()
	wire.name = "FenceWire"
	wire.mesh = tool.commit()
	var steel := StandardMaterial3D.new()
	steel.albedo_color = Color(0.45, 0.46, 0.47)
	steel.metallic = 0.7
	steel.roughness = 0.5
	steel.cull_mode = BaseMaterial3D.CULL_DISABLED
	wire.material_override = steel
	wire.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_root.add_child(wire)
	wire.global_position = Vector3.ZERO
	var poles := MultiMesh.new()
	poles.transform_format = MultiMesh.TRANSFORM_3D
	var pole := BoxMesh.new()
	pole.size = Vector3(0.09, FENCE_HEIGHT + 0.1, 0.09)
	poles.mesh = pole
	poles.instance_count = posts.size()
	for k in posts.size():
		poles.set_instance_transform(k, Transform3D(Basis(), (posts[k][0] as Vector3) + Vector3.UP * (FENCE_HEIGHT + 0.1) * 0.5))
	var post_node := MultiMeshInstance3D.new()
	post_node.name = "FencePosts"
	post_node.multimesh = poles
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.32, 0.22, 0.12)
	wood.roughness = 0.9
	post_node.material_override = wood
	_root.add_child(post_node)
	post_node.global_position = Vector3.ZERO
	# the gates: two tall gateposts each, and the main gate's sign
	for g in reserve["gates"].size():
		_draw_gate(reserve, reserve["gates"][g], gates[g], outline)
	# (kept in _root's frame: it moves with Godot's 0)
	var local: Array = []
	for piece in pieces:
		local.append([_root.to_local(piece[0]), _root.to_local(piece[1])])
	_built[index] = local
	if main.fence_test:
		# (the free camera outside the main gate, looking in at it)
		var gate: Vector3 = gates[0]
		var inward: Vector3 = world.from_lat_lon(_centre(reserve).x, _centre(reserve).y) - gate
		inward.y = 0.0
		inward = inward.normalized()
		main.camera.start_free()
		main.camera.global_position = gate - inward * 18.0 + Vector3.UP * 4.0
		main.camera.look_at(gate + Vector3.UP * 1.5, Vector3.UP)
		main.camera._free_pitch = main.camera.rotation.x
		main.camera._free_yaw = main.camera.rotation.y
	print("Geodrive: %s's game fence: %d posts, %.1f km, %d gates" % [reserve["name"], posts.size(), pieces.size() * POST_STEP / 1000.0, gates.size()])


# An area with no fence (a town): only its ticket booths, each where its file puts it
func _place_booths(index: int) -> void:
	var reserve: Dictionary = reserves[index]
	for gate in reserve["gates"]:
		if gate.get("tickets", false):
			var at: Vector3 = world.from_lat_lon(gate["lat"], gate["lon"])
			_add_kiosk(reserve, at, Vector3.RIGHT, false)
			_add_sign(reserve, at)
	_built[index] = []
	print("Geodrive: %s: no fence, %d booths" % [reserve["name"], reserve["gates"].filter(func(g): return g.get("tickets", false)).size()])


func _add_sign(reserve: Dictionary, at: Vector3) -> void:
	var sign := Label3D.new()
	sign.text = String(_text(reserve, "booth")).replace("%s", reserve["name"])
	sign.font_size = 96
	sign.pixel_size = 0.008
	sign.outline_size = 18
	sign.modulate = Color(1.0, 0.92, 0.75)
	sign.billboard = BaseMaterial3D.BILLBOARD_FIXED_Y
	_root.add_child(sign)
	sign.global_position = Vector3(at.x, world.ground_height(at) + 5.2, at.z)	# (over the booth's own label)


func _draw_gate(reserve: Dictionary, gate: Dictionary, at: Vector3, outline: Array) -> void:
	# (which way the fence runs there: the outline's nearest side)
	var along := Vector3.RIGHT
	var best := INF
	for i in outline.size():
		var a: Vector3 = world.from_lat_lon(outline[i][0], outline[i][1])
		var b: Vector3 = world.from_lat_lon(outline[(i + 1) % outline.size()][0], outline[(i + 1) % outline.size()][1])
		var ab := Vector3(b.x - a.x, 0.0, b.z - a.z)
		var t := clampf(Vector3(at.x - a.x, 0.0, at.z - a.z).dot(ab) / maxf(ab.length_squared(), 0.01), 0.0, 1.0)
		var d := Vector3(at.x - a.x - ab.x * t, 0.0, at.z - a.z - ab.z * t).length()
		if d < best:
			best = d
			along = ab.normalized()
	var wood := StandardMaterial3D.new()
	wood.albedo_color = Color(0.28, 0.19, 0.1)
	wood.roughness = 0.9
	for side in [-1.0, 1.0]:
		var spot: Vector3 = at + along * (side * GATE_GAP * 0.5)
		spot.y = world.ground_height(spot)
		var post := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = Vector3(0.3, 3.2, 0.3)
		post.mesh = box
		post.material_override = wood
		_root.add_child(post)
		post.global_position = spot + Vector3.UP * 1.6
	if gate.get("tickets", false):
		_add_kiosk(reserve, at, along)
		_add_sign(reserve, at)


# The fence solid along the stretch near you (boxes, rebuilt as you move)
func _update_solid(near: Vector3) -> void:
	var wanted: Array = []
	for index in _built:
		for piece in _built[index]:
			var a: Vector3 = piece[0]
			if Vector2(a.x - near.x, a.z - near.z).length() < SOLID_REACH:
				wanted.append(piece)
	if wanted == _solid_near:
		return
	_solid_near = wanted
	for child in _solid.get_children():
		child.queue_free()
	for piece in wanted:
		var a: Vector3 = piece[0]
		var b: Vector3 = piece[1]
		var length := Vector2(b.x - a.x, b.z - a.z).length()
		if length < 0.1:
			continue
		var shape := BoxShape3D.new()
		shape.size = Vector3(length, FENCE_HEIGHT, 0.12)
		var node := CollisionShape3D.new()
		node.shape = shape
		var middle := (a + b) * 0.5 + Vector3.UP * FENCE_HEIGHT * 0.5
		node.transform = Transform3D(Basis(Vector3.UP, atan2(-(b.z - a.z), b.x - a.x)), middle)
		_solid.add_child(node)


# --- Godot's 0 and new areas ---

func shift(offset: Vector3) -> void:
	_root.position += offset
	if hunter and hunter.is_running():
		hunter.shift(offset)


func reset_area() -> void:
	_end_hunt(false)
	if hunter and hunter.is_running():
		hunter.stop()
	living = -1
	_kiosks = []
	for child in _root.get_children():
		if child != _solid:
			child.queue_free()
	for child in _solid.get_children():
		child.queue_free()
	_root.position = Vector3.ZERO
	_built = {}
	_solid_near = []


# --- the ticket kiosk (outside the main gate) ---

func _add_kiosk(reserve: Dictionary, gate: Vector3, along: Vector3, beside_gate: bool = true) -> void:
	# (on the outside of the gate: away from the farm's middle, beside the road; a booth with no gate: just there)
	var middle: Vector3 = world.from_lat_lon(_centre(reserve).x, _centre(reserve).y)
	var out := Vector3(gate.x - middle.x, 0.0, gate.z - middle.z).normalized()
	var at := gate + out * 10.0 + along * (GATE_GAP * 0.5 + 4.0) if beside_gate else gate
	at.y = world.ground_height(at)
	var kiosk := Node3D.new()
	kiosk.name = "TicketKiosk"
	var walls := MeshInstance3D.new()
	var hut := BoxMesh.new()
	hut.size = Vector3(3.0, 2.6, 2.4)
	walls.mesh = hut
	walls.position = Vector3(0.0, 1.3, 0.0)
	var paint := StandardMaterial3D.new()
	paint.albedo_color = Color(0.55, 0.42, 0.25)
	walls.material_override = paint
	kiosk.add_child(walls)
	var roof := MeshInstance3D.new()
	var thatch := BoxMesh.new()
	thatch.size = Vector3(3.6, 0.3, 3.0)
	roof.mesh = thatch
	roof.position = Vector3(0.0, 2.75, 0.0)
	var straw := StandardMaterial3D.new()
	straw.albedo_color = Color(0.45, 0.36, 0.18)
	roof.material_override = straw
	kiosk.add_child(roof)
	var label := Label3D.new()
	label.text = _text(reserve, "sign")
	label.font_size = 64
	label.pixel_size = 0.006
	label.outline_size = 12
	label.position = Vector3(0.0, 3.4, 0.0)
	label.billboard = BaseMaterial3D.BILLBOARD_FIXED_Y
	kiosk.add_child(label)
	_root.add_child(kiosk)
	kiosk.global_position = at
	_kiosks.append({"node": kiosk, "reserve": reserves.find(reserve)})


# The kiosk within reach (on foot), or {}
func kiosk_near(at: Vector3) -> Dictionary:
	for kiosk in _kiosks:
		if is_instance_valid(kiosk["node"]) and Vector2(at.x - kiosk["node"].global_position.x, at.z - kiosk["node"].global_position.z).length() < KIOSK_REACH:
			return kiosk
	return {}


func chooser_open() -> bool:
	return _chooser != null and _chooser.visible


# E at the kiosk: what's open today (the district's seasons), and a ticket with a rifle or the bow
func open_tickets(kiosk: Dictionary) -> void:
	var reserve: Dictionary = reserves[kiosk["reserve"]]
	if _chooser == null:
		var layer := CanvasLayer.new()
		add_child(layer)
		_chooser = PanelContainer.new()
		var style := StyleBoxFlat.new()
		style.bg_color = Color(0.07, 0.08, 0.09, 0.94)
		style.set_corner_radius_all(10)
		style.set_content_margin_all(18)
		_chooser.add_theme_stylebox_override("panel", style)
		layer.add_child(_chooser)
	for child in _chooser.get_children():
		child.queue_free()
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 8)
	_chooser.add_child(rows)
	var title := Label.new()
	title.text = String(_text(reserve, "title")) % reserve["name"]
	title.add_theme_font_size_override("font_size", 24)
	rows.add_child(title)
	var month := int(Time.get_date_dict_from_system()["month"])
	var open: Array = []
	var shut: Array = []
	var protected: Array = []
	for s in GeodriveHunt.species_list():
		var stocked := false
		for entry in reserve.get("stock", []):
			stocked = stocked or entry[0] == s["id"]
		if not stocked:
			continue
		var season: Array = s["season"]
		if s["protected"]:
			protected.append(s["name"])
		elif season[0] == 0:
			shut.append("%s (closed in this district)" % s["name"])
		elif month >= season[0] and month <= season[1]:
			open.append("%s (%d a day)" % [s["name"], s["bag"]])
		else:
			shut.append("%s (%s to %s)" % [s["name"], _month(season[0]), _month(season[1])])
	var law := Label.new()
	law.text = "Open in %s (%s district, CapeNature 2025): %s\nNot now: %s\nProtected, never: %s" % [_month(month), reserve.get("district", ""),
		", ".join(open), ", ".join(shut), ", ".join(protected)]
	law.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	law.custom_minimum_size = Vector2(700.0, 0.0)
	law.modulate = Color(1, 1, 1, 0.85)
	if _text(reserve, "law"):
		rows.add_child(law)
	else:
		law.queue_free()
	var offered: Array = reserve.get("weapons", [])
	if offered == ["none"]:
		# (a game with no weapons, a race: a ticket, nothing to carry)
		var part := Button.new()
		part.text = "Take part"
		part.custom_minimum_size = Vector2(700.0, 60.0)
		part.focus_mode = Control.FOCUS_NONE
		part.pressed.connect(_buy.bind(kiosk["reserve"], {}))
		rows.add_child(part)
	for load in (GeodriveHunt.loads() if offered != ["none"] else []):
		if String(load["id"]).begins_with("workshop_") or (not offered.is_empty() and not offered.has(load["id"])):
			continue	# (the workshop's preview; one this game's booth doesn't offer)
		var button := Button.new()
		button.text = "%s\n%d ft-lb at the %s; %s" % [load["name"], roundi(load["energy"]), "bow" if load["arrow"] else "muzzle",
			"12 arrows (one nocked at a time)" if load["arrow"] else "20 rounds, %d in the magazine, zeroed at %d m" % [load["magazine"], roundi(load["zero"])]]
		button.custom_minimum_size = Vector2(700.0, 60.0)
		button.focus_mode = Control.FOCUS_NONE
		button.pressed.connect(_buy.bind(kiosk["reserve"], load))
		rows.add_child(button)
	var note := Label.new()
	note.text = _text(reserve, "note")
	note.modulate = Color(1, 1, 1, 0.7)
	rows.add_child(note)
	_chooser.visible = true
	_chooser.reset_size()
	_chooser.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


static func _month(m: int) -> String:
	return ["", "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"][clampi(m, 0, 12)]


func close_tickets() -> void:
	if chooser_open():
		_chooser.visible = false
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _buy(reserve: int, load: Dictionary) -> void:
	close_tickets()
	if reserves[reserve].has("golf") and main.golf:
		main.golf.start(reserves[reserve])	# (a round of golf: golf.gd plays it)
		return
	ticket = reserve
	if weapon:
		weapon.queue_free()
		weapon = null
	if load.is_empty():
		main.hud.set_status("Ticket for %s: you're in. To the start!" % reserves[reserve]["name"], 5.0)
		return	# (nothing to carry: a race)
	weapon = WEAPON.new()
	weapon.name = "Weapon"
	weapon.hunt = self
	weapon.main = main
	weapon.setup(load)
	add_child(weapon)
	main.hud.set_status(String(_text(reserves[reserve], "ticket")) % [reserves[reserve]["name"], load["name"]], 6.0)


func _unhandled_input(event: InputEvent) -> void:
	if chooser_open() and event is InputEventKey and event.pressed and event.keycode == KEY_ESCAPE:
		close_tickets()
		get_viewport().set_input_as_handled()


# --- the hunt: on while you're inside the farm with its ticket ---

func _hunt_step(delta: float, player: Node3D) -> void:
	var day := Time.get_date_string_from_system()
	if day != _taken_day:
		_taken_day = day
		taken = {}
	var here: PackedFloat64Array = world.to_lat_lon(player.global_position)
	var inside := reserve_at(here[0], here[1])
	# the animals: out while you're near their farm (its fence drawn), gone once you're far off
	# (areas can overlap, a town's game and one made in it: the one you're playing first, then the one you've a
	# ticket for, then any other; an area whose creatures are out only while it's played isn't one till then)
	var near := -1
	for i in reserves.size():
		if not _built.has(i):
			continue
		var centre := _centre(reserves[i])
		if _metres(here[0], here[1], centre.x, centre.y) >= _reach(reserves[i]):
			continue
		if reserves[i].get("creatures_out", "always") == "playing" and hunting != i and not (ticket == i and _in(i, here)):
			continue
		if near < 0 or i == hunting or (i == ticket and near != hunting):
			near = i
	if living >= 0 and near != living:
		_end_hunt(true)
		hunter.stop()
		living = -1
	if living < 0 and near >= 0 and hunter:
		_let_out(near)
	# the hunt: inside the farm with its ticket
	if hunting < 0 and ticket >= 0 and _in(ticket, here) and living == ticket:
		_start_hunt(ticket)
	elif hunting >= 0 and not _in(hunting, here):
		_end_hunt(true)
	if main.vegetation and (inside >= 0) != _was_inside:
		_was_inside = inside >= 0
		main.vegetation.set_wide(_was_inside)	# (on going in or out only)
	if living >= 0 and hunter:
		# (someone else's game: the host's animals shown while they come; our own if they stop, the host away)
		hunter.set_follower(main.net != null and main.net.is_client() and Time.get_ticks_msec() / 1000.0 - _last_shared < 2.0)
		# you, while you're playing: on foot (caught if they reach you), or in the car (come for, not caught)
		var people := PackedVector3Array()
		var on_foot := []
		if hunting >= 0 and main.walker and not main.walker.down_wanted:
			people.append(main.walker.global_position)
			on_foot.append(_grace_left <= 0.0)
		elif hunting >= 0 and main.car and not main.walker and not (main.aviation and main.aviation.flying):
			people.append(main.car.global_position)
			on_foot.append(false)
		hunter.set_people(people, on_foot)
		# your car among them, driven or parked: pushing them aside, hitting them, spooking them as it comes
		hunter.set_cars([_car_now()] if main.car and not main.car.freeze else [])
		hunter.step(delta)	# (~0.01 ms a frame for a farm's 210)
		for hit in hunter.take_hits():
			# (the car slowed by the creature's share of their mass, the way it hit it)
			main.car.linear_velocity -= (hit["way"] as Vector3) * float(hit["car_loses"])
			car_hits.append(hit)
			if rules and hunting >= 0 and hit["killed"]:
				rules.event("kill", hit["species"])
		if rules and hunting >= 0:
			rules.step(delta)
			_race_step(player)
			if rules:
				_rules_said()
			_rules_line()
		if not hunter.take_caught().is_empty() and main.walker:
			if rules:
				rules.event("caught")
			main.walker.down_wanted = true
			_down_left = DOWN_FOR
		var camera: Camera3D = main.camera
		# (through a scope they look nearer: drawn finer farther out)
		var zoom: float = tan(deg_to_rad(main.camera.cockpit_fov) * 0.5) / tan(deg_to_rad(main.camera.zoom_fov) * 0.5) if main.camera.zoom_fov > 0.0 else 1.0
		hunter.draw(camera.global_position, DRAWN_TO, zoom)	# (~0.2 ms)
		for line in hunter.take_news():
			main.hud.set_status(line, 3.0)
	_grace_left = maxf(_grace_left - delta, 0.0)
	if _down_left > 0.0:
		_down_left -= delta
		if _down_left <= 0.0 and main.walker and main.walker.down_wanted:
			_get_up()
	# (in an area, without its ticket: where its booth is, now and then)
	_booth_hint -= delta
	if inside >= 0 and ticket != inside and _booth_hint <= 0.0 and kiosk_near(player.global_position).is_empty() and not (main.golf and main.golf.active):
		_booth_hint = 20.0
		for booth in booths():
			if booth["area"] == inside:
				main.hud.set_status("%s: no ticket yet. F1: play here, or the booth: %s  (M: map)" % [booth["name"], way_to(here[0], here[1], booth["lat"], booth["lon"])], 6.0)
				break
	elif inside < 0:
		_booth_hint = 0.0	# (said again as soon as you're in one)
	# (on foot by the kiosk: what E does there)
	_hint -= delta
	if _hint <= 0.0 and main.walker and not chooser_open():
		_hint = 1.0
		if not kiosk_near(main.walker.global_position).is_empty():
			var kiosk := kiosk_near(main.walker.global_position)
			main.hud.set_status(String(_text(reserves[kiosk["reserve"]], "hint")) + ("  (you have one)" if ticket >= 0 else ""), 1.2)


# A game made (the workshop's): there at once; one with its id (a game changed) replaced, put up again
func add_reserve(reserve: Dictionary) -> void:
	var index := -1
	for i in reserves.size():
		if reserve.get("id", "") != "" and reserves[i].get("id", "") == reserve["id"]:
			index = i
	if index < 0:
		reserves.append(reserve)
		return
	# (the one it replaces: its game stopped, its creatures and booth gone, put up again as it's now)
	if hunting == index:
		_end_hunt(false)
	if living == index and hunter:
		hunter.stop()
		living = -1
	for kiosk in _kiosks.filter(func(k): return k["reserve"] == index):
		kiosk["node"].queue_free()
	_kiosks = _kiosks.filter(func(k): return k["reserve"] != index)
	_built.erase(index)
	reserves[index] = reserve


# The host's game (net.gd): checked, then there as one made here would be (marked shared: gone again when you leave);
# false if it isn't one
func share_reserve(reserve: Dictionary) -> bool:
	if not (reserve.get("name") is String and reserve.get("id") is String and _outline_ok(reserve.get("outline"))):
		return false
	var gates = reserve.get("gates", [])
	if not (gates is Array and gates.all(func(g): return g is Dictionary and g.get("lat") is float and g.get("lon") is float)):
		return false
	reserve["gates"] = gates
	var checkpoints = reserve.get("checkpoints", [])
	if not (checkpoints is Array and checkpoints.all(func(c): return c is Dictionary and c.get("lat") is float and c.get("lon") is float)):
		return false
	reserve["shared"] = true
	reserve["made"] = false	# (the host's to change, not yours)
	reserve["id"] = "shared_" + String(reserve["id"])	# (not one of yours with the same name)
	add_reserve(reserve)
	return true


static func _outline_ok(outline) -> bool:
	return outline is Array and outline.size() >= 3 and outline.all(func(c): return c is Array and c.size() >= 2 and c[0] is float and c[1] is float)


# Out of the host's game: its games gone (any played, over), yours as they were
func forget_shared() -> void:
	if not reserves.any(func(r): return r.get("shared", false)):
		return
	var keep_ticket: String = reserves[ticket].get("id", "") if ticket >= 0 and not reserves[ticket].get("shared", false) else ""
	reset_area()	# (the games stopped, their booths and fences gone: put up again as they're found)
	reserves = reserves.filter(func(r): return not r.get("shared", false))
	ticket = -1
	for i in reserves.size():
		if keep_ticket != "" and reserves[i].get("id", "") == keep_ticket:
			ticket = i


# The ticket booths of the games round you (in the part of the world that's in: the rest are found by going there), for
# the maps: [{name, lat, lon, area (its index), state: "tickets" / "ticket" / "playing"}]
func booths() -> Array:
	var out := []
	for i in reserves.size():
		for gate in reserves[i]["gates"]:
			if gate.get("tickets", false) and world and world.is_ready() and world.contains_lat_lon(gate["lat"], gate["lon"]):
				out.append({"name": reserves[i]["name"], "lat": gate["lat"], "lon": gate["lon"], "area": i,
					"state": "playing" if hunting == i else ("ticket" if ticket == i else "tickets")})
	return out


# Which way and how far from a place to another, as a person says it: "1.2 km north-east"
static func way_to(from_lat: float, from_lon: float, lat: float, lon: float) -> String:
	var east := (lon - from_lon) * 111320.0 * cos(deg_to_rad(from_lat))
	var north := (lat - from_lat) * 110574.0
	var metres := Vector2(east, north).length()
	var names := ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"]
	var heading := fposmod(rad_to_deg(atan2(east, north)), 360.0)
	var distance := ("%d m" % roundi(metres)) if metres < 1000.0 else ("%.1f km" % (metres / 1000.0))
	return "%s %s" % [distance, names[int(round(heading / 45.0)) % 8]]


# The car as the animals meet it: where, which way, how fast, its box and mass
func _car_now() -> Dictionary:
	var car: RigidBody3D = main.car
	if _car_for != car.get_instance_id():
		_car_for = car.get_instance_id()
		_car_box = Vector3(2.0, 1.5, 4.8)	# (a bakkie's, if it has no box of its own)
		for child in car.get_children():
			if child is CollisionShape3D and child.shape is BoxShape3D:
				_car_box = child.shape.size
	return {"position": car.global_position, "forward": -car.global_transform.basis.z, "velocity": car.linear_velocity,
		"half_length": _car_box.z * 0.5, "half_width": _car_box.x * 0.5, "mass": car.mass}


# F1: the game you're playing stopped (its ticket handed back); or, in a game's area without one, its tickets offered
# here (no need to walk back to the booth)
func toggle_game() -> void:
	if hunting >= 0:
		var name: String = reserves[hunting]["name"]
		_end_hunt(true)
		main.hud.set_status("Stopped playing %s (F1: play again)" % name, 4.0)
		return
	var player := _player()
	if player == null or world == null:
		return
	var here: PackedFloat64Array = world.to_lat_lon(player.global_position)
	# (the games you're in: the one whose booth's nearest)
	var best := -1
	var nearest := INF
	for booth in booths():
		if _in(booth["area"], here):
			var d := _metres(here[0], here[1], booth["lat"], booth["lon"])
			if d < nearest:
				nearest = d
				best = booth["area"]
	if best >= 0:
		open_tickets({"reserve": best})
	else:
		main.hud.set_status("No game here: the maps (M) show the games round you", 3.0)


# Caught, down a while: up again by the car (as after being shot in a game with others)
func _get_up() -> void:
	_down_left = 0.0
	var walker: Node3D = main.walker
	walker.down_wanted = false
	if main.car:
		walker.global_position = main.car.global_position + main.car.global_transform.basis.x * -2.2 + Vector3.UP * 0.5
		walker.velocity = Vector3.ZERO
	# (not set on at once: those round the car put back farther off, and a few seconds' grace)
	if hunter and living >= 0:
		hunter.put_back(walker.global_position, 60.0)
	_grace_left = GRACE
	main.hud.set_status("Up again, by your car (%d s before they can catch you)" % roundi(GRACE), 3.0)


# The farm's animals let out (you've come near it)
func _let_out(index: int) -> void:
	var reserve: Dictionary = reserves[index]
	var outline := PackedVector3Array()
	for p in reserve["outline"]:
		outline.append(world.from_lat_lon(p[0], p[1]))
	var water := PackedVector3Array()
	for w in reserve.get("water", []):
		water.append(world.from_lat_lon(w["lat"], w["lon"]))
	# (the same seed everywhere: in a multiplayer game every player's herds are the same herds, the host's shown)
	hunter.start(world, outline, water, reserve.get("stock", []), absi(String(reserve["name"]).hash()), reserve.get("avoid_buildings", false), reserve.get("spawn", {}))
	living = index
	_last_shared = -100.0
	print("Geodrive: %s's animals out: %d" % [reserve["name"], hunter.get_count()])


func _start_hunt(index: int) -> void:
	var reserve: Dictionary = reserves[index]
	hunting = index
	_shots = []
	_start_rules(reserve)
	var start: String = _text(reserve, "start")
	main.hud.set_status(start % ([reserve["name"], hunter.get_count()] if start.count("%") >= 2 else [hunter.get_count()]), 5.0)
	print("Geodrive: the hunt on %s: %d animals" % [reserve["name"], hunter.get_count()])


# A game's rules (its area's "rules"), started with it
func _start_rules(reserve: Dictionary) -> void:
	rules = null
	_clear_course()
	main.hud.set_game_line("")
	var checkpoints: Array = reserve.get("checkpoints", [])
	if (reserve.get("rules", []).is_empty() and checkpoints.size() < 2) or not ClassDB.class_exists("GeodriveGame"):
		return
	rules = GeodriveGame.new()
	var wrong: String = rules.load(reserve["rules"])
	if wrong != "":
		push_error("Geodrive: %s's rules: %s" % [reserve["name"], wrong])
		rules = null
		return
	if checkpoints.size() >= 2:
		rules.set_course(checkpoints.size(), int(reserve.get("laps", 0)))
		_make_course(checkpoints, int(reserve.get("laps", 0)) > 0)
	_finished_told = false
	rules.start()
	_rules_said()


# A race's checkpoints in the world: a tall see-through column each (the next one gold and bright; the start green),
# its number over it
func _make_course(checkpoints: Array, circuit: bool) -> void:
	for i in checkpoints.size():
		var c: Dictionary = checkpoints[i]
		var at: Vector3 = world.from_lat_lon(c["lat"], c["lon"])
		at.y = world.ground_height(at)
		var radius: float = c.get("radius", 8.0)
		var node := Node3D.new()
		var column := MeshInstance3D.new()
		var shape := CylinderMesh.new()
		shape.top_radius = radius
		shape.bottom_radius = radius
		shape.height = 10.0
		shape.cap_top = false
		shape.cap_bottom = false
		column.mesh = shape
		column.position = Vector3.UP * 5.0
		var paint := StandardMaterial3D.new()
		paint.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		paint.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		paint.cull_mode = BaseMaterial3D.CULL_DISABLED
		column.material_override = paint
		column.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		node.add_child(column)
		var label := Label3D.new()
		label.text = "START" if i == 0 else ("FINISH" if i == checkpoints.size() - 1 and not circuit else str(i + 1))
		label.font_size = 160
		label.pixel_size = 0.02
		label.outline_size = 24
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.position = Vector3.UP * 11.5
		node.add_child(label)
		_root.add_child(node)
		node.global_position = at
		_course.append({"node": node, "paint": paint, "at": at, "radius": radius})
	_paint_course()


func _paint_course() -> void:
	var next: int = rules.get_next_checkpoint() if rules else -1
	for i in _course.size():
		var paint: StandardMaterial3D = _course[i]["paint"]
		paint.albedo_color = Color(1.0, 0.8, 0.2, 0.35) if i == next else (Color(0.3, 1.0, 0.4, 0.12) if i == 0 else Color(1.0, 1.0, 1.0, 0.06))


func _clear_course() -> void:
	for c in _course:
		if is_instance_valid(c["node"]):
			c["node"].queue_free()
	_course = []


# Each frame racing: the next checkpoint reached (within its radius, across the ground) told; the finish's time kept
func _race_step(player: Node3D) -> void:
	if rules == null or _course.is_empty():
		return
	var next: int = rules.get_next_checkpoint()
	var c: Dictionary = _course[next]
	var to: Vector3 = player.global_position - (c["node"] as Node3D).global_position
	if Vector2(to.x, to.z).length() > float(c["radius"]):
		return
	var game_id: String = String(reserves[hunting].get("id", reserves[hunting]["name"]))
	rules.event("reach", str(next))
	var finished: float = rules.get_finish_time()
	_paint_course()
	if finished >= 0.0 and not _finished_told:
		_finished_told = true
		var best := _best_time(game_id, finished)
		var name: String = reserves[hunting]["name"]
		_rules_said()	# (the race's own rules first: its time over theirs)
		main.hud.show_banner("Finished: %s%s" % [race_clock(finished), ("   (your best %s)" % race_clock(best)) if best < finished else "   (your best)"], true, 8.0)
		print("Geodrive: %s finished in %s (best %s)" % [name, race_clock(finished), race_clock(best)])
		if hunting >= 0:
			_end_hunt(true)	# (finished, no rule of its own to end it: it's over; F1 races it again)
	else:
		_rules_said()


static func race_clock(seconds: float) -> String:
	var minutes := int(seconds / 60.0)
	return "%d:%04.1f" % [minutes, seconds - minutes * 60.0]


# A race's best time (its id's), this one kept if it's better: the best
func _best_time(id: String, time: float) -> float:
	var bests := {}
	if FileAccess.file_exists(BEST_PATH):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(BEST_PATH))
		if parsed is Dictionary:
			bests = parsed
	var best: float = float(bests.get(id, INF))
	if time < best and not main.testing:
		bests[id] = time
		var file := FileAccess.open(BEST_PATH, FileAccess.WRITE)
		if file:
			file.store_string(JSON.stringify(bests))
	return minf(best, time)


# What the rules say: on the status line; a win or a loss big, and the game over
func _rules_said() -> void:
	if rules == null:
		return
	for said in rules.take_said():
		if said["kind"] == "say":
			main.hud.set_status(said["text"], 4.0)
		else:
			main.hud.show_banner(said["text"], said["kind"] == "win")
			print("Geodrive: the game's %s: %s" % ["won" if said["kind"] == "win" else "lost", said["text"]])
	if rules.is_over():
		_end_hunt(true)


# The game's line: its score (of what wins), its time left, how often caught
func _rules_line() -> void:
	if rules == null:
		return
	var parts := []
	if not _course.is_empty() and rules.get_finish_time() < 0.0:
		var next: int = rules.get_next_checkpoint()
		var player := _player()
		var away := Vector2(player.global_position.x - _course[next]["at"].x, player.global_position.z - _course[next]["at"].z).length() if player else 0.0
		parts.append(("To the start, %d m" % roundi(away)) if rules.get_race_time() < 0.0 else ("Checkpoint %d / %d  %d m" % [next + 1, _course.size(), roundi(away)]))
		if rules.get_race_time() >= 0.0:
			parts.append(race_clock(rules.get_race_time()))
		if rules.get_laps() > 0:
			parts.append("Lap %d / %d" % [maxi(rules.get_lap(), 1), rules.get_laps()])
	var target: float = rules.get_target()
	if target >= 0.0 or rules.get_score() != 0.0 or _course.is_empty():
		parts.append("Score %d%s" % [roundi(rules.get_score()), (" / %d" % roundi(target)) if target >= 0.0 else ""])
	if rules.get_time_left() >= 0.0:
		var left := ceili(rules.get_time_left())
		parts.append("%d:%02d left" % [left / 60, left % 60])
	if rules.get_caught() > 0:
		parts.append("caught %d" % rules.get_caught())
	main.hud.set_game_line("   ".join(parts))


# Out of the farm: the ticket and its weapon handed back, and what was taken (the animals stay out while you're near)
func _end_hunt(tell: bool) -> void:
	if hunting < 0:
		return
	var reserve: Dictionary = reserves[hunting]
	var result: String = rules.get_result() if rules else ""
	rules = null
	_clear_course()
	main.hud.set_game_line("")
	var killed := {}
	for shot in _shots:
		if shot.get("hit", false) and not shot.get("already_dead", false) and (shot.get("down", false) or shot.get("dies", false)):
			killed[shot["name"]] = killed.get(shot["name"], 0) + 1
	hunting = -1
	ticket = -1
	if weapon:
		weapon.queue_free()
		weapon = null
	var illegal := _shots.filter(func(s): return s.get("hit", false) and not s.get("legal", true))
	var bag := ", ".join(killed.keys().map(func(k): return "%d %s" % [killed[k], k])) if not killed.is_empty() else "nothing"
	var line := "%s%s: %s, %s; %d shots%s" % [("You've %s: " % result) if result != "" else "Out of ", reserve["name"], _text(reserve, "bag"), bag, _shots.size(), (", %d of them illegal" % illegal.size()) if not illegal.is_empty() else ""]
	print("Geodrive: " + line)
	if tell:
		main.hud.set_status(line, 8.0)


# The host's animals (net.gd): shown here, if they're this farm's
func shared(index: int, bytes: PackedByteArray) -> void:
	if index == living and hunter and hunter.apply(bytes):
		_last_shared = Time.get_ticks_msec() / 1000.0


# Someone else's shot heard (net.gd: Delay s for the sound to come, Loud dB): the animals here hear it too
func heard_shot(at: Vector3, kind: String, delay: float, loud: float) -> void:
	WEAPON.play_sound(self, main, kind, delay, loud)
	if hunter and hunter.is_running() and kind != "bow":
		hunter.heard(at)


# A shot from the weapon: flown through the farm (the animals, the ground); the bag and the law kept
func shoot(load: String, eye: Vector3, aim: Vector3) -> Dictionary:
	if hunting < 0 or hunter == null:
		return {}
	var skip: Array = []
	if main.walker:
		skip.append(main.walker.get_rid())
	if main.car:
		skip.append(main.car.get_rid())
	var month := int(Time.get_date_dict_from_system()["month"])
	var result: Dictionary = hunter.shoot(load, eye, aim, skip, month, taken)
	# (an area with no hunting law, a game's: nothing's illegal there)
	if not _text(reserves[hunting], "law") and result.get("hit", false):
		result["legal"] = true
	# (an animal hit in someone else's game: the host told, its animals are the real ones)
	if result.get("hit", false) and main.net and main.net.is_client():
		main.net.animal_shot(hunting, int(result["index"]), eye, result.get("down", false), float(result.get("dies_after", -1.0)), result.get("wounded", false))
	if result.get("hit", false) and not result.get("already_dead", false) and (result.get("down", false) or result.get("dies", false)):
		taken[result["species"]] = int(taken.get(result["species"], 0)) + 1
	_shots.append(result)
	# (the rules told after it's counted: a winning shot's in the bag)
	if rules and result.get("hit", false) and not result.get("already_dead", false) and (result.get("down", false) or result.get("dies", false)):
		rules.event("kill", result["species"])
		_rules_said()
	return result
