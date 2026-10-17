# The menu, as in the Unreal game: a panel on the left with one page at a time. Before anything is loaded it's the
# start screen (the loading screen's night sky and turning Earth): Continue, Play somewhere new, Play with friends.
# While driving (Esc or Tab) it shades the paused world: Resume, Teleport, Car, Drive somewhere else, Settings, Quit.
# Esc or Tab in it goes back a page, or closes it from the main page once there's somewhere to drive.
class_name GameMenu
extends CanvasLayer

signal closed
signal drive_requested(lat: float, lon: float, place_name: String)
signal save_spot_requested(spot_name: String)
signal car_chosen(body: int)
signal fly_requested(kind: int, airport: Dictionary)
signal airport_requested(airport: Dictionary)	# (to its plane stand, on foot)
signal world_map_requested
signal settings_changed

const ORANGE := Color(1.0, 0.45, 0.1)
const HEADING := Color(1.0, 0.6, 0.25)
const PANEL := Color(0.02, 0.025, 0.03, 0.92)

var settings: GameSettings
var wheel: Node = null	# the steering wheel, if there is one (its forces change straight away)
var game: Node = null	# main: the car and its seat
# set by main before opening: whether there's an area to drive, its name, the ready-made areas, the saved spots, where
# the car is
var has_area := false
var area_name := ""
var areas: Array = []
var spots: Array = []
var here_lat := 0.0
var here_lon := 0.0
var in_uk := false

var page := "main"
var settings_tab := 0
var chosen_area: Dictionary = {}

var _shade: ColorRect
var _globe: Globe
var _subtitle: Label
var _content: VBoxContainer
var _scroll: ScrollContainer
var _regular: SystemFont
var _bold: SystemFont
var _pedal_status: Label
var _pedal_details := {}
var _control_status: Label
var _control_values := {}
var _search_status: Label


# The buttons' look: a dark box each, lighter under the mouse, an orange edge with the keyboard on it (the world map's list
# uses it too, so its places read as buttons)
static func button_theme() -> Theme:
	var theme := Theme.new()
	for kind in ["normal", "hover", "pressed", "focus", "disabled"]:
		var box := StyleBoxFlat.new()
		box.bg_color = {"normal": Color(0.11, 0.12, 0.14), "hover": Color(0.19, 0.2, 0.23), "pressed": Color(0.3, 0.2, 0.12),
			"focus": Color(0, 0, 0, 0), "disabled": Color(0.08, 0.08, 0.09)}[kind]
		if kind == "focus":
			box.draw_center = false
			box.border_color = HEADING
			box.set_border_width_all(2)
		box.set_corner_radius_all(4)
		box.set_content_margin_all(6)
		theme.set_stylebox(kind, "Button", box)
	return theme

func _ready() -> void:
	layer = 50
	process_mode = Node.PROCESS_MODE_ALWAYS
	_regular = SystemFont.new()
	_bold = SystemFont.new()
	_bold.font_weight = 700
	var theme := button_theme()
	theme.default_font = _regular
	theme.set_color("font_color", "Label", Color(0.92, 0.93, 0.95))

	_shade = ColorRect.new()
	_shade.set_anchors_preset(Control.PRESET_FULL_RECT)
	_shade.theme = theme
	add_child(_shade)
	_globe = Globe.new()
	_globe.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_globe.set_anchors_preset(Control.PRESET_CENTER_RIGHT)
	_globe.custom_minimum_size = Vector2(520, 520)
	_globe.offset_left = -610
	_globe.offset_right = -90
	_globe.offset_top = -260
	_globe.offset_bottom = 260
	_shade.add_child(_globe)

	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = PANEL
	style.content_margin_left = 28
	style.content_margin_right = 28
	style.content_margin_top = 24
	style.content_margin_bottom = 24
	panel.add_theme_stylebox_override("panel", style)
	panel.set_anchors_preset(Control.PRESET_LEFT_WIDE)
	panel.offset_left = 80
	panel.offset_right = 80 + 600
	panel.offset_top = 50
	panel.offset_bottom = -50
	_shade.add_child(panel)
	var column := VBoxContainer.new()
	panel.add_child(column)
	var title := _text("OPEN-PLANET", 40, ORANGE, true)
	column.add_child(title)
	_subtitle = _text("", 16, Color(0.7, 0.7, 0.7), false)
	_subtitle.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_subtitle)
	var gap := Control.new()
	gap.custom_minimum_size = Vector2(0, 14)
	column.add_child(gap)
	_scroll = ScrollContainer.new()
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(_scroll)
	_content = VBoxContainer.new()
	_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_content.add_theme_constant_override("separation", 6)
	_scroll.add_child(_content)
	visible = false


func is_open() -> bool:
	return visible


var _entry := "main"	# (the page it was opened on: from the phone, Back on it closes the menu)

func open(first_page: String = "main") -> void:
	visible = true
	_entry = first_page
	# (offline the world waits while the menu is open; with friends it carries on)
	get_tree().paused = has_area and not (game and game.net and game.net.is_online())
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_shade.color = Color(0, 0, 0, 0.55) if has_area else LoadingScreen.BACKGROUND
	_globe.visible = not has_area
	show_page(first_page)


func close() -> void:
	if not visible:
		return
	visible = false
	get_tree().paused = false
	closed.emit()


func _unhandled_input(event: InputEvent) -> void:
	if visible and event is InputEventKey and event.pressed and not event.echo and event.keycode in [KEY_ESCAPE, KEY_TAB]:
		get_viewport().set_input_as_handled()
		back()
	elif visible and event is InputEventJoypadButton and event.pressed and event.button_index in [JOY_BUTTON_B, JOY_BUTTON_START]:
		get_viewport().set_input_as_handled()
		back()


func back() -> void:
	if page == _entry and page != "main" and has_area:
		close()
	elif page == "start_point":
		show_page("go")
	elif page != "main":
		show_page("main")
	elif has_area:
		close()


