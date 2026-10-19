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
var _failure := ""	# (why it stopped, in geotile's words)
var _step := ""	# (the step it's on, and how far in it)
var _ready_to_play := false	# (a download that wasn't to play is done: the panel's button says Play there)
var _curl_started := false	# (this step's download has shown a real reading, not just the redirect's 100%)
var _detail := ""
var _warning := ""	# (an optional part that missed: the map's made without it)
var _range := Vector2.ZERO	# (the slice of the bar the step it's on has)
var _building := false	# (past the download: on to what's made from it)
var _handover := 0.0	# (when it was made: step 3 shows until the loading screen takes over)
var _shown := -1	# (the second last shown)


func _ready() -> void:
	layer = 70	# (over the world map, so a download started there shows on it; it keeps going while the map pauses the game)
	process_mode = Node.PROCESS_MODE_ALWAYS
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
	_yes.pressed.connect(func():
		if _ready_to_play:
			_ready_to_play = false
			_yes.visible = false
			_panel.visible = false
			ready_to_go.emit(_wanted["lat"], _wanted["lon"], _wanted["name"])
			return
		_start()
		_yes.text = "Download it")
	buttons.add_child(_yes)
	_no = Button.new()
	_no.text = "Not now"
	_no.pressed.connect(_cancel)
	buttons.add_child(_no)
	_panel.visible = false


func busy() -> bool:
	return _pid >= 0


# Stops geotile and everything it started: it runs curl through cmd, and killing only geotile left the download going on its
# own (a Windows process's children don't die with it), so the whole tree is killed
func _kill_download() -> void:
	if _pid < 0:
		return
	if OS.get_name() == "Windows":
		OS.execute("taskkill", ["/PID", str(_pid), "/T", "/F"])
	else:
		OS.kill(_pid)
	_pid = -1


# The game closing with a download going: it stops with it, not left running on its own
func _exit_tree() -> void:
	_kill_download()


# Straight to getting it (chosen on the map or in the menu: no need to ask again)
func fetch(lat: float, lon: float, place_name: String, km: float = 0.0, play: bool = true) -> void:
	offer(lat, lon, place_name)
	if _yes.visible:
		_wanted["play"] = play	# (false: just download it; Play there is offered when it's done, and nothing moves you)
		_wanted["km"] = km	# (above 0: just that far round the spot, from OpenStreetMap's own servers: a town, not a whole region)
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
	_ready_to_play = false
	_yes.text = "Download it"
	_wanted = {"lat": lat, "lon": lon, "name": place_name, "host": for_host, "km": 0.0, "play": true}
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
	if _wanted.get("km", 0.0) > 0.0:
		args.append_array(["--km=%d" % roundi(_wanted["km"]), "--name=" + str(_wanted["name"])])
	var run: Dictionary = OS.execute_with_pipe(tool, args, false)
	if run.is_empty():
		_status.text = "Couldn't start geotile"
		return
	_pid = run["pid"]
	_out = run["stdio"]
	_err = run["stderr"]
	_lines = ""
	_failure = ""
	_step = ""
	_detail = ""
	_warning = ""
	_building = false
	_handover = 0.0
	_shown = -1
	_made = ""
	_started = Time.get_ticks_msec() / 1000.0
	_title.text = "Getting %s" % _wanted["name"]
	_status.text = "Finding the map that covers it..."
	_bar.visible = true
	_bar.value = 0.0
	_range = Vector2.ZERO
	_yes.visible = false
	_no.text = "Stop"
	# (the mouse is taken to play on meanwhile only if there's a game to play: on the front menu it's needed for Stop)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if (main.car or main.walker) and not main.world_map.visible else Input.MOUSE_MODE_VISIBLE
	print("Geodrive: getting %s: %s %s" % [_wanted["name"], tool, " ".join(args)])


