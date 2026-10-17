# Getting more of the world from inside the game: going somewhere no region file covers yet, the game offers to make
# it, then geotile (beside a packaged game's exe; in the editor, Tools/geotile) does in the background what
# "geotile region" does by hand: the smallest Geofabrik extract that covers the spot, downloaded and packed, its heights
# and far terrain fetched, into the game's data folder. Its lines are shown as it goes (the downloads with how far
# they've got). Once it's done the world takes up the new files and you go there. The game carries on meanwhile, and it
# can be stopped (a stopped one carries on where it was, next time).
extends CanvasLayer

signal ready_to_go(lat: float, lon: float, place_name: String)
signal ready_to_join(lat: float, lon: float, place_name: String)

var main: Node
var _panel: PanelContainer
var _title: Label
var _status: Label
var _bar: ProgressBar
var _yes: Button
var _no: Button
var _pid := -1
var _out: FileAccess
var _err: FileAccess
var _lines := ""
var _wanted := {}	# {lat, lon, name}
var _made := ""	# (what geotile made: its region file's name)
var _started := 0.0


func _ready() -> void:
	layer = 50
	_panel = PanelContainer.new()
	_panel.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM)
	_panel.offset_left = -330.0
	_panel.offset_right = 330.0
	_panel.offset_top = -260.0
	_panel.offset_bottom = -90.0
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.07, 0.1, 0.92)
	style.set_corner_radius_all(10)
	style.set_content_margin_all(18)
	_panel.add_theme_stylebox_override("panel", style)
	add_child(_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	_panel.add_child(column)
	_title = Label.new()
	_title.add_theme_font_size_override("font_size", 22)
	column.add_child(_title)
	_status = Label.new()
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status.add_theme_font_size_override("font_size", 16)
	_status.add_theme_color_override("font_color", Color(0.8, 0.85, 0.9))
	column.add_child(_status)
	_bar = ProgressBar.new()
	_bar.custom_minimum_size = Vector2(0.0, 18.0)
	column.add_child(_bar)
	var buttons := HBoxContainer.new()
	buttons.alignment = BoxContainer.ALIGNMENT_END
	buttons.add_theme_constant_override("separation", 10)
	column.add_child(buttons)
	_yes = Button.new()
	_yes.text = "Download it"
	_yes.pressed.connect(_start)
	buttons.add_child(_yes)
	_no = Button.new()
	_no.text = "Not now"
	_no.pressed.connect(_cancel)
	buttons.add_child(_no)
	_panel.visible = false


func busy() -> bool:
	return _pid >= 0


# Straight to getting it (chosen on the map or in the menu: no need to ask again)
func fetch(lat: float, lon: float, place_name: String) -> void:
	offer(lat, lon, place_name)
	if _yes.visible:
		_start()


# The places you have: {stem, name, mb, south, west, north, east} each (from their files, biggest first)
static func places() -> Array:
	var folder := GameSettings.cache_dir().path_join("regions")
	var out := []
	for file in DirAccess.get_files_at(folder):
		if not file.ends_with(".pmtiles"):
			continue
		var stem := file.get_basename()
		var bytes := 0
		for other in DirAccess.get_files_at(folder):
			if other.begins_with(stem + "."):
				bytes += FileAccess.get_file_as_bytes(folder.path_join(other)).size() if other.ends_with(".json") else _size(folder.path_join(other))
		var heights := GameSettings.cache_dir().path_join("heights/cop_%s.pmtiles" % stem)
		if FileAccess.file_exists(heights):
			bytes += _size(heights)
		var note = JSON.parse_string(FileAccess.get_file_as_string(folder.path_join(stem + ".region.json"))) if FileAccess.file_exists(folder.path_join(stem + ".region.json")) else null
		var name: String = note.get("name", "") if note is Dictionary else ""
		var entry := {"stem": stem, "name": name if name != "" else stem.replace("_", " ").capitalize(), "mb": bytes / 1048576.0}
		# (its bounds, from its archive's header: 1e-7 degrees at byte 102, min lon, min lat, max lon, max lat)
		var head := FileAccess.open(folder.path_join(file), FileAccess.READ)
		if head:
			var b := head.get_buffer(127)
			if b.size() >= 118:
				entry["west"] = b.decode_s32(102) / 1e7
				entry["south"] = b.decode_s32(106) / 1e7
				entry["east"] = b.decode_s32(110) / 1e7
				entry["north"] = b.decode_s32(114) / 1e7
		out.append(entry)
	out.sort_custom(func(a, b): return a["mb"] > b["mb"])
	return out


static func _size(path: String) -> int:
	var f := FileAccess.open(path, FileAccess.READ)
	return f.get_length() if f else 0


# A place deleted: its map, world map, airports, note and heights. One the game has open now (its files in use) goes
# when the game next starts (delete_on_start.txt)
func delete_place(stem: String) -> void:
	var folder := GameSettings.cache_dir().path_join("regions")
	var files := []
	for other in DirAccess.get_files_at(folder):
		if other.begins_with(stem + "."):
			files.append(folder.path_join(other))
	var heights := GameSettings.cache_dir().path_join("heights/cop_%s.pmtiles" % stem)
	if FileAccess.file_exists(heights):
		files.append(heights)
	var later := []
	for path in files:
		if DirAccess.remove_absolute(path) != OK:
			later.append(path)
	if not later.is_empty():
		var list := GameSettings.cache_dir().path_join("delete_on_start.txt")
		var old := FileAccess.get_file_as_string(list)
		var file := FileAccess.open(list, FileAccess.WRITE)
		file.store_string(old + "\n".join(later) + "\n")
	WorldMap.forget_regions()
	if main and main.aviation:
		main.aviation._load_airports()
		if main.game_gen:
			main.game_gen._read_regions()	# (its golf courses, race tracks, reserves: games there too)
	main.hud.set_status("%s deleted%s" % [stem.replace("_", " ").capitalize(), " (its files go when the game next starts: it's using them)" if not later.is_empty() else ""], 5.0)
	print("Geodrive: deleted %s (%d files, %d next start)" % [stem, files.size() - later.size(), later.size()])


# (at the start, before any map's opened: the places deleted last time)
static func finish_deletes() -> void:
	var list := GameSettings.cache_dir().path_join("delete_on_start.txt")
	if not FileAccess.file_exists(list):
		return
	for path in FileAccess.get_file_as_string(list).split("\n", false):
		DirAccess.remove_absolute(path.strip_edges())
	DirAccess.remove_absolute(list)


# Somewhere with no map yet: offer to make it
func offer(lat: float, lon: float, place_name: String, for_host := false) -> void:
	if busy():
		main.hud.set_status("Already getting %s" % _wanted["name"], 3.0)
		return
	_wanted = {"lat": lat, "lon": lon, "name": place_name, "host": for_host}
	_title.text = "No map of %s yet" % place_name
	_status.text = "Download it? The game fetches its OpenStreetMap map and its heights (Copernicus 30 m) and packs them: " \
		+ "about a minute for a small place, much longer for a big one. You can carry on playing meanwhile."
	_bar.visible = false
	_yes.visible = true
	_no.text = "Not now"
	_panel.visible = true
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func _tool() -> String:
	var beside := OS.get_executable_path().get_base_dir().path_join("geotile.exe")
	if OS.has_feature("template") and FileAccess.file_exists(beside):
		return beside
	return ProjectSettings.globalize_path("res://").path_join("../Tools/geotile/bin/geotile.exe").simplify_path()


func _start() -> void:
	var tool := _tool()
	if not FileAccess.file_exists(tool):
		_status.text = "geotile.exe isn't here (%s): it makes the maps" % tool
		return
	var args := ["region", "--at=%.6f,%.6f" % [_wanted["lat"], _wanted["lon"]], GameSettings.cache_dir()]
	var run: Dictionary = OS.execute_with_pipe(tool, args, false)
	if run.is_empty():
		_status.text = "Couldn't start geotile"
		return
	_pid = run["pid"]
	_out = run["stdio"]
	_err = run["stderr"]
	_lines = ""
	_made = ""
	_started = Time.get_ticks_msec() / 1000.0
	_title.text = "Getting %s" % _wanted["name"]
	_status.text = "Finding the map that covers it..."
	_bar.visible = true
	_bar.value = 0.0
	_yes.visible = false
	_no.text = "Stop"
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	print("Geodrive: getting %s: %s %s" % [_wanted["name"], tool, " ".join(args)])


func _cancel() -> void:
	if busy():
		OS.kill(_pid)
		_pid = -1
		main.hud.set_status("Stopped: it carries on where it was next time", 4.0)
	_panel.visible = false
	if main.walker or main.car:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	else:
		main.menu.open("main")	# (nowhere to be yet: back to the menu, to go somewhere else)


# (what geotile says: its steps on stdout ("[  41 s] the heights", "READY <name>"), curl's progress on stderr)
func _process(_delta: float) -> void:
	if not busy():
		return
	_lines += _read(_out)
	var progress := _read(_err)
	var percent := progress.rfind("%")
	if percent > 0:
		var start := progress.rfind(" ", percent)
		var value := progress.substr(start + 1, percent - start - 1)
		if value.is_valid_float():
			_bar.value = float(value)
	while _lines.contains("\n"):
		var line := _lines.get_slice("\n", 0).strip_edges()
		_lines = _lines.substr(_lines.find("\n") + 1)
		if line.begins_with("READY "):
			_made = line.substr(6)
		elif line.begins_with("["):
			_status.text = line.substr(line.find("]") + 1).strip_edges()
			_bar.value = 0.0
			print("Geodrive: getting %s: %s" % [_wanted["name"], line])
		elif line.begins_with("no extract") or line.begins_with("error"):
			_status.text = line
	if not OS.is_process_running(_pid):
		_pid = -1
		_lines += _read(_out)
		if _lines.contains("READY "):
			_made = _lines.substr(_lines.find("READY ") + 6).get_slice("\n", 0).strip_edges()
		if _made != "":
			_finished()
		else:
			_title.text = "Couldn't get %s" % _wanted["name"]
			_no.text = "Close"
			print("Geodrive: getting %s failed: %s" % [_wanted["name"], _status.text])


func _read(pipe: FileAccess) -> String:
	if pipe == null:
		return ""
	var bytes := PackedByteArray()
	while true:
		var chunk := pipe.get_buffer(4096)
		if chunk.is_empty():
			break
		bytes.append_array(chunk)
	return bytes.get_string_from_utf8()


# Made: the world takes up the new files (the airports too) and off you go
func _finished() -> void:
	var added: int = main.world.refresh_maps() if main.world else 0
	WorldMap.forget_regions()
	if main.aviation:
		main.aviation._load_airports()
		if main.game_gen:
			main.game_gen._read_regions()	# (its golf courses, race tracks, reserves: games there too)
	print("Geodrive: got %s (%s: %d new files) in %.0f s" % [_wanted["name"], _made, added, Time.get_ticks_msec() / 1000.0 - _started])
	_panel.visible = false
	main.hud.set_status("%s is ready: going there" % _wanted["name"], 4.0)
	if _wanted.get("host", false):
		ready_to_join.emit(_wanted["lat"], _wanted["lon"], _wanted["name"])	# (a friend's area: joined there)
	else:
		ready_to_go.emit(_wanted["lat"], _wanted["lon"], _wanted["name"])
