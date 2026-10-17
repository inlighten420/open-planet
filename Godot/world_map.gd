# The world map: the whole Earth as a map (Natural Earth's land and sea, ice, towns, lakes, rivers, borders and main
# roads, drawn ahead as pictures at 7 zoom levels: Tools/make_worldmap.py, worldmap.bin in the data folder), every
# place you have map data for drawn on it with its own roads and towns, and every part of the world you can get
# (Geofabrik's extracts, the downloads geotile makes regions of): hover one to see it, click to choose it, and the panel
# says whether you have it (Go there, Delete) or not (Download). Scroll to zoom about the mouse, drag to move.
# Smooth however it's moved: the pictures are tiles decoded on workers (a coarser one stands in till a finer one's in);
# your regions' roads are meshes on the graphics card made once (each road class's lines, in degrees), only moved and
# scaled with the map, so they're always there and always on the picture under them; the labels are drawn each frame,
# a grid keeping them apart. Drawn as Mercator (as web maps are): its shape never changes as it's dragged, only as it's
# zoomed; the pictures (made plain, a degree a degree) are drawn in strips bent to it.
# While it's open the game's 3D isn't drawn. Esc, Tab or M closes it.
class_name WorldMap
extends CanvasLayer

signal travel_requested(lat: float, lon: float, place_name: String)
signal download_requested(lat: float, lon: float, place_name: String)
signal delete_requested(stem: String)
signal closed

const KINDS := ["city", "town", "village", "suburb", "place"]
const PANEL := Color(0.02, 0.025, 0.03, 0.97)
const YOU := Color(0.1, 0.55, 0.95)
const SEA := Color8(166, 198, 222)
const HAVE := Color(0.15, 0.62, 0.3)
const CHOSEN := Color(0.95, 0.45, 0.1)
const TILE := 256
const LEVELS := 7
const REDRAW_AFTER := 0.15	# s still before the side list's made again
const MERCATOR_TOP := 180.0	# (85.05 degrees north, in Mercator's degrees: the map's top; its foot the same south)
const ROAD_COLOURS := [Color(0.92, 0.5, 0.12), Color(0.85, 0.62, 0.2), Color(0.55, 0.55, 0.52), Color(0.62, 0.62, 0.6)]

static var index: RefCounted = null	# GeodriveWorldIndex: your regions' towns and main roads (regions/*.world.json)
static var _loading_task := -1
static var _extracts: Array = []	# Geofabrik's: {id, name, rings [PackedVector2Array lon,lat], box Rect2, area}
static var _extracts_task := -1
static var _extracts_read: Array = []

# set by main before opening: where the car is and the area that's built
var has_car := false
var pause_game := true	# (not online: the game goes on)
var car_lat := 0.0
var car_lon := 0.0
var area_boxes := []	# the built pieces: each [south, west, north, east]
var booths := []	# the games' ticket booths (hunt.gd's booths(): name, lat, lon, state)

var centre_lat := -29.0
var centre_lon := 25.0
var pixels_per_degree := 60.0	# (a degree of longitude's pixels: the same all over the map)
var _user_moved := false
var _canvas: Control
var _overlay: Control	# (the labels, the car, the note)
var _roads: Array = []	# [MeshInstance2D, class, pixel offset]: your regions' roads, moved with the map
var _road_lines: Array = []	# (each class's lines, made Mercator on a worker)
var _road_task := -1
var _redraw_in := 0.0	# (the side list: made again once the map's still)
var _list: VBoxContainer
var _note: Label
var _quick: HFlowContainer
var _choice: VBoxContainer	# (the part of the world chosen: its name, whether you have it, its buttons)
var _search := ""
var _list_due := true
var _list_timer := 0.0
var _dragging := false
var _drag_start := Vector2.ZERO
var _last_mouse := Vector2.ZERO
var _regular: SystemFont
var _bold: SystemFont
var _waiting_for_index := false
var _places: Array = []	# the world's towns for labels: [name, lat, lon, rank, population, capital]
var _places_read: Array = []
var _places_task := -1
var _have := {}	# region file names (south_africa) you have
var _hover := -1	# the extract under the mouse
var _chosen := -1	# the one clicked
var _chosen_at := Vector2.ZERO	# (lat, lon) where

# the pictures: worldmap.bin's index, and the tiles decoded (key -> ImageTexture), being decoded, decoded waiting
var _tile_file: FileAccess
var _tile_index := {}	# key -> [offset, length]
var _tiles := {}
var _decoding := {}	# key -> its worker task (each waited for once done: a task never waited for crashes the game's exit)
var _decoded := {}
var _decoded_lock := Mutex.new()
var timings := {}	# (the slowest of each part of its work, ms: for the test)


