# The workshop (the build menu's Make): guns and objects made with the shape editor, games on the map. Weapons made,
# as a file in your own pack (the Saved folder's
# packs/workshop/weapons/), on offer at every ticket booth from then on. Start from a weapon there is, or a cartridge
# (its real bullet, drag and muzzle speed); then how it's worked (bolt, semi, auto and its rate), its magazine, the
# rounds carried, its sights, the model in the hands; the barrel nudges the muzzle speed. Its kick is its own: the
# bullet's momentum (a .308 hunting rifle's 1), less for a submachine gun's weight. What it carries is shown as it's
# made: energy at the muzzle and 100 m out, the drop there.
class_name Workshop
extends RefCounted

# real cartridges: [name, bullet grains, G1 ballistic coefficient, muzzle m/s (a typical barrel)]
const CARTRIDGES := [
	["9x19 mm Parabellum, 124 gr", 124.0, 0.15, 380.0],
	[".22 Long Rifle, 40 gr", 40.0, 0.125, 330.0],
	["5.56x45 mm NATO, 62 gr", 62.0, 0.304, 920.0],
	["7.62x39 mm, 123 gr", 123.0, 0.295, 715.0],
	[".243 Winchester, 100 gr", 100.0, 0.405, 902.0],
	[".308 Winchester, 150 gr", 150.0, 0.387, 860.0],
	[".375 H&H Magnum, 300 gr", 300.0, 0.398, 771.0],
	["12 gauge slug, 437 gr", 437.0, 0.08, 490.0],
]
const PREVIEW := "workshop_preview"	# (the draft, as Core has it, for what it carries)

var builder: Node
var draft := {}
var tab := 1	# (a gun's right-hand side: 1 its shape, 0 what it does)
var thing := "weapon"	# (what's being made: a weapon, a prop, a game)
var prop := {}	# (the prop being made: {id, name, look})
var prop_editor: ShapeEditor
var editor: ShapeEditor	# (how it looks: the shape editor, on the draft's look)
const CREATURE_MAKER := preload("res://creature_maker.gd")
var creature: RefCounted	# (the creature being made: creature_maker.gd)


func _init(owner: Node) -> void:
	builder = owner
	start_from(CARTRIDGES[0], "Submachine gun")
	draft["action"] = "auto"
	draft["rate"] = 800.0
	draft["magazine"] = 30
	draft["rounds"] = 150
	draft["sight"] = "iron"
	draft["model"] = "smg"
	draft["zero"] = 50.0
	draft["reload_time"] = 2.5
	_new_look()


# The draft's look: its model's own to start from (till it's been changed, it follows the model)
func _new_look() -> void:
	draft["look"] = PartModel.default_look(String(draft.get("model", "rifle"))).duplicate(true)
	draft["look_changed"] = false
	editor = ShapeEditor.new(draft["look"], builder)
	editor.changed.connect(func(): draft["look_changed"] = true)


# A cartridge's bullet: the draft's (a name if it has none)
func start_from(cartridge: Array, name: String = "") -> void:
	draft["grains"] = cartridge[1]
	draft["bc"] = cartridge[2]
	draft["muzzle"] = cartridge[3]
	draft["cartridge"] = cartridge[0]
	if name != "":
		draft["name"] = name


# A weapon there is: the draft its copy (renamed, so it's a new one)
func copy_of(load: Dictionary) -> void:
	for key in ["grains", "bc", "muzzle", "zero", "sight_height", "magazine", "arrow", "action", "rate", "rounds", "reload_time", "sight", "model"]:
		draft[key] = load[key]
	draft["name"] = String(load["name"]) + " (mine)"
	draft["cartridge"] = load["name"]
	_new_look()
	if load.get("look") is Dictionary and not load["look"].get("parts", []).is_empty():
		draft["look"] = load["look"].duplicate(true)
		draft["look_changed"] = true
		editor = ShapeEditor.new(draft["look"], builder)


