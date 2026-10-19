# The road editor ("fix the world"): in the free camera, the roads round you as handles you can grab and move, sideways
# and up or down, add to and take from, with the ground, buildings, plants and water hidden or shown one at a time to see
# how they meet. What you changed is copied (and saved beside your pictures) as each road's new line with heights, to send
# back: a hand-drawn guideline of where the road should go, which the game's rules are then fixed to follow everywhere.
#
#   F2 (Settings > Controls): on / off        H: the editor's keys shown / hidden
#   hold the right mouse button: look round; WASD, E / Q: move, Shift faster
#   left-click a point and drag: move it (a road's end moves its junction, and every road there, with it)
#   double-click a road between its points: a new point there        Delete: the picked point out (not a road's end)
#   mouse wheel or Page Up / Down with a point picked: up / down 25 cm (Shift: 1 m)        K: smooth the picked road
#   [ / ]: how far a move reaches along the road (soft: the points round it follow, less and less, so it stays smooth;
#   0: the point alone). A road's ends stay where they are unless you move the end itself, and then the roads joining
#   there bend with it the same way
#   - / =: the picked road narrower / wider (50 cm; Shift: 2 m)        Ctrl + wheel or Ctrl + PgUp / PgDn: the whole picked
#   road up / down (its junctions stay: the road between them rises or sinks, fading to them)
#   L: the picked road made to flow: what you drew taken as the idea, and the road laid through it as a real one would be
#   (straight runs, even bends, an even slope, carrying straight on from the roads it joins at each end); L again: smoother
#   G: the roads coloured by height (blue low to red high, as the road diagram) or by how steep they are (green flat,
#   yellow 6%, orange 10%, red 15% or more)
#   1 ground  2 buildings and tunnels  3 trees  4 water: hide / show
#   Ctrl+Z undo   Ctrl+Y (or Ctrl+Shift+Z) redo   Ctrl+C copy the edits
extends Node3D

const REACH := 50.0	# m round the camera whose roads are fetched
const HANDLE_REACH := 60.0	# m from the camera within which their points show as handles
const REFRESH := 20.0	# m the camera moves before the roads round it are fetched again
const PICK_PIXELS := 14.0
const STEP := 0.25	# m up or down per wheel step (Shift: x4)
const SOFT_STEP := 5.0	# m the reach of a move changes by, per [ or ]
const WIDTH_STEP := 0.5	# m of road width per - or = (Shift: x4)
const MIN_WIDTH := 2.5
const SOFT_MAX := 60.0
const LIFT := Vector3(0.0, 0.3, 0.0)	# (lines and handles drawn this far over the road, so they show on it)
# the world's render layers (main.gd's PROFILE_PARTS): what each number key hides
const LAYERS := {KEY_1: ["ground", (1 << 10) | (1 << 14) | (1 << 15)], KEY_2: ["buildings and tunnels", 1 << 13],
	KEY_3: ["trees", (1 << 17) | (1 << 18)], KEY_4: ["water", 1 << 16]}

var world: Node	# (main.gd's GeodriveWorld)
var made: Callable	# (main.gd's note_made: a copied edit is what F6 sends)
var camera: Camera3D	# (main.gd's car camera, in its free mode)
var active := false
var looking := false	# (the right mouse button held: the mouse turns the view)

var _roads := {}	# id -> {"info": Dictionary from road_points, "points": PackedVector3Array as edited, "orig": as the game made it}
var _at_junction := {}	# junction id -> [[road id, 0 for its start or 1 for its end]]
var _fetched_at := Vector3.INF
var _grab := {}	# {"road": id, "index": i} while a point is held or picked
var _dragging := false
var _undo: Array = []
var _redo: Array = []
var _hidden_before := 0
var _hidden := {}	# key -> hidden
var _help := true
var _by_grade := false	# (G: colours by how steep, not how high)
var _low := 0.0	# m: the heights the colours run between (the roads round the camera)
var _high := 1.0
var _soft := 15.0	# m along the road a move reaches, fading (0: the point alone)
var _base := {}	# the roads as they were when the move began (a drag, a raise): the move is applied to them each time
var _lines: MeshInstance3D
var _ribbons: MeshInstance3D
var _handles: MultiMeshInstance3D
var _panel: Label
var _status := ""