func _timed(part: String, started: int) -> void:
	timings[part] = maxf(timings.get(part, 0.0), (Time.get_ticks_usec() - started) / 1000.0)


func _ready() -> void:
	layer = 60
	process_mode = Node.PROCESS_MODE_ALWAYS
	_regular = SystemFont.new()
	_bold = SystemFont.new()
	_bold.font_weight = 700
	var root := HBoxContainer.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_theme_constant_override("separation", 0)
	add_child(root)
	_canvas = Control.new()
	_canvas.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_canvas.clip_contents = true
	_canvas.focus_mode = Control.FOCUS_ALL
	_canvas.draw.connect(_draw_map)
	_canvas.gui_input.connect(_map_input)
	_canvas.resized.connect(_on_resized)
	root.add_child(_canvas)
	_overlay = Control.new()
	_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_overlay.draw.connect(_draw_overlay)
	_canvas.add_child(_overlay)

	var panel := PanelContainer.new()
	panel.custom_minimum_size = Vector2(440, 0)
	var style := StyleBoxFlat.new()
	style.bg_color = PANEL
	style.content_margin_left = 20
	style.content_margin_right = 20
	style.content_margin_top = 18
	style.content_margin_bottom = 18
	panel.add_theme_stylebox_override("panel", style)
	root.add_child(panel)
	panel.theme = GameMenu.button_theme()	# (the places in boxes, as the menu's buttons)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 8)
	panel.add_child(column)
	column.add_child(_text("WORLD MAP", 27, GameMenu.ORANGE, true))
	_note = _text("", 14, Color(0.7, 0.7, 0.7), false)
	_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_note)
	_choice = VBoxContainer.new()
	_choice.add_theme_constant_override("separation", 6)
	column.add_child(_choice)
	var search := LineEdit.new()
	search.placeholder_text = "Search for a town..."
	search.add_theme_font_size_override("font_size", 17)
	search.text_changed.connect(func(text: String):
		_search = text.strip_edges()
		_list_due = true)
	column.add_child(search)
	_quick = HFlowContainer.new()
	_quick.add_theme_constant_override("h_separation", 4)
	_quick.add_theme_constant_override("v_separation", 4)
	column.add_child(_quick)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	_list = VBoxContainer.new()
	_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_list.add_theme_constant_override("separation", 5)
	scroll.add_child(_list)
	var close_button := Button.new()
	close_button.text = "Close"
	close_button.add_theme_font_override("font", _bold)
	close_button.add_theme_font_size_override("font_size", 17)
	close_button.pressed.connect(close)
	column.add_child(close_button)
	visible = false


func _text(text: String, font_size: int, colour: Color, bold: bool) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", colour)
	label.add_theme_font_override("font", _bold if bold else _regular)
	return label


func is_open() -> bool:
	return visible


# Your regions changed (one made, one deleted): read again next time it opens
static func forget_regions() -> void:
	index = null


func open() -> void:
	visible = true
	get_tree().paused = pause_game
	get_viewport().disable_3d = true	# (the world behind isn't seen: not drawn)
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_user_moved = false
	_hover = -1
	_chosen = -1
	_show_choice()
	if has_car:
		# over the car, at a regional scale
		centre_lat = car_lat
		centre_lon = car_lon
		pixels_per_degree = 700.0 * _squeeze()
	else:
		centre_lat = 10.0
		centre_lon = 15.0
		pixels_per_degree = 4.0
	_open_pictures()
	_read_haves()
	if _places.is_empty() and _places_task < 0:
		# (the world's towns' names, read on a worker: 7,000 of them)
		var path := GameSettings.cache_dir().path_join("worldmap_places.json")
		_places_task = WorkerThreadPool.add_task(func():
			var parsed = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
			_places_read = parsed if parsed is Array else [])
	if index == null:
		index = ClassDB.instantiate("GeodriveWorldIndex")
		var folder := GameSettings.cache_dir().path_join("regions")
		var reading: RefCounted = index
		_loading_task = WorkerThreadPool.add_task(func(): reading.load(folder))
	_waiting_for_index = not index.is_loaded()
	_note.text = "Reading the world map..." if _waiting_for_index else _note_text()
	if not _waiting_for_index:
		_on_index_ready()
	_read_extracts()
	_canvas.grab_focus.call_deferred()
	_changed()


# Going (the game quitting): every worker task of the map's waited for, none left to the pool to clear up after it
func _exit_tree() -> void:
	for task in _decoding.values():
		WorkerThreadPool.wait_for_task_completion(task)
	_decoding.clear()
	for task in [_places_task, _road_task, _extracts_task, _loading_task]:
		if task >= 0:
			WorkerThreadPool.wait_for_task_completion(task)
	_places_task = -1
	_road_task = -1
	if _extracts_task >= 0:
		_extracts_task = -1
		_extracts = _extracts_read
	_loading_task = -1