func show_page(new_page: String) -> void:
	page = new_page
	for child in _content.get_children():
		_content.remove_child(child)
		child.queue_free()
	match page:
		"start_point":
			_start_point_page()
		"settings":
			_settings_page()
		"multiplayer":
			_multiplayer_page()
		"go":
			_go_page()
		"places":
			_places_page()
		"events":
			_events_page()
		_:
			_main_page()
	# the first button (or the open settings tab) takes the keys and the gamepad
	for child in _content.get_children():
		var button := _first_button(child)
		if page == "settings" and child is HBoxContainer:
			for tab in child.get_children():
				if tab.has_meta("open"):
					button = tab as Button
		if button:
			button.grab_focus.call_deferred()
			break


func _first_button(node: Node) -> Button:
	if node is Button:
		return node
	for child in node.get_children():
		var found := _first_button(child)
		if found:
			return found
	return null


# --- building blocks ---

func _text(text: String, font_size: int, colour: Color, bold: bool) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", colour)
	label.add_theme_font_override("font", _bold if bold else _regular)
	return label


# A button with a bold name and a grey line under it saying what it does
func _button(label: String, action: Callable, detail: String = "", enabled: bool = true, font_size: int = 21) -> Button:
	var holder := MarginContainer.new()
	var button := Button.new()
	button.disabled = not enabled
	button.pressed.connect(action)
	holder.add_child(button)
	var inside := MarginContainer.new()
	inside.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for side in ["left", "right"]:
		inside.add_theme_constant_override("margin_" + side, 16)
	for side in ["top", "bottom"]:
		inside.add_theme_constant_override("margin_" + side, 9)
	var lines := VBoxContainer.new()
	lines.mouse_filter = Control.MOUSE_FILTER_IGNORE
	lines.add_theme_constant_override("separation", 1)
	var name_label := _text(label, font_size, Color(0.95, 0.95, 0.95) if enabled else Color(0.5, 0.5, 0.5), true)
	name_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	lines.add_child(name_label)
	if detail != "":
		var detail_label := _text(detail, 13, Color(0.75, 0.75, 0.75), false)
		detail_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		detail_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		lines.add_child(detail_label)
	inside.add_child(lines)
	holder.add_child(inside)
	_content.add_child(holder)
	return button


func _heading(text: String) -> void:
	var label := _text(text, 17, HEADING, true)
	var holder := MarginContainer.new()
	holder.add_theme_constant_override("margin_top", 12)
	holder.add_child(label)
	_content.add_child(holder)


func _note(text: String) -> void:
	var label := _text(text, 15, Color(0.8, 0.8, 0.8), false)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_content.add_child(label)


# A setting: its name, then < value >; Step(direction) changes it, Describe() says what it is
func _stepper(label: String, step: Callable, describe: Callable) -> void:
	var row := HBoxContainer.new()
	var name_label := _text(label, 19, Color(0.92, 0.93, 0.95), false)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(name_label)
	var value := _text(str(describe.call()), 19, Color(1, 1, 1), true)
	value.custom_minimum_size = Vector2(170, 0)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	value.clip_text = true
	for direction in [-1, 1]:
		var arrow := Button.new()
		arrow.text = "  <  " if direction < 0 else "  >  "
		arrow.add_theme_font_override("font", _bold)
		arrow.add_theme_font_size_override("font_size", 19)
		arrow.pressed.connect(func():
			step.call(direction)
			value.text = str(describe.call())
			settings_changed.emit())
		row.add_child(arrow)
		if direction < 0:
			row.add_child(value)
	_content.add_child(row)


# A text box with a button next to it
func _text_action(hint: String, initial: String, button_label: String, action: Callable) -> void:
	var row := HBoxContainer.new()
	var box := LineEdit.new()
	box.placeholder_text = hint
	box.text = initial
	box.max_length = 64
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.add_theme_font_size_override("font_size", 19)
	row.add_child(box)
	var button := Button.new()
	button.text = "  " + button_label + "  "
	button.add_theme_font_override("font", _bold)
	button.add_theme_font_size_override("font_size", 19)
	button.pressed.connect(func(): action.call(box.text.strip_edges()))
	box.text_submitted.connect(func(_text: String): action.call(box.text.strip_edges()))
	row.add_child(button)
	_content.add_child(row)


func _back(to: String = "main") -> void:
	_button("Back", show_page.bind(to))


# --- pages ---

# Kept simple: where you go, you arrive on foot by your car; planes are at the airports' stands; your car is in
# Settings > Game
func _main_page() -> void:
	if not has_area:
		_start_page()
		return
	_subtitle.text = area_name
	_button("Resume", close, "Tab: your phone (the map, your cars, games, building, friends)", true, 30)
	_button("Settings", show_page.bind("settings"), "Picture, controls and wheel, sound, game")
	_button("Quit", func(): get_tree().quit())


# The start screen: the one big thing is going back to where you were; then somewhere new (the globe: click a spot),
# friends; settings and quit small at the bottom
func _start_page() -> void:
	_subtitle.text = "The real world, on foot, by car or plane."
	var last := UnrealSettings.last_spot()
	if not last.is_empty():
		_button("Continue", _drive.bind(last["lat"], last["lon"], last["name"]), "Back to " + last["name"], true, 34)
	_button("Play somewhere new", _open_world_map, "Spin the globe and click where you want to be", true, 34 if last.is_empty() else 24)
	_button("Play an event", show_page.bind("events"), "A race track, a golf course, a reserve, a park: on the real map, ready to play", true, 24)
	_button("Play with friends", show_page.bind("multiplayer"), "Host a game, or join one", true, 24)
	var gap := Control.new()
	gap.custom_minimum_size = Vector2(0, 30)
	_content.add_child(gap)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	_content.add_child(row)
	for entry in [["Settings", show_page.bind("settings")], ["Quit", func(): get_tree().quit()]]:
		var small := Button.new()
		small.text = "   %s   " % entry[0]
		small.add_theme_font_size_override("font_size", 17)
		small.pressed.connect(entry[1])
		row.add_child(small)