func _ready() -> void:
	var line_mat := StandardMaterial3D.new()
	line_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	line_mat.vertex_color_use_as_albedo = true
	line_mat.no_depth_test = true
	_lines = MeshInstance3D.new()
	_lines.mesh = ImmediateMesh.new()
	_lines.material_override = line_mat
	add_child(_lines)
	var ribbon_mat := StandardMaterial3D.new()
	ribbon_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ribbon_mat.vertex_color_use_as_albedo = true
	ribbon_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	ribbon_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_ribbons = MeshInstance3D.new()
	_ribbons.mesh = ImmediateMesh.new()
	_ribbons.material_override = ribbon_mat
	add_child(_ribbons)
	var box := BoxMesh.new()
	box.size = Vector3.ONE
	var handle_mat := StandardMaterial3D.new()
	handle_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	handle_mat.vertex_color_use_as_albedo = true
	handle_mat.no_depth_test = true
	box.material = handle_mat
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.use_colors = true
	multi.mesh = box
	_handles = MultiMeshInstance3D.new()
	_handles.multimesh = multi
	add_child(_handles)
	var layer := CanvasLayer.new()
	add_child(layer)
	_panel = Label.new()
	_panel.position = Vector2(16, 120)
	_panel.add_theme_color_override("font_shadow_color", Color.BLACK)
	_panel.add_theme_constant_override("shadow_offset_x", 1)
	_panel.add_theme_constant_override("shadow_offset_y", 1)
	layer.add_child(_panel)
	visible = false
	_panel.visible = false


func start() -> void:
	active = true
	visible = true
	_panel.visible = true
	_hidden_before = camera.hidden_layers
	for key in LAYERS:
		_hidden[key] = true	# (the roads alone to begin with: bring each back to see how they meet)
	_apply_layers()
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_fetched_at = Vector3.INF
	_refresh()
	_status = "Editing the roads. Nothing changes in your game: Ctrl+C copies what you drew, to send us."
	_draw()


func stop() -> void:
	active = false
	visible = false
	_panel.visible = false
	looking = false
	_grab = {}
	_dragging = false
	camera.hidden_layers = _hidden_before
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _apply_layers() -> void:
	var mask := _hidden_before
	for key in LAYERS:
		if _hidden.get(key, false):
			mask |= LAYERS[key][1]
	camera.hidden_layers = mask


func _process(_delta: float) -> void:
	if not active or world == null or not world.is_ready():
		return
	if camera.global_position.distance_to(_fetched_at) > REFRESH and not _dragging:
		_refresh()
		_draw()
	_panel.text = _panel_text()


# The roads round the camera, kept as edited where they were already fetched
func _refresh() -> void:
	_fetched_at = camera.global_position
	for road in world.road_points(camera.global_position, REACH):
		var id: int = road["id"]
		if _roads.has(id):
			continue
		var points: PackedVector3Array = road["points"]
		_roads[id] = {"info": road, "points": points.duplicate(), "orig": points.duplicate(), "half": float(road["half"])}
		for end in [[road["from"], 0], [road["to"], 1]]:
			if not _at_junction.has(end[0]):
				_at_junction[end[0]] = []
			_at_junction[end[0]].append([id, end[1]])


func _edited(id: int) -> bool:
	var r: Dictionary = _roads[id]
	var p: PackedVector3Array = r["points"]
	var o: PackedVector3Array = r["orig"]
	if p.size() != o.size() or absf(float(r["half"]) - float(r["info"]["half"])) > 0.01:
		return true
	for i in p.size():
		if p[i].distance_to(o[i]) > 0.05:
			return true
	return false


# The road diagram's colours: blue, cyan, green, yellow, red from low to high
const RAMP := [Color(0.145, 0.388, 0.922), Color(0.024, 0.714, 0.831), Color(0.133, 0.773, 0.369), Color(0.918, 0.702, 0.031), Color(0.937, 0.267, 0.267)]


func _height_colour(y: float) -> Color:
	var t := clampf((y - _low) / maxf(_high - _low, 1.0), 0.0, 1.0) * (RAMP.size() - 1)
	var i := mini(int(t), RAMP.size() - 2)
	return (RAMP[i] as Color).lerp(RAMP[i + 1], t - i)


func _grade_colour(a: Vector3, b: Vector3) -> Color:
	var run := Vector2(b.x - a.x, b.z - a.z).length()
	var grade := absf(b.y - a.y) / maxf(run, 0.5) * 100.0
	if grade < 6.0:
		return Color(0.133, 0.773, 0.369)
	if grade < 10.0:
		return Color(0.918, 0.802, 0.031)
	if grade < 15.0:
		return Color(1.0, 0.55, 0.1)
	return Color(0.937, 0.2, 0.2)