func close() -> void:
	if not visible:
		return
	visible = false
	get_tree().paused = false
	get_viewport().disable_3d = false
	closed.emit()


func _note_text() -> String:
	if index.is_empty():
		return "You haven't any places yet. Click anywhere on the map and download it: the game fetches its roads and heights, then takes you there."
	return "Your places are outlined green. Click a town to go there, or anywhere to download that part of the world."


func _on_index_ready() -> void:
	_waiting_for_index = false
	_note.text = _note_text()
	if _loading_task >= 0:
		var t1 := Time.get_ticks_usec()
		WorkerThreadPool.wait_for_task_completion(_loading_task)
		_loading_task = -1
		_timed("index taken", t1)
		print("Geodrive: ", index.get_summary())
	if not has_car and not index.is_empty():
		_fit_all()
	_make_quick_buttons()
	_make_roads()
	_list_due = true
	_changed()


# Your regions' roads, a mesh a class (the big ones twice, a pixel apart: bolder), under the labels: their lines made
# Mercator on a worker, the meshes when it's done
func _make_roads() -> void:
	if _road_task >= 0:
		return
	var lines := []
	_road_lines = lines
	var reading: RefCounted = index
	_road_task = WorkerThreadPool.add_task(func():
		for road_class in 4:
			var points: PackedVector2Array = reading.road_lines(road_class)	# (longitude, -latitude)
			for i in points.size():
				points[i].y = -WorldMap.mercator(-points[i].y)
			lines.append(points))


func _make_road_meshes() -> void:
	for entry in _roads:
		entry[0].queue_free()
	_roads.clear()
	for road_class in [3, 2, 1, 0]:
		var lines: PackedVector2Array = _road_lines[road_class]
		if lines.is_empty():
			continue
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = lines
		var mesh := ArrayMesh.new()
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_LINES, arrays)
		for offset in ([Vector2.ZERO, Vector2(1, 0), Vector2(0, 1)] if road_class == 0 else ([Vector2.ZERO, Vector2(1, 0)] if road_class == 1 else [Vector2.ZERO])):
			var node := MeshInstance2D.new()
			node.mesh = mesh
			node.modulate = ROAD_COLOURS[road_class]
			_canvas.add_child(node)
			_canvas.move_child(node, _overlay.get_index())	# (under the labels)
			_roads.append([node, road_class, offset])
	_place_roads()


func _place_roads() -> void:
	var size := _canvas.size
	var most := 3 if pixels_per_degree > 400.0 else (2 if pixels_per_degree > 120.0 else (1 if pixels_per_degree > 30.0 else 0))
	var origin := Vector2(size.x * 0.5 - centre_lon * pixels_per_degree, size.y * 0.5 + mercator(centre_lat) * pixels_per_degree)
	for entry in _roads:
		var node: MeshInstance2D = entry[0]
		node.visible = entry[1] <= most and pixels_per_degree > 8.0
		node.transform = Transform2D(Vector2(pixels_per_degree, 0.0), Vector2(0.0, pixels_per_degree), origin + entry[2])


func _make_quick_buttons() -> void:
	for child in _quick.get_children():
		child.queue_free()
	_quick_button("Whole world", func():
		centre_lat = 10.0
		centre_lon = 15.0
		pixels_per_degree = maxf(_canvas.size.x / 380.0, 2.0)
		_user_moved = true
		_changed())
	if not index.is_empty():
		_quick_button("My places", func():
			_fit_all()
			_user_moved = true)
	if has_car:
		_quick_button("Where I am", _look_at.bind(car_lat, car_lon))
	for i in index.places_in(-90.0, -180.0, 90.0, 180.0, 0, 8):
		_quick_button(index.place_name(i), _look_at.bind(index.place_lat(i), index.place_lon(i)))


func _quick_button(label: String, action: Callable) -> void:
	var button := Button.new()
	button.text = label
	button.add_theme_font_override("font", _bold)
	button.add_theme_font_size_override("font_size", 14)
	button.pressed.connect(action)
	_quick.add_child(button)


func _look_at(lat: float, lon: float) -> void:
	centre_lat = lat
	centre_lon = lon
	pixels_per_degree = 2500.0 * _squeeze()
	_user_moved = true
	_changed()


func _fit_all() -> void:
	var b: PackedFloat64Array = index.get_bounds()
	if b[0] > b[2]:
		return
	var size := _canvas.size
	var south := mercator(b[0])
	var north := mercator(b[2])
	centre_lat = unmercator((south + north) * 0.5)
	centre_lon = (b[1] + b[3]) * 0.5
	pixels_per_degree = minf(size.y / maxf(north - south, 0.01), size.x / maxf(b[3] - b[1], 0.01)) * 0.92
	_changed()


