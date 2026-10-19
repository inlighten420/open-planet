# Games made by the world itself, as its roads and buildings are: the same for everyone (seeded by where they are:
# friends find the same games), made from what's there. First the real places that suit a game, from OpenStreetMap:
# the regions' own (geotile games -> Cache/regions/*.games.json, made with the region: no waiting, offline), or where
# there's none, asked of Overpass (once for each ~11 km tile, kept in the cache): a game reserve, nature reserve or national park (a hunt, of
# Africa's animals where they live; elsewhere a race), a park or a golf course (an outbreak or a race), a stadium or
# sports centre (an outbreak), a race track (a race); each named after the place, its kind, name and animals picked at
# random from what suits it. Then, where there's no such place, now and then a 3 km square's own: a town's outbreak,
# the country's hunt, a race along the real road. Each is a game as the game maker makes them (its booth on the road,
# its rules), there till you go far away; not saved, but "Make it yours" (the phone's Games) copies one to change.
# One looked at each turn (the camshaft rule: the main thread never waits long).
extends Node

const CELL := 0.03	# degrees: a square ~3 km
const CHANCE := 0.45	# of a square with no real place in it having a game
const TILE := 0.1	# degrees: the places are asked for a tile at a time
const NEAR_BOOTH := 1000.0	# m: not where a game's booth is already (but a race track's own: always)
const REACH := 6000.0	# m: places this near are made games
const WILD := ["springbok", "blesbok", "impala", "kudu", "red_hartebeest", "zebra", "warthog", "gemsbok", "blue_wildebeest", "eland"]
const NAMES := {"race": ["Sprint", "Dash", "Rally", "Time Trial", "Run"], "zombies": ["Outbreak", "Last Stand", "Siege", "Night of the Dead"],
	"hunt": ["Safari", "Hunt", "Trophy Weekend", "Cull"]}
const OVERPASS := "https://overpass-api.de/api/interpreter"

var main: Node
var enabled := true
var places_online := true	# (asking OpenStreetMap for the places: the tests that can't wait for it, not)
var _done := {}	# square (Vector2i) or place id -> true: looked at (a game made there or not)
var _wait := 0.0
var _places := {}	# the real places known, by tile (Vector2i): [{id, name, kind, lat, lon, half (m), course}]
var _known := {}	# (their ids)
var _tiles := {}	# tile (Vector2i) -> "asking" or "had"
var _covered: Array = []	# the regions' files' boxes [s, w, n, e]: their tiles not asked of Overpass
var clearings: Array = []	# (no trees on a golf course's fairways and greens: main.update_plant_clearings)
var _asking: HTTPRequest


func _ready() -> void:
	_read_regions()


# Every region's places for games (geotile games: made with the region)
func _read_regions() -> void:
	_covered = []
	clearings = []
	var folder := GameSettings.cache_dir().path_join("regions")
	for file in DirAccess.get_files_at(folder):
		if not file.ends_with(".games.json"):
			continue
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(folder.path_join(file)))
		if not (parsed is Dictionary):
			continue
		var places: Array = parsed.get("places", [])
		for place in places:
			if place.get("kind", "") == "golf":
				place["golf_file"] = folder.path_join(file.trim_suffix(".games.json") + ".golf").path_join(String(place["id"]) + ".json")
				# (its fairways kept clear, 22 m each side of tee to pin, and its green round the pin)
				for line in place.get("fairways", []):
					clearings.append([22.0, false, PackedFloat64Array(line)])
					if line.size() >= 4:
						clearings.append([18.0, false, PackedFloat64Array([line[-2], line[-1], line[-2], line[-1] + 0.000001])])
				place.erase("fairways")
		_add_places(places)
		if not places.is_empty():
			var lats := places.map(func(p): return float(p["lat"]))
			var lons := places.map(func(p): return float(p["lon"]))
			_covered.append([lats.min(), lons.min(), lats.max(), lons.max()])
	print("Geodrive: %d places for games, from %d regions; %d golf fairways and greens kept clear" % [_known.size(), _covered.size(), clearings.size()])
	main.update_plant_clearings()