func _draw() -> void:
	# (the colours' range: the roads near the camera, so a hillside's spread shows)
	_low = INF
	_high = -INF
	for id in _roads:
		for q in (_roads[id]["points"] as PackedVector3Array):
			if q.distance_to(camera.global_position) <= HANDLE_REACH * 1.5:
				_low = minf(_low, q.y)
				_high = maxf(_high, q.y)
	if _low > _high:
		_low = 0.0
		_high = 1.0
	var lines: ImmediateMesh = _lines.mesh
	var ribbons: ImmediateMesh = _ribbons.mesh
	lines.clear_surfaces()
	ribbons.clear_surfaces()
	var handles := []
	lines.surface_begin(Mesh.PRIMITIVE_LINES)
	for id in _roads:
		var r: Dictionary = _roads[id]
		var p: PackedVector3Array = r["points"]
		var edited := _edited(id)
		var drawn: PackedVector3Array = _curve(p) if edited else p
		for i in drawn.size() - 1:
			if _by_grade:
				var c := _grade_colour(drawn[i], drawn[i + 1])
				lines.surface_set_color(c)
				lines.surface_add_vertex(drawn[i] + LIFT)
				lines.surface_set_color(c)
				lines.surface_add_vertex(drawn[i + 1] + LIFT)
			else:
				lines.surface_set_color(_height_colour(drawn[i].y))
				lines.surface_add_vertex(drawn[i] + LIFT)
				lines.surface_set_color(_height_colour(drawn[i + 1].y))
				lines.surface_add_vertex(drawn[i + 1] + LIFT)
		if edited:
			var o: PackedVector3Array = r["orig"]
			for i in o.size() - 1:	# (the game's own line, faint, to compare)
				lines.surface_set_color(Color(1, 1, 1, 0.35))
				lines.surface_add_vertex(o[i] + LIFT)
				lines.surface_add_vertex(o[i + 1] + LIFT)
		for i in p.size():
			if p[i].distance_to(camera.global_position) > HANDLE_REACH:
				continue
			var picked: bool = not _grab.is_empty() and _grab["road"] == id and _grab["index"] == i
			var end := i == 0 or i == p.size() - 1
			# (a junction white, a point the road's height colour; the picked one magenta, an edited road's points ringed orange)
			var colour := Color.WHITE if end else (_height_colour(p[i].y) if not _by_grade else Color(0.9, 0.9, 0.9))
			if edited and not end:
				colour = colour.lerp(Color(1.0, 0.55, 0.1), 0.5)
			handles.append([p[i], 0.9 if end else 0.5, Color(1.0, 0.0, 1.0) if picked else colour])
	lines.surface_end()
	# the edited roads at their width, so it shows where they would lie against the ground and the buildings
	var wide := []
	for id in _roads:
		if _edited(id) or (not _grab.is_empty() and _grab["road"] == id):
			wide.append(id)
	if not wide.is_empty():
		ribbons.surface_begin(Mesh.PRIMITIVE_TRIANGLES)
		for id in wide:
			# (one bending strip through a smooth curve of its points, its corners joined: as the game's road mesh is)
			var p := _curve(_roads[id]["points"])
			var half: float = _roads[id]["half"]
			var left := PackedVector3Array()
			var right := PackedVector3Array()
			for i in p.size():
				var before := p[i] - p[maxi(i - 1, 0)]
				var after := p[mini(i + 1, p.size() - 1)] - p[i]
				var n1 := Vector3(-before.z, 0.0, before.x).normalized() if before.length() > 0.01 else Vector3.ZERO
				var n2 := Vector3(-after.z, 0.0, after.x).normalized() if after.length() > 0.01 else Vector3.ZERO
				var n := (n1 + n2).normalized() if (n1 + n2).length() > 0.01 else (n2 if n2 != Vector3.ZERO else n1)
				var mitre := 1.0 / maxf(0.5, n.dot(n2 if n2 != Vector3.ZERO else n1))	# (wider at a bend, so its width stays)
				left.append(p[i] + Vector3.UP * 0.1 - n * half * mitre)
				right.append(p[i] + Vector3.UP * 0.1 + n * half * mitre)
			for i in p.size() - 1:
				for v in [left[i], right[i], right[i + 1], left[i], right[i + 1], left[i + 1]]:
					ribbons.surface_set_color(Color(1.0, 0.55, 0.1, 0.45))
					ribbons.surface_add_vertex(v)
		ribbons.surface_end()
	var multi := _handles.multimesh
	multi.instance_count = handles.size()
	for k in handles.size():
		multi.set_instance_transform(k, Transform3D(Basis.from_scale(Vector3.ONE * handles[k][1]), handles[k][0] + LIFT))
		multi.set_instance_color(k, handles[k][2])


# A smooth curve through a road's points (Catmull-Rom, a few steps between each two): how it would bend, for drawing
func _curve(p: PackedVector3Array) -> PackedVector3Array:
	if p.size() < 3:
		return p
	var out := PackedVector3Array()
	for i in p.size() - 1:
		var a := p[maxi(i - 1, 0)]
		var b := p[i]
		var c := p[i + 1]
		var d := p[mini(i + 2, p.size() - 1)]
		var steps := clampi(int(b.distance_to(c) / 1.5), 1, 12)
		for k in steps:
			var t := float(k) / steps
			var t2 := t * t
			var t3 := t2 * t
			out.append(0.5 * ((2.0 * b) + (c - a) * t + (2.0 * a - 5.0 * b + 4.0 * c - d) * t2 + (3.0 * b - a - 3.0 * c + d) * t3))
	out.append(p[p.size() - 1])
	return out