var _airport_list: VBoxContainer


# The phone's Map: every way to go, on one page (you arrive on foot by your car)
func _go_page() -> void:
	_subtitle.text = "Map: where to go. You arrive on foot, your car parked by you"
	_button("World map", _open_world_map, "The whole world: click a town of yours to go there, or anywhere else to download it", true, 26)
	# the area you're in (the smallest, where they overlap) and its places
	if has_area:
		var here_area: Dictionary = {}
		var smallest := INF
		for area in areas:
			var inside: bool = here_lat >= area["south"] and here_lat <= area["north"] and here_lon >= area["west"] and here_lon <= area["east"]
			var extent: float = (area["north"] - area["south"]) * (area["east"] - area["west"])
			if inside and extent < smallest:
				smallest = extent
				here_area = area
		if not here_area.is_empty():
			_heading("Here: " + here_area["name"])
			for place in here_area["places"]:
				_button(place["name"], _drive.bind(place["lat"], place["lon"], place["name"]))
	_heading("Your saved spots")
	for spot in spots:
		_button(spot["name"], _drive.bind(spot["lat"], spot["lon"], spot["name"]), "%.4f, %.4f" % [spot["lat"], spot["lon"]])
	if spots.is_empty():
		_note("None yet. Go somewhere you like and save it here.")
	if has_area:
		_text_action("Name for where you are", "", "Save spot", func(text: String):
			save_spot_requested.emit(text)
			show_page("go"))
	if game and game.aviation:
		_heading("Airports (to fly: their plane stand)")
		var aviation: Node = game.aviation
		_text_action("An airport's name or code (George, FACT, CPT)", "", "Search", func(text: String): _list_airports(aviation.search(text)))
		_airport_list = VBoxContainer.new()
		_content.add_child(_airport_list)
		_list_airports(aviation.nearest(here_lat if has_area else -33.97, here_lon if has_area else 18.6, 5, aviation.STAND_RUNWAY))
	_heading("Anywhere else")
	_text_action("A place (Knysna Heads, Long Street Cape Town) or lat, lon", "", "Go", _search_and_drive)
	_search_status = _text("", 15, Color(0.4, 0.85, 1.0), true)
	_search_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_content.add_child(_search_status)
	_note("Places are found with OpenStreetMap's search (Nominatim); numbers work too: right-click a place in Google Maps to copy them.")
	_button("Your downloaded places", show_page.bind("places"), "How big each is; delete the ones you don't need")
	_back()


var events_kind := "track"
var events_text := ""

# Play an event: every race track, golf course, reserve and park on your maps (the world makes a game at each),
# nearest where you last were first; Go: there, at its start
func _events_page() -> void:
	_subtitle.text = "Play an event: on the real map, ready to play"
	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", 4)
	_content.add_child(tabs)
	for tab in [["track", "Race tracks"], ["golf", "Golf"], ["reserve", "Reserves"], ["park", "Parks"], ["stadium", "Stadiums"]]:
		var chip := Button.new()
		chip.text = "  %s  " % tab[1]
		chip.add_theme_font_size_override("font_size", 17)
		if events_kind == tab[0]:
			chip.add_theme_color_override("font_color", HEADING)
		chip.pressed.connect(func(): events_kind = tab[0]; show_page("events"))
		tabs.add_child(chip)
	_text_action("Search by name (Kyalami, Steenberg...)", events_text, "Search", func(text: String):
		events_text = text
		show_page("events"))
	var last := UnrealSettings.last_spot()
	var from := PackedFloat64Array([last.get("lat", -33.92), last.get("lon", 18.42)])
	var found: Array = game.game_gen.events(events_kind, from, events_text, 40) if game and game.game_gen else []
	for place in found:
		var far: String = ("%.1f km" % place["km"]) if place["km"] < 100.0 else ("%d km" % roundi(place["km"]))
		_button(place["name"], func():
			var at: PackedFloat64Array = game.game_gen.start_of(place)
			_drive(at[0], at[1], place["name"]), "%s, %s away" % [game.game_gen.KINDS.get(place["kind"], place["kind"]), far])
	if found.is_empty():
		_note("None on your maps yet. Play somewhere new: the world map, and download a country.")
	_back()


func _list_airports(found: Array) -> void:
	for child in _airport_list.get_children():
		child.queue_free()
	var aviation: Node = game.aviation
	for entry in found:
		var airport: Dictionary = entry["airport"]
		var runway: Dictionary = aviation.longest(airport)
		var detail := "runway %s, %d m" % [runway["ref"], roundi(aviation.runway_length(runway))]
		if entry["km"] >= 0.0:
			detail += ", %d km away" % roundi(entry["km"])
		var button := _button(aviation.label(airport), func():
			airport_requested.emit(airport)
			close(), detail)
		# (into the list, under the search box)
		var holder := button.get_parent()
		holder.get_parent().remove_child(holder)
		_airport_list.add_child(holder)
	if found.is_empty():
		_airport_list.add_child(_text("None found", 16, Color(0.7, 0.7, 0.7), false))






func _start_point_page() -> void:
	var area := chosen_area
	_subtitle.text = area["name"] + ": where do you want to start?"
	_button("On the main road", _drive.bind((area["south"] + area["north"]) * 0.5, (area["west"] + area["east"]) * 0.5, area["name"]))
	for place in area["places"]:
		_button(place["name"], _drive.bind(place["lat"], place["lon"], place["name"]))
	_back("go")


