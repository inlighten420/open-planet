# The shape editor: a look (part_model.gd's) made by hand. A 3D view of it on a grid, its size shown (and made what's
# typed); the shapes to add, its parts listed. A part's picked by clicking it in the view (or in the list); arrows on it
# (x red, y green, z blue) drag it: moved, sized or turned along them (the mode's), snapped to the grid (or not); the
# part itself dragged moves it across the view. Its colour (the palette, or any), its finish, made a cut (taking its
# shape out of the rest), or the point aimed over; copy, delete; sliders for the exact numbers. Left drag elsewhere
# turns the view, the wheel zooms. For any kind of thing with a look, at its own scale (KINDS: a gun's in centimetres,
# a car's in metres); its owner's told of every change (changed).
class_name ShapeEditor
extends RefCounted

signal changed

# each kind's scale: its grid's step and its major lines', how sizes are shown, the sliders' reach, the view's start
const KINDS := {
	"gun": {"grid": 0.01, "major": 0.1, "unit": "cm", "per": 100.0, "reach": 0.8, "biggest": 1.5, "view": 1.2},
	"prop": {"grid": 0.05, "major": 0.5, "unit": "cm", "per": 100.0, "reach": 3.0, "biggest": 5.0, "view": 4.0},
	"car": {"grid": 0.1, "major": 1.0, "unit": "m", "per": 1.0, "reach": 6.0, "biggest": 8.0, "view": 9.0},
}
const MODES := ["move", "size", "turn"]

var look: Dictionary	# (the one being made: edited in place)
var kind := "gun"
var picked := -1
var mode := "move"
var snap := true
var _view: SubViewport
var _camera: Camera3D
var _model: Node3D
var _mark: MeshInstance3D	# (the picked part's box and its arrows)
var _grid: MeshInstance3D
var _yaw := 0.7
var _pitch := 0.35
var _distance := 1.2
var _dragged := false
var _drag := {}	# (dragging: {axis (-1: across the view), start values, the mouse moved so far})
var _panel: Control	# (the controls' column: made again as the pick changes)
var _size_label: Label
var _owner_menu: Node	# (the builder: its menu buttons' look)


func _init(given: Dictionary, menu_owner: Node, of_kind: String = "gun") -> void:
	look = given
	if not look.has("parts"):
		look["parts"] = []
	_owner_menu = menu_owner
	kind = of_kind if KINDS.has(of_kind) else "gun"
	_distance = KINDS[kind]["view"]
	if not look["parts"].is_empty():
		_distance = maxf(_distance, bounds().get_longest_axis_size() * 1.6)	# (all of it in view: a table made a gun is big)


func _scale() -> Dictionary:
	return KINDS[kind]


# Its controls in Into: the view on the left, the rest on the right
func fill(into: Container) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 14)
	into.add_child(row)
	var left := VBoxContainer.new()
	row.add_child(left)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 6)
	column.custom_minimum_size = Vector2(560.0, 0.0)
	row.add_child(column)
	fill_view(left)
	fill_panel(column)


# The view alone in Left (its size under it); the controls, if wanted, then go in a column of their own (fill_panel)
func fill_view(left: Container) -> void:
	left.add_child(_make_view())
	_size_label = Label.new()
	_size_label.modulate = Color(0.75, 0.95, 1.0)
	left.add_child(_size_label)
	_panel = null
	_rebuild()


func fill_panel(column: Container) -> void:
	_panel = column
	_fill_panel()
	_rebuild()


# --- the view ---

