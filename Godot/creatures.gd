# Creatures as data: a creature is a file, no code: a pack's creatures/*.json (packs.gd: the game's packs and anyone's
# own). A file is one creature or a list of them, as GeodriveHunt.species_data gives one (and so writes any back out):
#   {"id": "nyala", "name": "Nyala", "like": "kudu",          (like: start from another; only what differs given)
#    "mass": 85, "shoulder": 1.05, "length": 1.6,             (kg, m: an adult's, the sexes' mean)
#    "walk": 1.2, "run": 14, "herd": [2, 8],                  (m/s; the fewest and most in a herd)
#    "diet": "browser", "colour": "#6b5040",                  (grazer, browser, mixed, carnivore; its coat)
#    "horns": true, "protected": false, "season": [6, 7], "bag": 1,   (the law where it's hunted: months, a day's bag)
#    "shape": {"head_up": 1.08, "torso_depth": -1, "back_slope": 0, "ears": 1.3, "horns": "pair", "horn_length": 0.6,
#              "bend": [0.45, 0.85, 0.75, -0.1], "mane": "none", "long_tail": false, "coat": "pinstripes"},
#    "behaviour": {"browsing": 1, "rests": false, "meal": [60, 240], "thirsty_after": [400, 800], "hears": 400,
#                  "wary": [15, 40], "flees_for": [8, 15], "prey": true,
#                  "hunts": false, or {"every": [480, 900], "within": 800, "prey_up_to": 700, "rush_from": 40,
#                                      "rush_for": 20, "rush_speed": 1.25, "feeds_for": [500, 700]}}}
# (horns: none, pair, nose; mane: none, lion, crest; coat: plain, countershade, stripes, patches, mask, pinstripes;
# bend: each horn's two lengths, back from upright then out to the side, rad. Behaviour: building blocks, by its diet
# unless given; times in s, distances in m; thirsty_after [0, 0]: never drinks.) Read once, before any hunt.
class_name Creatures
extends RefCounted


# Every creature in a folder's files (read by Core): how many were taken (a bad one's told and left out)
static func load_folder(folder: String) -> int:
	var added := 0
	for path in Packs.files_in(folder):
		added += GeodriveHunt.read_file(FileAccess.get_file_as_string(path), "creatures", path)
	return added


# A creature written out as a file (to be changed and read back, or shared)
static func save(id: String, path: String) -> bool:
	var text: String = GeodriveHunt.species_text(id)
	if text.is_empty():
		return false
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(text)
	return true


# --creaturetest: a pack's creatures read (one like the kudu, one whole, a built-in changed, a bad one), checked, and
# the kudu written out and read back as it was. PASS or FAIL
static func self_test() -> bool:
	Packs.load_all()	# (the hunting pack's creatures: the test's are like them)
	var folder := OS.get_user_data_dir().path_join("creature_test")
	DirAccess.make_dir_recursive_absolute(folder)
	for file in DirAccess.get_files_at(folder):
		DirAccess.remove_absolute(folder.path_join(file))
	var write := func(name: String, text: String):
		var f := FileAccess.open(folder.path_join(name), FileAccess.WRITE)
		f.store_string(text)
		f.close()
	write.call("pack.json", JSON.stringify([
		{"id": "test_nyala", "name": "Nyala", "like": "kudu", "mass": 85, "shoulder": 1.05, "length": 1.6, "colour": "#6b5040"},
		{"id": "test_beast", "name": "Beast", "mass": 120, "shoulder": 1.1, "length": 1.8, "walk": 1.0, "run": 9, "herd": [1, 3],
			"diet": "carnivore", "shape": {"horns": "nose", "coat": "stripes", "mane": "crest"}},
		{"id": "warthog", "bag": 3},
		{"id": "test_grazing_hunter", "like": "zebra", "behaviour": {"hunts": {"every": [60, 90], "prey_up_to": 80},
			"thirsty_after": [0, 0], "hears": 0}},
		{"id": "test_bad", "mass": 50, "shoulder": 0.8, "length": 1.0, "walk": 2.0, "run": 1.0},
		{"id": "test_bad_meal", "like": "impala", "behaviour": {"meal": [240, 60]}},
	]))
	var kudu: Dictionary = GeodriveHunt.species_data("kudu")
	var name_before: String = GeodriveHunt.species_data("warthog")["name"]
	var added := load_folder(folder)
	var nyala: Dictionary = GeodriveHunt.species_data("test_nyala")
	var beast: Dictionary = GeodriveHunt.species_data("test_beast")
	var warthog: Dictionary = GeodriveHunt.species_data("warthog")
	var hunter: Dictionary = GeodriveHunt.species_data("test_grazing_hunter")
	var zebra: Dictionary = GeodriveHunt.species_data("zebra")
	var checks := {
		"four taken, the bad ones not": added == 4 and GeodriveHunt.species_data("test_bad").is_empty() and GeodriveHunt.species_data("test_bad_meal").is_empty(),
		"nyala's own figures": nyala.get("mass") == 85.0 and nyala.get("colour") == "#6b5040",
		"nyala like a kudu otherwise": nyala.get("run") == kudu["run"] and nyala.get("shape") == kudu["shape"] and nyala.get("diet") == "browser",
		"beast whole": beast.get("diet") == "carnivore" and beast.get("horns") == true and beast.get("shape", {}).get("horns") == "nose"
			and beast.get("shape", {}).get("coat") == "stripes" and beast.get("herd") == [1.0, 3.0],
		"warthog changed, its name kept": warthog["bag"] == 3 and warthog["name"] == name_before,
		"beast a carnivore's ways: rests, hunts, isn't prey": beast.get("behaviour", {}).get("rests") == true
			and beast.get("behaviour", {}).get("hunts") is Dictionary and beast.get("behaviour", {}).get("prey") == false,
		"a zebra given the hunting block hunts; the rest a zebra's": hunter["behaviour"]["hunts"] is Dictionary
			and hunter["behaviour"]["hunts"]["every"] == [60.0, 90.0] and hunter["behaviour"]["hunts"]["prey_up_to"] == 80.0
			and hunter["behaviour"]["hunts"]["rush_from"] == 40.0 and hunter["behaviour"]["thirsty_after"] == [0.0, 0.0]
			and hunter["behaviour"]["hears"] == 0.0 and hunter["behaviour"]["meal"] == zebra["behaviour"]["meal"]
			and zebra["behaviour"]["hunts"] == false,
	}
	# written out, read back: the same
	var copy := folder.path_join("kudu_copy.json")
	save("kudu", copy)
	var again: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(copy))
	again["id"] = "test_kudu_copy"
	GeodriveHunt.add_species(again)
	var round_trip: Dictionary = GeodriveHunt.species_data("test_kudu_copy")
	round_trip["id"] = "kudu"
	checks["kudu written and read back the same"] = round_trip == kudu
	var ok := true
	for check in checks:
		print("Geodrive creature test: %s: %s" % [check, "ok" if checks[check] else "WRONG"])
		ok = ok and checks[check]
	print("Geodrive creature test: %s" % ("PASS" if ok else "FAIL"))
	return ok