# Places: the parts of the world you have (how big; go there, delete), and getting more (the world map: click anywhere)
func _places_page() -> void:
	var have: Array = game.region_fetch.places() if game and game.region_fetch else []
	_subtitle.text = "%d place%s, %.1f GB" % [have.size(), "" if have.size() == 1 else "s", have.reduce(func(sum, p): return sum + p["mb"], 0.0) / 1024.0]
	_button("Get more places", _open_world_map, "The world map: click anywhere on Earth and download it (its roads, buildings and heights), then go there")
	_heading("Yours")
	if have.is_empty():
		_note("None yet. Get one from the world map: anywhere on Earth.")
	for place in have:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 6)
		_content.add_child(row)
		var go := _button("%s  (%s)" % [place["name"], ("under 1 MB" if place["mb"] < 1.0 else "%.0f MB" % place["mb"]) if place["mb"] < 1024.0 else ("%.1f GB" % (place["mb"] / 1024.0))],
			func(): _drive((place["south"] + place["north"]) * 0.5, (place["west"] + place["east"]) * 0.5, place["name"]),
			"Go there (its middle)", place.has("south"))
		var holder := go.get_parent() as Control	# (the button in its box, moved into the row)
		_content.remove_child(holder)
		holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(holder)
		var delete := Button.new()
		delete.text = "Delete"
		delete.add_theme_font_size_override("font_size", 16)
		delete.pressed.connect(func():
			game.region_fetch.delete_place(place["stem"])
			show_page("places"))
		row.add_child(delete)
	_back()


func _open_world_map() -> void:
	visible = false
	get_tree().paused = false
	world_map_requested.emit()


# A place by name or numbers: numbers go straight there; a name is looked up with OpenStreetMap's search (as the Unreal
# game's geo.Go: one search per place ever, the answer kept in Cache)
func _search_and_drive(text: String) -> void:
	if text == "":
		return
	var parts := text.replace(" ", "").split(",")
	if parts.size() == 2 and parts[0].is_valid_float() and parts[1].is_valid_float():
		_drive(float(parts[0]), float(parts[1]), "%s, %s" % [parts[0], parts[1]])
		return
	var cache := GameSettings.cache_dir().path_join("geocode_%s.txt" % text.strip_edges().to_lower().md5_text())
	var cached := FileAccess.get_file_as_string(cache).split(",")
	if cached.size() == 2 and cached[0].is_valid_float() and cached[1].is_valid_float():
		_drive(float(cached[0]), float(cached[1]), text)
		return
	_search_status.text = "Searching for %s..." % text
	var request := HTTPRequest.new()
	add_child(request)
	request.request_completed.connect(func(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray):
		request.queue_free()
		var found: Variant = JSON.parse_string(body.get_string_from_utf8()) if result == HTTPRequest.RESULT_SUCCESS and code == 200 else null
		if not (found is Array) or found.is_empty():
			if is_instance_valid(_search_status):
				_search_status.text = ("Couldn't find %s." % text) if found is Array else ("The search didn't answer (%d). Are you online?" % code)
			return
		var first: Dictionary = found[0]
		var lat := float(first.get("lat", "0"))
		var lon := float(first.get("lon", "0"))
		var file := FileAccess.open(cache, FileAccess.WRITE)
		if file:
			file.store_string("%.7f,%.7f" % [lat, lon])
		print("Geodrive: found %s: %s (%.5f, %.5f)" % [text, first.get("display_name", ""), lat, lon])
		_drive(lat, lon, str(first.get("display_name", text)).get_slice(",", 0)))
	var url := "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&q=" + text.uri_encode()
	request.request(url, ["User-Agent: Open-Planet/0.1 (development build)"])


func _drive(lat: float, lon: float, place_name: String) -> void:
	close()
	drive_requested.emit(lat, lon, place_name)






# --- settings ---

func _settings_page() -> void:
	_subtitle.text = "Settings are saved automatically"
	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", 4)
	for i in 4:
		var tab := Button.new()
		tab.text = ["Graphics", "Controls", "Sound", "Game"][i]
		tab.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		tab.add_theme_font_override("font", _bold)
		tab.add_theme_font_size_override("font_size", 19)
		if i == settings_tab:
			tab.set_meta("open", true)
			tab.add_theme_color_override("font_color", HEADING)
			tab.add_theme_color_override("font_focus_color", HEADING)
			tab.add_theme_color_override("font_hover_color", HEADING)
		tab.pressed.connect(func():
			settings_tab = i
			show_page("settings"))
		tabs.add_child(tab)
	_content.add_child(tabs)
	match settings_tab:
		0:
			_graphics_settings()
		1:
			_control_settings()
		2:
			_sound_settings()
		_:
			_game_settings()
	_back()


func _cycle(value: int, direction: int, count: int) -> int:
	return (value + direction + count) % count