func _panel_text() -> String:
	var edited := 0
	for id in _roads:
		edited += 1 if _edited(id) else 0
	var held := ""
	if not _grab.is_empty() and _roads.has(_grab["road"]):
		var r: Dictionary = _roads[_grab["road"]]
		var p: Vector3 = r["points"][_grab["index"]]
		var pts: PackedVector3Array = r["points"]
		var k: int = _grab["index"]
		var grades := []
		for pair in [[k - 1, k], [k, k + 1]]:
			if pair[0] >= 0 and pair[1] < pts.size():
				var run := Vector2(pts[pair[1]].x - pts[pair[0]].x, pts[pair[1]].z - pts[pair[0]].z).length()
				grades.append("%+.1f%%" % ((pts[pair[1]].y - pts[pair[0]].y) / maxf(run, 0.5) * 100.0))
		held = "\nPicked: %s (%s), %.1f m wide, point %d of %d: %.1f m above the sea (%+.1f m), %.1f m from the game's line; grade %s" % [_named(r["info"]), r["info"]["highway"],
			float(r["half"]) * 2.0, k + 1, pts.size(), p.y, p.y - _orig_height_near(r, p), _off_orig(r, p), " then ".join(grades)]
	var head := "ROAD EDITOR (fix the world)   F2 leave   H %s the keys" % ("hide" if _help else "show")
	var legend := ("G colours: how steep (green under 6%, yellow 6-10%, orange 10-15%, red 15% or more)" if _by_grade
		else "G colours: height, blue %.1f m to red %.1f m above the sea" % [_low, _high])
	head += "\n" + legend
	if not _help:
		return "%s\n%d roads edited%s\n%s" % [head, edited, held, _status]
	var shown := []
	for key in LAYERS:
		shown.append("%s %s %s" % [OS.get_keycode_string(key), LAYERS[key][0], "hidden" if _hidden.get(key, false) else "shown"])
	return "\n".join([head,
		"Hold right mouse: look round    WASD: move    E / Q: up / down    Shift: faster",
		"Left-click a point and drag: move it (a road's end moves its junction too)",
		"Double-click a road between points: add a point    Delete: remove the picked point",
		"Wheel or PgUp / PgDn: picked point up / down 25 cm (Shift: 1 m)    K: smooth the picked road",
		"[ / ]: a move reaches %.0f m along the road, fading, so it stays smooth (0: the point alone)" % _soft,
		"L: make the picked road flow (straight runs, even bends and slope, in line with the roads at its ends; L again: smoother)",
		"- / =: picked road narrower / wider (Shift: 2 m)    Ctrl + wheel or Ctrl + PgUp / PgDn: the whole picked road up / down",
		"   ".join(shown),
		"Ctrl+Z undo    Ctrl+Y redo    Ctrl+C copy the edits (and save them beside your pictures), then %s: send them to us" % Controls.text("send"),
		"%d roads edited%s" % [edited, held], _status])


func _named(info: Dictionary) -> String:
	return info["name"] if info["name"] != "" else "unnamed road"


# How far a point is from the game's own line of its road (sideways), and the game's height nearest it
func _off_orig(r: Dictionary, p: Vector3) -> float:
	var o: PackedVector3Array = r["orig"]
	var best := INF
	for i in o.size() - 1:
		var a := Vector2(o[i].x, o[i].z)
		var b := Vector2(o[i + 1].x, o[i + 1].z)
		var q := Vector2(p.x, p.z)
		var ab := b - a
		var t := clampf((q - a).dot(ab) / maxf(ab.length_squared(), 1e-6), 0.0, 1.0)
		best = minf(best, q.distance_to(a + ab * t))
	return best if best < INF else 0.0


func _orig_height_near(r: Dictionary, p: Vector3) -> float:
	var o: PackedVector3Array = r["orig"]
	var best := INF
	var height := p.y
	for q in o:
		var d := Vector2(q.x - p.x, q.z - p.z).length()
		if d < best:
			best = d
			height = q.y
	return height


# --- input: true when the editor used it ---