func _process(delta: float) -> void:
	_wait -= delta
	if not enabled or _wait > 0.0:
		return
	_wait = 1.0
	var world: Node = main.world
	var hunt: Node = main.hunt
	var player: Node3D = main.walker if main.walker else main.car
	if world == null or hunt == null or player == null or not world.is_ready() or main.waiting != "":
		return
	var here: PackedFloat64Array = world.to_lat_lon(player.global_position)
	var tile := Vector2i(floori(here[0] / TILE), floori(here[1] / TILE))
	if places_online and not _tiles.has(tile) and not _is_covered(here):
		_ask_places(tile)
	# the real places first (the nearest, in the tiles round you), then the squares
	var best := {}
	var nearest := REACH
	for dy in [-1, 0, 1]:
		for dx in [-1, 0, 1]:
			for place in _places.get(tile + Vector2i(dx, dy), []):
				if _done.has(place["id"]) or not world.contains_lat_lon(place["lat"], place["lon"]):
					continue
				var d: float = hunt._metres(here[0], here[1], place["lat"], place["lon"])
				if d < nearest:
					nearest = d
					best = place
	if not best.is_empty():
		_done[best["id"]] = true
		_place_game(best)
		return
	if _tiles.get(tile, "") == "asking":
		return	# (the squares wait for the places: a square with one in it is its)
	var cell := Vector2i(floori(here[0] / CELL), floori(here[1] / CELL))
	for dy in [0, -1, 1]:
		for dx in [0, -1, 1]:
			var square := cell + Vector2i(dx, dy)
			if not _done.has(square) and _make(square):
				return	# (one a turn)


# --- the real places (OpenStreetMap's, by Overpass; kept in the cache) ---

func _ask_places(tile: Vector2i) -> void:
	var cache := GameSettings.cache_dir().path_join("game_places_%d_%d.json" % [tile.x, tile.y])
	var kept: Variant = JSON.parse_string(FileAccess.get_file_as_string(cache)) if FileAccess.file_exists(cache) else null
	if kept is Array:
		_tiles[tile] = "had"
		_add_places(kept)
		return
	if _asking != null:
		return	# (one question at a time)
	_tiles[tile] = "asking"
	var s := tile.x * TILE
	var w := tile.y * TILE
	var box := "(%.3f,%.3f,%.3f,%.3f)" % [s, w, s + TILE, w + TILE]
	# (the places: their middles and sizes; the race tracks: their shapes, to race round)
	var query := "[out:json][timeout:80];(nwr[\"leisure\"~\"^(nature_reserve|park|stadium|sports_centre|golf_course)$\"][\"name\"]%s;" % box \
		+ "nwr[\"boundary\"~\"^(protected_area|national_park)$\"][\"name\"]%s;);out tags center bb qt 300;" % box \
		+ "(way[\"highway\"=\"raceway\"]%s;way[\"leisure\"=\"track\"][\"sport\"~\"motor|karting|motocross\"]%s;);out tags geom qt 60;" % [box, box]
	_asking = HTTPRequest.new()
	_asking.timeout = 90.0	# (it can be slow: ~40 s; once a tile, then kept)
	add_child(_asking)
	print("Geodrive: asking OpenStreetMap for the places for games in %s" % box)
	_asking.request_completed.connect(func(result: int, code: int, _headers: PackedStringArray, body: PackedByteArray):
		_asking.queue_free()
		_asking = null
		var answer: Variant = JSON.parse_string(body.get_string_from_utf8()) if result == HTTPRequest.RESULT_SUCCESS and code == 200 else null
		if not (answer is Dictionary):
			_tiles[tile] = "had"	# (no answer: this time, the squares alone)
			print("Geodrive: the places for games didn't come (%d, %d)" % [result, code])
			return
		var found := _read_places(answer.get("elements", []))
		var file := FileAccess.open(cache, FileAccess.WRITE)
		if file:
			file.store_string(JSON.stringify(found))
		_tiles[tile] = "had"
		_add_places(found)
		print("Geodrive: %d places for games round here" % found.size()))
	_asking.request(OVERPASS, ["User-Agent: Open-Planet/0.2 (https://github.com/inlighten420/open-planet)", "Content-Type: application/x-www-form-urlencoded"],
		HTTPClient.METHOD_POST, "data=" + query.uri_encode())