func _graphics_settings() -> void:
	var s := settings
	_stepper("Preset", func(d: int):
		s.use_preset(clampi(s.preset + d, 0, 3))
		s.save_graphics()
		show_page.call_deferred("settings"),
		func(): return GameSettings.PRESETS[s.preset] if s.is_preset() else "Custom")
	_button("Find the best settings for this PC", func():
		s.detect()
		s.save_graphics()
		settings_changed.emit()
		show_page("settings"),
		("Chose: " + s.detected) if s.detected != "" else "By your graphics card: the preset that keeps it smooth")
	_note("Low suits integrated graphics (laptops), Medium a GTX 1060-class card, High and Ultra newer cards. Changing the preset resets the settings below to its own.")
	_heading("Picture")
	_stepper("Render resolution", func(d: int):
		s.render_scale = clampi(s.render_scale + d * 5, 50, 100)
		s.save_godot("RenderScale", s.render_scale),
		func(): return "%d%%" % s.render_scale)
	_stepper("Anti-aliasing", func(d: int):
		s.anti_aliasing = clampi(s.anti_aliasing + d, 0, 3)
		s.save_godot("AntiAliasing", s.anti_aliasing),
		func(): return GameSettings.AA_NAMES[s.anti_aliasing])
	_note("Render resolution below 100% draws fewer pixels and scales the picture up (AMD FSR): the biggest speed-up for slower PCs.")
	_stepper("Shadows", func(d: int):
		s.shadows = clampi(s.shadows + d, 0, 3)
		s.save_godot("Shadows", s.shadows),
		func(): return GameSettings.PRESETS[s.shadows])
	_stepper("Lighting", func(d: int):
		s.lighting = clampi(s.lighting + d, 0, 3)
		s.save_godot("Lighting", s.lighting),
		func(): return GameSettings.PRESETS[s.lighting])
	_note("Lighting: Medium adds the sun lighting the haze, High soft contact shadows (corners, kerbs, under cars), Ultra a glow round bright things. Each costs the graphics card a little more.")
	_stepper("Textures", func(d: int):
		s.textures = clampi(s.textures + d, 0, 3)
		s.save_godot("Textures", s.textures),
		func(): return ["Off", "Near", "Medium", "Far"][s.textures])
	_note("Textures: the grass, sand and rock detail on the ground, and how far it goes (250, 400 or 600 m). Uses some graphics card memory and a little time.")
	_stepper("Vegetation", func(d: int):
		s.vegetation = clampi(s.vegetation + d, 0, 4)
		s.save_godot("Vegetation", s.vegetation),
		func(): return VegetationLook.NAMES[s.vegetation])
	_note("Vegetation: trees and bushes where they really grow (from satellite land cover); further off the woods are a canopy. Low: every tree to 300 m; Ultra: to 900 m, scattered ones to 1.8 km, and shadows from those near.")
	# the world: how far you see, how far it's built in full, how far buildings show, and how much is built at once
	_heading("World")
	_stepper("View distance", func(d: int):
		s.view_distance = clampi(s.view_distance + d, 0, GameSettings.VIEW_METRES.size() - 1)
		s.save_godot("ViewDistance", s.view_distance),
		func(): return "%s (%d km)" % [GameSettings.VIEW_NAMES[s.view_distance], roundi(s.view_metres() / 1000.0)])
	_note("How far you can see: the land out to the horizon (beyond the detail distance it's plain ground, no roads or buildings), with the haze to match. Costs the graphics card; a little processor now and then.")
	_stepper("Detail distance", func(d: int):
		var index := maxi(GameSettings.DETAIL_KM.find(s.detail_km), 0)
		s.detail_km = GameSettings.DETAIL_KM[clampi(index + d, 0, GameSettings.DETAIL_KM.size() - 1)]
		s.save_quality("DetailDistanceKm", s.detail_km),
		func(): return "%d km%s" % [s.detail_km, " (normal)" if s.detail_km == 7 else ""])
	_note("How far roads, buildings and the detailed ground are built round you as you drive. Costs the processor and memory (building them) and the graphics card (drawing them): lower it on an older PC.")
	_stepper("Building distance", func(d: int):
		var index := maxi(GameSettings.BUILDING_TENTHS.find(s.building_tenths), 0)
		s.building_tenths = GameSettings.BUILDING_TENTHS[clampi(index + d, 0, GameSettings.BUILDING_TENTHS.size() - 1)]
		s.save_quality("BuildingDistanceTenths", s.building_tenths),
		func():
			if s.building_tenths <= 0 or s.building_metres() < s.building_tenths * 100.0:
				return "All (%d km)" % s.detail_km
			return ("%d km" % (s.building_tenths / 10.0)) if s.building_tenths % 10 == 0 else ("%.1f km" % (s.building_tenths / 10.0)))
	_note("How far away buildings are drawn, up to the detail distance. Cities like London have many times the buildings of a South African town: lower this if the FPS drops there. Costs the graphics card.")
	_stepper("Advanced: world piece size", func(d: int):
		var index := maxi(GameSettings.WORLD_SIZES.find(s.world_size), 0)
		s.world_size = GameSettings.WORLD_SIZES[_cycle(index, d, GameSettings.WORLD_SIZES.size())]
		s.save_quality("WorldSize", s.world_size),
		func():
			if s.world_size > 0:
				return "%d x %d km" % [s.world_size, s.world_size]
			return "Auto (%d km)" % s.auto_world_km())
	_note("How much world is built at once, ahead of you in the background (busy cities build smaller pieces by themselves). Bigger needs more memory and builds less often. Auto picks by your PC's memory. Takes effect with the next area.")
	_heading("Screen")
	_stepper("Window", func(d: int):
		s.window_mode = _cycle(s.window_mode, d, 3)
		s.save_godot("WindowMode", s.window_mode),
		func(): return GameSettings.WINDOW_NAMES[s.window_mode])
	_stepper("Window size", func(d: int):
		s.resolution = clampi(s.resolution + d, 0, GameSettings.RESOLUTIONS.size() - 1)
		s.save_godot("Resolution", s.resolution),
		func():
			var r: Vector2i = GameSettings.RESOLUTIONS[s.resolution]
			return "%d x %d" % [r.x, r.y])
	_note("Fullscreen and Borderless use the monitor's own resolution: use Render resolution above to draw fewer pixels there.")
	_stepper("VSync", func(_d: int):
		s.vsync = not s.vsync
		s.save_godot("bVSync", s.vsync),
		func(): return "On" if s.vsync else "Off")
	_stepper("Frame rate limit", func(d: int):
		var index := maxi(GameSettings.FRAME_LIMITS.find(s.frame_limit), 0)
		s.frame_limit = GameSettings.FRAME_LIMITS[_cycle(index, d, GameSettings.FRAME_LIMITS.size())]
		s.save_godot("FrameLimit", s.frame_limit),
		func(): return "Unlimited" if s.frame_limit <= 0 else "%d fps" % s.frame_limit)
	_stepper("Performance overlay", func(d: int):
		var level := _cycle((2 if s.show_memory else 1) if s.show_fps else 0, d, 3)
		s.show_fps = level > 0
		s.show_memory = level == 2
		s.save_quality("bShowFPS", s.show_fps)
		s.save_quality("bShowMemory", s.show_memory),
		func(): return ("FPS + memory" if s.show_memory else "FPS") if s.show_fps else "Hidden")
	_note("Memory shows the game's RAM and the graphics card's memory in use.")