func _changed() -> void:
	_list_due = true
	_redraw_in = REDRAW_AFTER
	_place_roads()
	_overlay.queue_redraw()
	_canvas.queue_redraw()


func _on_resized() -> void:
	# until you move it, the map keeps the whole region (or the car) in the middle as the window settles
	if visible and index and index.is_loaded() and not _user_moved and not has_car and not index.is_empty():
		_fit_all()
	_overlay.size = _canvas.size
	_place_roads()
	_overlay.queue_redraw()
	_canvas.queue_redraw()


# --- where things are on screen ---

# A latitude as Mercator's: in degrees, as far up the map as the equator's degrees are wide there (85.05 is 180)
static func mercator(lat: float) -> float:
	return rad_to_deg(log(tan(PI * 0.25 + deg_to_rad(clampf(lat, -85.05, 85.05)) * 0.5)))


static func unmercator(y: float) -> float:
	return rad_to_deg(2.0 * atan(exp(deg_to_rad(y))) - PI * 0.5)


# How much narrower a degree of longitude is than one of latitude, in the middle of the map
func _squeeze() -> float:
	return maxf(cos(deg_to_rad(centre_lat)), 0.05)


# A degree of latitude's pixels in the middle of the map (what the pictures need to be fine enough for)
func _local_scale() -> float:
	return pixels_per_degree / _squeeze()


func _to_screen(lat: float, lon: float) -> Vector2:
	return Vector2((lon - centre_lon) * pixels_per_degree + _canvas.size.x * 0.5, (mercator(centre_lat) - mercator(lat)) * pixels_per_degree + _canvas.size.y * 0.5)


func _to_lat_lon(at: Vector2) -> Vector2:
	return Vector2(unmercator(mercator(centre_lat) - (at.y - _canvas.size.y * 0.5) / pixels_per_degree), centre_lon + (at.x - _canvas.size.x * 0.5) / pixels_per_degree)


# Moved up (or down) the map by so many of Mercator's degrees
func _move_north(by: float) -> void:
	centre_lat = unmercator(clampf(mercator(centre_lat) + by, -MERCATOR_TOP, MERCATOR_TOP))


# The smallest place of yours shown at this zoom: cities always; towns, villages, suburbs and hamlets as you zoom in
func _max_rank() -> int:
	if pixels_per_degree > 6000.0:
		return 4
	if pixels_per_degree > 2200.0:
		return 3
	if pixels_per_degree > 600.0:
		return 2
	return 1 if pixels_per_degree > 140.0 else 0


# The box in view: south, west, north, east
func _in_view() -> PackedFloat64Array:
	var low := _to_lat_lon(Vector2(0.0, _canvas.size.y))
	var high := _to_lat_lon(Vector2(_canvas.size.x, 0.0))
	return PackedFloat64Array([low.x, low.y, high.x, high.y])


func _process(delta: float) -> void:
	if not visible:
		return
	if _waiting_for_index and index.is_loaded():
		_on_index_ready()
	if _places_task >= 0 and WorkerThreadPool.is_task_completed(_places_task):
		WorkerThreadPool.wait_for_task_completion(_places_task)
		_places_task = -1
		_places = _places_read
		_overlay.queue_redraw()
	if _road_task >= 0 and WorkerThreadPool.is_task_completed(_road_task):
		var t2 := Time.get_ticks_usec()
		WorkerThreadPool.wait_for_task_completion(_road_task)
		_road_task = -1
		_make_road_meshes()
		_timed("roads made", t2)
	if _extracts_task >= 0 and WorkerThreadPool.is_task_completed(_extracts_task):
		var t0 := Time.get_ticks_usec()
		WorkerThreadPool.wait_for_task_completion(_extracts_task)
		_extracts_task = -1
		_extracts = _extracts_read
		_timed("extracts taken", t0)
		print("Geodrive: the world map: %d parts of the world to download" % _extracts.size())
		_canvas.queue_redraw()
	# tiles decoded on workers: made textures, a dozen a frame at most (~0.15 ms each)
	_decoded_lock.lock()
	var ready := {}
	for key in _decoded.keys().slice(0, 12):
		ready[key] = _decoded[key]
		_decoded.erase(key)
	_decoded_lock.unlock()
	var t0 := Time.get_ticks_usec()
	for key in ready:
		_tiles[key] = ImageTexture.create_from_image(ready[key])
		WorkerThreadPool.wait_for_task_completion(_decoding[key])	# (done: at once)
		_decoding.erase(key)
	_timed("textures", t0)
	if not ready.is_empty():
		_canvas.queue_redraw()
	if _redraw_in > 0.0:
		_redraw_in -= delta
	# the list follows the map once it's still (making its buttons takes a while)
	_list_timer -= delta
	if _list_due and _list_timer <= 0.0 and _redraw_in <= 0.0 and index.is_loaded():
		_list_due = false
		_list_timer = 0.25
		var t1 := Time.get_ticks_usec()
		_rebuild_list()
		_timed("list", t1)