# Overpass's elements as places: {id, name, kind, lat, lon, half}; the same place in two of its forms once
func _read_places(elements: Array) -> Array:
	var out := []
	var names := {}
	# (a race track: its longest piece by its name, its shape kept)
	var tracks := {}
	for e in elements:
		if not e.has("geometry"):
			continue
		var tags: Dictionary = e.get("tags", {})
		var course := []
		var metres := 0.0
		for point in e["geometry"]:
			var next := [float(point["lat"]), float(point["lon"])]
			if not course.is_empty():
				metres += _metres_between(course[-1], next)
			course.append(next)
		var name := String(tags.get("name", "Race track"))
		if metres > 600.0 and (not tracks.has(name) or tracks[name]["metres"] < metres):
			var middle: Array = course[course.size() / 2]
			tracks[name] = {"id": "w%d" % int(e.get("id", 0)), "name": name, "kind": "track", "lat": middle[0], "lon": middle[1],
				"half": 600.0, "course": course, "metres": metres}
	for name in tracks:
		names[name] = true
		out.append(tracks[name])
	for e in elements:
		if e.has("geometry"):
			continue
		var tags: Dictionary = e.get("tags", {})
		var name := String(tags.get("name", ""))
		var at: Dictionary = e.get("center", {"lat": e.get("lat", 0.0), "lon": e.get("lon", 0.0)})
		if name == "" or names.has(name):
			continue
		var kind := ""
		var leisure := String(tags.get("leisure", ""))
		if tags.get("boundary", "") in ["protected_area", "national_park"] or leisure == "nature_reserve":
			kind = "reserve"
		elif leisure in ["park", "golf_course"]:
			kind = "park"
		elif leisure in ["stadium", "sports_centre"]:
			kind = "stadium"
		if kind == "":
			continue
		var half := 400.0
		if e.has("bounds"):
			var b: Dictionary = e["bounds"]
			half = Vector2((float(b["maxlon"]) - float(b["minlon"])) * 111320.0 * cos(deg_to_rad(float(at["lat"]))), (float(b["maxlat"]) - float(b["minlat"])) * 110574.0).length() * 0.5
		if (kind == "park" and half < 150.0) or (kind == "stadium" and half < 60.0):
			continue	# (a patch of grass, a hall or a court: not a game's)
		names[name] = true
		out.append({"id": "%s%d" % [String(e.get("type", "n")).left(1), int(e.get("id", 0))], "name": name, "kind": kind,
			"lat": float(at["lat"]), "lon": float(at["lon"]), "half": clampf(half, 250.0, 2000.0)})
	return out


func _add_places(found: Array) -> void:
	for place in found:
		if _known.has(place["id"]):
			continue
		_known[place["id"]] = true
		var tile := Vector2i(floori(float(place["lat"]) / TILE), floori(float(place["lon"]) / TILE))
		_places.get_or_add(tile, []).append(place)


func _is_covered(here: PackedFloat64Array) -> bool:
	for box in _covered:
		if here[0] >= box[0] and here[0] <= box[2] and here[1] >= box[1] and here[1] <= box[3]:
			return true
	return false


# A real place's game: its kind, name and animals at random from what suits it (the same for everyone: seeded by it)
func _place_game(place: Dictionary) -> void:
	var world: Node = main.world
	var random := RandomNumberGenerator.new()
	random.seed = hash([place["id"], "geodrive games"])
	var road: Dictionary = world.nearest_road(world.from_lat_lon(place["lat"], place["lon"]), 1500.0)
	if not road.get("found", false):
		return
	var africa: bool = place["lat"] > -35.5 and place["lat"] < -15.0 and place["lon"] > 11.0 and place["lon"] < 41.0
	if place["kind"] == "golf":
		_golf_game(place)
		return
	var kinds: Array = {"reserve": ["hunt", "hunt", "race"] if africa else ["race"], "park": ["zombies", "race"], "stadium": ["zombies"],
		"track": ["race"]}[place["kind"]]
	var kind: String = kinds[random.randi() % kinds.size()]
	_make_game("osm_" + place["id"], kind, place["name"], road, random, place["half"], place.get("course", []))