# The draft as a weapon's file: its id from its name; its kick from its momentum
func as_weapon() -> Dictionary:
	var id := "my_" + String(draft["name"]).to_lower().validate_filename().replace(" ", "_").replace(",", "")
	var momentum: float = float(draft["grains"]) * float(draft["muzzle"]) / (150.0 * 860.0)
	var kick := clampf(momentum * (0.6 if draft.get("model", "rifle") == "smg" else 1.0), 0.05, 3.0)
	var iron: bool = draft.get("sight", "scope") == "iron"
	return {"id": id, "name": draft["name"], "grains": draft["grains"], "bc": draft["bc"], "muzzle": draft["muzzle"],
		"zero": draft.get("zero", 50.0 if iron else 100.0), "sight_height": draft.get("sight_height", 0.03 if iron else 0.045),
		"magazine": int(draft["magazine"]), "arrow": draft.get("arrow", false), "action": draft["action"],
		"rate": float(draft.get("rate", 0.0)) if draft["action"] == "auto" else 0.0, "rounds": maxi(int(draft["rounds"]), int(draft["magazine"])),
		"reload_time": draft.get("reload_time", 3.0), "sight": draft["sight"], "model": draft["model"], "kick": snappedf(kick, 0.01),
		"look": draft.get("look", {})}


# What it carries, as Core flies it: {energy (ft-lb at the muzzle), energy_100, drop_100 (m)}; {} if it isn't a weapon
func carries() -> Dictionary:
	var preview := as_weapon()
	preview["id"] = PREVIEW
	preview["name"] = "(being made)"
	if GeodriveHunt.add_load(preview) < 0:
		return {}
	for load in GeodriveHunt.loads():
		if load["id"] == PREVIEW:
			return load
	return {}


# Kept: the weapon's file in your pack, and Core told (at every booth now); its id, or "" (and why, in the log)
func save() -> String:
	var weapon := as_weapon()
	if GeodriveHunt.add_load(weapon) < 0:
		return ""
	var pack := Packs.workshop()
	DirAccess.make_dir_recursive_absolute(pack.path_join("weapons"))
	if not FileAccess.file_exists(pack.path_join("pack.json")):
		var about := FileAccess.open(pack.path_join("pack.json"), FileAccess.WRITE)
		about.store_string(JSON.stringify({"name": "My workshop", "about": "What I've made in the game's workshop."}, "\t") + "\n")
	var file := FileAccess.open(pack.path_join("weapons").path_join(weapon["id"] + ".json"), FileAccess.WRITE)
	if file == null:
		return ""
	file.store_string(JSON.stringify(weapon, "\t", false) + "\n")
	file.close()
	builder.main.net.share_packs()	# (hosting: everyone has it)
	return weapon["id"]


# --- its screen in the build menu: Make ---
# Three steps, one thing each: what you're making (big cards), what to start from (a template, or one of yours), then
# one screen: its 3D view on the left; on the right its shape's controls, or (a gun) what it does. Its name, Save and
# Place it / Take it along the top; "Make it a ..." keeps its shape and makes it the other kind.

const KINDS := [["weapon", "Gun", "Made to shoot: its cartridge, how it fires, its sights. At every ticket booth once saved"],
	["prop", "Object", "Anything to put on the map: furniture, signs, what's in a shed"],
	["creature", "Creature", "An animal or a beast: its body, coat and horns; how fast, how many, what it does"],
	["game", "Game", "On the real map: its area or a race's course, its creatures and weapons, its rules"]]
# objects to start from: their parts (part_model.gd's)
const OBJECTS := {
	"Blank": [{"shape": "box", "at": [0.0, 0.25, 0.0], "size": [0.5, 0.5, 0.5], "turn": [0, 0, 0], "colour": "#9a9a9a", "finish": "matte"}],
	"Crate": [{"shape": "box", "at": [0.0, 0.3, 0.0], "size": [0.6, 0.6, 0.6], "turn": [0, 0, 0], "colour": "#7a4a26", "finish": "wood"}],
	"Table": [{"shape": "box", "at": [0.0, 0.74, 0.0], "size": [0.9, 0.04, 1.6], "turn": [0, 0, 0], "colour": "#8a5a32", "finish": "wood"},
		{"shape": "box", "at": [0.38, 0.36, 0.72], "size": [0.06, 0.72, 0.06], "turn": [0, 0, 0], "colour": "#6a4224", "finish": "wood"},
		{"shape": "box", "at": [-0.38, 0.36, 0.72], "size": [0.06, 0.72, 0.06], "turn": [0, 0, 0], "colour": "#6a4224", "finish": "wood"},
		{"shape": "box", "at": [0.38, 0.36, -0.72], "size": [0.06, 0.72, 0.06], "turn": [0, 0, 0], "colour": "#6a4224", "finish": "wood"},
		{"shape": "box", "at": [-0.38, 0.36, -0.72], "size": [0.06, 0.72, 0.06], "turn": [0, 0, 0], "colour": "#6a4224", "finish": "wood"}],
	"Bench": [{"shape": "box", "at": [0.0, 0.45, 0.0], "size": [0.4, 0.05, 1.5], "turn": [0, 0, 0], "colour": "#8a5a32", "finish": "wood"},
		{"shape": "box", "at": [0.0, 0.22, 0.65], "size": [0.36, 0.44, 0.06], "turn": [0, 0, 0], "colour": "#333333", "finish": "metal"},
		{"shape": "box", "at": [0.0, 0.22, -0.65], "size": [0.36, 0.44, 0.06], "turn": [0, 0, 0], "colour": "#333333", "finish": "metal"}],
}
const ABOUT := {"Blank": "One box: build up from it", "Crate": "60 cm, wood", "Table": "160 cm, four legs", "Bench": "150 cm, wood and steel"}
var step := "edit"	# kind (what you're making), start (from what), edit


