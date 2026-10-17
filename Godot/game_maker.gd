# The game maker (the workshop's Games): a game made on the real map, no code. Its area marked on the M map (a click a
# corner, right click takes the last back, Enter when it's done, Esc to stop), its booth where you stand, what's in it
# (how many of each creature there is), which weapons its booth offers, and how it goes: its creatures spread over it
# or a pool round the players, out always or only while it's played, a fence round it or not, the hunting law or none.
# Saved as an area (a reserve) in your pack (packs/workshop/reserves/): its booth's there at once, F1 plays it.
class_name GameMaker
extends Node

var main: Node
var builder: Node
var game := {}	# the one being made: {id, name, outline [[lat, lon]...], booth [lat, lon] or [], stock {id: count},
				# weapons [ids] (none: all), pool, out ("always", "playing"), fence, law}
var marking := false	# (marking on the M map: its area, or its checkpoints)
var marking_what := "area"
var _marked_from_menu := false


func _ready() -> void:
	new_game()


func new_game() -> void:
	game = {"id": "", "name": "My game", "outline": [], "booth": [], "stock": {}, "weapons": [], "pool": true, "out": "playing", "fence": false, "law": false,
		"rules": [], "checkpoints": [], "laps": 0}


# A game made before: the draft its own again (from its area's file)
func edit_game(reserve: Dictionary) -> void:
	var stock := {}
	for entry in reserve.get("stock", []):
		stock[String(entry[0])] = int(entry[1])
	var booth: Array = []
	for gate in reserve.get("gates", []):
		if gate.get("tickets", false):
			booth = [gate["lat"], gate["lon"]]
	game = {"id": reserve.get("id", ""), "name": reserve["name"], "outline": reserve["outline"].duplicate(true), "booth": booth, "stock": stock,
		"weapons": reserve.get("weapons", []).duplicate(), "pool": reserve.get("spawn", {}).get("around_people", false),
		"out": reserve.get("creatures_out", "always"), "fence": reserve.get("fence", true), "law": reserve.get("texts", {}).get("law", true),
		"rules": reserve.get("rules", []).duplicate(true), "checkpoints": reserve.get("checkpoints", []).duplicate(true), "laps": int(reserve.get("laps", 0))}


# --- the area, on the M map ---

func start_marking(what: String = "area") -> void:
	marking = true
	marking_what = what
	game["outline" if what == "area" else "checkpoints"] = []
	builder._show_menu(false)
	if not main.camera.map:
		main.camera.toggle_map()
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	main.hud.set_status(("Mark the game's area: click each corner on the map" if what == "area" else "Mark the race's checkpoints in order, the start first: click each on the map")
		+ " (right click: take the last back; the wheel zooms). Enter: done. Esc: stop", 8.0)


func _unhandled_input(event: InputEvent) -> void:
	if not marking:
		return
	if event is InputEventMouseButton and event.pressed and event.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
		var points: Array = game["outline"] if marking_what == "area" else game["checkpoints"]
		if event.button_index == MOUSE_BUTTON_LEFT:
			if marking_what == "area":
				add_corner(ground_at(event.position))
			else:
				add_checkpoint(ground_at(event.position))
		elif not points.is_empty():
			points.pop_back()
		get_viewport().set_input_as_handled()
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode in [KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE]:
		finish_marking(event.keycode != KEY_ESCAPE)
		get_viewport().set_input_as_handled()


# Where a point on the M map is on the ground (what the view from above shows there; past the solid ground, the
# car's height)
func ground_at(screen: Vector2) -> Vector3:
	var camera: Camera3D = main.camera
	var from := camera.project_ray_origin(screen)
	var way := camera.project_ray_normal(screen)
	var hit: Dictionary = main.get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(from, from + way * 30000.0))
	if not hit.is_empty():
		return hit["position"]
	var level: float = main.car.global_position.y if main.car else 0.0
	return from + way * ((level - from.y) / way.y) if absf(way.y) > 1e-6 else from


func add_corner(at: Vector3) -> void:
	var lat_lon: PackedFloat64Array = main.world.to_lat_lon(at)
	game["outline"].append([lat_lon[0], lat_lon[1]])


# A race's checkpoint (its circle 8 m round)
func add_checkpoint(at: Vector3) -> void:
	var lat_lon: PackedFloat64Array = main.world.to_lat_lon(at)
	game["checkpoints"].append({"lat": lat_lon[0], "lon": lat_lon[1], "radius": 8.0})