# --- the pictures ---

func _open_pictures() -> void:
	if _tile_file:
		return
	var path := GameSettings.cache_dir().path_join("worldmap.bin")
	_tile_file = FileAccess.open(path, FileAccess.READ)
	if _tile_file == null:
		print("Geodrive: no world map pictures (%s): Tools/make_worldmap.py makes them" % path)
		return
	if _tile_file.get_buffer(4).get_string_from_ascii() != "GDWM":
		_tile_file = null
		return
	var count := _tile_file.get_32()
	var table := _tile_file.get_buffer(count * 16)
	for i in count:
		_tile_index[table.decode_s32(i * 16)] = [table.decode_s64(i * 16 + 4), table.decode_s32(i * 16 + 12)]


func _tile(level: int, x: int, y: int) -> Texture2D:
	var key := (level << 24) | (x << 12) | y
	if _tiles.has(key):
		return _tiles[key]
	if not _tile_index.has(key) or _decoding.has(key) or _tile_file == null:
		return null
	# (its bytes read here, a quick seek; decoded on a worker)
	var where: Array = _tile_index[key]
	_tile_file.seek(where[0])
	var bytes := _tile_file.get_buffer(where[1])
	_decoding[key] = WorkerThreadPool.add_task(func():
		var image := Image.new()
		image.load_webp_from_buffer(bytes)
		_decoded_lock.lock()
		_decoded[key] = image
		_decoded_lock.unlock())
	return null


func _draw_tiles() -> void:
	# (the level whose pixels are at least as fine as the screen's; drawn over the next coarser, which stands in)
	var wanted := clampi(int(ceil(log(maxf(_local_scale(), 1.0) * 360.0 / 512.0) / log(2.0))), 0, LEVELS - 1)
	for level: int in [maxi(wanted - 2, 0), maxi(wanted - 1, 0), wanted]:
		if level == wanted - 2 and wanted < 2:
			continue
		var cols := 1 << (level + 1)
		var rows := 1 << level
		var ppd := TILE * cols / 360.0
		var view := _in_view()
		var x0 := clampi(int(floor((view[1] + 180.0) * ppd / TILE)), 0, cols - 1)
		var x1 := clampi(int(floor((view[3] + 180.0) * ppd / TILE)), 0, cols - 1)
		var y0 := clampi(int(floor((90.0 - view[2]) * ppd / TILE)), 0, rows - 1)
		var y1 := clampi(int(floor((90.0 - view[0]) * ppd / TILE)), 0, rows - 1)
		for y in range(y0, y1 + 1):
			for x in range(x0, x1 + 1):
				var texture := _tile(level, x, y)
				if texture == null:
					continue
				var west := -180.0 + x * TILE / ppd
				var north := 90.0 - y * TILE / ppd
				# (in strips a couple of degrees tall, each stretched to Mercator: a picture's plain, a degree a degree)
				var strips := clampi(int(ceil(TILE / ppd / 2.0)), 1, 64)
				var left := _to_screen(0.0, west).x
				var right := _to_screen(0.0, west + TILE / ppd).x
				for s in strips:
					var top_lat := north - s * TILE / ppd / strips
					var bottom_lat := north - (s + 1) * TILE / ppd / strips
					if top_lat < -85.06 or bottom_lat > 85.06:
						continue
					var top := _to_screen(top_lat, west).y
					var bottom := _to_screen(bottom_lat, west).y
					if bottom < 0.0 or top > _canvas.size.y:
						continue
					_canvas.draw_texture_rect_region(texture, Rect2(left, top, right - left + 0.6, bottom - top + 0.6),
						Rect2(0.0, float(TILE) * s / strips, TILE, float(TILE) / strips))


# --- the parts of the world to get (Geofabrik's extracts) ---

func _read_haves() -> void:
	_have.clear()
	var folder := GameSettings.cache_dir().path_join("regions")
	for file in DirAccess.get_files_at(folder):
		if file.ends_with(".pmtiles"):
			_have[file.get_basename()] = true


static func stem_of(id: String) -> String:
	return id.replace("-", "_").replace("/", "_")