func _control_settings() -> void:
	var s := settings
	_heading("Keyboard and gamepad")
	_stepper("Steering speed", func(d: int):
		s.steer_speed = clampi(s.steer_speed + d, 1, 10)
		s.save_quality("KeyboardSteerSpeed", s.steer_speed),
		func(): return "%d%s" % [s.steer_speed, " (normal)" if s.steer_speed == 5 else (" (quicker)" if s.steer_speed > 5 else " (slower)")])
	_note("How fast the wheels turn when you press A / D, and how far they turn at speed.")
	_heading("Driver aids")
	_stepper("ABS", func(d: int):
		s.abs_level = clampi(s.abs_level + d, 0, 10)
		s.save_godot("ABSLevel", s.abs_level),
		func(): return "Off" if s.abs_level == 0 else "%d%s" % [s.abs_level, " (normal)" if s.abs_level == 3 else ""])
	_stepper("Traction control", func(d: int):
		s.tc_level = clampi(s.tc_level + d, 0, 10)
		s.save_godot("TCLevel", s.tc_level),
		func(): return "Off" if s.tc_level == 0 else "%d%s" % [s.tc_level, " (normal)" if s.tc_level == 3 else ""])
	_note("How much the ABS lets the brakes lock and the traction control lets the driven wheels spin: 0 is off (all yours), higher steps in sooner.")
	_heading("Steering wheel")
	_stepper("Wheel force", func(d: int):
		s.wheel_gain = clampf(roundf(s.wheel_gain * 20.0 + d) / 20.0, 0.05, 1.0)
		s.save_wheel("FFBGain", s.wheel_gain)
		_reload_wheel(),
		func(): return "%d%%" % roundi(s.wheel_gain * 100.0))
	_stepper("Force feedback", func(_d: int):
		s.ffb_enabled = not s.ffb_enabled
		s.save_wheel("bFFBEnabled", s.ffb_enabled)
		_reload_wheel(),
		func(): return "On" if s.ffb_enabled else "Off")
	_stepper("Wheel rotation", func(d: int):
		s.wheel_rotation = clampf(s.wheel_rotation + d * 30.0, 180.0, 1440.0)
		s.save_wheel("WheelRotationDegrees", s.wheel_rotation)
		_reload_wheel(),
		func(): return "%d deg" % roundi(s.wheel_rotation))
	_stepper("Steering ratio", func(d: int):
		s.steering_ratio = clampf(s.steering_ratio + d * 0.5, 6.0, 24.0)
		s.save_wheel("SteeringRatio", s.steering_ratio)
		_reload_wheel(),
		func(): return "%.1f : 1" % s.steering_ratio)
	_stepper("Steering direction", func(_d: int):
		s.invert_steering = not s.invert_steering
		s.save_wheel("bInvertSteering", s.invert_steering)
		_reload_wheel(),
		func(): return "Reversed" if s.invert_steering else "Normal")
	_stepper("Force direction", func(_d: int):
		s.invert_ffb = not s.invert_ffb
		s.save_wheel("bInvertFFB", s.invert_ffb)
		_reload_wheel(),
		func(): return "Reversed" if s.invert_ffb else "Normal")
	_note("Wheel rotation must match the wheel's own software (lock to lock). Steering ratio: steering-wheel degrees per degree of the front wheels (a real car is about 12 to 16; lower is quicker). Reverse a direction if the wheel turns or pulls the wrong way.")
	_note("Wheel force: how hard the wheel pulls. Lower is lighter, felt straight away.")
	for effect in [["Brake lock & ABS", "lockup_gain", ["LockupGain", "ABSGain"]], ["Tyre scrub & gravel", "scrub_gain", ["ScrubGain"]], ["Road bumps", "road_gain", ["RoadGain"]]]:
		var property: String = effect[1]
		var keys: Array = effect[2]
		_stepper(effect[0], func(d: int):
			var before := float(s.get(property))
			var next := clampf(roundf(before * 4.0 + d) / 4.0, 0.0, 2.0)
			s.set(property, next)
			for key in keys:
				if key == "ABSGain" and not is_equal_approx(s.abs_gain, before):
					continue	# (ABS saved apart, by hand or the Unreal game: left as it is)
				if key == "ABSGain":
					s.abs_gain = next
				s.save_wheel(key, next)
			_reload_wheel(),
			func(): return "%d%%" % roundi(float(s.get(property)) * 100.0))
	_note("What the tyres make you feel: the shudder of locking brakes and ABS pulsing, front tyres scrubbing wide, and bumps under the front wheels.")
	# pedals: click, then press that pedal all the way down; the clutch can be the pedal or automatic
	_heading("Pedals")
	var connected: bool = wheel != null and wheel.is_wheel_connected()
	_pedal_details.clear()
	for pedal in ["throttle", "brake", "clutch"]:
		var button := _button("Set %s pedal" % pedal, func():
			if wheel:
				wheel.begin_pedal_capture(pedal),
			_pedal_text(pedal), connected)
		_pedal_details[pedal] = _detail_label(button)
	_pedal_status = _text(wheel.get_pedal_status() if wheel else "", 17, Color(0.4, 0.85, 1.0), true)
	_pedal_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_content.add_child(_pedal_status)
	_stepper("Clutch", func(_d: int):
		s.clutch_pedal = not s.clutch_pedal
		s.save_wheel("bUseClutchPedal", s.clutch_pedal)
		_reload_wheel(),
		func(): return "Pedal" if s.clutch_pedal else "Automatic")
	_note("With the clutch pedal, let it out gently to pull away; drop it with too few revs and the engine stalls. Push the clutch in to restart. Gear changes don't need the clutch.")

	# the camera: views, the cockpit's field of view, the driver's seat, and the wheel's own buttons for looking round
	_heading("Camera")
	_note("C changes view: behind the car, the driver's seat with the steering wheel, the driver's seat without it (when your own wheel is in front of you), and the bonnet. Z / X look left and right, V looks back, the mouse looks around.")
	_stepper("Your height", func(d: int):
		s.body_height = clampi(s.body_height + d, 120, 230)
		s.save_quality("BodyHeightCm", s.body_height)
		if game:
			game.apply_body_height(),
		func(): return "%d cm (%d' %d\")" % [s.body_height, roundi(s.body_height / 2.54) / 12, roundi(s.body_height / 2.54) % 12])
	_note("Your own height: your eyes in the driver's seat, and on foot (Enter gets out of the car and back in) how high you see, how far you step and how fast you walk.")
	_stepper("Cockpit field of view", func(d: int):
		s.cockpit_fov = clampi(s.cockpit_fov + d * 5, 50, 110)
		s.save_quality("CockpitFOV", s.cockpit_fov),
		func(): return "%d%s" % [s.cockpit_fov, " (normal)" if s.cockpit_fov == 80 else ""])
	for axis in [0, 1]:
		_stepper("Seat forward / back" if axis == 0 else "Seat up / down", func(d: int):
			if game and game.car:
				var seat: Vector2 = game.get_seat()
				seat[axis] = roundf(seat[axis] / 2.0) * 2.0 + d * 2.0
				game.set_seat(seat),
			func():
				if game == null or game.car == null:
					return "in a car"
				var seat: Vector2 = game.get_seat()
				return "%+.0f cm" % seat[axis])
	_note("The seat moves where your eyes are in the two driver's seat views, for this car. It can also be moved with the wheel's buttons below.")
	_heading("Wheel buttons for the camera and seat")
	_control_values.clear()
	for control in ClassDB.class_call_static("GeodriveWheel", "control_names"):
		_control_row(control, connected)
	_control_status = _text(wheel.get_control_status() if wheel else "", 17, Color(0.4, 0.85, 1.0), true)
	_control_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_content.add_child(_control_status)
	_note("Click Set, then push the stick that way or press the button. Looking goes back to the road when you let go; looking back snaps round and back.")
	if not connected:
		_note("No steering wheel connected: plug it in and open this page again to set the pedals and buttons.")