func _cancel() -> void:
	if busy():
		_kill_download()
		main.hud.set_status("Stopped: it carries on where it was next time", 4.0)
	_panel.visible = false
	if main.world_map.visible:
		return	# (the map's still open: you carry on there)
	if main.walker or main.car:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	else:
		main.menu.open("main")	# (nowhere to be yet: back to the menu, to go somewhere else)


# (what geotile says: its steps on stdout ("[  41 s] the heights", "READY <name>"), curl's progress on stderr)
func _process(_delta: float) -> void:
	if _handover > 0.0:
		# (the loading screen is over it, or a few seconds on if there's none: the panel goes)
		var covered: bool = "loading" in main and main.loading != null and main.loading.loading
		if covered or Time.get_ticks_msec() / 1000.0 - _handover > 6.0:
			_handover = 0.0
			_panel.visible = false
	if not busy():
		return
	_lines += _read(_out)
	# (stderr: curl's bar redraws with \r, its last "NN.N%" is how far; and geotile's failures, "error: ...", shown if it
	# stops. Each bit read is looked at once, so an old 100% doesn't hold the bar up through the next step)
	var fresh := _read(_err)
	# (every percent in it, in order: curl first draws a full "100.0%" bar for the redirect to the real file, then starts at 0%.
	# A 100% only counts once a real reading below it was seen in this step, or the bar would stick at the end at once)
	var percent := fresh.find("%")
	while percent > 0:
		var start := maxi(fresh.rfind(" ", percent), fresh.rfind("\r", percent))
		var value := fresh.substr(start + 1, percent - start - 1)
		if value.is_valid_float():
			var fraction := float(value) / 100.0
			if fraction < 0.9995:
				_curl_started = true
			if _curl_started:
				_progress(fraction)
				if _step.begins_with("downloading"):
					_detail = "Downloaded %.0f%% (it depends on your internet and Geofabrik's speed)" % (fraction * 100.0)
		percent = fresh.find("%", percent + 1)
	var error_at := fresh.find("error")
	if error_at >= 0:
		_failure = fresh.substr(error_at).get_slice("\n", 0).strip_edges()
	while _lines.contains("\n"):
		var line := _lines.get_slice("\n", 0).strip_edges()
		_lines = _lines.substr(_lines.find("\n") + 1)
		_take(line)
	var now := int(Time.get_ticks_msec() / 1000.0 - _started)
	if now != _shown:
		_shown = now
		_show()
	if not OS.is_process_running(_pid):
		_pid = -1
		_lines += _read(_out) + "\n"
		var last := _read(_err)
		var error_end := last.find("error")
		if error_end >= 0:
			_failure = last.substr(error_end).get_slice("\n", 0).strip_edges()
		while _lines.contains("\n"):
			var line := _lines.get_slice("\n", 0).strip_edges()
			_lines = _lines.substr(_lines.find("\n") + 1)
			_take(line)
		if _made != "":
			_finished()
		else:
			_title.text = "Couldn't get %s" % _wanted["name"]
			_status.text = "%s\n\nTry again in a while (check the internet). What was already downloaded is kept: it carries on where it was." \
				% (_failure if _failure != "" else "It stopped at: %s" % _step)
			_bar.visible = false
			if _wanted.get("km", 0.0) > 0.0:
				# (a small area comes from OpenStreetMap's public servers, which are sometimes busy: the whole region's a surer, bigger download)
				_status.text += "

Or get the whole region this town is in instead (a bigger download, from a different server)."
				_yes.text = "Get the whole region"
				_yes.visible = true
				_wanted["km"] = 0.0
			_no.text = "Close"
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE	# (a button to click: the mouse was captured for playing on meanwhile)
			print("Geodrive: getting %s failed: %s" % [_wanted["name"], _status.text.replace("\n", " ")])