func finish_marking(keep: bool) -> void:
	marking = false
	var area := marking_what == "area"
	if keep and area and game["outline"].size() < 3:
		main.hud.set_status("An area needs at least 3 corners", 3.0)
		keep = false
	if keep and not area and game["checkpoints"].size() < 2:
		main.hud.set_status("A race needs at least 2 checkpoints (its start and finish)", 3.0)
		keep = false
	if not keep:
		game["outline" if area else "checkpoints"] = []
	if main.camera.map:
		main.camera.toggle_map()
	# (back to the workshop's games)
	builder._page = builder.WORKSHOP_PAGE
	builder._show_menu(true)


# Its area in hectares (its outline, flat)
func hectares() -> float:
	var outline: Array = game["outline"]
	if outline.size() < 3:
		return 0.0
	var lat0: float = outline[0][0]
	var sum := 0.0
	for i in outline.size():
		var a: Array = outline[i]
		var b: Array = outline[(i + 1) % outline.size()]
		var ax := (float(a[1]) - float(outline[0][1])) * 111320.0 * cos(deg_to_rad(lat0))
		var ay := (float(a[0]) - lat0) * 110574.0
		var bx := (float(b[1]) - float(outline[0][1])) * 111320.0 * cos(deg_to_rad(lat0))
		var by := (float(b[0]) - lat0) * 110574.0
		sum += ax * by - bx * ay
	return absf(sum) * 0.5 / 10000.0


# Its outline drawn on the M map as it's marked: the corners, the sides (closing back to the first), its size
func draw(canvas: Control, camera: Camera3D) -> void:
	if marking_what == "checkpoints":
		_draw_checkpoints(canvas, camera)
		return
	var points := PackedVector2Array()
	for corner in game["outline"]:
		var at: Vector3 = main.world.from_lat_lon(corner[0], corner[1])
		if not camera.is_position_behind(at):
			points.append(camera.unproject_position(at))
	var gold := Color(1.0, 0.8, 0.2)
	for i in points.size():
		canvas.draw_circle(points[i], 6.0, gold)
		if i > 0:
			canvas.draw_line(points[i - 1], points[i], gold, 3.0)
	if points.size() >= 3:
		canvas.draw_line(points[-1], points[0], Color(gold, 0.5), 2.0)
		var fill := points.duplicate()
		canvas.draw_colored_polygon(fill, Color(1.0, 0.8, 0.2, 0.12))
	var size := canvas.size
	canvas.draw_string(ThemeDB.fallback_font, Vector2(20.0, size.y - 70.0), "%s: %d corners, %.1f ha   (click: a corner   right click: take it back   Enter: done   Esc: stop)" % [
		game["name"], game["outline"].size(), hectares()], HORIZONTAL_ALIGNMENT_LEFT, -1, 18, gold)