func _pedal_text(pedal: String) -> String:
	var axis: int = wheel.get_pedal_axis(pedal) if wheel else -1
	return "Not set" if axis < 0 else "Set (axis %d)" % axis


# The grey line under a menu button's name (to change it as things are set)
func _detail_label(button: Button) -> Label:
	var lines: VBoxContainer = button.get_parent().get_child(1).get_child(0)
	return lines.get_child(1) if lines.get_child_count() > 1 else null


# A control set by pressing it: its name, what it is now, Set and Clear
func _control_row(control: String, connected: bool) -> void:
	var row := HBoxContainer.new()
	var name_label := _text(ClassDB.class_call_static("GeodriveWheel", "control_label", control), 19, Color(0.92, 0.93, 0.95), false)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(name_label)
	var value := _text(wheel.describe_control(control) if wheel else "-", 17, Color(1, 1, 1), true)
	value.custom_minimum_size = Vector2(150, 0)
	value.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	row.add_child(value)
	_control_values[control] = value
	for action in ["Set", "Clear"]:
		var button := Button.new()
		button.text = "  " + action + "  "
		button.disabled = not connected
		button.add_theme_font_override("font", _bold)
		button.add_theme_font_size_override("font_size", 16)
		button.pressed.connect(func():
			if action == "Set":
				wheel.begin_control_capture(control)
			else:
				wheel.clear_control(control))
		row.add_child(button)
	_content.add_child(row)


# While the menu's open: the pedal and button setup's progress, and the wheel's setup saved once something is set
func _process(_delta: float) -> void:
	if not visible or wheel == null:
		return
	if wheel.take_changed():
		var values: Dictionary = wheel.saved_values()
		for key in values:
			UnrealSettings.set_values(GameSettings.WHEEL, key, values[key])
	if page != "settings" or settings_tab != 1:
		return
	if is_instance_valid(_pedal_status):
		_pedal_status.text = wheel.get_pedal_status()
		for pedal in _pedal_details:
			var detail: Label = _pedal_details[pedal]
			if is_instance_valid(detail):
				detail.text = _pedal_text(pedal)
	if is_instance_valid(_control_status):
		_control_status.text = wheel.get_control_status()
		for control in _control_values:
			var value: Label = _control_values[control]
			if is_instance_valid(value):
				value.text = wheel.describe_control(control)


func _sound_settings() -> void:
	var s := settings
	for level in [["Volume", "geo.Sound.Volume"], ["Engine loudness", "geo.Sound.Engine"], ["Rasp (brightness)", "geo.Sound.Rasp"], ["Exhaust echo", "geo.Sound.Pipe"], ["Pops and crackles", "geo.Sound.Crackle"]]:
		var key: String = level[1]
		_stepper(level[0], func(d: int):
			s.sound_levels[key] = clampf(roundf(float(s.sound_levels[key]) * 10.0 + d) / 10.0, 0.0, 2.0)
			s.save_sound(),
			func(): return "%d%%" % roundi(float(s.sound_levels[key]) * 100.0))
	_note("100% is how each car is meant to sound. Rasp: lower is deeper and smoother, higher is sharper. Exhaust echo: how much the pipe rings; 0% is dry. Pops: the crackle when you lift off at high revs.")


func _reload_wheel() -> void:
	if wheel:
		wheel.load_settings(UnrealSettings.path())


var advanced_open := false	# (the Game tab's debug settings, shown only when asked for)