func _make_view() -> Control:
	var box := SubViewportContainer.new()
	box.stretch = true
	box.custom_minimum_size = Vector2(460.0, 440.0)
	_view = SubViewport.new()
	_view.own_world_3d = true
	_view.msaa_3d = Viewport.MSAA_4X
	box.add_child(_view)
	var sky := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.16, 0.17, 0.19)
	environment.ambient_light_color = Color(0.6, 0.62, 0.66)
	environment.ambient_light_energy = 0.6
	sky.environment = environment
	_view.add_child(sky)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(-0.9, 0.6, 0.0)
	sun.light_energy = 1.4
	_view.add_child(sun)
	_camera = Camera3D.new()
	_camera.fov = 40.0
	_camera.near = 0.01
	_camera.far = 200.0
	_view.add_child(_camera)
	# (lines drawn over everything, coloured by their points: the picked part's box and arrows; the grid under it)
	var lines := StandardMaterial3D.new()
	lines.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	lines.vertex_color_use_as_albedo = true
	lines.no_depth_test = true
	_mark = MeshInstance3D.new()
	_mark.mesh = ImmediateMesh.new()
	_mark.material_override = lines
	_view.add_child(_mark)
	var floor_lines := lines.duplicate()
	floor_lines.no_depth_test = false
	floor_lines.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_grid = MeshInstance3D.new()
	_grid.mesh = ImmediateMesh.new()
	_grid.material_override = floor_lines
	_view.add_child(_grid)
	box.gui_input.connect(_view_input)
	_place_camera()
	return box


func _view_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		_distance = clampf(_distance * (0.9 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.1), _scale()["reach"] * 0.1, _scale()["biggest"] * 4.0)
		_place_camera()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_dragged = false
			# (an arrow of the picked part's under the mouse, or the picked part itself: dragging it)
			var axis := _arrow_at(event.position)
			if axis >= 0 or (picked >= 0 and _part_at(event.position) == picked):
				var part: Dictionary = look["parts"][picked]
				_drag = {"axis": axis, "at": part["at"].duplicate(), "size": part["size"].duplicate(), "turn": part.get("turn", [0, 0, 0]).duplicate(),
					"moved": Vector2.ZERO}
		else:
			if not _drag.is_empty():
				_drag = {}
				_fill_panel()	# (the sliders' numbers, as it's left)
				changed.emit()
			elif not _dragged:
				var hit := _part_at(event.position)
				if hit >= 0:
					pick(hit)
	elif event is InputEventMouseMotion and event.button_mask & MOUSE_BUTTON_MASK_LEFT:
		_dragged = true
		if not _drag.is_empty():
			_drag["moved"] += event.relative
			_drag_part()
		else:
			_yaw -= event.relative.x * 0.01
			_pitch = clampf(_pitch + event.relative.y * 0.01, -1.4, 1.4)
			_place_camera()


func _place_camera() -> void:
	if _camera == null:
		return
	var centre := _middle()
	var at := centre + Vector3(sin(_yaw) * cos(_pitch), sin(_pitch), cos(_yaw) * cos(_pitch)) * _distance
	# (its transform set straight: look_at wants it in the tree, and the view's made before it's shown)
	_camera.transform = Transform3D(Basis.looking_at(centre - at, Vector3.UP), at)
	_draw_mark()


# (the middle of what's made: the view turns round it)
func _middle() -> Vector3:
	var box := bounds()
	return box.get_center() if box.size != Vector3.ZERO else Vector3.ZERO


# What's made, end to end: the box round all its solid parts (each part's corners as it's turned)
func bounds() -> AABB:
	var box := AABB()
	var first := true
	for part in look["parts"]:
		if part.get("cut", false):
			continue
		var basis := _basis(part)
		var at := PartModel._vec(part.get("at", [0, 0, 0]), Vector3.ZERO)
		for x in [-0.5, 0.5]:
			for y in [-0.5, 0.5]:
				for z in [-0.5, 0.5]:
					var corner := at + basis * Vector3(x, y, z)
					if first:
						box = AABB(corner, Vector3.ZERO)
						first = false
					else:
						box = box.expand(corner)
	return box


static func _basis(part: Dictionary) -> Basis:
	var size := PartModel._vec(part.get("size", [0.1, 0.1, 0.1]), Vector3.ONE * 0.1)
	var turn := PartModel._vec(part.get("turn", [0, 0, 0]), Vector3.ZERO)
	return Basis.from_euler(Vector3(deg_to_rad(turn.x), deg_to_rad(turn.y), deg_to_rad(turn.z))).scaled_local(size)


