# Packs: a game made of files. Each pack is a folder: the game's own in packs/ (hunting: the game farm, its animals and
# weapons), anyone's own in the Saved folder's packs/. In a pack, any of:
#   pack.json            its name, what it is, its sources
#   creatures/*.json     its creatures (docs/MAKE_A_CREATURE.md)
#   weapons/*.json       its weapons: a rifle's load or a bow
#   reserves/*.json      its game farms (outline, gates, water, stock)
# The game's packs are read first, then the Saved folder's, each pack's files in name order: a later file with a
# creature's or weapon's id replaces it, and "like" can name one from an earlier file. Read once, before any hunt.
class_name Packs
extends RefCounted

const OWN := "res://packs"

static var _loaded := false
static var testing := false	# (a test run: the workshop's its own, begun empty; yours left alone)
static var test_name := "workshop_test"	# (a second player on the same PC: its own)


# The workshop's pack (what's made in the game): yours, or a test's
static func workshop() -> String:
	return GameSettings.saved_dir().path_join("packs/" + (test_name if testing else "workshop"))


# Every pack's folder: the game's, then the Saved folder's (each in name order)
static func folders() -> PackedStringArray:
	var out := PackedStringArray()
	for root in [OWN, GameSettings.saved_dir().path_join("packs")]:
		if not DirAccess.dir_exists_absolute(root):
			continue
		var names := DirAccess.get_directories_at(root)
		names.sort()
		for pack in names:
			# (the workshop's: yours in a game, a test's in a test)
			if pack in ["workshop", "workshop_test", "workshop_test_join"] and pack != Packs.workshop().get_file():
				continue
			out.append(root.path_join(pack))
	return out


# Every pack's files of a kind (creatures, weapons, reserves), in the order they're read
static func files(kind: String) -> PackedStringArray:
	var out := PackedStringArray()
	for pack in folders():
		out.append_array(files_in(pack.path_join(kind)))
	return out


static func files_in(folder: String) -> PackedStringArray:
	var out := PackedStringArray()
	if not DirAccess.dir_exists_absolute(folder):
		return out
	var names := DirAccess.get_files_at(folder)
	names.sort()
	for file in names:
		if file.get_extension() == "json":
			out.append(folder.path_join(file))
	return out


# The packs' weapons and creatures, read by Core (each one turned away is told in the log, and why)
static func load_all() -> void:
	if _loaded or not ClassDB.class_exists("GeodriveHunt"):
		return
	_loaded = true
	var weapons := 0
	var creatures := 0
	for path in files("weapons"):
		weapons += GeodriveHunt.read_file(FileAccess.get_file_as_string(path), "weapons", path)
	for path in files("creatures"):
		creatures += GeodriveHunt.read_file(FileAccess.get_file_as_string(path), "creatures", path)
	print("Geodrive: packs %s: %d creatures, %d weapons" % [", ".join(Array(folders()).map(func(f): return String(f).get_file())), creatures, weapons])


# Each pack's pack.json (its folder's name if it has none): {name, about, folder}
static func list() -> Array:
	var out := []
	for pack in folders():
		var path := pack.path_join("pack.json")
		var parsed = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
		var info: Dictionary = parsed if parsed is Dictionary else {}
		info["folder"] = pack
		info["name"] = info.get("name", pack.get_file())
		out.append(info)
	return out
