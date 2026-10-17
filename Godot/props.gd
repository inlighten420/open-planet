# Props: things made in the workshop (or a pack's: its props/*.json, a look and a name), put on the real map in build
# mode. The prop chosen (build menu, Props) follows where you look, see-through (green where it can go, red where it
# can't); R turns it (15 degrees), a click puts it there, sitting on what's under it. Each solid part is solid (a box
# its size: a cut makes no hole in them). Kept on the map (user://props.json: which, where, which way) and put up
# whenever its area's built; right click one twice to take it away.
class_name PropKeeper
extends Node3D

const SAVE_PATH := "user://props.json"
const TEST_PATH := "user://props_test.json"
const REACH := 14.0	# m: how far you can put one
const TURN_STEP := 15.0	# degrees, R

var builder: Node
var main: Node
var world: Node
var kinds := {}	# id -> {id, name, look}: every prop there is (the packs', the workshop's)
var placed: Array = []	# [{id, lat, lon, y, yaw}] (a taken one: {} kept, the others' numbers stay)
var chosen := ""	# the prop being put (its id; "": none)
var turn := 0.0	# degrees, on from the way you face
var _shown := {}	# placed index -> its node (those in the built area)
var _ghost: Node3D
var _ghost_for := ""
var _aim := {}	# {at, yaw, ok}
var _picked := -1	# (a placed one outlined to take away: its index, till when)
var _picked_until := 0.0
var _check := 0.0
var _ok_paint: StandardMaterial3D
var _no_paint: StandardMaterial3D
var sharing := false	# (joined someone's game: placed is the host's, not kept here; yours put back after)
var _own: Array = []


func _ready() -> void:
	_ok_paint = _paint(Color(0.3, 1.0, 0.4, 0.4))
	_no_paint = _paint(Color(1.0, 0.3, 0.3, 0.4))
	load_kinds()
	_load()