func _read_extracts() -> void:
	if not _extracts.is_empty() or _extracts_task >= 0:
		return
	var path := GameSettings.cache_dir().path_join("downloads/geofabrik-index-outlines.json")
	if not FileAccess.file_exists(path):
		# (fetched once, as geotile does: Geofabrik's list with each extract's outline, 3.8 MB)
		DirAccess.make_dir_recursive_absolute(path.get_base_dir())
		var request := HTTPRequest.new()
		request.download_file = path
		add_child(request)
		request.request_completed.connect(func(result: int, _code: int, _headers: PackedStringArray, _body: PackedByteArray):
			request.queue_free()
			if result == HTTPRequest.RESULT_SUCCESS:
				_read_extracts())
		request.request("https://download.geofabrik.de/index-v1.json")
		return
	_extracts_task = WorkerThreadPool.add_task(func():
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
		var out: Array = []
		if parsed is Dictionary:
			for feature in parsed.get("features", []):
				var geometry = feature.get("geometry")
				if not geometry is Dictionary:
					continue
				var polygons: Array = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
				var rings: Array = []
				var low := Vector2(INF, INF)
				var high := Vector2(-INF, -INF)
				for polygon in polygons:
					var outer: Array = polygon[0]
					var step := maxi(1, outer.size() / 400)	# (at most ~400 points a ring: an outline, not a survey)
					var ring := PackedVector2Array()
					for k in range(0, outer.size(), step):
						var p := Vector2(outer[k][0], outer[k][1])
						ring.append(p)
						low = low.min(p)
						high = high.max(p)
					rings.append(ring)
				var properties: Dictionary = feature["properties"]
				out.append({"id": properties["id"], "name": properties.get("name", properties["id"]), "rings": rings,
					"box": Rect2(low, high - low), "area": (high.x - low.x) * (high.y - low.y)})
		_extracts_read = out)


# The smallest extract whose outline holds a spot (lat, lon): -1 if none (the sea)
func _extract_at(lat: float, lon: float) -> int:
	var best := -1
	var smallest := INF
	var point := Vector2(lon, lat)
	for i in _extracts.size():
		var e: Dictionary = _extracts[i]
		if e["area"] >= smallest or not (e["box"] as Rect2).grow(0.0001).has_point(point):
			continue
		for ring in e["rings"]:
			if Geometry2D.is_point_in_polygon(point, ring):
				best = i
				smallest = e["area"]
				break
	return best


func _draw_extract(i: int, colour: Color, width: float) -> void:
	for ring in _extracts[i]["rings"]:
		var points := PackedVector2Array()
		for p in ring:
			points.append(_to_screen(p.y, p.x))
		points.append(points[0])
		_canvas.draw_polyline(points, colour, width, true)


# The panel's part for the part of the world chosen: its name; Go there and Delete if you have it, else Download
func _show_choice() -> void:
	for child in _choice.get_children():
		child.queue_free()
	if _chosen < 0:
		return
	var e: Dictionary = _extracts[_chosen]
	var stem := stem_of(e["id"])
	var have := _have.has(stem)
	_choice.add_child(_text(e["name"], 20, CHOSEN, true))
	_choice.add_child(_text("You have it" if have else "Not downloaded yet", 14, Color(0.6, 0.9, 0.6) if have else Color(0.75, 0.75, 0.75), false))
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	_choice.add_child(row)
	var spot: Vector2 = _chosen_at
	if have:
		_choice_button(row, "Go there", func():
			travel_requested.emit(spot.x, spot.y, e["name"])
			close())
		_choice_button(row, "Delete", func():
			delete_requested.emit(stem)
			_have.erase(stem)
			_show_choice())
	else:
		_choice_button(row, "Download", func():
			download_requested.emit(spot.x, spot.y, e["name"])
			close())


func _choice_button(row: HBoxContainer, label: String, action: Callable) -> void:
	var button := Button.new()
	button.text = label
	button.add_theme_font_override("font", _bold)
	button.add_theme_font_size_override("font_size", 16)
	button.pressed.connect(action)
	row.add_child(button)


# --- drawing ---

func _draw_map() -> void:
	var t0 := Time.get_ticks_usec()
	_draw_map_parts()
	_timed("map", t0)


func _draw_map_parts() -> void:
	var size := _canvas.size
	_canvas.draw_rect(Rect2(Vector2.ZERO, size), SEA)
	_draw_tiles()
	# (past the pictures' finest (91 px a degree, stretched), faded into plain land: your own roads drawn over it carry it)
	var fade := clampf((_local_scale() - 180.0) / 700.0, 0.0, 0.8)
	if fade > 0.0:
		_canvas.draw_rect(Rect2(Vector2.ZERO, size), Color(Color8(238, 235, 226), fade))
	# your places outlined, the one under the mouse and the one chosen
	for i in _extracts.size():
		if _have.has(stem_of(_extracts[i]["id"])):
			_draw_extract(i, HAVE, 2.5)
	if _hover >= 0 and _hover != _chosen:
		_draw_extract(_hover, Color(0.1, 0.1, 0.15, 0.7), 1.5)
	if _chosen >= 0:
		_draw_extract(_chosen, CHOSEN, 3.0)
	for box in area_boxes:
		var a := _to_screen(box[2], box[1])
		var b := _to_screen(box[0], box[3])
		_canvas.draw_rect(Rect2(a, b - a), Color(0.1, 0.45, 0.9, 0.9), false, 1.5)


