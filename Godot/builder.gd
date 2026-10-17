# Building, as in Rust: on foot, B and you're building. A see-through piece shows where it would go, snapped to the
# building you're looking at (or, for a foundation, a new one where you stand), green if it can go there and red,
# with why, if not (nothing holding it up, the ground not under it, something there already). The rules are Core's
# (GeoBuild: GeodriveStructure); this is the hands.
#   B: build mode on / off     left click: put the piece there     right click: take out the piece you look at
#   the bar along the bottom: every piece by its key, what it's made of, a prop being put
#   Q: the build menu: Place (the pieces, the material, a wall's size; your things; your games) and Make something new
#   mouse wheel or 1-9, 0, -, =: which piece (in ORDER)     T: which material (wood, brick, concrete)     R: turn stairs
#   and roofs
#   E (on foot, building or not): open or shut the door you look at
#   right click on a building of the map's (not one you built): it's outlined; right click again and it's taken away, to
#   build there (kept in user://removed_buildings.json by its map id; the menu brings them back)
# What's built is kept on the real map (user://buildings.json: each building's place, height and heading, and its
# pieces), and comes back whenever its area is built.
extends Node

const REACH := 14.0	# m: how far you can build
const SAVE_PATH := "user://buildings.json"
const TEST_PATH := "user://buildings_test.json"	# (a test's: begun empty each run, never the game's)
const REMOVED_PATH := "user://removed_buildings.json"	# the map's buildings taken away (their map ids)
const REMOVED_TEST_PATH := "user://removed_buildings_test.json"
const CUT_PATH := "user://cut_plants.json"	# the map's trees and bushes cut down (where each stood, and which)
const CUT_TEST_PATH := "user://cut_plants_test.json"
const CONFIRM := 4.0	# s: a map building outlined waits this long for the second right click
const KEYS := [KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL]
const DOOR := 7	# (GeoBuild's kinds: the door...)
const RAILING := 9	# (...the railing: wooden, or steel in brick or concrete)
# the pieces in the order the keys and the wheel go through them (GeoBuild's kinds: 0 foundation, 1 floor, 2 wall,
# 3 doorway, 7 door, 4 window, 10 glass wall, 8 half wall, 9 railing, 11 pillar, 5 stairs, 6 roof)
const ORDER := [0, 1, 2, 3, 7, 4, 10, 8, 9, 11, 5, 6]
const WALL := 2	# (the plain wall: the one with sizes)
# the build menu's pages: the pieces by kind of work
const CATEGORIES := [["Foundations & floors", [0, 1]], ["Walls", [2, 8, 10]], ["Doors & windows", [3, 7, 4]],
	["Posts & rails", [11, 9]], ["Stairs & roofs", [5, 6]]]
const SIZES := [[1, "Half high"], [2, "Half wide"], [4, "Thin"]]	# (GeoBuild's SizeFlag)

var main: Node
var world: Node
var active := false
var kind := 0	# (GeoBuild's: see ORDER)
var material := 0	# (0 wood, 1 brick, 2 concrete)
var turn := 0	# R: stairs and roofs turned on from the way you look
var size := 0	# a plain wall's size (SIZES' flags together)
var _menu: PanelContainer	# the build menu (Q)
var _picked := 0	# the map building outlined (its id; 0 none) and till when
var _picked_plant := {}	# (or the tree or bush outlined: vegetation.gd's plant_along)
var _picked_until := 0.0
var _mark: MeshInstance3D	# (its outline)
var _page := 1	# its page (CATEGORIES; past them, the workshop)
var workshop: Workshop	# (the workshop's page: weapons and props made)
var props: PropKeeper	# (props: put on the map in build mode, kept there)
var game_maker: GameMaker	# (the workshop's games: an area marked on the map, made a game)
const WORKSHOP_PAGE := 5	# (the menu's pages past the pieces': Make, your things, your games)
const PROPS_PAGE := 6
const GAMES_PAGE := 7
var _root: Node3D	# the buildings (moved with Godot's 0)
var _ghost: MeshInstance3D
var _ok_paint: StandardMaterial3D
var _no_paint: StandardMaterial3D
var _cross: Label
var _aim: Dictionary = {}	# what a click would do now
var _saved: Array = []	# every building kept: [{lat, lon, y, yaw, pieces}]
var _shown: Dictionary = {}	# saved index -> its GeodriveStructure, those in the built area


func _ready() -> void:
	props = PropKeeper.new()
	props.name = "Props"
	props.builder = self
	props.main = main
	props.world = world
	add_child(props)
	game_maker = GameMaker.new()
	game_maker.name = "GameMaker"
	game_maker.main = main
	game_maker.builder = self
	add_child(game_maker)
	_root = Node3D.new()
	_root.name = "Buildings"
	main.add_child(_root)
	_ghost = MeshInstance3D.new()
	_ghost.name = "BuildPreview"
	_ghost.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_ghost.visible = false
	main.add_child(_ghost)
	_ok_paint = _see_through(Color(0.3, 1.0, 0.4, 0.4))
	_no_paint = _see_through(Color(1.0, 0.25, 0.2, 0.4))
	var layer := CanvasLayer.new()
	add_child(layer)
	_cross = Label.new()
	_cross.text = "+"
	_cross.add_theme_font_size_override("font_size", 28)
	_cross.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_cross.visible = false
	layer.add_child(_cross)
	_menu = PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.07, 0.08, 0.09, 0.92)
	style.set_corner_radius_all(10)
	style.set_content_margin_all(18)
	_menu.add_theme_stylebox_override("panel", style)
	_menu.visible = false
	layer.add_child(_menu)
	_make_bar(layer)
	_mark = MeshInstance3D.new()
	_mark.name = "MapBuildingMark"
	_mark.mesh = ImmediateMesh.new()
	_mark.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var red := StandardMaterial3D.new()
	red.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	red.albedo_color = Color(1.0, 0.2, 0.15)
	red.no_depth_test = true
	_mark.material_override = red
	main.add_child(_mark)
	_load()