func _kind_name(of: String) -> String:
	for k in KINDS:
		if k[0] == of:
			return k[1]
	return of


# A big card: a bold name, a line under it
func _card(parent: Control, title: String, detail: String, action: Callable, size := Vector2(260.0, 110.0)) -> Button:
	var button: Button = builder._menu_button("", false, size.x, size.y)
	button.pressed.connect(action)
	var lines := VBoxContainer.new()
	lines.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT, Control.PRESET_MODE_MINSIZE, 10)
	lines.alignment = BoxContainer.ALIGNMENT_CENTER
	lines.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for line in [[title, 20, Color(0.95, 0.95, 0.95)], [detail, 13, Color(0.72, 0.72, 0.75)]]:
		var label := Label.new()
		label.text = line[0]
		label.add_theme_font_size_override("font_size", line[1])
		label.add_theme_color_override("font_color", line[2])
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		lines.add_child(label)
	button.add_child(lines)
	parent.add_child(button)
	return button


func fill(rows: VBoxContainer) -> void:
	var refill := func(): builder._fill_menu()
	match step:
		"kind":
			rows.add_child(builder._menu_label("What are you making?"))
			var cards := HBoxContainer.new()
			cards.add_theme_constant_override("separation", 12)
			rows.add_child(cards)
			for k in KINDS:
				_card(cards, k[1], k[2], func():
					thing = k[0]
					step = "edit" if thing == "game" else "start"
					if thing == "game":
						builder.game_maker.new_game()
					refill.call())
			return
		"start":
			_fill_start(rows)
			return
	if thing == "game":
		builder.game_maker.fill(rows)
		return
	if thing == "creature":
		_fill_creature(rows)
		return
	var gun := thing == "weapon"
	if not gun and prop.is_empty():
		new_prop()
	var shape: ShapeEditor = editor if gun else prop_editor
	# along the top: its name, keeping it, using it, making it the other kind
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 8)
	rows.add_child(top)
	top.add_child(builder._menu_label(_kind_name(thing) + ":"))
	var name_edit := LineEdit.new()
	name_edit.text = draft["name"] if gun else prop["name"]
	name_edit.custom_minimum_size = Vector2(300.0, 0.0)
	name_edit.text_changed.connect(func(t: String):
		if gun:
			draft["name"] = t
		else:
			prop["name"] = t)
	top.add_child(name_edit)
	if gun:
		_save_buttons(top)
	else:
		var keep: Button = builder._menu_button("Save it", false, 120.0)
		keep.pressed.connect(func():
			var id := save_prop()
			builder.main.hud.set_status(("Saved: %s (Q, My things)" % prop["name"]) if id != "" else "Couldn't save it", 3.0))
		top.add_child(keep)
		var place: Button = builder._menu_button("Place it", false, 120.0)
		place.pressed.connect(func():
			var id := save_prop()
			if id != "":
				builder.props.chosen = id
				builder.props.turn = 0.0
				builder._show_menu(false))
		top.add_child(place)
	var other := "prop" if gun else "weapon"
	var change: Button = builder._menu_button("Make it %s" % ("an object" if other == "prop" else "a gun"), false, 170.0)
	change.tooltip_text = "Change what this is: its shape stays"
	change.pressed.connect(func(): change_kind(other); refill.call())
	top.add_child(change)
	# its view, and on the right its shape's controls or (a gun) what it does
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 14)
	rows.add_child(row)
	var left := VBoxContainer.new()
	row.add_child(left)
	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 6)
	right.custom_minimum_size = Vector2(560.0, 0.0)
	row.add_child(right)
	shape.fill_view(left)
	if gun:
		var pages := HBoxContainer.new()
		right.add_child(pages)
		for at in [1, 0]:
			var page: Button = builder._menu_button(["What it does", "Its shape"][at], tab == at, 180.0, 34.0)
			page.pressed.connect(func(): tab = at; refill.call())
			pages.add_child(page)
	if gun and tab == 0:
		_fill_weapon(right)
	else:
		var controls := VBoxContainer.new()	# (its own: the editor empties it as it fills it again)
		controls.add_theme_constant_override("separation", 6)
		right.add_child(controls)
		shape.fill_panel(controls)


