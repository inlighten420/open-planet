# The phone (Tab): everything you do in the world, as big tiles, one place each: the map and your places, your cars
# (the one you pick waits parked on the road nearest you), the games round you, building, friends, settings. The world
# goes on while it's up (as a phone's would); Esc is only the pause. 1-6 open the apps too; Tab, Esc or the pad's B close
# it or go back to its home.

extends CanvasLayer

const ORANGE := Color(1.0, 0.45, 0.1)
const APPS := [["Map", "Places, spots, airports"], ["Cars", "Yours, parked nearby"], ["Games", "Round you"],
	["Build", "Build and make"], ["Friends", "Play together"], ["Settings", "Picture, controls, sound"]]

var main: Node
var page := "home"
var _panel: PanelContainer
var _title: Label
var _clock: Label
var _content: VBoxContainer
var _bold: SystemFont
var events_kind := ""	# (the Games page's tab: "" near you, or a kind of place)
var events_text := ""	# (its search)


func _ready() -> void:
	layer = 45	# (under the menu: its pages open over it)
	process_mode = Node.PROCESS_MODE_ALWAYS
	_bold = SystemFont.new()
	_bold.font_weight = 700
	var theme := GameMenu.button_theme()
	theme.set_color("font_color", "Label", Color(0.92, 0.93, 0.95))
	_panel = PanelContainer.new()
	_panel.theme = theme
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.03, 0.035, 0.045, 0.96)
	style.border_color = Color(0.22, 0.23, 0.26)
	style.set_border_width_all(3)
	style.set_corner_radius_all(30)
	style.set_content_margin_all(18)
	_panel.add_theme_stylebox_override("panel", style)
	_panel.set_anchors_preset(Control.PRESET_BOTTOM_RIGHT)
	_panel.offset_left = -440
	_panel.offset_right = -60
	_panel.offset_top = -700
	_panel.offset_bottom = -40
	add_child(_panel)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 10)
	_panel.add_child(column)
	var top := HBoxContainer.new()
	column.add_child(top)
	_title = _label("", 24, ORANGE, true)
	_title.autowrap_mode = TextServer.AUTOWRAP_OFF
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(_title)
	_clock = _label("", 18, Color(0.75, 0.75, 0.78), false)
	_clock.autowrap_mode = TextServer.AUTOWRAP_OFF
	top.add_child(_clock)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	column.add_child(scroll)
	_content = VBoxContainer.new()
	_content.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_content.add_theme_constant_override("separation", 8)
	scroll.add_child(_content)
	visible = false


func is_open() -> bool:
	return visible


func open(first_page: String = "home") -> void:
	visible = true
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	show_page(first_page)


func close() -> void:
	if not visible:
		return
	visible = false
	main._menu_closed()	# (the mouse looks round again)


func toggle() -> void:
	if visible:
		close()
	else:
		open()


func _process(_delta: float) -> void:
	if visible:
		_clock.text = Time.get_time_string_from_system().left(5)


func _unhandled_input(event: InputEvent) -> void:
	if not visible:
		return
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode in [KEY_ESCAPE, KEY_TAB]:
			get_viewport().set_input_as_handled()
			back()
		elif page == "home" and event.keycode >= KEY_1 and event.keycode < KEY_1 + APPS.size():
			get_viewport().set_input_as_handled()
			_open_app(APPS[event.keycode - KEY_1][0])
	elif event is InputEventJoypadButton and event.pressed and event.device == main.WALKER.gamepad() and event.button_index in [JOY_BUTTON_B, JOY_BUTTON_BACK]:
		get_viewport().set_input_as_handled()
		back()


func back() -> void:
	if page == "home":
		close()
	else:
		show_page("home")


func show_page(new_page: String) -> void:
	page = new_page
	for child in _content.get_children():
		_content.remove_child(child)
		child.queue_free()
	match page:
		"cars":
			_cars_page()
		"games":
			_games_page()
		_:
			_home_page()
	var first := _first_button(_content)
	if first:
		first.grab_focus.call_deferred()


func _first_button(node: Node) -> Button:
	if node is Button:
		return node
	for child in node.get_children():
		var found := _first_button(child)
		if found:
			return found
	return null


# --- building blocks ---

func _label(text: String, font_size: int, colour: Color, bold: bool) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", colour)
	if bold:
		label.add_theme_font_override("font", _bold)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


# A button with a bold name and a grey line under it (the card as tall as its text); lit (an orange name) for the one
# you have now
func _card(parent: Control, text: String, detail: String, action: Callable, lit: bool = false, height: float = 0.0) -> Button:
	var holder := MarginContainer.new()	# (the button behind, the text over it: the holder as tall as the text)
	holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	holder.custom_minimum_size = Vector2(0, height)
	var button := Button.new()
	button.pressed.connect(action)
	holder.add_child(button)
	var margin := MarginContainer.new()
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 12)
	var lines := VBoxContainer.new()
	lines.mouse_filter = Control.MOUSE_FILTER_IGNORE
	lines.alignment = BoxContainer.ALIGNMENT_CENTER
	lines.add_child(_label(text, 21, ORANGE if lit else Color(0.95, 0.95, 0.95), true))
	if detail != "":
		lines.add_child(_label(detail, 13, Color(0.72, 0.72, 0.75), false))
	margin.add_child(lines)
	holder.add_child(margin)
	parent.add_child(holder)
	return button


func _note(text: String) -> void:
	_content.add_child(_label(text, 14, Color(0.7, 0.7, 0.72), false))