# The buildings' look (structure.gdshader): their finishes' textures, each with its mean colour (made once)
static var _look: ShaderMaterial = null
static func look() -> ShaderMaterial:
	if _look == null:
		_look = ShaderMaterial.new()
		_look.shader = preload("res://structure.gdshader")
		for finish in [["planks", "planks"], ["brick", "brick"], ["concrete", "concrete"], ["tiles", "roof"]]:
			var texture := _texture("res://textures/buildings/%s_color.jpg" % finish[1])
			_look.set_shader_parameter(finish[0] + "_color", texture)
			_look.set_shader_parameter(finish[0] + "_mean", GroundLook.mean_colour(texture))
	return _look


# The buildings' glass (building_glass.gdshader): clear, a mirror at a glancing angle (made once)
static var _glass: ShaderMaterial = null
static func glass() -> ShaderMaterial:
	if _glass == null:
		_glass = ShaderMaterial.new()
		_glass.shader = preload("res://building_glass.gdshader")
	return _glass


# A texture: imported (the editor's), or straight from its file if it isn't yet (with its smaller versions)
static func _texture(path: String) -> Texture2D:
	if ResourceLoader.exists(path):
		return load(path)
	var image := Image.load_from_file(ProjectSettings.globalize_path(path))
	if image == null:
		return null
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


func _new_structure() -> GeodriveStructure:
	var structure := GeodriveStructure.new()
	structure.name = "Building"
	(structure.get_node("Look") as MeshInstance3D).material_override = look()
	(structure.get_node("Glass") as MeshInstance3D).material_override = glass()
	_root.add_child(structure)
	return structure


func _see_through(colour: Color) -> StandardMaterial3D:
	var paint := StandardMaterial3D.new()
	paint.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	paint.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	paint.albedo_color = colour
	paint.no_depth_test = false
	return paint


func toggle() -> void:
	props.stop()
	active = not active
	_ghost.visible = false
	_cross.visible = active
	if not active:
		_show_menu(false)
	_update_bar()
	if active:
		_tell("")
	else:
		main.hud.set_status("Building done", 1.5)


func _tell(why: String) -> void:
	main.hud.set_status("%s%s      R: turn   E: door   B: done" % [_piece_name(), ("   - " + why) if why != "" else ""], 0.6)


# What's chosen, in words: "Brick wall (half wide, thin)"
func _piece_name() -> String:
	var stuff: String = GeodriveStructure.material_name(material)
	if kind == DOOR or (kind == RAILING and material == 0):
		stuff = "Wooden"
	elif kind == RAILING:
		stuff = "Steel"
	var sizes := []
	if kind == WALL:
		for entry in SIZES:
			if size & entry[0]:
				sizes.append(entry[1].to_lower())
	return "%s %s%s" % [stuff, GeodriveStructure.kind_name(kind).to_lower(), (" (" + ", ".join(sizes) + ")") if not sizes.is_empty() else ""]


# --- the bar along the bottom (building) and the build menu (Q) ---
# The bar: every piece by its key, the one in hand lit, what it's made of (T), and a prop being put; Q for the rest.
# The menu: Place (the pieces, your things, your games: one grid each, picked with the chips along its top) and,
# always at its top right, Make something new (workshop.gd: what, from what, then making it).

const PLACE_PAGES := [[0, "Building"], [PROPS_PAGE, "My things"], [GAMES_PAGE, "Games"]]
const KEY_NAMES := ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="]
var _bar: HBoxContainer
var _bar_now := ""	# (what it shows: made again only when that changes)


func _make_bar(layer: CanvasLayer) -> void:
	_bar = HBoxContainer.new()
	_bar.add_theme_constant_override("separation", 4)
	_bar.visible = false
	layer.add_child(_bar)


func _update_bar() -> void:
	var now := "%s %d %d %d %s" % [active and not menu_open(), kind, material, size, props.chosen]
	if now == _bar_now:
		return
	_bar_now = now
	for child in _bar.get_children():
		_bar.remove_child(child)
		child.queue_free()
	_bar.visible = active and not menu_open()
	if not _bar.visible:
		return
	for at in ORDER.size():
		var k: int = ORDER[at]
		var slot := _menu_button("%s\n%s" % [KEY_NAMES[at], GeodriveStructure.kind_name(k)], k == kind and props.chosen == "", 80.0, 52.0)
		slot.add_theme_font_size_override("font_size", 13)
		slot.pressed.connect(func(): kind = k; props.stop())
		_bar.add_child(slot)
	var stuff := _menu_button("T\n%s" % GeodriveStructure.material_name(material), false, 80.0, 52.0)
	stuff.add_theme_font_size_override("font_size", 13)
	stuff.pressed.connect(func(): material = (material + 1) % GeodriveStructure.material_count())
	_bar.add_child(stuff)
	if props.chosen != "" and props.kinds.has(props.chosen):
		var thing := _menu_button("R turns\n%s" % props.kinds[props.chosen]["name"], true, 120.0, 52.0)
		thing.add_theme_font_size_override("font_size", 13)
		_bar.add_child(thing)
	var more := _menu_button("Q\nEverything", false, 96.0, 52.0)
	more.add_theme_font_size_override("font_size", 13)
	more.pressed.connect(func(): _show_menu(true))
	_bar.add_child(more)
	# (in the middle at the bottom, over the line of text there)
	_bar.reset_size()
	_bar.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM, Control.PRESET_MODE_MINSIZE, 70)