# Start from: a template, or one of yours (a gun: a copy of one there is)
func _fill_start(rows: VBoxContainer) -> void:
	var refill := func(): builder._fill_menu()
	rows.add_child(builder._menu_label("%s: start from" % _kind_name(thing)))
	var cards := HFlowContainer.new()
	cards.custom_minimum_size = Vector2(1000.0, 0.0)
	cards.add_theme_constant_override("h_separation", 10)
	cards.add_theme_constant_override("v_separation", 10)
	rows.add_child(cards)
	var go_edit := func():
		step = "edit"
		tab = 1
		refill.call()
	var card_size := Vector2(220.0, 80.0)
	if thing == "creature":
		for kind in GeodriveHunt.species_list():
			if String(kind["id"]) == "workshop_preview":
				continue
			var yours := String(kind["id"]).begins_with("my_")
			_card(cards, String(kind["name"]), ("Yours: change it" if yours else "%d kg" % roundi(float(kind["mass"]))), func():
				creature = CREATURE_MAKER.new(builder)
				creature.start_from(kind["id"])
				go_edit.call(), Vector2(180.0, 84.0))
		return
	if thing == "weapon":
		_card(cards, "Submachine gun", "9 mm, full auto, plain sights", func(): _start_gun("smg"); go_edit.call(), card_size)
		_card(cards, "Hunting rifle", ".308, bolt action, a scope", func(): _start_gun("rifle"); go_edit.call(), card_size)
		for load in GeodriveHunt.loads().filter(func(l): return l["id"] != PREVIEW):
			_card(cards, String(load["name"]), "A copy to change", func(): copy_of(load); go_edit.call(), card_size)
	else:
		for template in OBJECTS:
			_card(cards, template, ABOUT[template], func():
				prop = {"id": "", "name": "My " + String(template).to_lower(), "look": {"parts": OBJECTS[template].duplicate(true)}}
				prop_editor = ShapeEditor.new(prop["look"], builder, "prop")
				go_edit.call(), card_size)
		var ids: Array = builder.props.kinds.keys()
		ids.sort()
		for id in ids:
			_card(cards, String(builder.props.kinds[id]["name"]), "Yours: change it", func(): edit_prop(builder.props.kinds[id]); go_edit.call(), card_size)


# A creature: its name and Save along the top; its view on the left; its shape or what it does on the right
func _fill_creature(rows: VBoxContainer) -> void:
	var refill := func(): builder._fill_menu()
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 8)
	rows.add_child(top)
	top.add_child(builder._menu_label("Creature:"))
	var name_edit := LineEdit.new()
	name_edit.text = creature.data["name"]
	name_edit.custom_minimum_size = Vector2(300.0, 0.0)
	name_edit.text_changed.connect(func(t: String): creature.data["name"] = t)
	top.add_child(name_edit)
	var keep: Button = builder._menu_button("Save it", false, 120.0)
	keep.tooltip_text = "Then it's in the game maker's creatures"
	keep.pressed.connect(func():
		var id: String = creature.save()
		builder.main.hud.set_status(("Saved: %s. Put it in a game: Make, Game" % creature.data["name"]) if id != "" else "Couldn't save it: see the log", 4.0))
	top.add_child(keep)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 14)
	rows.add_child(row)
	var left := VBoxContainer.new()
	row.add_child(left)
	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 6)
	right.custom_minimum_size = Vector2(560.0, 0.0)
	row.add_child(right)
	creature.fill_view(left)
	var pages := HBoxContainer.new()
	right.add_child(pages)
	for at in [1, 0]:
		var page: Button = builder._menu_button(["What it does", "Its shape"][at], creature.tab == at, 180.0, 34.0)
		page.pressed.connect(func(): creature.tab = at; refill.call())
		pages.add_child(page)
	if creature.tab == 1:
		creature.fill_shape(right)
	else:
		creature.fill_does(right)


