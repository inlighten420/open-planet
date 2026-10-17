# The creature maker (Make, Creature): one of the creatures there is to start from (the hunting pack's, the zombie, or
# one of yours), then moulded: its size, its body's shape, its coat and colour, its horns, mane and tail, all seen as
# they change (Core's own body: GeodriveHunt.body_parts, in a little 3D view you turn by dragging); and what it does:
# how fast, in herds of how many, what it eats, whether it runs from people or goes for them. Saved as a creature's
# file in your workshop (creatures/<id>.json, as docs/MAKE_A_CREATURE.md has them), and Core told: the game maker
# offers it at once.
extends RefCounted

const COATS := ["plain", "countershade", "stripes", "patches", "mask", "pinstripes"]
const HORNS := ["none", "pair", "nose"]
const MANES := ["none", "lion", "crest"]
const DIETS := [["grazer", "Grass"], ["browser", "Leaves"], ["mixed", "Both"], ["carnivore", "Meat"]]

var builder: Node
var data := {}	# (the creature being made: a creature file's dictionary)
var tab := 1	# (the right-hand side: 1 its shape, 0 what it does)
var _view: SubViewport
var _model: Node3D
var _yaw := 0.6
var _distance := 4.0
var _camera: Camera3D
var _note: Label


func _init(owner: Node) -> void:
	builder = owner


# A creature there is to start from: a copy, named as yours (its id from the name when it's saved)
func start_from(id: String) -> void:
	data = GeodriveHunt.species_data(id).duplicate(true)
	if String(data.get("id", "")).begins_with("my_"):
		return	# (yours: changed, not copied)
	data["name"] = "My " + String(data.get("name", "creature")).to_lower()
	data["id"] = ""
	_distance = maxf(1.8, maxf(float(data.get("length", 1.5)), float(data.get("shoulder", 1.0)) * 1.3) * 1.9)


func _id() -> String:
	return "my_" + String(data["name"]).to_lower().validate_filename().replace(" ", "_").replace(",", "")


# Core told (so the view shows it, and it's offered): -1 if it isn't a creature yet (why, in the log)
func _apply() -> int:
	var shape: Dictionary = data.get("shape", {})
	data["horns"] = shape.get("horns", "none") != "none"
	var told := data.duplicate(true)
	told["id"] = _id() if data["id"] == "" else data["id"]
	return GeodriveHunt.add_species(told)


# Kept: its file in your workshop (creatures/); its id, or "" (and why, in the log)
func save() -> String:
	data["id"] = _id()
	if GeodriveHunt.add_species(data) < 0:
		return ""
	var folder := Packs.workshop().path_join("creatures")
	DirAccess.make_dir_recursive_absolute(folder)
	if not Creatures.save(data["id"], folder.path_join(data["id"] + ".json")):
		return ""
	builder.main.net.share_packs()
	return data["id"]


# --- its screen: the view on the left, its shape or what it does on the right ---

func fill_view(left: Container) -> void:
	var holder := SubViewportContainer.new()
	holder.custom_minimum_size = Vector2(460.0, 440.0)
	holder.stretch = true
	holder.gui_input.connect(_view_input)
	left.add_child(holder)
	_view = SubViewport.new()
	_view.own_world_3d = true
	_view.msaa_3d = Viewport.MSAA_4X
	holder.add_child(_view)
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.13, 0.14, 0.16)
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	environment.ambient_light_color = Color(0.55, 0.55, 0.6)
	var world := WorldEnvironment.new()
	world.environment = environment
	_view.add_child(world)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(-0.9, 0.6, 0.0)
	_view.add_child(sun)
	var ground := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(60.0, 60.0)
	ground.mesh = plane
	var grass := StandardMaterial3D.new()
	grass.albedo_color = Color(0.32, 0.38, 0.22)
	ground.material_override = grass
	_view.add_child(ground)
	_camera = Camera3D.new()
	_view.add_child(_camera)
	_model = Node3D.new()
	_view.add_child(_model)
	_note = Label.new()
	_note.modulate = Color(0.75, 0.95, 1.0)
	left.add_child(_note)
	_rebuild()