func menu_open() -> bool:
	return _menu != null and _menu.visible


func _show_menu(on: bool) -> void:
	if on == menu_open():
		return
	_menu.visible = on
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if on else Input.MOUSE_MODE_CAPTURED
	if on:
		_fill_menu()
	_update_bar()


# The game maker on a game here (the phone's Games): a new one round where you stand (its booth at your feet), or a copy
# of one the world made (Of) to change, yours
func make_game_here(of: Dictionary = {}) -> void:
	if main.walker == null:
		main.hud.set_status("Out of the car first (Enter): games are made on foot", 3.0)
		return
	if not active:
		toggle()
	if workshop == null:
		workshop = Workshop.new(self)
	workshop.thing = "game"
	workshop.step = "edit"
	if of.is_empty():
		game_maker.new_game()
		var here: PackedFloat64Array = world.to_lat_lon(main.walker.global_position)
		game_maker.game["booth"] = [here[0], here[1]]
		game_maker.game["outline"] = main.game_gen._square_round(here, 300.0)
		game_maker.game["name"] = "My game here"
	else:
		game_maker.edit_game(of)
		game_maker.game["id"] = ""
		game_maker.game["name"] = "My " + String(of["name"])
	_page = WORKSHOP_PAGE
	_show_menu(true)


# Make something new: the workshop's first step (what you're making)
func make_new() -> void:
	if workshop == null:
		workshop = Workshop.new(self)
	workshop.step = "kind"
	_page = WORKSHOP_PAGE
	_fill_menu()


# The menu as it is now: Place (its chips, the page's grid) or Make (the workshop)
func _fill_menu() -> void:
	for child in _menu.get_children():
		_menu.remove_child(child)	# (out of the layout at once, so the menu's fitted to the new alone)
		child.queue_free()
	var rows := VBoxContainer.new()
	rows.add_theme_constant_override("separation", 12)
	_menu.add_child(rows)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 6)
	rows.add_child(top)
	var title := Label.new()
	title.add_theme_font_size_override("font_size", 26)
	title.custom_minimum_size = Vector2(110.0, 0.0)
	top.add_child(title)
	if _page == WORKSHOP_PAGE:
		title.text = "Make"
		var back := _menu_button("Back to placing", false, 170.0)
		back.pressed.connect(func(): _page = 0; _fill_menu())
		top.add_child(back)
		if workshop == null:
			workshop = Workshop.new(self)
		if workshop.step != "kind":
			var restart := _menu_button("Something else", false, 150.0)
			restart.pressed.connect(make_new)
			top.add_child(restart)
		workshop.fill(rows)
		fit_menu()
		return
	title.text = "Place"
	for entry in PLACE_PAGES:
		var lit: bool = _page == entry[0] or (entry[0] == 0 and _page < CATEGORIES.size())
		var chip := _menu_button(entry[1], lit, 130.0)
		chip.pressed.connect(func(): _page = entry[0]; _fill_menu())
		top.add_child(chip)
	var gap := Control.new()
	gap.custom_minimum_size = Vector2(40.0, 0.0)
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(gap)
	var make := _menu_button("+  Make something new", true, 230.0)
	make.pressed.connect(make_new)
	top.add_child(make)
	match _page:
		PROPS_PAGE:
			_fill_things(rows)
		GAMES_PAGE:
			_fill_games(rows)
		_:
			_fill_building(rows)
	fit_menu()


# Building: the pieces by kind of work (a column each), what they're made of, a wall's size, the map's taken away
func _fill_building(rows: VBoxContainer) -> void:
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 10)
	rows.add_child(columns)
	for category in CATEGORIES:
		var column := VBoxContainer.new()
		columns.add_child(column)
		var heading := _menu_label(category[0])
		heading.modulate = Color(1, 1, 1, 0.65)
		column.add_child(heading)
		for k in category[1]:
			var button := _menu_button("%s   [%s]" % [GeodriveStructure.kind_name(k), KEY_NAMES[ORDER.find(k)]], k == kind and props.chosen == "", 170.0, 48.0)
			button.pressed.connect(_pick.bind(k))
			column.add_child(button)
	var stuff := HBoxContainer.new()
	rows.add_child(stuff)
	stuff.add_child(_menu_label("Made of  [T]"))
	for m in GeodriveStructure.material_count():
		var button := _menu_button(GeodriveStructure.material_name(m), m == material, 110.0)
		button.pressed.connect(func(): material = m; _fill_menu())
		stuff.add_child(button)
	if kind == WALL:
		var sized := HBoxContainer.new()
		rows.add_child(sized)
		sized.add_child(_menu_label("Wall size"))
		for entry in SIZES:
			var button := _menu_button(entry[1], (size & entry[0]) != 0, 110.0)
			button.pressed.connect(func(): size ^= entry[0]; _fill_menu())
			sized.add_child(button)
	var taken: int = (world.get_removed_buildings().size() if world else 0) + _cut_list.size()
	if taken > 0:
		var back := HBoxContainer.new()
		rows.add_child(back)
		back.add_child(_menu_label("Taken away"))
		var undo := _menu_button("Bring back the %d map buildings and trees" % taken, false, 360.0)
		undo.pressed.connect(func(): bring_back(); _fill_menu())
		back.add_child(undo)
	_hint(rows, "Click: put it where you look.   Right click: take it out (a map building, tree or bush: twice).   Q: close")