func _draw_checkpoints(canvas: Control, camera: Camera3D) -> void:
	var gold := Color(1.0, 0.8, 0.2)
	var last := Vector2.INF
	for i in game["checkpoints"].size():
		var c: Dictionary = game["checkpoints"][i]
		var at: Vector3 = main.world.from_lat_lon(c["lat"], c["lon"])
		if camera.is_position_behind(at):
			continue
		var spot := camera.unproject_position(at)
		if last != Vector2.INF:
			canvas.draw_line(last, spot, Color(gold, 0.7), 3.0)
		canvas.draw_circle(spot, 9.0, Color(0.3, 1.0, 0.4) if i == 0 else gold)
		canvas.draw_string(ThemeDB.fallback_font, spot + Vector2(10.0, -8.0), "START" if i == 0 else str(i + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 16, gold)
		last = spot
	canvas.draw_string(ThemeDB.fallback_font, Vector2(20.0, canvas.size.y - 70.0), "%s: %d checkpoints (the start first)   (click: the next   right click: take it back   Enter: done   Esc: stop)" % [
		game["name"], game["checkpoints"].size()], HORIZONTAL_ALIGNMENT_LEFT, -1, 18, gold)


# A race with no area marked: its course and booth, and 150 m round them
func _area_round_course() -> Array:
	var lats := []
	var lons := []
	for c in game["checkpoints"]:
		lats.append(float(c["lat"]))
		lons.append(float(c["lon"]))
	if not game["booth"].is_empty():
		lats.append(float(game["booth"][0]))
		lons.append(float(game["booth"][1]))
	var pad_lat := 150.0 / 110574.0
	var pad_lon := 150.0 / (111320.0 * cos(deg_to_rad(float(lats[0]))))
	var s: float = lats.min() - pad_lat
	var n: float = lats.max() + pad_lat
	var w: float = lons.min() - pad_lon
	var e: float = lons.max() + pad_lon
	return [[n, w], [n, e], [s, e], [s, w]]


# --- saved: an area in your pack ---

# The game as an area's file (hunt.gd's reserves): its outline, its booth, its stock, how it goes, its words
func as_reserve() -> Dictionary:
	var stock := []
	for id in game["stock"]:
		if int(game["stock"][id]) > 0:
			stock.append([id, int(game["stock"][id])])
	var booth: Array = game["booth"] if not game["booth"].is_empty() else ([game["checkpoints"][0]["lat"], game["checkpoints"][0]["lon"]] if not game["checkpoints"].is_empty() else game["outline"][0])
	var reserve := {"id": game["id"], "name": game["name"], "about": "Made in the workshop.", "made": true,
		"fence": game["fence"], "avoid_buildings": true, "creatures_out": game["out"], "outline": game["outline"],
		"gates": [{"name": "Booth", "lat": booth[0], "lon": booth[1], "tickets": true}], "water": [], "stock": stock,
		"texts": {"sign": "PLAY\n(E)", "booth": "%s\nTickets here", "title": "%s: a ticket", "law": game["law"],
			"note": "Take a weapon. Inside the area the game's on; out of it, it's over. F1: stop playing.        Esc: not now",
			"hint": String(game["name"]) + ": E (or F1)", "start": "%s: %d creatures. Right click to aim, left click to shoot, R to reload",
			"ticket": "Ticket for %s: %s", "bag": "taken"}}
	if game["pool"]:
		reserve["spawn"] = {"around_people": true, "nearest": 40, "farthest": 150, "leave_past": 250, "dead_for": 30}
	if not game["weapons"].is_empty():
		reserve["weapons"] = game["weapons"]
	if not game["rules"].is_empty():
		reserve["rules"] = game["rules"]
	if game["checkpoints"].size() >= 2:
		reserve["checkpoints"] = game["checkpoints"]
		reserve["laps"] = int(game["laps"])
	return reserve


# --- its rules (Core's GeoGame: when, then) ---

# The quick rules, each with its number: [label, what it makes from N]
const PRESETS := [
	["+N a kill", "per_kill"], ["Score N wins", "score_wins"], ["Kill N to win", "kills_win"],
	["N minutes, then lost", "time_limit"], ["Survive N minutes", "survive"], ["Caught N times: lost", "caught_lose"],
	["Race: finish wins", "race"], ["Finish in N s", "race_in"]]
var number := 10	# (the quick rules' N)


func add_preset(kind: String, n: int) -> void:
	var rules: Array = game["rules"]
	match kind:
		"per_kill":
			rules.append({"when": "kill", "then": [{"do": "score", "by": n}]})
		"score_wins":
			rules.append({"when": "score", "reaches": n, "then": [{"do": "win", "text": "%d points: you've won" % n}]})
		"kills_win":
			rules.append({"when": "kills", "count": n, "then": [{"do": "win", "text": "%d down: you've won" % n}]})
		"time_limit":
			rules.append({"when": "start", "then": [{"do": "timer", "seconds": n * 60}]})
			rules.append({"when": "time_up", "then": [{"do": "lose", "text": "Time's up"}]})
		"survive":
			rules.append({"when": "start", "then": [{"do": "timer", "seconds": n * 60}]})
			rules.append({"when": "time_up", "then": [{"do": "win", "text": "You survived %d minutes" % n}]})
		"caught_lose":
			rules.append({"when": "caught_times", "count": n, "then": [{"do": "lose", "text": "Caught %d times" % n}]})
		"race":
			rules.append({"when": "finish", "then": [{"do": "win", "text": "Finished"}]})
		"race_in":
			rules.append({"when": "checkpoint", "of": "0", "then": [{"do": "timer", "seconds": n}]})
			rules.append({"when": "finish", "then": [{"do": "win", "text": "Finished in time"}]})
			rules.append({"when": "time_up", "then": [{"do": "lose", "text": "Too slow"}]})


# A rule in words: "When a zombie's killed: +1"
static func describe(rule: Dictionary) -> String:
	var of: String = rule.get("of", "")
	var when: String = {
		"start": "When it starts", "kill": "When %s's killed" % (("a " + of) if of != "" else "anything"), "caught": "When you're caught",
		"score": "When the score reaches %d" % int(rule.get("reaches", 0)), "kills": "When %d %s're down" % [int(rule.get("count", 0)), of if of != "" else "of them"],
		"caught_times": "When you've been caught %d times" % int(rule.get("count", 0)), "after": "After %d s" % int(rule.get("seconds", 0)),
		"every": "Every %d s" % int(rule.get("seconds", 0)), "time_up": "When the time's up",
		"checkpoint": ("At the start" if of == "0" else ("At checkpoint %d" % (int(of) + 1) if of != "" else "At each checkpoint")),
		"lap": "Each lap", "finish": "At the finish"}.get(rule.get("when", ""), String(rule.get("when", "?")))
	var then := []
	for step in rule.get("then", []):
		match String(step.get("do", "")):
			"score":
				then.append("%+d" % int(step.get("by", 1)))
			"win":
				then.append("won (\"%s\")" % step.get("text", ""))
			"lose":
				then.append("lost (\"%s\")" % step.get("text", ""))
			"say":
				then.append("say \"%s\"" % step.get("text", ""))
			"timer":
				var s := int(step.get("seconds", 0))
				then.append("a %d:%02d countdown" % [s / 60, s % 60])
			"time":
				then.append("%d s more" % int(step.get("seconds", 0)))
	return "%s: %s" % [when, ", ".join(then)]


# Saved in your pack, and there in the game at once: its id, or "" (and why, told)
func save() -> String:
	if game["outline"].size() < 3 and game["checkpoints"].size() >= 2:
		game["outline"] = _area_round_course()	# (a race: its area round its course)
	if game["outline"].size() < 3:
		main.hud.set_status("Mark its area first (Mark it on the map), or a race's checkpoints", 3.0)
		return ""
	game["id"] = "my_" + String(game["name"]).to_lower().validate_filename().replace(" ", "_").replace(",", "")
	var folder := Packs.workshop().path_join("reserves")
	DirAccess.make_dir_recursive_absolute(folder)
	var reserve := as_reserve()
	var file := FileAccess.open(folder.path_join(game["id"] + ".json"), FileAccess.WRITE)
	if file == null:
		return ""
	file.store_string(JSON.stringify(reserve, "\t", false) + "\n")
	file.close()
	main.hunt.add_reserve(reserve)
	main.net.share_packs()	# (hosting: everyone has it)
	return game["id"]


# --- its page in the workshop ---

func fill(rows: VBoxContainer) -> void:
	var line := func(label: String) -> HBoxContainer:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 8)
		rows.add_child(row)
		row.add_child(builder._menu_label(label))
		return row
	var refill := func(): builder._fill_menu()
	# yours to change, or a new one
	var yours: HBoxContainer = line.call("Change")
	for reserve in main.hunt.reserves:
		if reserve.get("made", false):
			var button: Button = builder._menu_button(String(reserve["name"]), reserve.get("id", "") == game["id"], 150.0, 32.0)
			button.pressed.connect(func(): edit_game(reserve); refill.call())
			yours.add_child(button)
	var fresh: Button = builder._menu_button("New", false, 70.0, 32.0)
	fresh.pressed.connect(func(): new_game(); refill.call())
	yours.add_child(fresh)
	var named: HBoxContainer = line.call("Name")
	var name_edit := LineEdit.new()
	name_edit.text = game["name"]
	name_edit.custom_minimum_size = Vector2(320.0, 0.0)
	name_edit.text_changed.connect(func(t: String): game["name"] = t)
	named.add_child(name_edit)
	# where
	var area: HBoxContainer = line.call("Its area")
	var mark: Button = builder._menu_button("Mark it on the map", false, 200.0, 32.0)
	mark.pressed.connect(start_marking)
	area.add_child(mark)
	area.add_child(builder._menu_label(("%d corners, %.1f ha" % [game["outline"].size(), hectares()]) if game["outline"].size() >= 3 else "not marked yet"))
	# a race's course: checkpoints in order (the start first), its laps
	var course: HBoxContainer = line.call("A race")
	var mark_course: Button = builder._menu_button("Mark checkpoints on the map", false, 260.0, 32.0)
	mark_course.pressed.connect(start_marking.bind("checkpoints"))
	course.add_child(mark_course)
	var add_here: Button = builder._menu_button("Add one where I am", false, 190.0, 32.0)
	add_here.pressed.connect(func():
		var player: Node3D = main.walker if main.walker else main.car
		add_checkpoint(player.global_position)
		refill.call())
	course.add_child(add_here)
	var clear: Button = builder._menu_button("Clear", false, 70.0, 32.0)
	clear.pressed.connect(func(): game["checkpoints"] = []; refill.call())
	course.add_child(clear)
	course.add_child(builder._menu_label("%d checkpoints" % game["checkpoints"].size()))
	var laps := SpinBox.new()
	laps.min_value = 0
	laps.max_value = 50
	laps.value = int(game["laps"])
	laps.prefix = "laps"
	laps.tooltip_text = "0: point to point (the last the finish); 1 or more: round a circuit to the start again"
	laps.value_changed.connect(func(v: float): game["laps"] = int(v))
	course.add_child(laps)
	var booth: HBoxContainer = line.call("Its booth")
	var here: Button = builder._menu_button("Where I'm standing", false, 200.0, 32.0)
	here.pressed.connect(func():
		var player: Node3D = main.walker if main.walker else main.car
		var lat_lon: PackedFloat64Array = main.world.to_lat_lon(player.global_position)
		game["booth"] = [lat_lon[0], lat_lon[1]]
		refill.call())
	booth.add_child(here)
	booth.add_child(builder._menu_label("placed" if not game["booth"].is_empty() else "(else at its first corner)"))
	# what's in it: how many of each creature
	rows.add_child(builder._menu_label("What's in it (how many of each)"))
	var creatures := GridContainer.new()
	creatures.columns = 4
	rows.add_child(creatures)
	for species in GeodriveHunt.species_list():
		var cell := HBoxContainer.new()
		var label := Label.new()
		label.text = species["name"]
		label.custom_minimum_size = Vector2(150.0, 0.0)
		cell.add_child(label)
		var count := SpinBox.new()
		count.min_value = 0
		count.max_value = 500
		count.step = 5
		count.value = int(game["stock"].get(species["id"], 0))
		count.custom_minimum_size = Vector2(90.0, 0.0)
		count.value_changed.connect(func(v: float): game["stock"][species["id"]] = int(v))
		cell.add_child(count)
		creatures.add_child(cell)
	# which weapons its booth offers (none lit: all)
	var arms: HBoxContainer = line.call("Weapons")
	for load in GeodriveHunt.loads():
		if String(load["id"]).begins_with("workshop_"):
			continue	# (the workshop's preview)
		var button: Button = builder._menu_button(String(load["name"]).get_slice(",", 0), game["weapons"].has(load["id"]), 120.0, 32.0)
		button.pressed.connect(func():
			game["weapons"].erase("none")
			if game["weapons"].has(load["id"]):
				game["weapons"].erase(load["id"])
			else:
				game["weapons"].append(load["id"])
			refill.call())
		arms.add_child(button)
	var unarmed: Button = builder._menu_button("None (a race)", game["weapons"] == ["none"], 140.0, 32.0)
	unarmed.pressed.connect(func():
		game["weapons"] = [] if game["weapons"] == ["none"] else ["none"]
		refill.call())
	arms.add_child(unarmed)
	arms.add_child(builder._menu_label("(none lit: all)"))
	# how it goes
	var options: Array = [["Creatures", "pool", [[false, "Spread over it"], [true, "Round the players"]]],
		["Out", "out", [["always", "Always"], ["playing", "While it's played"]]],
		["Fence", "fence", [[false, "None"], [true, "A game fence"]]], ["Hunting law", "law", [[false, "None"], [true, "The district's"]]]]
	for option in options:
		var row: HBoxContainer = line.call(option[0])
		for choice in option[2]:
			var button: Button = builder._menu_button(choice[1], game[option[1]] == choice[0], 170.0, 32.0)
			button.pressed.connect(func(): game[option[1]] = choice[0]; refill.call())
			row.add_child(button)
	# its rules: the quick ones (with their number), and those it has (each to take out)
	var quick: HBoxContainer = line.call("Rules")
	var n := SpinBox.new()
	n.min_value = 1
	n.max_value = 1000
	n.value = number
	n.prefix = "N"
	n.custom_minimum_size = Vector2(90.0, 0.0)
	n.value_changed.connect(func(v: float): number = int(v))
	quick.add_child(n)
	for preset in PRESETS:
		var button: Button = builder._menu_button(preset[0], false, 150.0, 32.0)
		button.pressed.connect(func(): add_preset(preset[1], number); refill.call())
		quick.add_child(button)
	if game["rules"].is_empty():
		rows.add_child(builder._menu_label("(no rules: played till you leave it)"))
	for i in game["rules"].size():
		var row := HBoxContainer.new()
		rows.add_child(row)
		var gone: Button = builder._menu_button("x", false, 32.0, 28.0)
		gone.pressed.connect(func(): game["rules"].remove_at(i); refill.call())
		row.add_child(gone)
		var words := Label.new()
		words.text = describe(game["rules"][i])
		row.add_child(words)
	var done: HBoxContainer = line.call("")
	var keep: Button = builder._menu_button("Save it (its booth's there now)", false, 300.0)
	keep.pressed.connect(func():
		var id := save()
		if id != "":
			main.hud.set_status("Saved: %s. Its booth's on the map; F1 there plays it" % game["name"], 5.0))
	done.add_child(keep)