# The part under a point in the view: the nearest whose box the ray from the eye meets (-1: none)
func _part_at(at: Vector2) -> int:
	var from := _camera.project_ray_origin(at)
	var way := _camera.project_ray_normal(at)
	var best := -1
	var nearest := INF
	var parts: Array = look["parts"]
	for i in parts.size():
		# (the ray into the part's own frame, where it's a box of 1 m round its middle)
		var into := Transform3D(_basis(parts[i]), PartModel._vec(parts[i].get("at", [0, 0, 0]), Vector3.ZERO)).affine_inverse()
		var o := into * from
		var d := into.basis * way
		var near := -INF
		var far := INF
		var missed := false
		for k in 3:
			if absf(d[k]) < 1e-9:
				if absf(o[k]) > 0.5:
					missed = true
				continue
			var t1 := (-0.5 - o[k]) / d[k]
			var t2 := (0.5 - o[k]) / d[k]
			near = maxf(near, minf(t1, t2))
			far = minf(far, maxf(t1, t2))
		if missed or near > far or far < 0.0:
			continue
		# (how far along the ray, in the view's own measure)
		var hit := Transform3D(_basis(parts[i]), PartModel._vec(parts[i].get("at", [0, 0, 0]), Vector3.ZERO)) * (o + d * maxf(near, 0.0))
		var distance := hit.distance_to(from)
		if distance < nearest:
			nearest = distance
			best = i
	return best


# The picked part's arrows (x, y, z: from its middle, a sixth of the view's width long), as the view shows them
func _arrows() -> Array:
	if picked < 0 or picked >= look["parts"].size():
		return []
	var part: Dictionary = look["parts"][picked]
	var at := PartModel._vec(part.get("at", [0, 0, 0]), Vector3.ZERO)
	var length := _distance * 0.22
	var basis := Basis.from_euler(_vec3(part.get("turn", [0, 0, 0])) * (PI / 180.0)) if mode == "size" else Basis()
	var out := []
	for axis in 3:
		out.append([at, at + basis[axis].normalized() * length])
	return out


static func _vec3(value) -> Vector3:
	return PartModel._vec(value, Vector3.ZERO)


# The arrow under a point in the view (within 12 px of it): its axis, or -1
func _arrow_at(at: Vector2) -> int:
	var arrows := _arrows()
	var best := -1
	var nearest := 12.0
	for axis in arrows.size():
		var a := _camera.unproject_position(arrows[axis][0])
		var b := _camera.unproject_position(arrows[axis][1])
		var d := Geometry2D.get_closest_point_to_segment(at, a, b).distance_to(at)
		if d < nearest and (at - a).length() > 6.0:
			nearest = d
			best = axis
	return best


# A drag: along an arrow (moved, sized or turned by the mode), or across the view (moved); snapped to the grid
func _drag_part() -> void:
	var part: Dictionary = look["parts"][picked]
	var grid: float = _scale()["grid"] if snap else 0.0
	var snapped_to := func(v: float, step: float) -> float: return snappedf(v, step) if step > 0.0 else v
	var axis: int = _drag["axis"]
	var moved: Vector2 = _drag["moved"]
	if axis < 0:
		# (across the view: the mouse's pixels as metres at the part's distance)
		var at := _vec3(_drag["at"])
		var per_pixel := 2.0 * _camera.global_position.distance_to(at) * tan(deg_to_rad(_camera.fov) * 0.5) / maxf(_view.size.y, 1.0)
		var to := at + (_camera.global_transform.basis.x * moved.x - _camera.global_transform.basis.y * moved.y) * per_pixel
		part["at"] = [snapped_to.call(to.x, grid), snapped_to.call(to.y, grid), snapped_to.call(to.z, grid)]
	else:
		# (along the arrow: the mouse's way along it on the screen, its pixels as metres there)
		var arrows := _arrows()
		var a := _camera.unproject_position(arrows[axis][0])
		var b := _camera.unproject_position(arrows[axis][1])
		var screen := b - a
		var metres: float = (arrows[axis][1] - arrows[axis][0]).length()
		var along: float = moved.dot(screen.normalized()) * metres / maxf(screen.length(), 1.0)
		match mode:
			"move":
				var at: Array = _drag["at"].duplicate()
				at[axis] = snapped_to.call(float(at[axis]) + along, grid)
				part["at"] = at
			"size":
				var size: Array = _drag["size"].duplicate()
				size[axis] = maxf(snapped_to.call(float(size[axis]) + along * 2.0, grid), maxf(grid, 0.002))
				part["size"] = size
			"turn":
				var turn: Array = _drag["turn"].duplicate()
				turn[axis] = snapped_to.call(float(turn[axis]) + (moved.x - moved.y) * 0.5, 5.0 if snap else 0.0)
				part["turn"] = turn
	_rebuild()