func _view_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and event.button_mask & MOUSE_BUTTON_MASK_LEFT:
		_yaw -= event.relative.x * 0.01
		_place_camera()
	elif event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		_distance *= 0.9 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.1
		_place_camera()


func _place_camera() -> void:
	var height := float(data.get("shoulder", 1.0)) * 0.7
	_camera.position = Vector3(sin(_yaw), 0.35, cos(_yaw)) * _distance + Vector3.UP * height
	_camera.look_at(Vector3.UP * height * 0.8)


# The view made again: Core's body for the creature as it is now
func _rebuild() -> void:
	if _model == null or not is_instance_valid(_model):
		return
	for child in _model.get_children():
		child.queue_free()
	var index := _apply()
	if index < 0:
		_note.text = "Not a creature yet: see the log for why (a run slower than its walk?)"
		return
	var round := SphereMesh.new()
	round.radius = 0.5
	round.height = 1.0
	round.radial_segments = 16
	round.rings = 8
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.5
	cone.height = 1.0
	cone.radial_segments = 8
	for part in GeodriveHunt.body_parts(_id() if data["id"] == "" else data["id"]):
		var mesh := MeshInstance3D.new()
		mesh.mesh = cone if part["horn"] else round
		mesh.transform = part["frame"]
		var paint := StandardMaterial3D.new()
		paint.albedo_color = part["colour"]
		paint.roughness = 0.85
		mesh.material_override = paint
		_model.add_child(mesh)
	_note.text = "%s: %d kg, %.2f m at the shoulder, %.1f m long   (drag to turn it, the wheel zooms)" % [
		data["name"], roundi(float(data.get("mass", 0.0))), float(data.get("shoulder", 0.0)), float(data.get("length", 0.0))]
	_place_camera()


func fill_shape(rows: VBoxContainer) -> void:
	var shape: Dictionary = data.get_or_add("shape", {})
	var upright: bool = shape.get("frame", "four_legs") == "upright"
	_choices(rows, "Body", [["four_legs", "Four legs"], ["upright", "Upright"]], shape.get("frame", "four_legs"), func(v): shape["frame"] = v)
	_slider(rows, "Weight", data, "mass", 5.0, 3000.0, 1.0, "%d kg")
	_slider(rows, "Shoulder", data, "shoulder", 0.2, 5.0, 0.01, "%.2f m")
	if not upright:
		_slider(rows, "Length", data, "length", 0.3, 5.0, 0.01, "%.2f m")
	_slider(rows, "Head carried", shape, "head_up", 0.6, 3.0, 0.01, "%.2f")
	if not upright:
		_slider(rows, "Back slope", shape, "back_slope", -0.3, 0.6, 0.01, "%.2f")
	_slider(rows, "Ears", shape, "ears", 0.0, 3.0, 0.05, "%.2f")
	var colour_row := _line(rows, "Colour")
	var picker := ColorPickerButton.new()
	picker.custom_minimum_size = Vector2(120.0, 30.0)
	picker.color = Color.html(String(data.get("colour", "#806040")))
	picker.popup_closed.connect(func():
		data["colour"] = "#" + picker.color.to_html(false)
		_rebuild())
	colour_row.add_child(picker)
	_choices(rows, "Coat", COATS.map(func(c): return [c, String(c).capitalize()]), shape.get("coat", "plain"), func(v): shape["coat"] = v)
	if not upright:
		_choices(rows, "Horns", HORNS.map(func(c): return [c, String(c).capitalize()]), shape.get("horns", "none"), func(v): shape["horns"] = v)
		if shape.get("horns", "none") != "none":
			if float(shape.get("horn_length", -1.0)) < 0.0:
				shape["horn_length"] = 0.4
			_slider(rows, "Horn length", shape, "horn_length", 0.05, 1.8, 0.01, "%.2f m")
		_choices(rows, "Mane", MANES.map(func(c): return [c, String(c).capitalize()]), shape.get("mane", "none"), func(v): shape["mane"] = v)
		_choices(rows, "Tail", [[false, "Short"], [true, "Long, a tuft"]], shape.get("long_tail", false), func(v): shape["long_tail"] = v)
	else:
		_choices(rows, "Arms", [[false, "Hanging"], [true, "Held out"]], shape.get("arms_forward", false), func(v): shape["arms_forward"] = v)


