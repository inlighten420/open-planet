# The game's settings, saved as they change. What means the same in both games is shared with the Unreal game (its
# GameUserSettings.ini keys: the car, speed units, keyboard steering, cockpit field of view, world pieces, building
# distance, the wheel's forces and clutch); the graphics are Godot's own, in a [Godot] section of the same file.
class_name GameSettings
extends RefCounted

# The game's version: on the start screen, in bug reports and the log, and in the release zip's name (package_godot.ps1
# reads it from here). Friends can only play together on the same one
const VERSION := "0.2"

const QUALITY := "/Script/Geodrive.GeodriveQualitySubsystem"
const WHEEL := "/Script/GeoFFB.GeoFFBSubsystem"
const GODOT := "Godot"

const PRESETS := ["Low", "Medium", "High", "Ultra"]
const AA_NAMES := ["Off", "FXAA", "MSAA 2x", "MSAA 4x"]
const VIEW_NAMES := ["Near", "Medium", "Far", "Furthest", "Very far", "Extreme"]
const VIEW_METRES := [8000.0, 15000.0, 22000.0, 30000.0, 40000.0, 50000.0]
const WINDOW_NAMES := ["Fullscreen", "Borderless", "Windowed"]
const RESOLUTIONS := [Vector2i(1280, 720), Vector2i(1600, 900), Vector2i(1920, 1080), Vector2i(2560, 1440), Vector2i(3840, 2160)]
const FRAME_LIMITS := [0, 30, 60, 90, 120, 144, 165, 240]
const WORLD_SIZES := [0, 8, 12, 16, 20, 24]
const DETAIL_KM := [3, 5, 7, 10]	# how far roads, buildings and the detailed ground are built
const BUILDING_TENTHS := [5, 10, 20, 30, 50, 0]	# (0, all to 7 km, is the far end)
const CAR_ORDER := [4, 3, 1, 2]
const CAR_NAMES := { 1: "RS 4.0", 2: "Formula", 3: "Hatchback 1.6", 4: "Bakkie 3.0 TDi" }
const CAR_ABOUT := {
	1: "A rear-engined sports coupe like a Porsche 911 GT3 RS (992). 1450 kg, a 4.0 flat six (386 kW at 8500 rpm, 465 Nm, 9000 rpm) behind the rear axle, 7-speed twin clutch, rear-wheel drive, semi-slick tyres, 409 kg of downforce at 200 km/h from its swan-neck wing.",
	4: "A double-cab bakkie like a 2007 Isuzu KB300 TDi LX. 1850 kg, a 3.0 turbo diesel (96 kW, 280 Nm low down), 5-speed manual, rear-wheel drive with a diff lock (L), all-terrain tyres, leaf springs.",
	3: "A small everyday car, like a Suzuki Swift Sport. 1120 kg, 100 kW, 6 speeds, front-wheel drive, road tyres. The one for real roads at real speeds.",
	2: "An open-wheel racing car. 800 kg and near 1000 hp, 8 speeds, huge grip and downforce. A handful on public roads.",
}

# shared with the Unreal game
var car_body := 4
var speed_units := 0	# 0 auto (mph in the UK), 1 km/h, 2 mph
var steer_speed := 5	# 1-10
var cockpit_fov := 80
var body_height := 194	# cm: the player's own height (walking, and the driver's eyes in the cockpit); 6' 4", the player's own
var world_size := 0	# km; 0 = by the memory there is
var traffic_cars := 0	# 0-150, a third of them parked
var building_tenths := 0	# tenths of a km; 0 = all (7 km)
var detail_km := 7	# km: how far roads, buildings and the detailed ground are built (the Unreal game's too)
var show_fps := false
var show_memory := false
var wheel_gain := 0.6	# (Core's defaults when nothing is saved: 0.6, 900 degrees, 12)
var lockup_gain := 1.0
var abs_gain := 1.0	# (set with the brake lock unless it was saved apart)
var scrub_gain := 1.0
var road_gain := 1.0
var clutch_pedal := true
var wheel_rotation := 900.0	# lock to lock, degrees (must match the wheel's own software)
var steering_ratio := 12.0
var invert_steering := false
var ffb_enabled := true
var invert_ffb := false
var sound_levels := {"geo.Sound.Volume": 1.0, "geo.Sound.Engine": 1.0, "geo.Sound.Rasp": 1.0, "geo.Sound.Pipe": 1.0, "geo.Sound.Crackle": 1.0}