static func _paint(colour: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	return m


# Every prop there is: the packs' props/*.json (the workshop's own among them)
func load_kinds() -> void:
	kinds = {}
	for path in Packs.files("props"):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
		for entry in (parsed if parsed is Array else [parsed]):
			if entry is Dictionary and entry.has("id") and entry.get("look") is Dictionary:
				kinds[String(entry["id"])] = entry


# One made or changed (the workshop's): it's there at once, and those of it on the map shown again
func add_kind(prop: Dictionary) -> void:
	kinds[String(prop["id"])] = prop
	for index in _shown.keys():
		if placed[index].get("id") == prop["id"]:
			_shown[index].queue_free()
			_shown.erase(index)
	_ghost_for = ""


# --- the look and the solid ---

# A prop: its look, its solid parts each a box (no cuts in them), its sitting on its lowest point
func make(prop: Dictionary, solid: bool = true) -> Node3D:
	var node := Node3D.new()
	node.name = "Prop_" + String(prop["id"])
	var look: Dictionary = prop["look"]
	node.add_child(PartModel.build(look, true))
	if solid:
		var body := StaticBody3D.new()
		body.name = "Solid"
		for part in look.get("parts", []):
			if part.get("cut", false):
				continue
			var turn_by := PartModel._vec(part.get("turn", [0, 0, 0]), Vector3.ZERO) * (PI / 180.0)
			var box := BoxShape3D.new()
			box.size = PartModel._vec(part.get("size", [0.1, 0.1, 0.1]), Vector3.ONE * 0.1).abs().max(Vector3.ONE * 0.01)
			var shape := CollisionShape3D.new()
			shape.shape = box
			shape.transform = Transform3D(Basis.from_euler(turn_by), PartModel._vec(part.get("at", [0, 0, 0]), Vector3.ZERO))
			body.add_child(shape)
		node.add_child(body)
	return node


# How far its lowest point is under its middle's 0 (to sit it on what's under it)
static func bottom_of(look: Dictionary) -> float:
	var low := INF
	for part in look.get("parts", []):
		if part.get("cut", false):
			continue
		var basis := ShapeEditor._basis(part)
		var at := PartModel._vec(part.get("at", [0, 0, 0]), Vector3.ZERO)
		for x in [-0.5, 0.5]:
			for y in [-0.5, 0.5]:
				for z in [-0.5, 0.5]:
					low = minf(low, (at + basis * Vector3(x, y, z)).y)
	return low if low < INF else 0.0


# --- putting one ---

# Each frame in build mode with a prop chosen: where it'd go (on what you look at, within reach), shown see-through
func aim() -> void:
	if not kinds.has(chosen):
		_hide_ghost()
		return
	var camera: Camera3D = main.camera
	var from := camera.global_position
	var query := PhysicsRayQueryParameters3D.create(from, from - camera.global_transform.basis.z * REACH)
	query.exclude = [main.walker.get_rid()]
	var hit: Dictionary = main.get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		_aim = {}
		_hide_ghost()
		main.hud.set_status("Prop: %s - look at the ground, a floor or a wall within %d m" % [kinds[chosen]["name"], roundi(REACH)], 0.6)
		return
	var facing := -camera.global_transform.basis.z
	var yaw := atan2(-facing.x, -facing.z) + deg_to_rad(turn)
	var look: Dictionary = kinds[chosen]["look"]
	var at: Vector3 = hit["position"] - Vector3.UP * bottom_of(look)
	# (it can go where what's under it is near level: not up a wall)
	var ok: bool = (hit["normal"] as Vector3).y > 0.5
	_aim = {"at": at, "yaw": yaw, "ok": ok}
	if _ghost_for != chosen:
		_hide_ghost()
		_ghost = make(kinds[chosen], false)
		_ghost_for = chosen
		add_child(_ghost)
	_ghost.global_transform = Transform3D(Basis(Vector3.UP, yaw), at)
	var paint := _ok_paint if ok else _no_paint
	for node in _ghost.find_children("*", "GeometryInstance3D", true, false):
		(node as GeometryInstance3D).material_override = paint
	_ghost.visible = true
	main.hud.set_status("Prop: %s%s   (click: put   R: turn   right click twice: take one away   Q: menu   B: done)" % [kinds[chosen]["name"],
		"" if ok else " - it needs something level under it"], 0.6)


func _hide_ghost() -> void:
	if _ghost:
		_ghost.queue_free()
		_ghost = null
		_ghost_for = ""


func stop() -> void:
	chosen = ""
	_aim = {}
	_hide_ghost()


# A click: the chosen prop put where it's shown (kept on the map)
func put() -> bool:
	if _aim.is_empty() or not _aim["ok"]:
		return false
	var lat_lon: PackedFloat64Array = world.to_lat_lon(_aim["at"])
	var entry := {"id": chosen, "lat": lat_lon[0], "lon": lat_lon[1], "y": (_aim["at"] as Vector3).y, "yaw": _aim["yaw"]}
	if sharing and main.net.is_client():
		main.net.prop_put(shared_entry(entry), kinds[chosen])	# (the host puts it: it comes back to everyone)
		return true
	placed.append(entry)
	_show(placed.size() - 1)
	_save()
	if main.net.is_host():
		main.net.share_props()
	return true


# A right click: on a placed prop, it's outlined; again within 4 s, it's taken away. False if it wasn't one
func take(hit: Dictionary) -> bool:
	var collider: Object = hit.get("collider")
	if not (collider is Node and (collider as Node).get_parent() and (collider as Node).get_parent().has_meta("placed")):
		return false
	var index: int = (collider as Node).get_parent().get_meta("placed")
	var now := Time.get_ticks_msec() / 1000.0
	if index == _picked and now < _picked_until:
		if sharing and main.net.is_client():
			main.net.prop_take(index)	# (the host takes it away: it goes from everyone's)
		else:
			take_index(index)
			if main.net.is_host():
				main.net.share_props()
		_picked = -1
		main.hud.set_status("The prop's taken away", 2.0)
		return true
	_picked = index
	_picked_until = now + 4.0
	main.hud.set_status("A prop: right click again to take it away", 4.0)
	return true


# One taken away (by its number); false if there's none
func take_index(index: int) -> bool:
	if index < 0 or index >= placed.size() or placed[index].is_empty():
		return false
	if _shown.has(index):
		_shown[index].queue_free()
		_shown.erase(index)
	placed[index] = {}
	_save()
	return true


# --- shared (net.gd): the host's map ---

# One placed, as it's sent: its height above the ground there, not Godot's (each player's ground is their own)
func shared_entry(entry: Dictionary) -> Dictionary:
	var out := entry.duplicate()
	if world and world.contains_lat_lon(entry["lat"], entry["lon"]):
		var at: Vector3 = world.from_lat_lon(entry["lat"], entry["lon"])
		out["above"] = float(entry["y"]) - world.ground_height(at)
	return out


# The host: a joiner's put here (its kind too, if it's new here); false if it isn't one
func put_shared(entry: Dictionary, kind: Dictionary) -> bool:
	var id = entry.get("id")
	if not (id is String) or not (entry.get("lat") is float and entry.get("lon") is float and entry.get("yaw") is float):
		return false
	if not kinds.has(id):
		if kind.get("id") != id or not (kind.get("look") is Dictionary):
			return false
		add_kind(kind)
	var mine := {"id": id, "lat": entry["lat"], "lon": entry["lon"], "yaw": entry["yaw"], "y": float(entry.get("y", 0.0))}
	if entry.get("above") is float and world and world.contains_lat_lon(entry["lat"], entry["lon"]):
		mine["y"] = world.ground_height(world.from_lat_lon(entry["lat"], entry["lon"])) + entry["above"]
	placed.append(mine)
	if world and world.contains_lat_lon(mine["lat"], mine["lon"]):
		_show(placed.size() - 1)
	_save()
	return true


# A joiner: the host's props are what's on the map (yours kept aside, not changed)
func use_shared(list: Array) -> void:
	if not sharing:
		_own = placed
		sharing = true
	placed = list.map(func(e): return e if e is Dictionary and _valid(e) else {})
	reset_area()
	_check = 0.0


# Out of the host's game: yours back
func stop_sharing() -> void:
	if not sharing:
		return
	sharing = false
	placed = _own
	_own = []
	load_kinds()
	reset_area()
	_check = 0.0


static func _valid(entry: Dictionary) -> bool:
	return entry.get("id") is String and entry.get("lat") is float and entry.get("lon") is float and entry.get("yaw") is float \
		and (entry.get("y") is float or entry.get("above") is float)


# --- kept on the map ---

func _show(index: int) -> void:
	var entry: Dictionary = placed[index]
	if not kinds.has(entry["id"]):
		return	# (its kind's gone: its pack taken out)
	var node := make(kinds[entry["id"]])
	node.set_meta("placed", index)
	add_child(node)
	var at: Vector3 = world.from_lat_lon(entry["lat"], entry["lon"])
	# (the host's: on this player's own ground, as high above it)
	var y: float = world.ground_height(at) + float(entry["above"]) if sharing and entry.has("above") else float(entry.get("y", 0.0))
	node.global_transform = Transform3D(Basis(Vector3.UP, float(entry["yaw"])), Vector3(at.x, y, at.z))
	_shown[index] = node


# Each second: the placed props in the built area put up
func physics(delta: float) -> void:
	_check -= delta
	if _check > 0.0 or world == null or not world.is_ready():
		return
	_check = 1.0
	for index in placed.size():
		var entry: Dictionary = placed[index]
		if entry.is_empty() or _shown.has(index) or not world.contains_lat_lon(entry["lat"], entry["lon"]):
			continue
		_show(index)


func _path() -> String:
	return (TEST_PATH.get_basename() + Packs.test_name.trim_prefix("workshop_test") + ".json") if main.testing else SAVE_PATH


func _save() -> void:
	if sharing:
		return	# (the host's: kept by the host)
	var file := FileAccess.open(_path(), FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({"props": placed.filter(func(e): return not e.is_empty())}))


func _load() -> void:
	if main.testing:
		DirAccess.remove_absolute(TEST_PATH)	# (each test from nothing)
	if FileAccess.file_exists(_path()):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(_path()))
		if parsed is Dictionary:
			placed = parsed.get("props", [])
	if not placed.is_empty():
		print("Geodrive: %d props on the map" % placed.size())


# A new area: what was shown goes (it comes back if this area has it)
func reset_area() -> void:
	for node in _shown.values():
		node.queue_free()
	_shown = {}
	stop()


func shift(offset: Vector3) -> void:
	for node in _shown.values():
		node.global_position += offset