func fill_does(rows: VBoxContainer) -> void:
	var does: Dictionary = data.get_or_add("behaviour", {})
	_slider(rows, "Walks at", data, "walk", 0.2, 4.0, 0.1, "%.1f m/s")
	_slider(rows, "Runs at", data, "run", 0.5, 30.0, 0.5, "%.1f m/s")
	var herd: Array = data.get_or_add("herd", [1, 1])
	var sizes := {"fewest": herd[0], "most": herd[1]}
	_slider(rows, "Herd, fewest", sizes, "fewest", 1.0, 50.0, 1.0, "%d", func(): herd[0] = int(sizes["fewest"]); herd[1] = maxi(int(herd[1]), int(herd[0])))
	_slider(rows, "Herd, most", sizes, "most", 1.0, 100.0, 1.0, "%d", func(): herd[1] = maxi(int(sizes["most"]), int(herd[0])))
	_choices(rows, "Eats", DIETS, data.get("diet", "grazer"), func(v): data["diet"] = v)
	_slider(rows, "Hears you at", does, "hears", 0.0, 800.0, 10.0, "%d m")
	var toward_people: bool = does.get("goes_for_people", false) is Dictionary
	_choices(rows, "People", [[0, "Runs from them"], [1, "Goes for them"]], 1 if toward_people else 0, func(v):
		if v == 1:
			does["goes_for_people"] = {"within": 40, "speed": 1, "reach": 1, "gives_up": 120}
			does["flees"] = false
			does["prey"] = false
		else:
			does.erase("goes_for_people")
			does["flees"] = true)
	var hunter: bool = does.get("hunts", false) is Dictionary
	_choices(rows, "Hunts animals", [[false, "No"], [true, "Yes"]], hunter, func(v):
		does["hunts"] = {"every": [480, 900], "within": 800, "prey_up_to": 700, "rush_from": 40, "rush_for": 20, "rush_speed": 1.25, "feeds_for": [500, 700]} if v else false)


# --- building blocks (the builder's buttons) ---

func _line(rows: VBoxContainer, label: String) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	rows.add_child(row)
	row.add_child(builder._menu_label(label))
	return row


# Buttons, one lit: choosing one changes the creature (and the screen, as what's shown can change)
func _choices(rows: VBoxContainer, label: String, options: Array, now: Variant, set_to: Callable) -> void:
	var row := _line(rows, label)
	for option in options:
		var button: Button = builder._menu_button(option[1], option[0] == now, 0.0, 30.0)
		button.pressed.connect(func():
			set_to.call(option[0])
			_rebuild()
			builder._fill_menu())
		row.add_child(button)


func _slider(rows: VBoxContainer, label: String, into: Dictionary, key: String, low: float, high: float, step: float, shown: String, after: Callable = Callable()) -> void:
	var row := _line(rows, label)
	var slider := HSlider.new()
	slider.min_value = low
	slider.max_value = high
	slider.step = step
	slider.value = float(into.get(key, low))
	slider.custom_minimum_size = Vector2(260.0, 24.0)
	slider.focus_mode = Control.FOCUS_NONE
	var value := Label.new()
	value.text = shown % slider.value
	slider.value_changed.connect(func(v: float):
		into[key] = v
		if after.is_valid():
			after.call()
		value.text = shown % v
		_rebuild())	# (seen as it changes)
	row.add_child(slider)
	row.add_child(value)