# A gun to start from: a submachine gun or a hunting rifle, its own look
func _start_gun(model: String) -> void:
	if model == "smg":
		start_from(CARTRIDGES[0], "My submachine gun")
		draft.merge({"action": "auto", "rate": 800.0, "magazine": 30, "rounds": 150, "sight": "iron", "zero": 50.0, "reload_time": 2.5}, true)
	else:
		start_from(CARTRIDGES[5], "My hunting rifle")
		draft.merge({"action": "bolt", "rate": 0.0, "magazine": 4, "rounds": 20, "sight": "scope", "zero": 100.0, "reload_time": 3.0}, true)
	draft["model"] = model
	_new_look()


# Change what this is (its shape kept, at the new kind's scale): an object made a gun (a submachine gun's workings to
# start from), a gun made an object
func change_kind(to: String) -> void:
	if to == thing:
		return
	if to == "prop":
		prop = {"id": "", "name": draft["name"], "look": draft["look"].duplicate(true)}
		prop_editor = ShapeEditor.new(prop["look"], builder, "prop")
	elif to == "weapon":
		var name: String = prop.get("name", "My gun")
		var look: Dictionary = prop.get("look", {"parts": []}).duplicate(true)
		_start_gun("smg")
		draft["name"] = name
		draft["look"] = look
		draft["look_changed"] = true
		editor = ShapeEditor.new(draft["look"], builder)
		editor.changed.connect(func(): draft["look_changed"] = true)
	thing = to


# What a gun does: its cartridge, how it fires, its magazine, sights; what it carries
func _fill_weapon(rows: VBoxContainer) -> void:
	var line := func(label: String) -> HBoxContainer:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 8)
		rows.add_child(row)
		row.add_child(builder._menu_label(label))
		return row
	var refill := func(): builder._fill_menu()
	var cartridge: HBoxContainer = line.call("Cartridge")
	var picker := OptionButton.new()
	for c in CARTRIDGES.size():
		picker.add_item(CARTRIDGES[c][0])
		if CARTRIDGES[c][0] == draft.get("cartridge", ""):
			picker.select(c)
	picker.item_selected.connect(func(i: int): start_from(CARTRIDGES[i]); refill.call())
	cartridge.add_child(picker)
	var worked: HBoxContainer = line.call("Fires")
	for action in [["bolt", "Bolt action"], ["semi", "Semi-auto"], ["auto", "Full auto"]]:
		var button: Button = builder._menu_button(action[1], draft["action"] == action[0], 120.0)
		button.pressed.connect(func(): draft["action"] = action[0]; refill.call())
		worked.add_child(button)
	if draft["action"] == "auto":
		_slider(line.call("Rate (a minute)"), "rate", 300.0, 1200.0, 50.0, "%d rounds")
	_slider(line.call("Magazine"), "magazine", 1.0, 100.0, 1.0, "%d rounds")
	_slider(line.call("Rounds carried"), "rounds", 10.0, 600.0, 10.0, "%d")
	_slider(line.call("Muzzle (barrel)"), "muzzle", 200.0, 1100.0, 5.0, "%d m/s")
	var sights: HBoxContainer = line.call("Sights")
	for sight in [["scope", "Scope"], ["iron", "Plain sights"]]:
		var button: Button = builder._menu_button(sight[1], draft["sight"] == sight[0], 120.0)
		button.pressed.connect(func(): draft["sight"] = sight[0]; draft["zero"] = 100.0 if sight[0] == "scope" else 50.0; refill.call())
		sights.add_child(button)
	var held: HBoxContainer = line.call("Held like")
	for model in [["rifle", "A rifle"], ["smg", "A submachine gun"]]:
		var button: Button = builder._menu_button(model[1], draft["model"] == model[0], 170.0)
		button.pressed.connect(func():
			draft["model"] = model[0]
			if not draft.get("look_changed", false):
				_new_look()	# (not changed yet: the model's own)
			refill.call())
		held.add_child(button)
	var stats := Label.new()
	stats.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	stats.custom_minimum_size = Vector2(540.0, 0.0)
	var carried := carries()
	var weapon := as_weapon()
	stats.text = ("%s: %d ft-lb at the muzzle, %d at 100 m (%.0f cm low there, zeroed at %d m); kick %.2f of a .308's%s" % [
		draft["cartridge"], roundi(carried.get("energy", 0.0)), roundi(carried.get("energy_100", 0.0)), float(carried.get("drop_100", 0.0)) * 100.0,
		roundi(weapon["zero"]), weapon["kick"], ("; %.0f rounds a second" % (float(weapon["rate"]) / 60.0)) if weapon["action"] == "auto" else ""]) \
		if not carried.is_empty() else "Not a weapon yet: see the log for why"
	stats.modulate = Color(0.75, 0.95, 1.0)
	rows.add_child(stats)