# My things: what you've made, to put on the map (a click), or to change
func _fill_things(rows: VBoxContainer) -> void:
	var grid := HFlowContainer.new()
	grid.custom_minimum_size = Vector2(900.0, 0.0)
	grid.add_theme_constant_override("h_separation", 8)
	grid.add_theme_constant_override("v_separation", 8)
	rows.add_child(grid)
	var ids: Array = props.kinds.keys()
	ids.sort()
	for id in ids:
		var card := VBoxContainer.new()
		grid.add_child(card)
		var button := _menu_button(String(props.kinds[id]["name"]), props.chosen == id, 170.0, 56.0)
		button.pressed.connect(func():
			props.chosen = id
			props.turn = 0.0
			_show_menu(false))
		card.add_child(button)
		var change := _menu_button("Change it", false, 170.0, 26.0)
		change.add_theme_font_size_override("font_size", 12)
		change.pressed.connect(func():
			if workshop == null:
				workshop = Workshop.new(self)
			workshop.thing = "prop"
			workshop.edit_prop(props.kinds[id])
			workshop.step = "edit"
			_page = WORKSHOP_PAGE
			_fill_menu())
		card.add_child(change)
	# (guns you've made: changed here; they're at the ticket booths)
	for load in GeodriveHunt.loads():
		if not String(load["id"]).begins_with("my_"):
			continue
		var gun := _menu_button("%s\n(a gun: change it)" % load["name"], false, 170.0, 84.0)
		gun.pressed.connect(func():
			if workshop == null:
				workshop = Workshop.new(self)
			workshop.thing = "weapon"
			workshop.copy_of(load)
			workshop.draft["name"] = load["name"]
			workshop.step = "edit"
			_page = WORKSHOP_PAGE
			_fill_menu())
		grid.add_child(gun)
	if grid.get_child_count() == 0:
		rows.add_child(_menu_label("Nothing yet: Make something new (top right)"))
	_hint(rows, "Choose one, then click to put it where you look (R turns it). Right click a thing twice: take it away.")


# Games: yours to change, or a new one
func _fill_games(rows: VBoxContainer) -> void:
	var grid := HFlowContainer.new()
	grid.custom_minimum_size = Vector2(900.0, 0.0)
	grid.add_theme_constant_override("h_separation", 8)
	grid.add_theme_constant_override("v_separation", 8)
	rows.add_child(grid)
	var open_game := func(reserve: Dictionary):
		if workshop == null:
			workshop = Workshop.new(self)
		workshop.thing = "game"
		workshop.step = "edit"
		if reserve.is_empty():
			game_maker.new_game()
		else:
			game_maker.edit_game(reserve)
		_page = WORKSHOP_PAGE
		_fill_menu()
	var fresh := _menu_button("+  A new game", false, 200.0, 56.0)
	fresh.pressed.connect(open_game.bind({}))
	grid.add_child(fresh)
	for reserve in main.hunt.reserves:
		if reserve.get("made", false):
			var button := _menu_button("%s\n(change it)" % reserve["name"], false, 200.0, 56.0)
			button.pressed.connect(open_game.bind(reserve))
			grid.add_child(button)
	_hint(rows, "A game is an area of the real map (or a race's course) with its booth, creatures, weapons and rules.")


func _hint(rows: VBoxContainer, text: String) -> void:
	var hint := Label.new()
	hint.text = text
	hint.modulate = Color(1, 1, 1, 0.65)
	rows.add_child(hint)


# The menu its contents' size, in the middle: now, and again once they're laid out (a page first laid out can ask for
# more than it needs: its colours one a line, before its width's known; the menu kept that size, off the screen)
func fit_menu() -> void:
	_menu.reset_size()
	_menu.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	(func():
		if is_instance_valid(_menu):
			_menu.reset_size()
			_menu.set_anchors_and_offsets_preset(Control.PRESET_CENTER)).call_deferred()


# A piece chosen in the menu: it shuts, but for a wall (its sizes are chosen next)
func _pick(k: int) -> void:
	kind = k
	props.stop()	# (a piece now, not a prop)
	if k == WALL:
		_fill_menu()
	else:
		_show_menu(false)


func _menu_button(text: String, lit: bool, width: float, height: float = 40.0) -> Button:
	var button := Button.new()
	button.text = text
	button.custom_minimum_size = Vector2(width, height)
	button.focus_mode = Control.FOCUS_NONE
	for state in ["normal", "hover", "pressed"]:
		var style := StyleBoxFlat.new()
		style.bg_color = {"normal": Color(0.14, 0.15, 0.17), "hover": Color(0.22, 0.23, 0.26), "pressed": Color(0.3, 0.2, 0.12)}[state]
		if lit:
			style.bg_color = Color(0.18, 0.32, 0.2) if state == "normal" else Color(0.24, 0.4, 0.26)
		style.set_corner_radius_all(6)
		style.set_content_margin_all(6)
		button.add_theme_stylebox_override(state, style)
	if lit:
		button.add_theme_color_override("font_color", Color(0.45, 1.0, 0.55))
		button.add_theme_color_override("font_hover_color", Color(0.45, 1.0, 0.55))
	return button