func handle(event: InputEvent) -> bool:
	if not active:
		return false
	var eye := get_viewport().get_camera_3d()
	if event is InputEventMouseButton:
		if event.button_index == MOUSE_BUTTON_RIGHT:
			looking = event.pressed
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if looking else Input.MOUSE_MODE_VISIBLE
			return true
		if event.button_index == MOUSE_BUTTON_LEFT:
			if event.pressed and event.double_click:
				_insert_at(event.position, eye)
				return true
			if event.pressed:
				var hit := _pick(event.position, eye)
				_grab = hit
				_dragging = not hit.is_empty()
				if _dragging:
					_snapshot()
					_base = _state()
			else:
				_dragging = false
			_draw()
			return true
		if event.pressed and (event.button_index == MOUSE_BUTTON_WHEEL_UP or event.button_index == MOUSE_BUTTON_WHEEL_DOWN) and not _grab.is_empty():
			var by := (STEP if event.button_index == MOUSE_BUTTON_WHEEL_UP else -STEP) * (4.0 if event.shift_pressed else 1.0)
			if event.ctrl_pressed:
				_raise_road(by)
			else:
				_raise(by)
			return true
	if event is InputEventMouseMotion:
		if _dragging and not looking:
			_drag_to(event.position, eye)
			return true
		return not looking	# (the mouse only turns the view while the right button is held)
	if event is InputEventKey and event.pressed:
		if event.ctrl_pressed and (event.keycode == KEY_Y or (event.keycode == KEY_Z and event.shift_pressed)):
			_redo_step()
			return true
		if event.ctrl_pressed and event.keycode == KEY_Z:
			_undo_step()
			return true
		if event.ctrl_pressed and event.keycode == KEY_C:
			_copy()
			return true
		if event.echo:
			return (event.keycode == KEY_PAGEUP or event.keycode == KEY_PAGEDOWN) and _held_raise(event)
		if event.keycode == KEY_H:
			_help = not _help
			return true
		if event.keycode == KEY_K:
			_smooth()
			return true
		if event.keycode == KEY_L:
			_flow()
			return true
		if event.keycode == KEY_G:
			_by_grade = not _by_grade
			_draw()
			return true
		if event.keycode == KEY_MINUS or event.keycode == KEY_EQUAL or event.keycode == KEY_KP_SUBTRACT or event.keycode == KEY_KP_ADD:
			_widen((WIDTH_STEP if event.keycode == KEY_EQUAL or event.keycode == KEY_KP_ADD else -WIDTH_STEP) * (4.0 if event.shift_pressed else 1.0))
			return true
		if event.keycode == KEY_BRACKETLEFT or event.keycode == KEY_BRACKETRIGHT:
			_soft = clampf(_soft + (SOFT_STEP if event.keycode == KEY_BRACKETRIGHT else -SOFT_STEP), 0.0, SOFT_MAX)
			_status = "A move now reaches %.0f m along the road%s." % [_soft, " (the point alone)" if _soft <= 0.0 else ", fading"]
			return true
		if event.keycode == KEY_DELETE:
			_remove_picked()
			return true
		if LAYERS.has(event.keycode):
			_hidden[event.keycode] = not _hidden.get(event.keycode, false)
			_apply_layers()
			return true
		if event.keycode == KEY_PAGEUP or event.keycode == KEY_PAGEDOWN:
			return _held_raise(event)
	return false


func _held_raise(event: InputEventKey) -> bool:
	if _grab.is_empty():
		return false
	var by := (STEP if event.keycode == KEY_PAGEUP else -STEP) * (4.0 if event.shift_pressed else 1.0)
	if event.ctrl_pressed:
		_raise_road(by)
	else:
		_raise(by)
	return true


# - / =: the picked road narrower or wider (its whole length; the copy says the new width)
func _widen(by: float) -> void:
	if _grab.is_empty():
		_status = "Pick a point of the road to widen or narrow first."
		return
	_snapshot()
	var r: Dictionary = _roads[_grab["road"]]
	r["half"] = maxf(MIN_WIDTH * 0.5, float(r["half"]) + by * 0.5)
	_status = "%s now %.1f m wide (the game had %.1f m)." % [_named(r["info"]), float(r["half"]) * 2.0, float(r["info"]["half"]) * 2.0]
	_draw()


# Ctrl + wheel: the whole picked road up or down; its ends stay with their junctions, the road between eases to them
func _raise_road(by: float) -> void:
	_snapshot()
	var r: Dictionary = _roads[_grab["road"]]
	var p: PackedVector3Array = r["points"]
	var along := PackedFloat64Array()
	along.resize(p.size())
	for i in range(1, p.size()):
		along[i] = along[i - 1] + Vector2(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z).length()
	var total := maxf(along[p.size() - 1], 0.01)
	var ease := minf(15.0, total * 0.3)	# (m from each end over which it rises from its junction's height)
	for i in range(1, p.size() - 1):
		var from_end := minf(along[i], total - along[i])
		var w := 1.0 if from_end >= ease else 0.5 - 0.5 * cos(from_end / ease * PI)
		p[i] = p[i] + Vector3.UP * by * w
	r["points"] = p
	_status = "%s raised %+.2f m between its junctions (move an end to move a junction)." % [_named(r["info"]), by]
	_draw()


func _pick(at: Vector2, eye: Camera3D) -> Dictionary:
	var best := {}
	var best_d := PICK_PIXELS
	for id in _roads:
		var p: PackedVector3Array = _roads[id]["points"]
		for i in p.size():
			var spot := p[i] + LIFT
			if eye.is_position_behind(spot) or spot.distance_to(camera.global_position) > HANDLE_REACH:
				continue
			var d := eye.unproject_position(spot).distance_to(at)
			if d < best_d:
				best_d = d
				best = {"road": id, "index": i}
	return best