# A golf course with its holes mapped: a round of golf there, its booth on the road nearest the first tee
func _golf_game(place: Dictionary) -> void:
	var world: Node = main.world
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(String(place.get("golf_file", ""))))
	if not (parsed is Dictionary) or parsed.get("holes", []).is_empty():
		return
	var tee: Array = parsed["holes"][0]["line"][0]
	var road: Dictionary = world.nearest_road(world.from_lat_lon(tee[0], tee[1]), 800.0)
	var at: PackedFloat64Array = world.to_lat_lon(road["position"]) if road.get("found", false) else PackedFloat64Array([tee[0], tee[1]])
	var pars: Array = place.get("pars", [])
	var par := 0
	for p in pars:
		par += int(p) if int(p) > 0 else 4
	var reserve := {"id": "golf_" + String(place["id"]), "name": place["name"], "about": "Golf on the real course.", "made": false, "generated": true,
		"fence": false, "creatures_out": "playing", "outline": _square_round(PackedFloat64Array([place["lat"], place["lon"]]), float(place["half"])),
		"gates": [{"name": "Clubhouse", "lat": at[0], "lon": at[1], "tickets": true}], "water": [], "stock": [], "weapons": ["none"],
		"golf": {"file": place["golf_file"], "holes": pars.size(), "par": par},
		"texts": {"sign": "GOLF\n(E)", "booth": "%s\nGolf: a round here", "title": "%s: a round of golf", "law": false,
			"note": "%d holes, par %d. By your ball, E to address it: the mouse aims, the wheel picks the club, hold the left button for power. T: to your ball. F1: stop.        Esc: not now" % [pars.size(), par],
			"hint": "Golf: E", "start": "%s", "ticket": "%s: %s", "bag": "strokes"}}
	main.hunt.add_reserve(reserve)
	print("Geodrive: a game here: %s (golf, %d holes, par %d)" % [place["name"], pars.size(), par])


# --- a 3 km square's own (no real place in it) ---

# The square's game, if it has one: true once it's been looked at (made or not), false if it isn't in the built area yet
func _make(square: Vector2i) -> bool:
	var world: Node = main.world
	var random := RandomNumberGenerator.new()
	random.seed = hash([square.x, square.y, "geodrive games"])
	var lat := (square.x + random.randf_range(0.2, 0.8)) * CELL
	var lon := (square.y + random.randf_range(0.2, 0.8)) * CELL
	if not world.contains_lat_lon(lat, lon):
		_why(square, "outside the built area")
		return false
	_done[square] = true	# (its roads are in once it's in the built area: its ground near it comes when someone's there)
	for place in _places.get(Vector2i(floori(lat / TILE), floori(lon / TILE)), []):
		if floori(place["lat"] / CELL) == square.x and floori(place["lon"] / CELL) == square.y:
			return true	# (a real place's square: its game is the place's)
	if random.randf() > CHANCE:
		return true
	var road: Dictionary = world.nearest_road(world.from_lat_lon(lat, lon), 600.0)
	if not road.get("found", false):
		return true
	# (a town, buildings all round: a race or an outbreak; open country: a race or a hunt, of Africa's animals only
	# where they live; in between, a race)
	var buildings := _buildings_round(road["position"])
	var africa: bool = lat > -35.5 and lat < -15.0 and lon > 11.0 and lon < 41.0
	var other := "zombies" if buildings >= 6 else ("hunt" if buildings <= 2 and africa else "race")
	var kind: String = ["race", other][random.randi() % 2]
	var street: String = road["name"] if str(road["name"]) != "" else {"race": "Back Road", "zombies": "Town", "hunt": "Bushveld"}[kind]
	_make_game("gen_%d_%d" % [square.x, square.y], kind, street, road, random, 500.0 if kind == "zombies" else 900.0)
	return true