func _menu_label(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.custom_minimum_size = Vector2(120.0, 0.0)
	return label


func _unhandled_input(event: InputEvent) -> void:
	if not active or main.walker == null or main.menu.is_open():
		return
	if event is InputEventKey and event.pressed and not event.echo and (event.keycode == KEY_Q or (event.keycode == KEY_ESCAPE and menu_open())):
		_show_menu(not menu_open())
		get_viewport().set_input_as_handled()
		return
	if menu_open():
		if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_T:
			material = (material + 1) % GeodriveStructure.material_count()
			_fill_menu()
		return	# (the menu's clicks are its own)
	if event is InputEventMouseButton and event.pressed:
		match event.button_index:
			MOUSE_BUTTON_LEFT:
				_put()
			MOUSE_BUTTON_RIGHT:
				_take_out()
			MOUSE_BUTTON_WHEEL_UP:
				kind = ORDER[(ORDER.find(kind) + ORDER.size() - 1) % ORDER.size()]
			MOUSE_BUTTON_WHEEL_DOWN:
				kind = ORDER[(ORDER.find(kind) + 1) % ORDER.size()]
	elif event is InputEventKey and event.pressed and not event.echo:
		var at := KEYS.find(event.keycode)
		if at >= 0 and at < ORDER.size():
			kind = ORDER[at]
			props.stop()
		elif event.keycode == KEY_T:
			material = (material + 1) % GeodriveStructure.material_count()
		elif event.keycode == KEY_R and props.chosen != "":
			props.turn = fmod(props.turn + PropKeeper.TURN_STEP, 360.0)
		elif event.keycode == KEY_R:
			turn = (turn + 1) % 4


func _process(_delta: float) -> void:
	if _picked != 0 and Time.get_ticks_msec() / 1000.0 > _picked_until:
		_picked = 0
		_outline(PackedVector3Array(), 0.0)
	if not active:
		return
	if main.walker == null:
		toggle()	# (back in the car: building's for on foot)
		return
	_update_bar()
	if props.chosen != "":
		# (a prop being put: it, not a piece)
		_ghost.visible = false
		props.aim()
		return
	_aim = _find_aim()
	if _aim.is_empty():
		_ghost.visible = false
		_tell("look at the ground or a building, within %d m" % roundi(REACH))
		return
	_ghost.mesh = GeodriveStructure.make_piece_mesh(kind, material, _aim["slot"])
	_ghost.global_transform = _aim["frame"]
	_ghost.material_override = _ok_paint if _aim["why"] == "" else _no_paint
	_ghost.visible = true
	_tell(_aim["why"])


# --- where the piece would go ---

func _ray() -> Dictionary:
	var camera: Camera3D = main.camera
	var from := camera.global_position
	var to := from - camera.global_transform.basis.z * REACH
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = [main.walker.get_rid()]
	return main.get_world_3d().direct_space_state.intersect_ray(query)


func _structure_of(collider: Object) -> GeodriveStructure:
	if collider is Node and collider.get_parent() is GeodriveStructure:
		return collider.get_parent()
	return null


# What a click would do: which building (or a new one: "frame" its place), which slot, and why not ("" it can)
func _find_aim() -> Dictionary:
	var hit := _ray()
	if hit.is_empty():
		return {}
	var facing := -(main.camera.global_transform.basis.z as Vector3)
	var structure := _structure_of(hit["collider"])
	if structure == null:
		structure = _nearby(hit["position"])
	if structure != null:
		var local: Vector3 = structure.to_local(hit["position"])
		var slot: Dictionary = structure.snap(kind, local, structure.global_transform.basis.inverse() * facing)
		slot["turn"] = (int(slot["turn"]) + turn) % 4
		slot["size"] = size if kind == WALL else 0
		var ground := _ground_under(structure.global_transform, slot) if kind == 0 else Vector2.ZERO
		return {"structure": structure, "slot": slot, "frame": structure.global_transform, "ground": ground,
			"why": structure.can_place(kind, slot, ground.x, ground.y)}
	# nothing built here: a new building, its first foundation where you look, its grid square to the way you face
	if kind != 0:
		return {"structure": null, "slot": {"x": 0, "y": 0, "level": 0}, "frame": Transform3D(), "ground": Vector2.ZERO, "why": "Start with a foundation (1)"}
	var yaw := atan2(-facing.x, -facing.z)
	var basis := Basis(Vector3.UP, yaw)
	var cell: float = GeodriveStructure.cell()
	var at: Vector3 = hit["position"] - basis * Vector3(cell * 0.5, 0.0, -cell * 0.5)
	at.y = hit["position"].y
	var slot := {"x": 0, "y": 0, "level": 0, "side": 0, "turn": 0}
	# (its top 20 cm over the highest ground under its square, as a builder sets a slab on a slope)
	var frame := Transform3D(basis, at)
	frame.origin.y += _ground_under(frame, slot).y + 0.2
	var ground := _ground_under(frame, slot)
	var why := ""
	if ground.y > 0.05 or ground.x < -GeodriveStructure.cell():	# (Core's foundation reaches 3 m down)
		why = "A foundation must stand on the ground"
	return {"structure": null, "slot": slot, "frame": frame, "ground": ground, "why": why}


# A building whose grid a new piece here would join (within a square of its pieces), or null
func _nearby(at: Vector3) -> GeodriveStructure:
	var cell: float = GeodriveStructure.cell()
	for structure in _root.get_children():
		if not structure is GeodriveStructure:
			continue
		var local: Vector3 = structure.to_local(at)
		var x := local.x / cell
		var y := -local.z / cell
		for piece in structure.get_pieces():
			if x > piece["x"] - 1.0 and x < piece["x"] + 2.0 and y > piece["y"] - 1.0 and y < piece["y"] + 2.0:
				return structure
	return null


# The lowest and highest ground under a square (its corners and middle), as heights in the building's frame
func _ground_under(frame: Transform3D, slot: Dictionary) -> Vector2:
	var cell: float = GeodriveStructure.cell()
	var space: PhysicsDirectSpaceState3D = main.get_world_3d().direct_space_state
	var low := INF
	var high := -INF
	for corner in [Vector2(0.05, 0.05), Vector2(0.95, 0.05), Vector2(0.05, 0.95), Vector2(0.95, 0.95), Vector2(0.5, 0.5)]:
		var local := Vector3((slot["x"] + corner.x) * cell, 0.0, -(slot["y"] + corner.y) * cell)
		var top := frame * (local + Vector3.UP * 4.0)
		var bottom := frame * (local + Vector3.DOWN * 8.0)
		var query := PhysicsRayQueryParameters3D.create(top, bottom)
		var skip: Array[RID] = []
		if main.walker:
			skip.append(main.walker.get_rid())
		for structure in _root.get_children():
			skip.append((structure.get_node("Solid") as StaticBody3D).get_rid())
		query.exclude = skip
		var hit := space.intersect_ray(query)
		var h: float = (frame.affine_inverse() * hit["position"]).y if not hit.is_empty() else -100.0
		low = minf(low, h)
		high = maxf(high, h)
	return Vector2(low, high)


# --- the hands ---

func _put() -> void:
	if props.chosen != "":
		props.put()
		return
	if _aim.is_empty() or _aim["why"] != "":
		return
	var structure: GeodriveStructure = _aim["structure"]
	if structure == null:
		structure = _new_structure()
		structure.global_transform = _aim["frame"]
	var why: String = structure.place(kind, material, _aim["slot"], _aim["ground"].x, _aim["ground"].y)
	if why != "":
		main.hud.set_status(why, 2.0)
		if structure.get_piece_count() == 0:
			structure.queue_free()
		return
	_keep(structure)


func _take_out() -> void:
	var hit := _ray()
	if not hit.is_empty() and props.take(hit):
		return	# (a prop: outlined, or taken away)
	# (a tree or bush in your sights, nearer than what the ray met: plants aren't solid, so it passes through them)
	if main.vegetation:
		var camera: Camera3D = main.camera
		var reach: float = REACH if hit.is_empty() else camera.global_position.distance_to(hit["position"]) + 1.0
		var plant: Dictionary = main.vegetation.plant_along(camera.global_position, -camera.global_transform.basis.z, reach)
		if not plant.is_empty():
			_cut(plant)
			return
	if hit.is_empty():
		return
	var structure := _structure_of(hit["collider"])
	if structure == null:
		_take_away(hit)
		return
	var index: int = structure.piece_at(structure.to_local(hit["position"] - (hit["normal"] as Vector3) * 0.05))
	if index < 0:
		return
	var down: int = structure.remove_piece(index)
	if down > 1:
		main.hud.set_status("%d pieces came down with it" % (down - 1), 2.0)
	if structure.get_piece_count() == 0:
		_forget(structure)
		structure.queue_free()
	else:
		_keep(structure)


# A building of the map's right clicked: outlined the first time, taken away the second (to build there)
func _take_away(hit: Dictionary) -> void:
	var found: Dictionary = world.building_at(hit["position"] - (hit["normal"] as Vector3) * 0.3)
	if not found.get("found", false):
		return
	var now := Time.get_ticks_msec() / 1000.0
	var id: int = found["id"]
	if id == _picked and now < _picked_until:
		world.remove_building(id)
		_save_removed()
		_picked = 0
		_outline(PackedVector3Array(), 0.0)
		main.hud.set_status("The map's building taken away (the build menu, Q, brings them back)", 3.0)
		return
	_picked = id
	_picked_plant = {}
	_picked_until = now + CONFIRM
	_outline(found["outline"], found["height"])
	main.hud.set_status("A building of the map's: right click again to take it away", CONFIRM)


# A tree or bush of the map's: right click it twice to cut it down (kept, where it stood, in user://cut_plants.json)
func _cut(plant: Dictionary) -> void:
	var now := Time.get_ticks_msec() / 1000.0
	var at: Vector3 = plant["position"]
	var what := "bush" if plant["channel"] == 1 else "tree"
	if not _picked_plant.is_empty() and _picked_plant["fx"] == plant["fx"] and _picked_plant["fz"] == plant["fz"] \
			and _picked_plant["channel"] == plant["channel"] and now < _picked_until:
		world.remove_plant(plant["channel"], plant["fx"], plant["fz"])
		var lat_lon: PackedFloat64Array = world.to_lat_lon(at)
		_cut_list.append({"lat": lat_lon[0], "lon": lat_lon[1], "channel": plant["channel"]})
		_save_cut()
		main.vegetation.replace_now()
		_picked_plant = {}
		_outline(PackedVector3Array(), 0.0)
		main.hud.set_status("The %s's cut down (the build menu, Q, brings them back)" % what, 3.0)
		return
	_picked = 0
	_picked_plant = plant
	_picked_until = now + CONFIRM
	# (a ring round it at a man's height, and down to the ground)
	var ring := PackedVector3Array()
	var radius := 0.8 if plant["channel"] == 1 else 1.5
	var tall := 1.2 if plant["channel"] == 1 else 3.0
	for k in 12:
		ring.append(at + Vector3(cos(k * TAU / 12.0) * radius, tall, sin(k * TAU / 12.0) * radius))
	_outline(ring, tall)
	main.hud.set_status("A %s: right click again to cut it down" % what, CONFIRM)


var _cut_list: Array = []	# (the plants cut down: {lat, lon, channel})
var _cut_loaded := false


func _cut_path() -> String:
	return CUT_TEST_PATH if main.testing else CUT_PATH


func _save_cut() -> void:
	var file := FileAccess.open(_cut_path(), FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({"cut": _cut_list}))


# The plants cut down, from the file: each's fine square found again where it stood (the squares are world-fixed metres:
# Godot's, but for where its 0 is)
func _load_cut() -> void:
	if main.testing and not _cut_loaded:
		DirAccess.remove_absolute(CUT_TEST_PATH)	# (each test from nothing)
	_cut_loaded = true
	_cut_list = []
	if FileAccess.file_exists(_cut_path()):
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(_cut_path()))
		if parsed is Dictionary:
			_cut_list = parsed.get("cut", [])
	_apply_cut()