# The held point to where the mouse points, at its own height (sideways only: up and down is the wheel)
func _drag_to(at: Vector2, eye: Camera3D) -> void:
	var r: Dictionary = _roads[_grab["road"]]
	var p: Vector3 = r["points"][_grab["index"]]
	var from := eye.project_ray_origin(at)
	var dir := eye.project_ray_normal(at)
	if absf(dir.y) < 0.001:
		return
	var t := (p.y + LIFT.y - from.y) / dir.y
	if t <= 0.0:
		return
	var to := from + dir * t
	var start: Vector3 = (_base[_grab["road"]][0] as PackedVector3Array)[_grab["index"]]
	_soft_move(_grab["road"], _grab["index"], Vector3(to.x - start.x, p.y - start.y, to.z - start.z))
	_draw()


func _raise(by: float) -> void:
	_snapshot()
	_base = _state()
	_soft_move(_grab["road"], _grab["index"], Vector3.UP * by)
	_draw()


# A move of one point by Delta, from the roads as they were when it began (_base): the points along the road within
# _soft of it follow, less and less (a smooth bump, no corner). Moving a road's end moves its junction: every road there
# bends with it the same way. Moving an inner point leaves the road's ends (its junctions) where they are
func _soft_move(id: int, index: int, delta: Vector3) -> void:
	var p: PackedVector3Array = (_base[id][0] as PackedVector3Array).duplicate()
	var last := p.size() - 1
	if index == 0 or index == last:
		var junction: int = _roads[id]["info"]["from"] if index == 0 else _roads[id]["info"]["to"]
		for entry in _at_junction.get(junction, [[id, 0 if index == 0 else 1]]):
			var q: PackedVector3Array = (_base[entry[0]][0] as PackedVector3Array).duplicate()
			var from_start: bool = entry[1] == 0
			var along := 0.0
			for k in q.size():
				var i: int = k if from_start else q.size() - 1 - k
				if k > 0:
					var prev: int = i - 1 if from_start else i + 1
					along += Vector2(q[i].x - q[prev].x, q[i].z - q[prev].z).length()
				var w := _falloff(along) if k > 0 else 1.0
				if k > 0 and (i == 0 or i == q.size() - 1):
					w = 0.0	# (its other end is another junction: it stays)
				if w <= 0.0 and k > 0:
					break
				q[i] = q[i] + delta * w
			_roads[entry[0]]["points"] = q
		return
	var along_at := PackedFloat64Array()
	along_at.resize(p.size())
	along_at[0] = 0.0
	for i in range(1, p.size()):
		along_at[i] = along_at[i - 1] + Vector2(p[i].x - p[i - 1].x, p[i].z - p[i - 1].z).length()
	for i in range(1, last):
		p[i] = p[i] + delta * (1.0 if i == index else _falloff(absf(along_at[i] - along_at[index])))
	_roads[id]["points"] = p


func _falloff(distance: float) -> float:
	if _soft <= 0.0:
		return 0.0
	var t := clampf(distance / _soft, 0.0, 1.0)
	return 0.5 + 0.5 * cos(t * PI)


# A point moved; a road's end is its junction, so every road's end there moves with it
func _move(id: int, index: int, to: Vector3) -> void:
	var r: Dictionary = _roads[id]
	var p: PackedVector3Array = r["points"]
	var last := p.size() - 1
	if index == 0 or index == last:
		var junction: int = r["info"]["from"] if index == 0 else r["info"]["to"]
		for entry in _at_junction.get(junction, [[id, 0 if index == 0 else 1]]):
			var other: PackedVector3Array = _roads[entry[0]]["points"]
			other[0 if entry[1] == 0 else other.size() - 1] = to
			_roads[entry[0]]["points"] = other
	else:
		p[index] = to
		r["points"] = p


# Double-click on a road between two of its points: a new point there, picked, ready to drag
func _insert_at(at: Vector2, eye: Camera3D) -> void:
	var best_d := PICK_PIXELS * 1.5
	var found := {}
	for id in _roads:
		var p: PackedVector3Array = _roads[id]["points"]
		for i in p.size() - 1:
			var a3 := p[i] + LIFT
			var b3 := p[i + 1] + LIFT
			if eye.is_position_behind(a3) or eye.is_position_behind(b3) or a3.distance_to(camera.global_position) > HANDLE_REACH * 1.5:
				continue
			var a := eye.unproject_position(a3)
			var b := eye.unproject_position(b3)
			var ab := b - a
			var t := clampf((at - a).dot(ab) / maxf(ab.length_squared(), 1e-6), 0.0, 1.0)
			var d := at.distance_to(a + ab * t)
			if d < best_d and t > 0.05 and t < 0.95:
				best_d = d
				found = {"road": id, "after": i, "t": t}
	if found.is_empty():
		_status = "Double-click on a road's line, between two of its points, to add one there."
		return
	_snapshot()
	var r: Dictionary = _roads[found["road"]]
	var p: PackedVector3Array = r["points"]
	var i: int = found["after"]
	p.insert(i + 1, p[i].lerp(p[i + 1], found["t"]))
	r["points"] = p
	_grab = {"road": found["road"], "index": i + 1}
	_status = "Point added: drag it, or raise or lower it."
	_draw()


