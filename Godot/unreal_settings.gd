# The settings the Unreal game saves (Saved/Config/WindowsEditor/GameUserSettings.ini), read and written here too, so
# both games share them: the camera view, saved spots, the wheel's setup. Unreal's own format: [Section] then Key=Value
# lines, an array as the same key on several lines. Only the lines asked about are changed; the rest stay as they are.
class_name UnrealSettings

const FAVOURITES := "GeodriveFavourites"


static func path() -> String:
	if OS.has_feature("template"):
		return GameSettings.saved_dir().path_join("GameUserSettings.ini")	# (a packaged game: the player's own)
	return GameSettings.saved_dir().path_join("Config/WindowsEditor/GameUserSettings.ini")


# The file's lines, read again only when it has changed (the settings read it many times as they open)
static var _cached := PackedStringArray()
static var _cached_time := -1
static var in_memory_only := false	# (the tests: what they change isn't saved over the player's settings)


static func _lines() -> PackedStringArray:
	var time := FileAccess.get_modified_time(path()) if FileAccess.file_exists(path()) else 0
	if (time != _cached_time or time == 0) and not (in_memory_only and _cached_time != -1):
		_cached = FileAccess.get_file_as_string(path()).replace("\r\n", "\n").split("\n")
		_cached_time = time
	return _cached


# Every value of a section: key -> Array of strings (in file order)
static func read_section(section: String) -> Dictionary:
	var out := {}
	var inside := false
	for line in _lines():
		if line.begins_with("["):
			inside = line == "[" + section + "]"
		elif inside and line.contains("="):
			var key := line.get_slice("=", 0)
			if not out.has(key):
				out[key] = []
			out[key].append(line.substr(key.length() + 1))
	return out


static func get_value(section: String, key: String, fallback: String = "") -> String:
	var values: Array = read_section(section).get(key, [])
	return values[-1] if values.size() > 0 else fallback


# Replaces a key's lines in a section with these values (none: the key goes), making the section if it isn't there
static func set_values(section: String, key: String, values: Array) -> void:
	var lines := _lines()
	var out := PackedStringArray()
	var inside := false
	var found := false
	var written := false
	for line in lines:
		if line.begins_with("["):
			if inside and not written:
				for value in values:
					out.append(key + "=" + str(value))
				written = true
			inside = line == "[" + section + "]"
			found = found or inside
		elif inside and line.get_slice("=", 0) == key:
			continue
		out.append(line)
	if inside and not written:
		# the section was the last one: keep its values before the file's closing blank lines
		while out.size() > 0 and out[-1] == "":
			out.remove_at(out.size() - 1)
		for value in values:
			out.append(key + "=" + str(value))
		out.append("")
		written = true
	if not found:
		while out.size() > 0 and out[-1] == "":
			out.remove_at(out.size() - 1)
		out.append("")
		out.append("[" + section + "]")
		for value in values:
			out.append(key + "=" + str(value))
		out.append("")
	if in_memory_only:
		_cached = out
		return
	DirAccess.make_dir_recursive_absolute(path().get_base_dir())	# (a new copy has no Saved folder yet)
	var file := FileAccess.open(path(), FileAccess.WRITE)
	if file == null:
		push_warning("Geodrive: couldn't save the settings to %s" % path())
		return
	file.store_string("\r\n".join(out))
	file.close()
	_cached = out
	_cached_time = FileAccess.get_modified_time(path())


static func set_value(section: String, key: String, value) -> void:
	set_values(section, key, [value])


# Saved spots, from every area: [{name, lat, lon, area}]
static func spots() -> Array:
	var out := []
	var section := read_section(FAVOURITES)
	for area in section:
		for entry in section[area]:
			var parts: PackedStringArray = str(entry).split("|")
			if parts.size() == 3:
				out.append({ "name": parts[0], "lat": float(parts[1]), "lon": float(parts[2]), "area": area })
	return out


# Where you last were (the start screen's Continue): {name, lat, lon}, or empty
static func last_spot() -> Dictionary:
	var parts := get_value("Geodrive", "LastSpot").split("|")
	if parts.size() != 3 or not parts[1].is_valid_float() or not parts[2].is_valid_float():
		return {}
	return { "name": parts[0], "lat": float(parts[1]), "lon": float(parts[2]) }


static func save_last_spot(spot_name: String, lat: float, lon: float) -> void:
	set_value("Geodrive", "LastSpot", "%s|%.6f|%.6f" % [spot_name.replace("|", " ").left(48), lat, lon])


# Saves a spot under the ready-made area it's in (as the Unreal game does), or "godot" if it's in none of them
static func save_spot(spot_name: String, lat: float, lon: float, areas: Array) -> void:
	var area_id := "godot"
	for area in areas:
		if lat >= area["south"] and lat <= area["north"] and lon >= area["west"] and lon <= area["east"]:
			area_id = area["id"]
			break
	var existing: Array = read_section(FAVOURITES).get(area_id, [])
	var clean := spot_name.replace("|", " ").left(32).strip_edges()
	if clean.is_empty():
		clean = "Spot %d" % (existing.size() + 1)
	existing.append("%s|%.6f|%.6f" % [clean, lat, lon])
	set_values(FAVOURITES, area_id, existing)