func _apply_cut() -> void:
	if world == null:
		return
	var offset: Vector3 = world.get_render_offset()
	var plants := []
	for entry in _cut_list:
		var at: Vector3 = world.from_lat_lon(entry["lat"], entry["lon"]) + offset
		var fine: float = VegetationLook.BUSH if int(entry["channel"]) == 1 else VegetationLook.FINE
		plants.append([int(entry["channel"]), int(floor(at.x / fine)), int(floor(at.z / fine))])
	world.set_removed_plants(plants)
	if main.vegetation:
		main.vegetation.replace_now()
	if not plants.is_empty():
		print("Geodrive: %d of the map's trees and bushes cut down" % plants.size())


# A map building's outline drawn round its top and down its corners (none: nothing)
func _outline(points: PackedVector3Array, height: float) -> void:
	var lines: ImmediateMesh = _mark.mesh
	lines.clear_surfaces()
	if points.size() < 2:
		return
	lines.surface_begin(Mesh.PRIMITIVE_LINES)
	for i in points.size():
		var a := points[i]
		var b := points[(i + 1) % points.size()]
		for p in [a, b, a, a - Vector3.UP * height]:
			lines.surface_add_vertex(p)
	lines.surface_end()


func _removed_path() -> String:
	return REMOVED_TEST_PATH if main.testing else REMOVED_PATH