# One line of what geotile says: a step ("[  41 s] the heights"), how far in it ("1 of 9: ... 36s"), what's made, a failure
func _take(line: String) -> void:
	if line.begins_with("READY "):
		_made = line.substr(6)
	elif line.begins_with("["):
		_step = line.substr(line.find("]") + 1).strip_edges()
		if _step.begins_with("packing"):
			_building = true	# (from here on it's making what's kept; a quick step can come and go between two seconds' looks)
			_bar.value = 0.0	# (the bar starts again for the second step, and from then on only moves forward)
		_detail = ""
		_curl_started = false
		# (the far terrain is the second part of the heights)
		if _step.begins_with("the heights"):
			_step += " (part 1 of 2)"
		elif _step.begins_with("the far terrain"):
			_step += " (part 2 of 2)"
		_set_range(_step)
		print("Geodrive: getting %s: %s" % [_wanted["name"], line])
	elif line.begins_with("no extract") or line.begins_with("error"):
		_failure = line
	elif line.begins_with("warning"):
		_warning = line.get_slice(":", 1).strip_edges()	# (something optional missed: the map's still made)
		print("Geodrive: getting %s: %s" % [_wanted["name"], line])
	elif line.begins_with("downloaded "):
		# ("downloaded 11.6 MB": a small area's map coming from OpenStreetMap's server, which has no total to give; the bar
		# creeps towards the end as it comes, never reaching it)
		var mb := line.substr(11).to_float()
		_progress(mb / (mb + 12.0) * 0.9)
		_detail = "Waiting for OpenStreetMap's server to make the area's map..." if mb < 0.05 else "Downloaded %.1f MB so far" % mb
	elif line.contains("% (") and line.get_slice("%", 0).is_valid_int():
		# ("37% (reading the map)": geotile's own count of the chunks it has done out of all there are)
		var percent := line.get_slice("%", 0).to_int()
		var what := line.get_slice("(", 1).trim_suffix(")")
		_progress(percent / 100.0)
		_detail = "%s%s: %d%%" % [what.substr(0, 1).to_upper(), what.substr(1), percent]
	else:
		# ("  1 of 9 (0 failed), 2s, about 0 min to go": the bar follows it)
		var of := line.find(" of ")
		if of > 0:
			var done := line.substr(0, of).strip_edges().get_slice(" ", 0)
			var total := line.substr(of + 4).strip_edges().get_slice(" ", 0).trim_suffix(":").trim_suffix(",")
			if done.is_valid_int() and total.is_valid_int() and int(total) > 0:
				_progress(float(done) / float(total))
				_detail = line.strip_edges()


# Each step of geotile's owns a slice of its step's bar (the first step's, then the second's), so the bar only goes
# forward through them: it doesn't start again at each download, which looked like going back, or stuck
func _set_range(step: String) -> void:
	var slices := {
		"fetching": Vector2(0.0, 10.0), "downloading": Vector2(10.0, 100.0), "the extract:": Vector2(100.0, 100.0),
		"packing": Vector2(0.0, 30.0), "the world map": Vector2(30.0, 35.0), "the airports": Vector2(35.0, 40.0),
		"the places for games": Vector2(40.0, 45.0), "the heights": Vector2(45.0, 80.0),
		"the far terrain": Vector2(80.0, 100.0), "the extract's packed": Vector2(100.0, 100.0)}
	_range = Vector2(_bar.value, _bar.value)	# (a step with no slice: the bar stays where it is)
	for key in slices:
		if step.begins_with(key):
			_range = slices[key]
	_bar.value = maxf(_bar.value, _range.x)


# How far in the step it's on, 0 to 1 (curl's percent, or "3 of 9")
func _progress(fraction: float) -> void:
	_bar.value = maxf(_bar.value, _range.x + (_range.y - _range.x) * clampf(fraction, 0.0, 1.0))


# The step, how far in it, and how long it's been going: so it's plain it's still carrying on
func _show() -> void:
	var text := _step if _step != "" else "Finding the map that covers it..."
	if _detail != "":
		text += "\n" + _detail
	if _warning != "":
		text += "\nNote: " + _warning
	# (geotile's order: the map's found and downloaded, then everything made from it, which is kept: only the first time)
	var phase := 2 if _building else 1
	_title.text = "Getting %s: step %d of 3, %s" % [_wanted["name"], phase, "preparing the map files (only the first time)" if phase == 2 else "downloading"]
	_status.text = "%s\n(still working: %d:%02d so far; you can carry on playing)" % [text, _shown / 60, _shown % 60]


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