# Godot's own
var abs_level := 3	# driver aids, 0-10 (0 = off)
var tc_level := 3
var follow_land := true	# the road style: follow the land, or legal design grades
var kerbs := true
var road_lines := false	# the roads' centrelines drawn (debug)
var scan_roads := false	# scan every road of each piece built (debug)
var preset := 2
var render_scale := 100
var anti_aliasing := 3
var shadows := 2
var lighting := 2	# the sun's haze, contact shadows, glow (lighting.gd)
var textures := 2	# how far the ground's texture detail goes (ground.gd)
var vegetation := 3	# the plants: 0 off, 1-4 low to ultra (vegetation.gd)
var view_distance := 3
var window_mode := 2
var resolution := 1
var vsync := true
var frame_limit := 0
var hud_shown := true
var debug_info := false
var detected := ""


func load_all() -> void:
	var q := UnrealSettings.read_section(QUALITY)
	var w := UnrealSettings.read_section(WHEEL)
	var g := UnrealSettings.read_section(GODOT)
	car_body = _int(q, "CarBody", car_body)
	if not CAR_NAMES.has(car_body):
		car_body = 4	# (Unreal's template car isn't in Godot)
	speed_units = clampi(_int(q, "SpeedUnits", speed_units), 0, 2)
	steer_speed = clampi(_int(q, "KeyboardSteerSpeed", steer_speed), 1, 10)
	cockpit_fov = clampi(_int(q, "CockpitFOV", cockpit_fov), 50, 110)
	body_height = clampi(_int(q, "BodyHeightCm", body_height), 120, 230)
	world_size = _int(q, "WorldSize", world_size)
	traffic_cars = clampi(_int(q, "TrafficCars", traffic_cars), 0, 150)
	building_tenths = _int(q, "BuildingDistanceTenths", building_tenths)
	detail_km = _int(q, "DetailDistanceKm", detail_km)
	if not DETAIL_KM.has(detail_km):
		detail_km = 7
	show_fps = _bool(q, "bShowFPS", show_fps)
	show_memory = _bool(q, "bShowMemory", show_memory)
	wheel_gain = _float(w, "FFBGain", wheel_gain)
	lockup_gain = _float(w, "LockupGain", lockup_gain)
	abs_gain = _float(w, "ABSGain", lockup_gain)
	scrub_gain = _float(w, "ScrubGain", scrub_gain)
	road_gain = _float(w, "RoadGain", road_gain)
	clutch_pedal = _bool(w, "bUseClutchPedal", clutch_pedal)
	wheel_rotation = _float(w, "WheelRotationDegrees", wheel_rotation)
	steering_ratio = _float(w, "SteeringRatio", steering_ratio)
	invert_steering = _bool(w, "bInvertSteering", invert_steering)
	ffb_enabled = _bool(w, "bFFBEnabled", ffb_enabled)
	invert_ffb = _bool(w, "bInvertFFB", invert_ffb)
	# Unreal's map: (("geo.Sound.Pipe", 0.800000),...)
	var levels: Array = q.get("SoundLevels", [])
	if levels.size() > 0:
		var text: String = levels[-1]
		for part in text.split("(\"", false):
			var key := part.get_slice("\"", 0)
			if sound_levels.has(key):
				sound_levels[key] = clampf(float(part.get_slice(",", 1).replace(")", "").strip_edges()), 0.0, 2.0)
	preset = clampi(_int(g, "Preset", preset), 0, PRESETS.size() - 1)
	render_scale = clampi(_int(g, "RenderScale", render_scale), 50, 100)
	anti_aliasing = clampi(_int(g, "AntiAliasing", anti_aliasing), 0, 3)
	shadows = clampi(_int(g, "Shadows", shadows), 0, 3)
	lighting = clampi(_int(g, "Lighting", [0, 1, 2, 3][preset]), 0, 3)	# (not saved yet: as the preset has it)
	textures = clampi(_int(g, "Textures", [0, 1, 2, 3][preset]), 0, 3)
	vegetation = clampi(_int(g, "Vegetation", [1, 2, 3, 4][preset]), 0, 4)
	view_distance = clampi(_int(g, "ViewDistance", view_distance), 0, VIEW_METRES.size() - 1)
	window_mode = clampi(_int(g, "WindowMode", window_mode), 0, 2)
	resolution = clampi(_int(g, "Resolution", resolution), 0, RESOLUTIONS.size() - 1)
	vsync = _bool(g, "bVSync", vsync)
	frame_limit = _int(g, "FrameLimit", frame_limit) if FRAME_LIMITS.has(_int(g, "FrameLimit", frame_limit)) else 0
	hud_shown = _bool(g, "bHUD", hud_shown)
	debug_info = _bool(g, "bDebugInfo", debug_info)
	abs_level = clampi(_int(g, "ABSLevel", abs_level), 0, 10)
	tc_level = clampi(_int(g, "TCLevel", tc_level), 0, 10)
	follow_land = _int(g, "RoadStyle", 0) == 0
	kerbs = _bool(g, "bKerbs", kerbs)
	road_lines = _bool(g, "bRoadLines", road_lines)
	scan_roads = _bool(g, "bScanRoads", scan_roads)