func _rebuild() -> void:
	if _view == null:
		return
	if _model:
		_model.queue_free()
	_model = PartModel.build(look, true)
	_view.add_child(_model)
	_draw_mark()
	_draw_grid()


# The picked part's box (yellow) and its arrows (x red, y green, z blue; the one dragged brighter)
func _draw_mark() -> void:
	if _mark == null:
		return	# (no view yet: it's drawn when there is)
	var lines: ImmediateMesh = _mark.mesh
	lines.clear_surfaces()
	if picked < 0 or picked >= look["parts"].size():
		return
	var part: Dictionary = look["parts"][picked]
	var basis := _basis(part).scaled_local(Vector3.ONE * 1.04)
	var at := PartModel._vec(part.get("at", [0, 0, 0]), Vector3.ZERO)
	var corner := func(v: Vector3) -> Vector3: return at + basis * v * 0.5
	lines.surface_begin(Mesh.PRIMITIVE_LINES)
	for a in [-1.0, 1.0]:
		for b in [-1.0, 1.0]:
			for edge in [[Vector3(-1, a, b), Vector3(1, a, b)], [Vector3(a, -1, b), Vector3(a, 1, b)], [Vector3(a, b, -1), Vector3(a, b, 1)]]:
				lines.surface_set_color(Color(1.0, 0.85, 0.2))
				lines.surface_add_vertex(corner.call(edge[0]))
				lines.surface_set_color(Color(1.0, 0.85, 0.2))
				lines.surface_add_vertex(corner.call(edge[1]))
	var colours := [Color(1.0, 0.3, 0.3), Color(0.35, 1.0, 0.35), Color(0.35, 0.6, 1.0)]
	var arrows := _arrows()
	for axis in arrows.size():
		var colour: Color = colours[axis].lightened(0.4) if _drag.get("axis", -2) == axis else colours[axis]
		var a: Vector3 = arrows[axis][0]
		var b: Vector3 = arrows[axis][1]
		# (its shaft, and a little cross at its head: a knob to grab)
		var head := (b - a).length() * 0.08
		for pair in [[a, b], [b + Vector3(head, 0, 0), b - Vector3(head, 0, 0)], [b + Vector3(0, head, 0), b - Vector3(0, head, 0)], [b + Vector3(0, 0, head), b - Vector3(0, 0, head)]]:
			lines.surface_set_color(colour)
			lines.surface_add_vertex(pair[0])
			lines.surface_set_color(colour)
			lines.surface_add_vertex(pair[1])
	lines.surface_end()