# A game of Kind at a road (its booth there), named after Where; its area Half metres each way (a race's: round its
# course). Not where another game's booth is already
func _make_game(id: String, kind: String, where: String, road: Dictionary, random: RandomNumberGenerator, half: float, track: Array = []) -> void:
	var world: Node = main.world
	var at: Vector3 = road["position"]
	var booth_at: PackedFloat64Array = world.to_lat_lon(at)
	for booth in main.hunt.booths():
		if track.is_empty() and main.hunt._metres(booth_at[0], booth_at[1], booth["lat"], booth["lon"]) < NEAR_BOOTH:
			return
	var game := {"id": id, "name": "%s %s" % [where, NAMES[kind][random.randi() % NAMES[kind].size()]], "outline": [],
		"booth": [booth_at[0], booth_at[1]], "stock": {}, "weapons": [], "pool": true, "out": "playing", "fence": false, "law": false,
		"rules": [], "checkpoints": [], "laps": 0}
	match kind:
		"race" when not track.is_empty():
			# (round the real circuit: a checkpoint every 300 m of it; closed, 3 laps; its booth by its start)
			var circuit := _along(track, 300.0)
			if circuit.size() < 4:
				return
			var closed := _metres_between(track[0], track[-1]) < 60.0
			if closed:
				circuit.pop_back()	# (the last is the first again: the lap's line)
			game["name"] = "%s %s" % [where, ["Grand Prix", "Club Race", "Track Day", "Sprint Race"][random.randi() % 4]]
			game["checkpoints"] = circuit
			var lap := 0.0
			for i in range(track.size() - 1):
				lap += _metres_between(track[i], track[i + 1])
			game["laps"] = (3 if lap < 6000.0 else 1) if closed else 0	# (a long one, the Nordschleife's 20.8 km: once round)
			game["weapons"] = ["none"]
			game["booth"] = _beside(track[0], track[1], 15.0)	# (by its start, not on the track)
			_rules(game, "race", 0)
		"race":
			var course := _course(at, road["direction"], random)
			if course.size() < 4:
				return
			game["checkpoints"] = course
			game["weapons"] = ["none"]
			var metres := float(course.size() - 1) * 400.0
			_rules(game, "race_in", roundi(metres / 14.0 * 1.4))	# (a fair pace on town roads, and some)
		"zombies":
			game["stock"] = {"zombie": random.randi_range(12, 30)}
			game["outline"] = _square_round(booth_at, half)
			_rules(game, "per_kill", 1)
			_rules(game, "survive", random.randi_range(3, 8))
		"hunt":
			var kinds := WILD.duplicate()
			for i in random.randi_range(2, 4):
				var species: String = kinds.pop_at(random.randi() % kinds.size())
				if not GeodriveHunt.species_data(species).is_empty():
					game["stock"][species] = random.randi_range(6, 18)
			game["outline"] = _square_round(booth_at, half)
			game["pool"] = false
			_rules(game, "kills_win", random.randi_range(2, 5))
			_rules(game, "time_limit", random.randi_range(10, 20))
	var maker: Node = main.builder.game_maker if main.builder else null
	if maker == null:
		return
	var mine: Dictionary = maker.game
	maker.game = game
	if game["outline"].is_empty():
		game["outline"] = maker._area_round_course()
	var reserve: Dictionary = maker.as_reserve()
	maker.game = mine
	reserve["made"] = false
	reserve["generated"] = true
	reserve["about"] = "Made by the world. Make it yours: the phone, Games."
	main.hunt.add_reserve(reserve)
	print("Geodrive: a game here: %s (%s)" % [game["name"], kind])


# --- the events: every place known for a game, to pick and go to (the phone's Games; the start screen) ---

const KINDS := {"track": "Race track", "golf": "Golf", "reserve": "Reserve: a hunt or a race", "park": "Park: an outbreak or a race",
	"stadium": "Stadium: an outbreak"}


# The places of Kind ("" any) whose name has Text, nearest From first: [{name, kind, lat, lon, km}]
func events(kind: String, from: PackedFloat64Array, text: String, count: int) -> Array:
	var found := []
	var wanted := text.strip_edges().to_lower()
	for tile in _places:
		for place in _places[tile]:
			if kind != "" and place["kind"] != kind:
				continue
			if wanted != "" and not String(place["name"]).to_lower().contains(wanted):
				continue
			found.append(place)
	var km := func(p) -> float: return main.hunt._metres(from[0], from[1], p["lat"], p["lon"]) / 1000.0 if main.hunt else 0.0
	found.sort_custom(func(a, b): return km.call(a) < km.call(b))
	var out := []
	for place in found.slice(0, count):
		var entry: Dictionary = place.duplicate()
		entry["km"] = km.call(place)
		out.append(entry)
	return out