func _int(section: Dictionary, key: String, fallback: int) -> int:
	var values: Array = section.get(key, [])
	return int(values[-1]) if values.size() > 0 else fallback


func _float(section: Dictionary, key: String, fallback: float) -> float:
	var values: Array = section.get(key, [])
	return float(values[-1]) if values.size() > 0 else fallback


func _bool(section: Dictionary, key: String, fallback: bool) -> bool:
	var values: Array = section.get(key, [])
	return str(values[-1]) == "True" if values.size() > 0 else fallback


# Where the map data and what's fetched are kept: in a packaged game the "data" folder beside Geodrive.exe (the regions
# and heights it came with; terrain and land cover fetched into it as you go); in the editor, this PC's own cache
static func cache_dir() -> String:
	if _cache_dir == "":
		_cache_dir = _find_cache_dir()
	return _cache_dir


static var _cache_dir := ""

const DATA_MEMORY := "user://data_folder.txt"	# (where the maps were last found: they outlive an update's new folder)

# Where the maps are, kept for the next version to find (called when the game starts with maps, and when it has just made some)
static func remember_data(folder: String) -> void:
	var memory := FileAccess.open(DATA_MEMORY, FileAccess.WRITE)
	if memory:
		memory.store_string(folder)


static func has_maps_in(folder: String) -> bool:
	return _has_maps(folder)


# Places in a data folder that are the player's own: not the demo areas that ship with the game (regions/demo.json says
# which), which every package has, and which must not make a new version think it already has the maps an earlier one's
# folder holds
static func _own_places(folder: String) -> int:
	var shipped := ["monaco.pmtiles"]
	for demo in _demos_in(folder):
		shipped.append(String(demo["stem"]) + ".pmtiles")
	var count := 0
	for file in DirAccess.get_files_at(folder.path_join("regions")):
		if file.ends_with(".pmtiles") and not shipped.has(file):
			count += 1
	return count


# The demo areas the game shipped with ({stem, name, lat, lon} each) whose maps are here
static func demo_areas() -> Array:
	var out := []
	for demo in _demos_in(cache_dir()):
		if FileAccess.file_exists(cache_dir().path_join("regions/%s.pmtiles" % demo["stem"])):
			out.append(demo)
	if out.is_empty() and FileAccess.file_exists(cache_dir().path_join("regions/monaco.pmtiles")):
		out.append({"stem": "monaco", "name": "Monte Carlo, Monaco", "lat": 43.7390, "lon": 7.4276})	# (an older package's demo)
	return out


# regions/demo.json: {"demos": [{stem, name, lat, lon}, ...]} (or one such, as the first packages with it wrote)
static func _demos_in(folder: String) -> Array:
	var text := FileAccess.get_file_as_string(folder.path_join("regions/demo.json")).strip_edges().trim_prefix("\uFEFF")
	var parsed = JSON.parse_string(text) if text != "" else null
	var list: Array = parsed.get("demos", [parsed]) if parsed is Dictionary else []
	return list.filter(func(d): return d is Dictionary and d.has("stem") and d.has("lat") and d.has("lon") and d.has("name"))