func _draw_overlay() -> void:
	var t0 := Time.get_ticks_usec()
	_draw_overlay_parts()
	_timed("overlay", t0)


func _draw_overlay_parts() -> void:
	var size := _canvas.size
	if index == null or not index.is_loaded():
		return
	var taken := {}	# (the screen in 48 x 16 px cells: a label only where its cells are free)
	# your regions' places, then the world's towns where yours have none
	var view := _in_view()
	if pixels_per_degree > 60.0:
		for i in index.places_in(view[0], view[1], view[2], view[3], _max_rank(), 500):
			var rank: int = index.place_rank(i)
			_label(index.place_name(i), _to_screen(index.place_lat(i), index.place_lon(i)), rank, taken)
	var most := 1 if pixels_per_degree < 3.0 else (2 if pixels_per_degree < 6.0 else (3 if pixels_per_degree < 12.0 else (4 if pixels_per_degree < 25.0 else (5 if pixels_per_degree < 50.0 else (6 if pixels_per_degree < 100.0 else (7 if pixels_per_degree < 250.0 else 10))))))
	for p in _places:
		if p.size() < 6 or p[3] > most:
			break	# (biggest first: the rest are smaller still)
		if p[1] < view[0] or p[1] > view[2] or p[2] < view[1] or p[2] > view[3]:
			continue
		_label(p[0], _to_screen(p[1], p[2]), 0 if p[5] == 1 else (1 if p[3] <= 3 else 2), taken)
	# the games' booths: a flag, the game's name, and whether you're playing it (over the towns' names)
	for booth in booths:
		var at := _to_screen(booth["lat"], booth["lon"])
		if at.x < -100.0 or at.y < -40.0 or at.x > size.x + 100.0 or at.y > size.y + 40.0:
			continue
		var colour := Color(0.1, 0.6, 0.2) if booth["state"] == "playing" else (Color(0.85, 0.6, 0.0) if booth["state"] == "ticket" else Color(0.9, 0.35, 0.05))
		_overlay.draw_line(at, at + Vector2(0.0, -22.0), Color(0.15, 0.15, 0.15), 2.5)
		_overlay.draw_colored_polygon(PackedVector2Array([at + Vector2(0.0, -22.0), at + Vector2(15.0, -17.0), at + Vector2(0.0, -12.0)]), colour)
		var label: String = booth["name"] + {"playing": "  (playing)", "ticket": "  (your ticket)", "tickets": "  (tickets)"}[booth["state"]]
		_overlay.draw_string_outline(_bold, at + Vector2(18.0, -12.0), label, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, 4, Color(1, 1, 1, 0.9))
		_overlay.draw_string(_bold, at + Vector2(18.0, -12.0), label, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, colour)
	if has_car:
		var car := _to_screen(car_lat, car_lon)
		_overlay.draw_circle(car, 7.0, Color.WHITE)
		_overlay.draw_circle(car, 5.0, YOU)
		_overlay.draw_string(_bold, car + Vector2(10, -10), "You", HORIZONTAL_ALIGNMENT_LEFT, -1, 15, Color(0.05, 0.25, 0.5))
	_overlay.draw_string(_regular, Vector2(14, size.y - 14),
		"Scroll to zoom, drag to move. Click a town to go there, anywhere else to choose that part of the world.  Esc closes.   Natural Earth; map data (c) OpenStreetMap contributors",
		HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color(0.15, 0.2, 0.25))