# Where an event starts: a race track's start, a golf course's first tee, or the place itself
func start_of(place: Dictionary) -> PackedFloat64Array:
	if place.has("course") and not place["course"].is_empty():
		return PackedFloat64Array([place["course"][0][0], place["course"][0][1]])
	if place.get("kind", "") == "golf":
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(String(place.get("golf_file", ""))))
		if parsed is Dictionary and not parsed.get("holes", []).is_empty():
			var tee: Array = parsed["holes"][0]["line"][0]
			return PackedFloat64Array([tee[0], tee[1]])
	return PackedFloat64Array([place["lat"], place["lon"]])


var _told := {}

func _why(square: Vector2i, why: String) -> void:
	if main.testing and _told.get(square, "") != why:
		_told[square] = why
		print("Geodrive: no game yet in square %s: %s" % [square, why])


# The game maker's quick rules, on a game that isn't the one being made
func _rules(game: Dictionary, kind: String, n: int) -> void:
	var maker: Node = main.builder.game_maker
	var mine: Dictionary = maker.game
	maker.game = game
	maker.add_preset(kind, n)
	maker.game = mine


# How many of 25 points round At (every 50 m, 200 m each way) are in a building: a town has them all round
func _buildings_round(at: Vector3) -> int:
	var found := 0
	for x in range(-2, 3):
		for z in range(-2, 3):
			if main.world.building_at(at + Vector3(x * 100.0, 0.0, z * 100.0)).get("found", false):
				found += 1
	return found


# A race's checkpoints along the real road from At: a step at a time, kept on the road (straight on at a junction, as
# near as it goes), one every 400 m, ~2-4 km
func _course(at: Vector3, direction: Vector3, random: RandomNumberGenerator) -> Array:
	var world: Node = main.world
	var heading := direction.normalized()
	if random.randf() < 0.5:
		heading = -heading
	var points := [at]
	var along := 0.0
	var since := 0.0
	var length := random.randf_range(2000.0, 4000.0)
	var p := at
	while along < length:
		var road: Dictionary = world.nearest_road(p + heading * 50.0, 40.0)
		if not road.get("found", false):
			break
		var next: Vector3 = road["position"]
		var step := Vector2(next.x - p.x, next.z - p.z).length()
		if step < 10.0:
			break	# (a dead end)
		var way: Vector3 = road["direction"]
		heading = (way if way.dot(heading) >= 0.0 else -way).normalized()
		along += step
		since += step
		p = next
		if since >= 400.0:
			points.append(p)
			since = 0.0
	var out := []
	for point in points:
		var lat_lon: PackedFloat64Array = world.to_lat_lon(point)
		out.append({"lat": lat_lon[0], "lon": lat_lon[1], "radius": 10.0})
	return out


# Points every Every metres along a line of [lat, lon]s (its first and last too): checkpoints
static func _along(line: Array, every: float) -> Array:
	var out := [{"lat": line[0][0], "lon": line[0][1], "radius": 14.0}]
	var since := 0.0
	for i in range(1, line.size()):
		since += _metres_between(line[i - 1], line[i])
		if since >= every or i == line.size() - 1:
			out.append({"lat": line[i][0], "lon": line[i][1], "radius": 14.0})
			since = 0.0
	return out


# A place Metres to the right of the line from A to B, at A: [lat, lon]
static func _beside(a: Array, b: Array, metres: float) -> Array:
	var east := (float(b[1]) - float(a[1])) * 111320.0 * cos(deg_to_rad(float(a[0])))
	var north := (float(b[0]) - float(a[0])) * 110574.0
	var way := Vector2(east, north).normalized()
	var right := Vector2(way.y, -way.x) * metres
	return [float(a[0]) + right.y / 110574.0, float(a[1]) + right.x / (111320.0 * cos(deg_to_rad(float(a[0]))))]


static func _metres_between(a: Array, b: Array) -> float:
	return Vector2((b[1] - a[1]) * 111320.0 * cos(deg_to_rad(a[0])), (b[0] - a[0]) * 110574.0).length()


# A square Half metres each way round a place: an area's outline
static func _square_round(centre: PackedFloat64Array, half: float) -> Array:
	var dlat := half / 110574.0
	var dlon := half / (111320.0 * cos(deg_to_rad(centre[0])))
	return [[centre[0] + dlat, centre[1] - dlon], [centre[0] + dlat, centre[1] + dlon], [centre[0] - dlat, centre[1] + dlon], [centre[0] - dlat, centre[1] - dlon]]