func _remove_picked() -> void:
	if _grab.is_empty():
		return
	var r: Dictionary = _roads[_grab["road"]]
	var p: PackedVector3Array = r["points"]
	var i: int = _grab["index"]
	if i == 0 or i == p.size() - 1:
		_status = "A road's end is its junction: move it, but it can't be removed."
		return
	_snapshot()
	p.remove_at(i)
	r["points"] = p
	_grab = {}
	_status = "Point removed."
	_draw()


# L: the picked road laid as a road would be through what was drawn. Its points are spread evenly (every FLOW_STEP m),
# then faired: each is moved towards where its neighbours two either side would put it on a straight or evenly bending
# line (the fourth difference to nothing: a curve that changes how sharply it bends only gradually), held lightly to what
# was drawn so it stays the idea. Beyond each end, the road it joins there (the one it carries straight on from) stands in
# for its next points, so it leaves and meets its junctions in line with them. Heights are faired with it: an even slope
const FLOW_STEP := 4.0	# m between the points of a road made to flow
const FLOW_ROUNDS := 400
const FLOW_FAIR := 0.04	# (how far each round moves a point towards the fair line)
const FLOW_HOLD := 0.01	# (and back towards what was drawn)


func _flow() -> void:
	if _grab.is_empty():
		_status = "Pick a point of the road to make flow first."
		return
	_snapshot()
	var id: int = _grab["road"]
	var r: Dictionary = _roads[id]
	var p := _resample(r["points"], FLOW_STEP)
	var n := p.size()
	if n < 4:
		_status = "Too short to make flow."
		return
	var drawn := p.duplicate()
	var before := _carry_on(id, 0, p[0], p[1])
	var after := _carry_on(id, 1, p[n - 1], p[n - 2])
	for _round in FLOW_ROUNDS:
		var q := p.duplicate()
		for i in range(1, n - 1):
			var a: Vector3 = p[i - 2] if i >= 2 else before
			var b: Vector3 = p[i - 1]
			var c: Vector3 = p[i + 1]
			var d: Vector3 = p[i + 2] if i + 2 < n else after
			var fourth := a - 4.0 * b + 6.0 * p[i] - 4.0 * c + d
			q[i] = p[i] - fourth * FLOW_FAIR + (drawn[i] - p[i]) * FLOW_HOLD
		p = q
	r["points"] = p
	_grab = {"road": id, "index": mini(_grab["index"], n - 1)}
	_status = "Made to flow (L again: smoother; Ctrl+Z to undo)."
	_draw()


# The point beyond a road's end (0 start, 1 end) where it would be if it carried straight on from the road it joins
# there: the one most in line with it. With none (a dead end), straight on along its own first piece
func _carry_on(id: int, end: int, at: Vector3, next: Vector3) -> Vector3:
	var info: Dictionary = _roads[id]["info"]
	var junction: int = info["from"] if end == 0 else info["to"]
	var out := (next - at).normalized()
	var best := at - (next - at)
	var best_dot := 0.3
	for entry in _at_junction.get(junction, []):
		if entry[0] == id:
			continue
		var q: PackedVector3Array = _roads[entry[0]]["points"]
		if q.size() < 2:
			continue
		var other: Vector3 = q[1] if entry[1] == 0 else q[q.size() - 2]
		var d := (at - other).normalized().dot(out)
		if d > best_dot:
			best_dot = d
			best = at + (at - other).normalized() * at.distance_to(next)
	return best


# A line's points spread evenly, Step m apart along it (its ends kept)
func _resample(p: PackedVector3Array, step: float) -> PackedVector3Array:
	var total := 0.0
	for i in p.size() - 1:
		total += p[i].distance_to(p[i + 1])
	var count := maxi(2, int(round(total / step)) + 1)
	var out := PackedVector3Array()
	var seg := 0
	var seg_start := 0.0
	for k in count:
		var want := total * k / (count - 1)
		while seg < p.size() - 2 and seg_start + p[seg].distance_to(p[seg + 1]) < want:
			seg_start += p[seg].distance_to(p[seg + 1])
			seg += 1
		var length := p[seg].distance_to(p[seg + 1])
		out.append(p[seg].lerp(p[seg + 1], clampf((want - seg_start) / maxf(length, 0.001), 0.0, 1.0)))
	out[0] = p[0]
	out[count - 1] = p[p.size() - 1]
	return out


# K: the picked road's inner points eased towards their neighbours (a few passes), so a hand-drawn line comes out smooth
func _smooth() -> void:
	if _grab.is_empty():
		_status = "Pick a point of the road to smooth first."
		return
	_snapshot()
	var r: Dictionary = _roads[_grab["road"]]
	var p: PackedVector3Array = r["points"]
	for _pass in 3:
		var q := p.duplicate()
		for i in range(1, p.size() - 1):
			q[i] = p[i].lerp((p[i - 1] + p[i + 1]) * 0.5, 0.5)
		p = q
	r["points"] = p
	_status = "Smoothed (K again for more, Ctrl+Z to undo)."
	_draw()