# --- props ---

# A new prop to make: a wooden crate to start from
func new_prop() -> void:
	prop = {"id": "", "name": "My crate", "look": {"parts": OBJECTS["Crate"].duplicate(true)}}
	prop_editor = ShapeEditor.new(prop["look"], builder, "prop")


func edit_prop(existing: Dictionary) -> void:
	prop = existing.duplicate(true)
	prop_editor = ShapeEditor.new(prop["look"], builder, "prop")


# Kept: its file in your pack (props/), there at once to be put; its id
func save_prop() -> String:
	if prop.is_empty():
		return ""
	prop["id"] = "my_" + String(prop["name"]).to_lower().validate_filename().replace(" ", "_").replace(",", "")
	var folder := Packs.workshop().path_join("props")
	DirAccess.make_dir_recursive_absolute(folder)
	_pack_file()
	var file := FileAccess.open(folder.path_join(prop["id"] + ".json"), FileAccess.WRITE)
	if file == null:
		return ""
	file.store_string(JSON.stringify(prop, "\t", false) + "\n")
	file.close()
	builder.props.add_kind(prop.duplicate(true))
	builder.main.net.share_packs()
	return prop["id"]


func _pack_file() -> void:
	var pack := Packs.workshop()
	DirAccess.make_dir_recursive_absolute(pack)
	if not FileAccess.file_exists(pack.path_join("pack.json")):
		var about := FileAccess.open(pack.path_join("pack.json"), FileAccess.WRITE)
		about.store_string(JSON.stringify({"name": "My workshop", "about": "What I've made in the game's workshop."}, "\t") + "\n")


# Save it; take it now (with a ticket)
func _save_buttons(row: Container) -> void:
	var keep: Button = builder._menu_button("Save it", false, 120.0)
	keep.tooltip_text = "At every ticket booth from then on"
	keep.pressed.connect(func():
		var id := save()
		builder.main.hud.set_status(("Saved: %s, at every ticket booth now" % draft["name"]) if id != "" else "Couldn't save it: see the log", 4.0))
	row.add_child(keep)
	var hunt: Node = builder.main.hunt
	if hunt and hunt.ticket >= 0:
		var take: Button = builder._menu_button("Take it now", false, 140.0)
		take.pressed.connect(func():
			var id := save()
			if id != "":
				hunt._buy(hunt.ticket, GeodriveHunt.loads().filter(func(l): return l["id"] == id)[0])
				builder._show_menu(false))
		row.add_child(take)


func _slider(row: HBoxContainer, key: String, low: float, high: float, step_by: float, shown: String) -> void:
	var slider := HSlider.new()
	slider.min_value = low
	slider.max_value = high
	slider.step = step_by
	slider.value = float(draft.get(key, low))
	slider.custom_minimum_size = Vector2(260.0, 24.0)
	slider.focus_mode = Control.FOCUS_NONE
	var value := Label.new()
	value.text = shown % roundi(slider.value)
	slider.value_changed.connect(func(v: float):
		draft[key] = v
		value.text = shown % roundi(v))
	slider.drag_ended.connect(func(_changed: bool): builder._fill_menu())	# (what it carries, again)
	row.add_child(slider)
	row.add_child(value)