# The demos this version shipped with, copied into the folder of maps it uses (an earlier version's): there, the menu
# finds them as it would beside it (their maps and heights; the far terrain and land cover are fetched as needed)
static func _bring_demos(from: String, into: String) -> void:
	var demos := _demos_in(from)
	if demos.is_empty():
		return
	for demo in demos:
		var stem := String(demo["stem"])
		for relative in ["regions/%s.pmtiles" % stem, "regions/%s.region.json" % stem, "regions/%s.world.json" % stem,
				"regions/%s.airports.json" % stem, "regions/%s.games.json" % stem, "heights/cop_%s.pmtiles" % stem, "heights/%s.pmtiles" % stem]:
			var source := from.path_join(relative)
			var target := into.path_join(relative)
			if FileAccess.file_exists(source) and (not FileAccess.file_exists(target) or FileAccess.get_file_as_bytes(target).size() != FileAccess.get_file_as_bytes(source).size()):
				DirAccess.make_dir_recursive_absolute(target.get_base_dir())
				DirAccess.copy_absolute(source, target)
	DirAccess.copy_absolute(from.path_join("regions/demo.json"), into.path_join("regions/demo.json"))


static func _has_maps(folder: String) -> bool:
	return DirAccess.dir_exists_absolute(folder.path_join("regions")) and _own_places(folder) > 0


# A new version unzipped to a new folder starts with no maps of its own: use the folder the maps were in last time
# (remembered across versions) while it's still there, so an update keeps everyone's maps without copying gigabytes
static func _find_cache_dir() -> String:
	var beside := OS.get_executable_path().get_base_dir().path_join("data")
	if OS.has_feature("template") and DirAccess.dir_exists_absolute(beside):
		if _has_maps(beside):
			remember_data(beside)
			return beside
		if FileAccess.file_exists(DATA_MEMORY):
			var old := FileAccess.get_file_as_string(DATA_MEMORY).strip_edges()
			if old != "" and DirAccess.dir_exists_absolute(old) and _has_maps(old):
				print("Geodrive: using the maps from the earlier version's folder: ", old)
				_bring_demos(beside, old)
				return old
		return beside
	return OS.get_environment("LOCALAPPDATA").path_join("Geodrive/Cache")


# Where the game writes its own files (settings, road reports): beside the project in the editor (the Unreal game's
# Saved folder, shared with it), the player's own folder in a packaged game
static func saved_dir() -> String:
	if OS.has_feature("template"):
		return OS.get_user_data_dir()
	return ProjectSettings.globalize_path("res://").path_join("../Saved").simplify_path()


static func _flag(value: bool) -> String:
	return "True" if value else "False"


func save_quality(key: String, value) -> void:
	UnrealSettings.set_value(QUALITY, key, _flag(value) if value is bool else value)


func save_wheel(key: String, value) -> void:
	UnrealSettings.set_value(WHEEL, key, _flag(value) if value is bool else ("%.6f" % value if value is float else value))


func save_godot(key: String, value) -> void:
	UnrealSettings.set_value(GODOT, key, _flag(value) if value is bool else value)


func save_sound() -> void:
	var entries := PackedStringArray()
	for key in sound_levels:
		if not is_equal_approx(sound_levels[key], 1.0):
			entries.append("(\"%s\", %.6f)" % [key, sound_levels[key]])
	UnrealSettings.set_values(QUALITY, "SoundLevels", ["(" + ",".join(entries) + ")"] if entries.size() > 0 else [])


func save_graphics() -> void:
	save_godot("Preset", preset)
	save_godot("RenderScale", render_scale)
	save_godot("AntiAliasing", anti_aliasing)
	save_godot("Shadows", shadows)
	save_godot("Lighting", lighting)
	save_godot("Textures", textures)
	save_godot("Vegetation", vegetation)
	save_godot("ViewDistance", view_distance)
	save_quality("DetailDistanceKm", detail_km)


# --- what the settings mean ---

# Keys turn the wheels at this rate (share of full lock a second), as the Unreal game's steering speed
func steer_rate() -> float:
	return 2.5 * pow(1.3, steer_speed - 5)


# The share of lock left at 40 m/s
func high_speed_lock() -> float:
	return clampf(0.3 + (steer_speed - 5) * 0.05, 0.05, 0.6)


func uses_mph(in_uk: bool) -> bool:
	return speed_units == 2 or (speed_units == 0 and in_uk)


# How far buildings are drawn (m): their own setting, never past the detail distance (they aren't built further)
func building_metres() -> float:
	var detail := detail_km * 1000.0
	return detail if building_tenths <= 0 else minf(building_tenths * 100.0, detail)


func detail_metres() -> float:
	return detail_km * 1000.0


func view_metres() -> float:
	return VIEW_METRES[view_distance]


func auto_world_km() -> int:
	var memory: Dictionary = OS.get_memory_info()
	var gb := float(memory.get("physical", 16 << 30)) / float(1 << 30)
	return 8 if gb < 12.0 else (12 if gb < 24.0 else 16)