# --- pages ---

func _home_page() -> void:
	_title.text = "Phone"
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 10)
	grid.add_theme_constant_override("v_separation", 10)
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_content.add_child(grid)
	for i in APPS.size():
		var app: Array = APPS[i]
		_card(grid, "%s" % app[0], "%s\n[%d]" % [app[1], i + 1], _open_app.bind(app[0]), false, 120.0)


func _open_app(app: String) -> void:
	match app:
		"Map":
			close()
			main.open_menu("go")
		"Cars":
			show_page("cars")
		"Games":
			show_page("games")
		"Build":
			close()
			if main.walker and main.builder:
				if not main.builder.active:
					main.builder.toggle()
			else:
				main.hud.set_status("Out of the car first (Enter): you build on foot", 3.0)
		"Friends":
			close()
			main.open_menu("multiplayer")
		"Settings":
			close()
			main.open_menu("settings")


# Your cars: the one picked waits parked on the road nearest you (marked), or, driving, you're in it at once
func _cars_page() -> void:
	_title.text = "Cars"
	for body in GameSettings.CAR_ORDER:
		var yours: bool = main.car != null and main.car.body == body
		var about: String = GameSettings.CAR_ABOUT.get(body, "")
		_card(_content, GameSettings.CAR_NAMES[body], ("Yours now. " if yours else "") + about.get_slice(".", 0) + ".", func():
			close()
			main.park_car(body), yours)
	_content.add_child(_label("Planes", 18, ORANGE, true))
	_card(_content, "At the airports", "Walk up to a plane stand and press E. Find an airport in Map.", func():
		close()
		main.open_menu("go"))


# The games round you (the rest are found by going there): how far and which way, the one you're playing
func _games_page() -> void:
	_title.text = "Games"
	var hunt: Node = main.hunt
	if hunt == null:
		_note("No games here.")
		return
	if hunt.hunting >= 0:
		_card(_content, "Stop playing", hunt.reserves[hunt.hunting]["name"], func():
			close()
			hunt.toggle_game(), true)
	var player: Node3D = main.walker if main.walker else main.car
	var here: PackedFloat64Array = main.world.to_lat_lon(player.global_position) if player and main.world else PackedFloat64Array([0.0, 0.0])
	# (tabs: the games round you, or every place of a kind on your maps, nearest first)
	var tabs := HFlowContainer.new()
	tabs.add_theme_constant_override("h_separation", 4)
	tabs.add_theme_constant_override("v_separation", 4)
	_content.add_child(tabs)
	for tab in [["", "Near"], ["track", "Tracks"], ["golf", "Golf"], ["reserve", "Reserves"], ["park", "Parks"]]:
		var chip := Button.new()
		chip.text = "  %s  " % tab[1]
		chip.add_theme_font_size_override("font_size", 15)
		if events_kind == tab[0]:
			chip.add_theme_color_override("font_color", ORANGE)
		chip.pressed.connect(func(): events_kind = tab[0]; show_page("games"))
		tabs.add_child(chip)
	if events_kind != "":
		_events(here)
		return
	_card(_content, "+  Make a game here", "Its area round where you stand, its booth at your feet", func():
		close()
		main.builder.make_game_here())
	var booths: Array = hunt.booths()
	booths.sort_custom(func(a, b): return absf(a["lat"] - here[0]) + absf(a["lon"] - here[1]) < absf(b["lat"] - here[0]) + absf(b["lon"] - here[1]))
	for booth in booths.slice(0, 8):
		var reserve: Dictionary = hunt.reserves[booth["area"]]
		var state: String = {"playing": "Playing now. ", "ticket": "Your ticket's ready. "}.get(booth["state"], "")
		_card(_content, booth["name"], state + "Booth " + hunt.way_to(here[0], here[1], booth["lat"], booth["lon"]) + ". Go to it", func():
			close()
			main.teleport(booth["lat"], booth["lon"], booth["name"]), booth["state"] != "tickets")
		if reserve.get("generated", false):
			var copy := Button.new()
			copy.text = "Make it yours (change it)"
			copy.add_theme_font_size_override("font_size", 13)
			copy.pressed.connect(func():
				close()
				main.builder.make_game_here(reserve))
			_content.add_child(copy)
	if booths.is_empty():
		_note("No games round here yet: the tabs above have every race track, golf course, reserve and park on your maps.")
	_note("In a game's area, F1 plays it (or stops it).")


# Every place of the tab's kind on your maps (the world makes a game at each), nearest first: Go takes you to its start
func _events(here: PackedFloat64Array) -> void:
	var search := LineEdit.new()
	search.placeholder_text = "Search by name..."
	search.text = events_text
	search.text_submitted.connect(func(t: String): events_text = t; show_page("games"))
	_content.add_child(search)
	var found: Array = main.game_gen.events(events_kind, here, events_text, 30) if main.game_gen else []
	for place in found:
		var far: String = ("%.1f km" % place["km"]) if place["km"] < 100.0 else ("%d km" % roundi(place["km"]))
		_card(_content, place["name"], "%s, %s away. Go" % [main.game_gen.KINDS.get(place["kind"], place["kind"]), far], func():
			close()
			var at: PackedFloat64Array = main.game_gen.start_of(place)
			main.teleport(at[0], at[1], place["name"]))
	if found.is_empty():
		_note("None on your maps%s. Get more of the world: the Map, the world map." % ((" called \"%s\"" % events_text) if events_text != "" else ""))