func _state() -> Dictionary:
	var state := {}
	for id in _roads:
		state[id] = [(_roads[id]["points"] as PackedVector3Array).duplicate(), _roads[id]["half"]]
	return state


func _restore(state: Dictionary) -> void:
	for id in state:
		if _roads.has(id):
			_roads[id]["points"] = (state[id][0] as PackedVector3Array).duplicate()
			_roads[id]["half"] = state[id][1]
	if not _grab.is_empty() and _roads.has(_grab["road"]) and _grab["index"] >= (_roads[_grab["road"]]["points"] as PackedVector3Array).size():
		_grab = {}
	_draw()


func _snapshot() -> void:
	_undo.append(_state())
	_redo.clear()
	if _undo.size() > 200:
		_undo.pop_front()


func _undo_step() -> void:
	if _undo.is_empty():
		_status = "Nothing to undo."
		return
	_redo.append(_state())
	_restore(_undo.pop_back())
	_status = "Undone (Ctrl+Y redoes it)."


func _redo_step() -> void:
	if _redo.is_empty():
		_status = "Nothing to redo."
		return
	_undo.append(_state())
	_restore(_redo.pop_back())
	_status = "Redone."


# --roadedit: the point nearest the middle of the view moved 4 m sideways and 3 m up, as a drag would, then copied
func self_test() -> void:
	var eye := get_viewport().get_camera_3d()
	var hit := _pick(get_viewport().get_visible_rect().size * 0.5, eye)
	if hit.is_empty():
		var best := INF
		for id in _roads:
			var p: PackedVector3Array = _roads[id]["points"]
			for i in p.size():
				var d := p[i].distance_to(camera.global_position)
				if d < best:
					best = d
					hit = {"road": id, "index": i}
	if hit.is_empty():
		print("Geodrive: [RoadEdit] no roads round the camera")
		return
	_grab = hit
	_snapshot()
	_base = _state()
	_soft_move(hit["road"], hit["index"], Vector3(4.0, 3.0, 0.0))
	_undo_step()
	_redo_step()
	_widen(1.0)
	_raise_road(0.5)
	_flow()
	_draw()
	_copy()
	print("Geodrive: [RoadEdit] %d roads round the camera; moved road %d point %d (undone and redone); %s" % [_roads.size(), hit["road"], hit["index"], _status])


# What was changed, as text to paste into a chat (and saved beside the pictures): each edited road's new line, from its
# start, as latitude, longitude and metres above the sea, with how far it moved from the game's. Drawn by hand: a
# guideline of where the road should go, not a survey
func _copy() -> void:
	var lines := []
	var count := 0
	var here: PackedFloat64Array = world.to_lat_lon(camera.global_position)
	for id in _roads:
		if not _edited(id):
			continue
		count += 1
		var r: Dictionary = _roads[id]
		var info: Dictionary = r["info"]
		var p: PackedVector3Array = r["points"]
		var most := 0.0
		var most_up := 0.0
		for q in p:
			most = maxf(most, _off_orig(r, q))
			var up := q.y - _orig_height_near(r, q)
			most_up = up if absf(up) > absf(most_up) else most_up
		var width := "%.1f m wide" % (float(info["half"]) * 2.0)
		if absf(float(r["half"]) - float(info["half"])) > 0.01:
			width = "%.1f m wide (the game had %.1f m)" % [float(r["half"]) * 2.0, float(info["half"]) * 2.0]
		lines.append("road %d %s (%s), layer %d%s%s, %s, junctions %d -> %d: %d -> %d points, up to %.1f m from the game's line, %+.1f m up or down" % [id,
			_named(info), info["highway"], info["layer"], ", tunnel" if info["tunnel"] else "", ", bridge" if info["bridge"] else "", width,
			info["from"], info["to"], (r["orig"] as PackedVector3Array).size(), p.size(), most, most_up])
		var pts := []
		for q in p:
			var ll: PackedFloat64Array = world.to_lat_lon(q)
			pts.append("%.6f, %.6f, %.1f" % [ll[0], ll[1], q.y])
		lines.append("    " + "; ".join(pts))
	if count == 0:
		_status = "Nothing edited yet: drag a point first."
		return
	var text := "Open-Planet road edits near %.6f, %.6f (%d roads). Drawn by hand in the game: a guideline of where the roads should go and how high, not exact lines. Each road's new line from its start: lat, lon, m above the sea.\n%s" % [here[0], here[1], count, "\n".join(lines)]
	DisplayServer.clipboard_set(text)
	var folder := OS.get_system_dir(OS.SYSTEM_DIR_PICTURES).path_join("Open-Planet")
	DirAccess.make_dir_recursive_absolute(folder)
	var path := folder.path_join("Road edits %s.txt" % Time.get_datetime_string_from_system().replace(":", "-").replace("T", " "))
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file:
		file.store_string(text)
	if made.is_valid():
		made.call("edit", "", text)
	_status = "Copied, and saved as %s. %s sends it to us on GitHub" % [path.get_file(), Controls.text("send")]