# The grid under what's made (its step's lines faint, its major lines brighter), and its size in words
func _draw_grid() -> void:
	if _grid == null:
		return
	var lines: ImmediateMesh = _grid.mesh
	lines.clear_surfaces()
	var box := bounds()
	var step: float = _scale()["grid"]
	var major: float = _scale()["major"]
	var reach := maxf(maxf(box.size.x, box.size.z) * 0.75 + major, major * 2.0)
	var middle := box.get_center()
	var y := box.position.y - step if box.size != Vector3.ZERO else 0.0
	var from_x := snappedf(middle.x - reach, major)
	var from_z := snappedf(middle.z - reach, major)
	var count := int(ceil(reach * 2.0 / step))
	if count > 400:
		count = int(ceil(reach * 2.0 / major))
		step = major
	lines.surface_begin(Mesh.PRIMITIVE_LINES)
	for k in count + 1:
		var on_major: bool = is_zero_approx(fmod(k * step + 1e-6, major) - 1e-6) or fmod(k * step + 1e-6, major) < 1e-4
		var colour := Color(0.65, 0.7, 0.8, 0.45) if on_major else Color(0.5, 0.55, 0.6, 0.15)
		var x := from_x + k * step
		var z := from_z + k * step
		for pair in [[Vector3(x, y, from_z), Vector3(x, y, from_z + count * step)], [Vector3(from_x, y, z), Vector3(from_x + count * step, y, z)]]:
			lines.surface_set_color(colour)
			lines.surface_add_vertex(pair[0])
			lines.surface_set_color(colour)
			lines.surface_add_vertex(pair[1])
	lines.surface_end()
	if _size_label:
		_size_label.text = "%s long, %s high, %s wide   (the grid: %s a square)" % [_measure(box.size.z), _measure(box.size.y), _measure(box.size.x), _measure(step)]


# A length as this kind shows it: "31.0 cm", "4.20 m"
func _measure(metres: float) -> String:
	return ("%.1f cm" % (metres * 100.0)) if _scale()["unit"] == "cm" else ("%.2f m" % metres)


# What's made, all of it, scaled evenly to be Length long (its sight too)
func scale_to(length: float) -> void:
	var now := bounds().size.z
	if now <= 0.0 or length <= 0.0:
		return
	var by := length / now
	for part in look["parts"]:
		part["at"] = [float(part["at"][0]) * by, float(part["at"][1]) * by, float(part["at"][2]) * by]
		part["size"] = [float(part["size"][0]) * by, float(part["size"][1]) * by, float(part["size"][2]) * by]
	if look.has("sight_at"):
		look["sight_at"] = [float(look["sight_at"][0]) * by, float(look["sight_at"][1]) * by, float(look["sight_at"][2]) * by]
	_fill_panel()
	_changed()


# --- the controls ---

func pick(i: int) -> void:
	picked = i
	_fill_panel()
	_draw_mark()


func _changed() -> void:
	_rebuild()
	changed.emit()


func add(shape: String) -> void:
	# (this kind's size of a new part, on top of what's made)
	var unit: float = float(_scale()["reach"]) / 16.0
	var size := [unit, unit, unit]
	if shape in ["cylinder", "cone", "tube"]:
		size = [unit * 0.6, unit * 2.0, unit * 0.6]
	var box := bounds()
	var top := box.get_center() + Vector3(0.0, box.size.y * 0.5 + unit, 0.0) if box.size != Vector3.ZERO else Vector3.ZERO
	look["parts"].append({"shape": shape, "at": [top.x, top.y, top.z], "size": size, "turn": [0, 0, 0], "colour": "#55575c", "finish": "plastic"})
	pick(look["parts"].size() - 1)
	_changed()