# A preset sets the picture settings below it to its own
func use_preset(level: int) -> void:
	preset = clampi(level, 0, 3)
	render_scale = [75, 90, 100, 100][preset]
	anti_aliasing = [1, 1, 3, 3][preset]
	shadows = [0, 1, 2, 3][preset]
	lighting = [0, 1, 2, 3][preset]
	textures = [0, 1, 2, 3][preset]
	vegetation = [1, 2, 3, 4][preset]
	view_distance = [1, 2, 3, 5][preset]
	detail_km = [3, 5, 7, 10][preset]


# The preset for this PC's graphics card, by its name: integrated graphics Low, older cards (GTX, RX 400/500, Arc) Medium,
# RTX 20/30 and the like High, RTX 40/50 and RX 7000/9000 Ultra
func detect() -> void:
	var gpu := RenderingServer.get_video_adapter_name()
	var name := gpu.to_lower()
	var level := 2
	var arc_card := RegEx.create_from_string("arc(\\(tm\\))? [ab]\\d").search(name) != null	# (A770, B580: "Arc Graphics" alone is built in)
	if arc_card:
		level = 1
	elif name.contains("intel") or name.contains("radeon(tm) graphics") or name.contains("vega") or name.contains("uhd") or name.contains("iris"):
		level = 0
	elif name.contains("gtx") or name.contains("rx 5") or name.contains("rx 4"):
		level = 1
	elif name.contains("rtx 40") or name.contains("rtx 50") or name.contains("rx 7") or name.contains("rx 9"):
		level = 3
	use_preset(level)
	detected = "%s (%s)" % [PRESETS[level], gpu]


func is_preset() -> bool:
	var saved := [render_scale, anti_aliasing, shadows, view_distance, detail_km, lighting, textures, vegetation]
	use_preset(preset)
	var same := saved == [render_scale, anti_aliasing, shadows, view_distance, detail_km, lighting, textures, vegetation]
	lighting = saved[5]
	textures = saved[6]
	vegetation = saved[7]
	detail_km = saved[4]
	render_scale = saved[0]
	anti_aliasing = saved[1]
	shadows = saved[2]
	view_distance = saved[3]
	return same


# The picture: render scale, anti-aliasing, the sun's shadows; and the window, VSync and frame limit unless a test runs
func apply_graphics(viewport: Viewport, sun: DirectionalLight3D, window_too: bool) -> void:
	viewport.scaling_3d_scale = render_scale / 100.0
	viewport.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR if render_scale < 100 else Viewport.SCALING_3D_MODE_BILINEAR
	viewport.screen_space_aa = Viewport.SCREEN_SPACE_AA_FXAA if anti_aliasing == 1 else Viewport.SCREEN_SPACE_AA_DISABLED
	viewport.msaa_3d = [Viewport.MSAA_DISABLED, Viewport.MSAA_DISABLED, Viewport.MSAA_2X, Viewport.MSAA_4X][anti_aliasing]
	if sun:
		sun.directional_shadow_max_distance = [150.0, 300.0, 600.0, 1000.0][shadows]
		sun.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL if shadows == 0 else DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
		RenderingServer.directional_shadow_atlas_set_size([1024, 2048, 4096, 8192][shadows], true)
		RenderingServer.directional_soft_shadow_filter_set_quality([RenderingServer.SHADOW_QUALITY_HARD, RenderingServer.SHADOW_QUALITY_SOFT_LOW, RenderingServer.SHADOW_QUALITY_SOFT_MEDIUM, RenderingServer.SHADOW_QUALITY_SOFT_HIGH][shadows])
	if not window_too:
		return
	apply_window()
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if vsync else DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = frame_limit


func apply_window() -> void:
	var mode: DisplayServer.WindowMode = [DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN, DisplayServer.WINDOW_MODE_FULLSCREEN, DisplayServer.WINDOW_MODE_WINDOWED][window_mode]
	if DisplayServer.window_get_mode() != mode:
		DisplayServer.window_set_mode(mode)
	if window_mode == 2:
		var screen := DisplayServer.screen_get_size()
		var size: Vector2i = RESOLUTIONS[resolution]
		size = Vector2i(mini(size.x, screen.x), mini(size.y, screen.y - 60))
		if DisplayServer.window_get_size() != size:
			DisplayServer.window_set_size(size)
			DisplayServer.window_set_position(DisplayServer.screen_get_position() + (screen - size) / 2)