func _save_removed() -> void:
	var file := FileAccess.open(_removed_path(), FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({"removed": Array(world.get_removed_buildings())}))


func _load_removed() -> void:
	if main.testing:
		DirAccess.remove_absolute(REMOVED_TEST_PATH)
	if not FileAccess.file_exists(_removed_path()) or world == null:
		return
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(_removed_path()))
	if parsed is Dictionary:
		var ids := PackedInt64Array()
		for id in parsed.get("removed", []):
			ids.append(int(id))
		world.set_removed_buildings(ids)
		print("Geodrive: %d of the map's buildings taken away" % ids.size())


# The menu's "bring them back": every map building taken away comes back, and every tree and bush cut down
func bring_back() -> void:
	world.set_removed_buildings(PackedInt64Array())
	_save_removed()
	_cut_list = []
	_save_cut()
	_apply_cut()
	main.hud.set_status("The map's buildings and trees are back", 2.0)


# E: the door looked at (within a few metres) opened or shut
func use() -> bool:
	if main.walker == null:
		return false
	var hit := _ray()
	if hit.is_empty() or (hit["position"] as Vector3).distance_to(main.camera.global_position) > 3.5:
		return false
	var structure := _structure_of(hit["collider"])
	if structure == null:
		return false
	var index: int = structure.piece_at(structure.to_local(hit["position"] - (hit["normal"] as Vector3) * 0.02))
	if index < 0 or structure.get_piece_kind(index) != DOOR:
		return false
	structure.toggle_door(index)
	_keep(structure)
	return true


# --- kept on the map ---

func _keep(structure: GeodriveStructure) -> void:
	var lat_lon: PackedFloat64Array = world.to_lat_lon(structure.global_position)
	var entry := {"lat": lat_lon[0], "lon": lat_lon[1], "y": structure.global_position.y, "yaw": structure.global_rotation.y,
		"pieces": structure.get_pieces()}
	var index: int = structure.get_meta("saved", -1)
	if index < 0:
		index = _saved.size()
		_saved.append(entry)
		structure.set_meta("saved", index)
		_shown[index] = structure
	else:
		_saved[index] = entry
	_save()


func _forget(structure: GeodriveStructure) -> void:
	var index: int = structure.get_meta("saved", -1)
	if index >= 0:
		_saved[index] = {}	# (kept as a gap: the others' numbers stay)
		_shown.erase(index)
		_save()


# Where they're kept: the game's file, or in a test run its own (tests had put their houses in the game's)
func _path() -> String:
	return TEST_PATH if main.testing else SAVE_PATH


func _save() -> void:
	var file := FileAccess.open(_path(), FileAccess.WRITE)
	if file:
		file.store_string(JSON.stringify({"buildings": _saved.filter(func(e): return not e.is_empty())}))


func _load() -> void:
	_load_removed()
	_load_cut()
	if main.testing:
		DirAccess.remove_absolute(TEST_PATH)	# (each test from nothing)
	if not FileAccess.file_exists(_path()):
		return
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(_path()))
	if parsed is Dictionary:
		_saved = parsed.get("buildings", [])
	print("Geodrive: %d buildings kept" % _saved.size())


# The kept buildings in the built area put up (each second, as areas come in)
var _check := 0.0
func physics(delta: float) -> void:
	props.physics(delta)
	_check -= delta
	if _check > 0.0 or world == null or not world.is_ready():
		return
	_check = 1.0
	for index in _saved.size():
		var entry: Dictionary = _saved[index]
		if entry.is_empty() or _shown.has(index) or not world.contains_lat_lon(entry["lat"], entry["lon"]):
			continue
		var structure := _new_structure()
		var at: Vector3 = world.from_lat_lon(entry["lat"], entry["lon"])
		structure.global_transform = Transform3D(Basis(Vector3.UP, entry["yaw"]), Vector3(at.x, entry["y"], at.z))
		var pieces: Array = entry["pieces"]
		for piece in pieces:
			for key in ["kind", "material", "x", "y", "level", "side", "turn", "part", "size"]:
				piece[key] = int(piece.get(key, 0))	# (JSON's numbers are floats; parts and sizes since walls had them)
		structure.set_pieces(pieces)
		structure.set_meta("saved", index)
		_shown[index] = structure