func _fill_panel() -> void:
	if _panel == null:
		return
	for child in _panel.get_children():
		_panel.remove_child(child)	# (out of the layout at once: queued to go, it'd be counted with the new till the frame's end)
		child.queue_free()
	if _owner_menu.has_method("fit_menu"):
		_owner_menu.fit_menu.call_deferred()	# (the menu fitted to what's in it, once it's laid out)
	var adds := HBoxContainer.new()
	_panel.add_child(adds)
	adds.add_child(_owner_menu._menu_label("Add"))
	for shape in PartModel.SHAPES:
		var button: Button = _owner_menu._menu_button(shape.capitalize(), false, 70.0, 32.0)
		button.pressed.connect(add.bind(shape))
		adds.add_child(button)
	# what the arrows do; the grid's snap; its size, typed
	var arrows := HBoxContainer.new()
	_panel.add_child(arrows)
	arrows.add_child(_owner_menu._menu_label("Arrows"))
	for m in MODES:
		var button: Button = _owner_menu._menu_button(m.capitalize(), mode == m, 64.0, 30.0)
		button.pressed.connect(func(): mode = m; _fill_panel(); _draw_mark())
		arrows.add_child(button)
	var snapping: Button = _owner_menu._menu_button("Snap " + _measure(_scale()["grid"]), snap, 110.0, 30.0)
	snapping.pressed.connect(func(): snap = not snap; _fill_panel())
	arrows.add_child(snapping)
	var length := LineEdit.new()
	length.placeholder_text = "long (%s)" % _scale()["unit"]
	length.custom_minimum_size = Vector2(90.0, 0.0)
	length.text_submitted.connect(func(t: String): if t.is_valid_float(): scale_to(float(t) / float(_scale()["per"])))
	arrows.add_child(length)
	var make: Button = _owner_menu._menu_button("Make it", false, 70.0, 30.0)
	make.pressed.connect(func(): if length.text.is_valid_float(): scale_to(float(length.text) / float(_scale()["per"])))
	arrows.add_child(make)
	# its parts, the picked one lit
	var list := ItemList.new()
	list.custom_minimum_size = Vector2(540.0, 90.0)
	list.focus_mode = Control.FOCUS_NONE
	var parts: Array = look["parts"]
	for i in parts.size():
		var part: Dictionary = parts[i]
		var name := "%d. %s %s%s" % [i + 1, String(part.get("finish", "matte")), String(part.get("shape", "box")), "  (cut)" if part.get("cut", false) else ""]
		list.add_item(name)
		list.set_item_custom_fg_color(i, Color.html(String(part.get("colour", "#888888"))).lightened(0.55))
	if picked >= 0 and picked < parts.size():
		list.select(picked)
	list.item_selected.connect(pick)
	_panel.add_child(list)
	if picked < 0 or picked >= parts.size():
		var hint := Label.new()
		hint.text = "Add a shape, or pick one (click it in the view, or in the list), then drag its arrows. Left drag elsewhere turns the view; the wheel zooms."
		hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		hint.modulate = Color(1, 1, 1, 0.7)
		_panel.add_child(hint)
		return
	var part: Dictionary = parts[picked]
	# its shape
	var shapes := HBoxContainer.new()
	_panel.add_child(shapes)
	shapes.add_child(_owner_menu._menu_label("Shape"))
	for shape in PartModel.SHAPES:
		var button: Button = _owner_menu._menu_button(shape.capitalize(), part.get("shape") == shape, 70.0, 30.0)
		button.pressed.connect(func(): part["shape"] = shape; _fill_panel(); _changed())
		shapes.add_child(button)
	# where, how big, how turned (this kind's units, and degrees): x, y, z a row each
	var unit: String = _scale()["unit"]
	var per: float = _scale()["per"]
	var reach: float = _scale()["reach"]
	var fine: float = _scale()["grid"] * per * 0.5
	var shown := "%.1f" if unit == "cm" else "%.2f"
	_sliders("Move (%s)" % unit, part, "at", -reach * per, reach * per, fine, shown, per)
	_sliders("Size (%s)" % unit, part, "size", fine, reach * per, fine, shown, per)
	_sliders("Turn (°)", part, "turn", -180.0, 180.0, 5.0, "%d", 1.0)
	# its colour: the palette, or any
	var colours := HFlowContainer.new()
	colours.custom_minimum_size = Vector2(540.0, 0.0)
	_panel.add_child(colours)
	for colour in PartModel.PALETTE:
		var swatch := Button.new()
		swatch.custom_minimum_size = Vector2(24.0, 24.0)
		swatch.focus_mode = Control.FOCUS_NONE
		var style := StyleBoxFlat.new()
		style.bg_color = Color.html(colour)
		style.set_border_width_all(3 if part.get("colour") == colour else 0)
		style.border_color = Color(1.0, 0.85, 0.2)
		swatch.add_theme_stylebox_override("normal", style)
		swatch.add_theme_stylebox_override("hover", style)
		swatch.pressed.connect(func(): part["colour"] = colour; _fill_panel(); _changed())
		colours.add_child(swatch)
	var any := ColorPickerButton.new()
	any.custom_minimum_size = Vector2(60.0, 24.0)
	any.color = Color.html(String(part.get("colour", "#55575c")))
	any.edit_alpha = false
	any.color_changed.connect(func(c: Color): part["colour"] = "#" + c.to_html(false); _changed())
	colours.add_child(any)
	# its finish; a cut; the sight; copy, delete
	var finish := HBoxContainer.new()
	_panel.add_child(finish)
	finish.add_child(_owner_menu._menu_label("Finish"))
	for f in PartModel.FINISHES:
		var button: Button = _owner_menu._menu_button(f.capitalize(), part.get("finish", "matte") == f, 80.0, 30.0)
		button.pressed.connect(func(): part["finish"] = f; _fill_panel(); _changed())
		finish.add_child(button)
	var tools := HBoxContainer.new()
	_panel.add_child(tools)
	var cut: Button = _owner_menu._menu_button("Cut (a hole)", part.get("cut", false), 120.0, 32.0)
	cut.pressed.connect(func(): part["cut"] = not part.get("cut", false); _fill_panel(); _changed())
	tools.add_child(cut)
	var sight: Button = _owner_menu._menu_button("Aim over this", false, 120.0, 32.0)
	sight.pressed.connect(func():
		# (its top's middle: what the eye looks over)
		var at := PartModel._vec(part.get("at", [0, 0, 0]), Vector3.ZERO)
		var size := PartModel._vec(part.get("size", [0.1, 0.1, 0.1]), Vector3.ONE * 0.1)
		look["sight_at"] = [at.x, at.y + size.y * 0.5, at.z]
		_changed())
	tools.add_child(sight)
	var copy: Button = _owner_menu._menu_button("Copy", false, 80.0, 32.0)
	copy.pressed.connect(func():
		var twin: Dictionary = part.duplicate(true)
		twin["at"] = [float(part["at"][0]) + 0.02, float(part["at"][1]), float(part["at"][2])]
		look["parts"].append(twin)
		pick(look["parts"].size() - 1)
		_changed())
	tools.add_child(copy)
	var gone: Button = _owner_menu._menu_button("Delete", false, 80.0, 32.0)
	gone.pressed.connect(func():
		look["parts"].remove_at(picked)
		pick(-1)
		_changed())
	tools.add_child(gone)