func _label(label: String, at: Vector2, rank: int, taken: Dictionary) -> void:
	if at.x < -50.0 or at.y < -20.0 or at.x > _canvas.size.x or at.y > _canvas.size.y + 20.0 or taken.size() > 900:
		return
	var font: Font = _bold if rank <= 1 else _regular
	var font_size := 16 if rank == 0 else (14 if rank == 1 else 12)
	var text_size := font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size)
	var box := Rect2(at + Vector2(-4.0, -text_size.y * 0.5), text_size + Vector2(12.0, 0.0))
	var cells := []
	for cy in range(int(floor(box.position.y / 16.0)), int(floor(box.end.y / 16.0)) + 1):
		for cx in range(int(floor(box.position.x / 48.0)), int(floor(box.end.x / 48.0)) + 1):
			var cell := Vector2i(cx, cy)
			if taken.has(cell):
				return
			cells.append(cell)
	for cell in cells:
		taken[cell] = true
	var dot := 6.0 if rank == 0 else (4.5 if rank == 1 else 3.0)
	_overlay.draw_rect(Rect2(at - Vector2(dot, dot) * 0.5, Vector2(dot, dot)), Color(0.2, 0.2, 0.25))
	var baseline := at + Vector2(6.0, font.get_ascent(font_size) - text_size.y * 0.5)
	_overlay.draw_string_outline(font, baseline, label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, 4, Color(1, 1, 1, 0.85))
	_overlay.draw_string(font, baseline, label, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0.12, 0.12, 0.16))


# --- the mouse and keys ---

func _map_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		# zoom about the mouse: the place under it stays put
		var before := _to_lat_lon(event.position)
		pixels_per_degree = clampf(pixels_per_degree * (1.3 if event.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.3), 1.5, 60000.0)
		var after := _to_lat_lon(event.position)
		_move_north(mercator(before.x) - mercator(after.x))
		centre_lon += before.y - after.y
		_user_moved = true
		_changed()
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_dragging = true
			_drag_start = event.position
			_last_mouse = event.position
		elif _dragging:
			_dragging = false
			if event.position.distance_to(_drag_start) < 5.0:
				_click(event.position)
	elif event is InputEventMouseMotion:
		if _dragging:
			centre_lon -= (event.position.x - _last_mouse.x) / pixels_per_degree
			_move_north((event.position.y - _last_mouse.y) / pixels_per_degree)
			_last_mouse = event.position
			_user_moved = true
			_changed()
		elif not _extracts.is_empty():
			var at := _to_lat_lon(event.position)
			var under := _extract_at(at.x, at.y)
			if under != _hover:
				_hover = under
				_canvas.queue_redraw()


# A click, not a drag: a town of yours within reach goes there; anywhere else chooses that part of the world
func _click(at: Vector2) -> void:
	if index and index.is_loaded() and pixels_per_degree > 60.0:
		var view := _in_view()
		var best := -1
		var best_distance := 16.0
		for i in index.places_in(view[0], view[1], view[2], view[3], _max_rank(), 3000):
			var distance := _to_screen(index.place_lat(i), index.place_lon(i)).distance_to(at)
			if distance < best_distance:
				best_distance = distance
				best = i
		if best >= 0:
			_travel(best)
			return
	var spot := _to_lat_lon(at)
	_chosen = _extract_at(spot.x, spot.y)
	_chosen_at = spot
	_show_choice()
	_canvas.queue_redraw()


func _unhandled_input(event: InputEvent) -> void:
	if visible and event is InputEventKey and event.pressed and not event.echo and event.keycode in [KEY_ESCAPE, KEY_TAB, KEY_M]:
		get_viewport().set_input_as_handled()
		close()


func _travel(i: int) -> void:
	var lat: float = index.place_lat(i)
	var lon: float = index.place_lon(i)
	var place_name: String = index.place_name(i)
	travel_requested.emit(lat, lon, place_name)
	close()


# --- the side list: the places in view (a little past the labels shown), or matching the search ---

func _rebuild_list() -> void:
	for child in _list.get_children():
		_list.remove_child(child)
		child.queue_free()
	var found: PackedInt32Array
	if _search != "":
		found = index.search(_search, 60)
	elif pixels_per_degree > 60.0:
		var view := _in_view()
		found = index.places_in(view[0], view[1], view[2], view[3], mini(_max_rank() + 1, 4), 60)
	for i in found:
		var rank: int = index.place_rank(i)
		var population: int = index.place_population(i)
		var kind: String = KINDS[clampi(rank, 0, 4)]
		var detail := ("%s, %s people" % [kind, _thousands(population)]) if population > 0 else kind
		var button := Button.new()
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.text = index.place_name(i) + "\n" + detail
		button.add_theme_font_override("font", _bold if rank <= 1 else _regular)
		button.add_theme_font_size_override("font_size", 15 if rank <= 1 else 14)
		button.pressed.connect(_travel.bind(i))
		_list.add_child(button)
	if found.is_empty() and not index.is_empty():
		var none := _text("Zoom in on one of your places to see its towns." if _search == "" else "No place of yours by that name.", 14, Color(0.7, 0.7, 0.7), false)
		none.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		_list.add_child(none)


func _thousands(number: int) -> String:
	var digits := str(number)
	var out := ""
	while digits.length() > 3:
		out = "," + digits.right(3) + out
		digits = digits.left(digits.length() - 3)
	return digits + out