func _game_settings() -> void:
	var s := settings
	_stepper("Traffic", func(d: int):
		# steps of 5 up to 40, then 10 (up to 150)
		var cars := s.traffic_cars
		s.traffic_cars = clampi(cars + d * (10 if (cars >= 40 if d > 0 else cars > 40) else 5), 0, 150)
		s.save_quality("TrafficCars", s.traffic_cars),
		func(): return "Off" if s.traffic_cars == 0 else "%d cars" % s.traffic_cars)
	_note("Traffic fills the road you are on and the roads you can turn into, joining at the road speed; some cars are parked and pull out as you come by. Each car also tests the roads: bumps it feels are logged to Saved/RoadReports.")
	_stepper("Speed in", func(d: int):
		s.speed_units = _cycle(s.speed_units, d, 3)
		s.save_quality("SpeedUnits", s.speed_units),
		func(): return ["Auto (%s)" % ("mph" if in_uk else "km/h"), "km/h", "mph"][s.speed_units])
	_stepper("On-screen text", func(_d: int):
		s.hud_shown = not s.hud_shown
		s.save_godot("bHUD", s.hud_shown),
		func(): return "Shown" if s.hud_shown else "Hidden")
	_note("H hides or shows all the on-screen text. Your car: the phone (Tab), Cars.")
	_heading("Roads")
	_stepper("Road style", func(_d: int):
		s.follow_land = not s.follow_land
		s.save_godot("RoadStyle", 0 if s.follow_land else 1),
		func(): return "Follow the land" if s.follow_land else "Design rules")
	_stepper("Kerbs in town", func(_d: int):
		s.kerbs = not s.kerbs
		s.save_godot("bKerbs", s.kerbs),
		func(): return "On" if s.kerbs else "Off")
	_note("Follow the land: roads keep to the ground and only the bumps are smoothed out (the default). Design rules: roads keep to legal grades, with cuttings and embankments. Kerbs: town roads get a kerb and a raised pavement. Both apply to the world built from now on.")
	if has_area:
		_button("Build this area again", func():
			close()
			if game:
				game.rebuild_area(), "With the road settings above, round where you are")
	_button("Hide the advanced settings" if advanced_open else "Advanced settings", func():
		advanced_open = not advanced_open
		show_page("settings"), "" if advanced_open else "Debug info, road checks, the robot driver")
	if not advanced_open:
		return
	_heading("Advanced")
	_stepper("Debug info (F3)", func(_d: int):
		s.debug_info = not s.debug_info
		s.save_godot("bDebugInfo", s.debug_info),
		func(): return "Shown" if s.debug_info else "Hidden")
	_stepper("Road centrelines", func(_d: int):
		s.road_lines = not s.road_lines
		s.save_godot("bRoadLines", s.road_lines),
		func(): return "Shown" if s.road_lines else "Hidden")
	_stepper("Scan the roads", func(_d: int):
		s.scan_roads = not s.scan_roads
		s.save_godot("bScanRoads", s.scan_roads),
		func(): return "On" if s.scan_roads else "Off")
	_note("Scan the roads: when each piece of the world is built, every road is checked for bumps, jumps, holes, grass through the road and the like; the summary goes to the log and every fault to Saved/RoadReports/scan_<time>.csv.")
	if has_area:
		_button("Robot driver: the main road", func():
			close()
			if game:
				game.start_robot("n2"), "Drives your car along the trunk road end to end, both ways, and reports every problem it feels (Saved/RoadReports)")
		_button("Robot driver: 10 trips through town", func():
			close()
			if game:
				game.start_robot("town", 10), "Random 2-6 km trips between ordinary roads")
		_button("Stop the robot driver", func():
			close()
			if game:
				game.start_robot("stop"), "And write its report so far")
	if has_area:
		_button("Reset the trip", func():
			if game:
				game.trip_km = 0.0
			show_page("settings"), "The trip counter under the speed")


# --- multiplayer, as the Unreal game's: host, join a friend by address, leave; your name over your car ---

func _multiplayer_page() -> void:
	var net: GameNet = game.net
	if net.is_client():
		_subtitle.text = "You're in a friend's game"
	elif net.is_host():
		_subtitle.text = "You're hosting: friends join with your address: %s" % ", ".join(net.addresses())
	else:
		_subtitle.text = "Play together over Tailscale (or on the same network): one hosts, the others join"
	if not net.is_online() and not net.is_connecting():
		_button("Host this game", func():
			if net.host():
				var at: PackedFloat64Array = game.world.to_lat_lon(game.car.global_position)
				net.set_area(at[0], at[1], net.player_name)
			show_page("multiplayer"), "Friends join you in this area" if has_area and game.car != null else "Start driving somewhere first, then host from here", has_area and game.car != null)
		if not (has_area and game.car != null):
			_button("Pick where to go", _open_world_map, "Go somewhere first; then come back here to host")
		_heading("Join a friend")
		_text_action("Host's address (100.x.x.x)", UnrealSettings.get_value("Geodrive", "LastJoinAddress", ""), "Join", func(address: String):
			if address == "":
				return
			UnrealSettings.set_value("Geodrive", "LastJoinAddress", address)
			net.join(address)
			show_page("multiplayer"))
	else:
		_button("Stop joining" if net.is_connecting() else ("Leave the game" if net.is_client() else "Stop hosting"), func():
			net.leave()
			show_page("multiplayer"))
		_note("%d player%s in the game." % [net.player_count(), "" if net.player_count() == 1 else "s"])
	if net.status != "":
		var status := _text(net.status, 15, Color(0.4, 0.85, 1.0), true)
		status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		_content.add_child(status)
	_heading("Your name")
	_text_action("Name over your car", net.player_name, "Save", func(new_name: String):
		if new_name == "":
			return
		net.player_name = new_name.left(24)
		UnrealSettings.set_value("Geodrive", "PlayerName", net.player_name)
		show_page("multiplayer"))
	_note("Both players need Tailscale (tailscale.com) on the same network, or to be on the same home network. The host keeps the area; friends who join are put on the road beside the host. Traffic and the robot driver are offline only. How to set it up: docs/MULTIPLAYER.md (on the project's GitHub page).")
	_back()