# A row of three sliders for a part's three numbers (Key's x, y, z), shown in Scale's units
func _sliders(label: String, part: Dictionary, key: String, low: float, high: float, step: float, shown: String, scale: float) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 4)
	_panel.add_child(row)
	var name := Label.new()
	name.text = label
	name.custom_minimum_size = Vector2(84.0, 0.0)
	row.add_child(name)
	var values: Array = part.get(key, [0.0, 0.0, 0.0])
	part[key] = values
	for axis in 3:
		var tag := Label.new()
		tag.text = "xyz"[axis]
		tag.modulate = [Color(1.0, 0.5, 0.5), Color(0.6, 1.0, 0.6), Color(0.6, 0.75, 1.0)][axis]
		row.add_child(tag)
		var slider := HSlider.new()
		slider.min_value = low
		slider.max_value = high
		slider.step = step
		slider.value = float(values[axis]) * scale
		slider.custom_minimum_size = Vector2(100.0, 20.0)
		slider.focus_mode = Control.FOCUS_NONE
		var value := Label.new()
		value.text = shown % slider.value
		value.custom_minimum_size = Vector2(44.0, 0.0)
		slider.value_changed.connect(func(v: float):
			values[axis] = v / scale
			value.text = shown % v
			_changed())
		row.add_child(slider)
		row.add_child(value)