# --buildtest: a double-storey house (2 x 2 squares: concrete foundations, brick walls with a door and windows, a
# concrete first floor, wooden stairs, brick walls again with a glass wall at the back, a tiled gable roof), and a
# veranda along its front (wooden pillars, a wooden roof, a brick half wall and a steel railing), as a builder would
func build_test_house(at: Vector3, yaw: float) -> Node3D:
	var space: PhysicsDirectSpaceState3D = main.get_world_3d().direct_space_state
	var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(at + Vector3.UP * 30.0, at + Vector3.DOWN * 60.0))
	if hit.is_empty():
		print("Geodrive build test: no ground there")
		return null
	var structure := _new_structure()
	# (its floor 20 cm over the highest ground under the whole house, as a builder sets the slab on a slope)
	var frame := Transform3D(Basis(Vector3.UP, yaw), hit["position"])
	var highest := -INF
	for x in 2:
		for y in 2:
			highest = maxf(highest, _ground_under(frame, {"x": x, "y": y}).y)
	frame.origin.y += highest + 0.2
	structure.global_transform = frame
	var refused := 0
	var put := func(k: int, m: int, x: int, y: int, level: int, side: int = 0, t: int = 0) -> void:
		var slot := {"x": x, "y": y, "level": level, "side": side, "turn": t}
		var ground := _ground_under(structure.global_transform, slot) if k == 0 else Vector2.ZERO
		var why: String = structure.place(k, m, slot, ground.x, ground.y)
		if why != "":
			refused += 1
			print("Geodrive build test: %s at %s refused: %s (ground %.2f to %.2f m)" % [GeodriveStructure.kind_name(k), slot, why, ground.x, ground.y])
	for x in 2:
		for y in 2:
			put.call(0, 2, x, y, 0)
	for level in 2:
		for i in 2:
			put.call(3 if i == 0 and level == 0 else 2, 1, i, 0, level, 1)
			put.call(10 if level == 1 and i == 0 else 2, 1, i, 2, level, 1)	# (the back: a glass wall upstairs)
			put.call(4 if i == 1 else 2, 1, 0, i, level, 0)
			put.call(4 if i == 1 else 2, 1, 2, i, level, 0)
		if level == 0:
			for x in 2:
				for y in 2:
					if not (x == 1 and y == 1):
						put.call(1, 2, x, y, 1)
			put.call(5, 0, 1, 1, 0, 0, 1)
	for x in 2:
		put.call(6, 1, x, 0, 2, 0, 1)
		put.call(6, 1, x, 1, 2, 0, 3)
	put.call(DOOR, 0, 0, 0, 0, 1)	# (the front door, shut)
	# the veranda: two foundations along the front, pillars at their outer corners, a roof over them (held by the
	# pillars and the front wall), a half wall and a railing along its edge
	for x in 2:
		put.call(0, 2, x, -1, 0)
	for x in 3:
		put.call(11, 0, x, -1, 0)
	for x in 2:
		put.call(1, 0, x, -1, 1)
	put.call(8, 1, 0, -1, 0, 1)
	put.call(RAILING, 2, 1, -1, 0, 1)
	print("Geodrive build test: %d pieces, %.1f t, %d refused" % [structure.get_piece_count(), structure.get_mass(), refused])
	_keep(structure)
	return structure


# --buildtest=stairs: two foundations and a flight of stairs on the second, rising away; where the walker starts (on
# the first, at the flight's foot) and which way is up the flight
func build_test_stairs(at: Vector3, yaw: float) -> Dictionary:
	var space: PhysicsDirectSpaceState3D = main.get_world_3d().direct_space_state
	var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(at + Vector3.UP * 30.0, at + Vector3.DOWN * 60.0))
	if hit.is_empty():
		return {}
	var structure := _new_structure()
	var frame := Transform3D(Basis(Vector3.UP, yaw), hit["position"])
	var highest := maxf(_ground_under(frame, {"x": -1, "y": 0}).y, _ground_under(frame, {"x": 0, "y": 0}).y)
	frame.origin.y += highest + 0.2
	structure.global_transform = frame
	for x in [-1, 0]:
		var ground := _ground_under(frame, {"x": x, "y": 0})
		var refused: String = structure.place(0, 2, {"x": x, "y": 0, "level": 0}, ground.x, ground.y)
		if refused != "":
			print("Geodrive build test: foundation %d refused: %s (ground %.2f to %.2f m)" % [x, refused, ground.x, ground.y])
	var why: String = structure.place(5, 0, {"x": 0, "y": 0, "level": 0, "turn": 0})
	print("Geodrive build test: stairs %s; %d pieces" % [why if why != "" else "up", structure.get_piece_count()])
	return {"start": frame * Vector3(-1.2, 0.05, -1.5), "up": frame.basis.x, "floor": frame.origin.y}


# A new area: what was shown goes (it comes back if this area has it)
func reset_area() -> void:
	for child in _root.get_children():
		child.queue_free()
	_shown = {}
	_root.position = Vector3.ZERO
	active = false
	_ghost.visible = false
	_cross.visible = false
	_apply_cut()	# (a new area: the cut plants' squares found again in it)
	props.reset_area()


func shift(offset: Vector3) -> void:
	props.shift(offset)
	_root.position += offset
	_mark.position += offset
	_ghost.global_position += offset