# Where you clicked can be outside what was made (at sea, or empty ground, inside the extract's outline: the map holds only
# where there's data), and going there would say "no map of it" again, right after it was made. Then the biggest town
# it does cover is where you go
func _spot_on_map() -> void:
	if main.world == null or main.world.has_map_at(_wanted["lat"], _wanted["lon"]):
		return
	var path := GameSettings.cache_dir().path_join("regions").path_join(_made + ".world.json")
	var note = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
	if note is Dictionary:
		var best := Vector2.INF
		var people := -1.0
		for place in note.get("places", []):
			if place is Array and place.size() >= 5 and float(place[4]) > people and main.world.has_map_at(float(place[2]), float(place[3])):
				best = Vector2(float(place[2]), float(place[3]))
				people = float(place[4])
		if best == Vector2.INF and note.get("bounds") is Array and note["bounds"].size() == 4:
			var b: Array = note["bounds"]
			best = Vector2((float(b[0]) + float(b[2])) * 0.5, (float(b[1]) + float(b[3])) * 0.5)
		if best != Vector2.INF:
			print("Geodrive: %s: nothing mapped at %.4f, %.4f (outside the data): going to %.4f, %.4f instead" % [_wanted["name"], _wanted["lat"], _wanted["lon"], best.x, best.y])
			_wanted["lat"] = best.x
			_wanted["lon"] = best.y


# Made: the world takes up the new files (the airports too) and off you go
func _finished() -> void:
	_title.text = "Getting %s: step 3 of 3, loading in" % _wanted["name"]
	_status.text = "The map files are made and kept. The 3D world is built as you arrive, and next time you come here only this step happens: a few seconds."
	_bar.visible = false
	await get_tree().process_frame	# (drawn before the world takes up the new files, which holds the frame a moment)
	var added: int = main.world.refresh_maps() if main.world else 0
	GameSettings.remember_data(GameSettings.cache_dir())	# (it has maps now: a new version started later uses this folder)
	WorldMap.forget_regions()
	if main.aviation:
		main.aviation._load_airports()
		if main.game_gen:
			main.game_gen._read_regions()	# (its golf courses, race tracks, reserves: games there too)
	_spot_on_map()
	print("Geodrive: got %s (%s: %d new files) in %.0f s" % [_wanted["name"], _made, added, Time.get_ticks_msec() / 1000.0 - _started])
	if not _wanted.get("play", true) and not _wanted.get("host", false):
		# (download only: nothing moves you. In a game, a note and on you play; on the front menu, a button to go there)
		main.hud.set_status("%s is downloaded: the phone's Map (Tab) or the world map takes you there" % _wanted["name"], 8.0)
		if main.world_map.visible:
			# (on the world map: it now has it, "Go there" and "Delete" in place of Download)
			main.world_map._read_haves()
			main.world_map._show_choice()
			_panel.visible = false
		elif main.car or main.walker:
			_panel.visible = false
		else:
			_title.text = "%s is downloaded" % _wanted["name"]
			_status.text = "It's ready whenever you are."
			_bar.visible = false
			_ready_to_play = true
			_yes.text = "Play there"
			_yes.visible = true
			_no.text = "Close"
			Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		return
	# (step 3 stays up until the loading screen covers it: no quiet gap)
	_handover = Time.get_ticks_msec() / 1000.0
	main.hud.set_status("%s is ready: going there%s" % [_wanted["name"], " (" + _warning + ")" if _warning != "" else ""], 6.0 if _warning != "" else 4.0)
	if _wanted.get("host", false):
		ready_to_join.emit(_wanted["lat"], _wanted["lon"], _wanted["name"])	# (a friend's area: joined there)
	else:
		ready_to_go.emit(_wanted["lat"], _wanted["lon"], _wanted["name"])
