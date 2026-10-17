# Geodrive on Godot: the real world built by Geodrive's engine-free Core, driven with the Unreal game's controls.
# Live (the GeodriveWorld extension in bin/): Godot builds the area, the ground streams in round you, and you drive the
# bakkie (GeodriveCar: Core's car physics and body) from a main road near the middle. Or from a file (--glb): the area
# geotile godot wrote to areas/<name>.glb, with the free camera.
#
#   Driving: W / S or Up / Down throttle and brake (hold the brake when stopped to reverse), A / D or Left / Right steer,
#            Space handbrake, L diff lock, I the engine off / on (on foot too, beside the car), F1 the game here on / off, E / Q gear up / down (manual), G back to automatic; a steering wheel drives
#            with force feedback (its setup read from the Unreal game's settings); a gamepad works too.
#   C cycles the view (chase, cockpit, cockpit without the steering wheel, bonnet); Z / X look left / right, V looks
#   back, the mouse looks round. F: free camera (WASD / arrows, E / Q up / down, mouse, Shift faster; F again puts the
#   car on the road there). M: map from above (mouse wheel zooms). Backspace: back on its wheels
#   where it is. H hides the HUD, F3 shows the numbers, B reports a bad road spot. Esc or Tab: the menu (menu.gd, as
#   the Unreal game's: resume, teleport, car, world map, drive somewhere else, settings, quit). With no place or test asked for it
#   opens on the front menu (Drive: your car, then where); a new area shows the loading screen (loading_screen.gd).
#   Command line (after --): --lat=  --lon=  --km=  (live; Cape Town by default)
#                            --glb --area=<name> (from a file);  --fly (no car);  --shot=<file.png> saves pictures and
#                            quits;  --drivetest prints a launch and a stop and quits;  --view=<0-3> starts in that view;
#                            --car=<1-4> drives that car;  --side puts the camera beside the car (front quarter);  --wheeltest reports the wheel
#                            (with no forces) and quits;  --flytest flies 2.4 km over the area, logging slow frames;
#                            --teleport=lat,lon (at frame 60) and --map (at frame 200), for tests.
#                            --verbose logs everything (slow frames, the world's detail lines).
#                            --menu opens on the front menu anyway;  --menutest=<folder> saves pictures of the menu
#                            pages, the loading screen and a car change, then quits.
extends Node3D

const WALKER := preload("res://walker.gd")	# (loaded by path: on foot)
const PHONE := preload("res://phone.gd")	# (the phone: Tab)
const GAME_GEN := preload("res://game_gen.gd")	# (games made by the world, as its roads are)
const GOLF := preload("res://golf.gd")	# (golf on the real courses)
const AVIATION := preload("res://aviation.gd")	# (flying: airports, runways, the aircraft)
const BUILDER := preload("res://builder.gd")	# (building, on foot: B)
const HUNT := preload("res://hunt.gd")	# (game farms: their fences, tickets at the gate, the hunt)

var area_name := "cape_town"
var shot_path := ""
var drive_test := false
var engine_test := false	# --enginetest: off on the handbrake, the throttle on, the engine switched off (no power, it spins down), on again (it starts: power again)
var _engine := {}
var side_view := false
var side_distance := 7.1	# m: --side=<m>[,<degrees>] how far off (a close look at a car), and round from its nose
var side_angle := 50.7
var wheel_test := false
var fly_test := false
var fly_test_time := 0.0
var fly_seconds := 60.0
var fly_speed := 40.0
var fly_frames: Array[float] = []
var start_view := -1
var car_override := -1	# --car=<1-4>: that car (pictures), not the saved one
var test_teleport := PackedFloat64Array()
var test_map := false
var key_test := false
var reset_test := false
var r_test := false	# --rtest: the car put off the road, then back on it (what F twice and the recoveries use)
var speed_test := 0.0	# seconds of autopilot, flat out along the roads (0 = off)
var speed_log := 0.0
var stuck_time := 0.0
var frame := 0
var last_camera := Vector3.ZERO
var shown_frames := 0	# frames since the area was in (the pictures count from there)
var info := {}
var world: Node3D = null
var car: RigidBody3D = null
var walker: CharacterBody3D = null	# on foot (Enter): out of the car, walking (walker.gd)
var aviation: Node = null	# flying (aviation.gd)
var builder: Node = null	# building (builder.gd)
var hunt: Node = null	# game farms and hunting (hunt.gd)
var build_hands := false	# --buildtest=hands: out of the car, B, a foundation where they look, a wall on it, and quits
var build_stairs := false	# --buildtest=stairs: a flight of stairs, walked up at a walk (no jumping); how high they got
var build_trees := false	# --buildtest=trees: on foot, the nearest tree aimed at from 6 m, right clicked twice: cut down (gone from every ring, kept in the file); brought back
var _trees_test := {}
var build_remove := false	# --buildtest=remove: a building of the map's near the car taken away (gone, drawn and solid)
var _remove_test := {}	# (its state: the building, the point over it, the roof's height)
var gpu_profile := false	# --gpuprofile: what the graphics card spends its frame on, each kind of thing switched off in turn
var build_test := false	# --buildtest: a double-storey house built beside the car, its pictures (with --shot)
var fly_request := ""	# --fly=kind,airport: a flight once the start area is in
var cruise_test := false	# --cruisetest (with --fly): a long level flight, the world's streaming logged
var plane_test := false	# --planetest: a scripted take-off and climb, the readings printed
var runway_timer := 0.0
var wheel: Node = null
var camera: CarCamera
var hud: Hud
var menu: GameMenu
var phone: CanvasLayer	# (Tab: the map, your cars, games, building, friends, settings)
var game_gen: Node	# (the world's own games: game_gen.gd)
var golf: Node	# (a round of golf: golf.gd)
var golf_test := false	# --golftest (at Steenberg: -34.068, 18.427): the clubs' carries, then three holes played by a robot golfer
var gen_test := false	# --gentest: the world's games round the start (Cape Town): made, each a booth, a race on the road
var region_fetch: Node	# region_fetch.gd: somewhere with no map yet, made in the background
const REGION_FETCH := preload("res://region_fetch.gd")
var loading: LoadingScreen
var world_map: WorldMap
var net: GameNet
var map_overlay: Control
var map_font := SystemFont.new()
var car_sound: Node = null
var settings := GameSettings.new()
var sun: DirectionalLight3D
var environment: Environment
var lighting: GameLighting
var vegetation: VegetationLook
var testing := false	# a test run: no front menu, the window left as it is
var last_frame_us := 0	# (the slow frame log)
var verbose := false	# --verbose: the whole log (slow frames, the world's detail lines), as the tests have it
var lighting_override := -1	# --lighting=0-3: that Lighting level for this run (pictures, measuring)
var textures_override := -1	# --textures=0-3: that Textures level for this run (pictures, measuring)
var vegetation_override := -1	# --vegetation=0-4: that Vegetation level for this run (pictures, measuring)
var map_draw_ms := 0.0	# (the slowest M map drawing, for the tests)
var tunnel_test := ""	# --tunneltest=<folder> (Monaco: --lat=43.7384 --lon=7.4246): its tunnels from inside and above, pictures
var event_name := "Killarney"	# (--event=<name>: which race track the event test goes to)
var event_test := ""	# --eventtest=<folder>: the start screen's events, then the phone's race tracks: Go, Killarney, there by its booth
var make_test := ""	# --maketest=<folder>: building's bar, Q's Place, Make (a table made a gun): pictures and checks
var phone_test := ""	# --phonetest=<folder>: out of the car, the phone's pages (pictures), a car parked on the road nearest
var menu_test := ""	# --menutest=<folder>: pictures of the menu pages and the loading screen
var error_until := 0.0
var km := 0.0	# --km=: the world pieces' size for this run (0: the setting)
var steer := 0.0
var title := ""
var test_time := 0.0
var test_step := 0
var trip_km := 0.0
var street := ""
var street_timer := 0.0
var flip_timer := 0.0
var flip_count := 0
var ride: Array = []	# the last 6 s: time, vertical speed, position, speed (for B)
var last_position := Vector3.ZERO
var waiting := ""	# "start" (a new drive), "area" (a new area round a far place), "hop" (a far place in the built area)
var came_from := PackedFloat64Array()	# where the car was when a new area started (lat, lon)
var going_back := false
var want_car := true
var _prepared := []	# the kinds of car made ahead during the loading screen (GeodriveCar.prepare), the driven one first
var hold_lat := 0.0
var hold_lon := 0.0
var hold_name := ""
var hold_facing := Vector3.FORWARD
var hold_target := Vector3.ZERO
var hold_check := Vector3.ZERO
var hold_have_target := false
var hold_until := 0.0
var mph := false
var traffic_override := -1	# --traffic=<n> (tests): that many traffic cars, not saved
var traffic_log := 0.0
var scan_override := false	# --scan: the road scan on, not saved
var robot_mode := ""	# --robot=<n2|town|all>[,trips[,shard,shards]]: the robot test driver once the area is in, then quit (headless and
# sped up: godot --headless --fixed-fps 60 ... ; Tools/robot_run.ps1 -Godot runs several copies)
var robot_status_timer := 0.0
var robot_started := false
var info_timer := 0.0
var knock_velocity := Vector3.ZERO
var knock_skip := 3
var last_knock := -10.0
var net_host := false	# --host: host once the area is in (tests)
var net_join := ""		# --join=<address>: join at the start (tests)
var net_test := 0.0		# --nettest=<s>: log the others' cars every 2 s, quit after this long
var net_log := 0.0
var f_test := false	# --ftest: fly the free camera 2 km off, press F: back to the car, where it waited
var f_test_time := 0.0
var f_km := 2.2
var camera_behind := false	# --behind (pictures): on foot, the camera behind them
var switch_test := -1.0	# --switchtest (with --fly): the aircraft sunk under the ground at 15 s (it must come back on the
	# runway), a car chosen mid-flight at 45 s (the flight over, the car on a road), and quits at 75 s
var walk_test := false	# --walktest: out of the car, walk, run, jump, back in (walker.gd), and quit
var crash_test := false	# --crashtest (with --fly=0,FACT): a dive into the ground; the wreck stays put; R back on its wheels; out; V, the car here
var _crash := {"step": 0, "time": 0.0}
var status_log := false	# --statuslog: the world's loading, and the memory, every 2 s (to see what a teleport leaves loading)
var _status_log_timer := 0.0
var world_map_test := false	# --worldmaptest: the world map opened, zoomed from the whole world to a town and back a step a frame, its slowest frame told; pictures with --shot
var _wm := {"step": 0, "frames": 0, "worst": 0.0}
var fetch_test := PackedFloat64Array()	# --fetchtest=lat,lon: go there, no map yet: "Download it" pressed, then there once it's made
var car_hit_test := false	# --carhittest: on foot, your own car rolled into you: at 3.6 km/h it pushes you aside (you stay up); at 40 km/h you're knocked down, the car a little slower, up again by it after
var _car_hit := {"time": 0.0}
var share_test := ""	# --sharetest=host|join (with --host / --join=127.0.0.1, in George): the host makes a crate (a prop), puts one, makes a race and a gun; the joiner gets them all (the crate on its map, the race's booth, the gun at the booths), puts a crate of its own (the host puts it: both have 2), leaves (its own back)
var _share := {"time": 0.0}
var mp_test := ""	# --mptest=host|join (with --host / --join=127.0.0.1): both at a game farm with tickets, the joiner shoots the host
var _mp_test := {"step": 0, "time": 0.0}
var hunt_test := false	# --hunttest (at a game farm): a ticket at its kiosk, in, the nearest legal animal shot, out again
var _hunt_test := {"step": 0, "time": 0.0}
var roadkill_test := false	# --roadkilltest (at a game farm): the car driven at an animal slowly (bumped: it runs), at another fast (killed: the car a little slower)
var _roadkill := {"step": 0, "time": 0.0}
var booth_test := false	# --boothtest (in George): before a ticket no zombies, a hint where the booth is, the M map and the world map with the booth (pictures with --shot); a ticket: the zombies out, one on a road (a picture)
var _booth := {"step": 0, "time": 0.0}
var workshop_test := false	# --workshoptest (in George): a submachine gun made in the workshop (9 mm, full auto, 800 a minute), saved; a ticket with it; the trigger held a second at a zombie 15 m off: ~13 rounds, it's down
var _workshop := {"step": 0, "time": 0.0}
var race_test := false	# --racetest: a race made in the game maker (4 checkpoints ahead of the car, point to point, no weapons, finish wins); F1 offers "Take part"; the car through them (the third early: it doesn't count): the game's line, finished, won, its time. Pictures with --shot
var _race := {"step": 0, "time": 0.0}
var rules_test := false	# --rulestest (in George): the zone's rules made +1 a zombie, 3 wins, 60 s: three zombies shot (the score's line, then won, the game over); then a 3 s limit: lost when it's up. Pictures with --shot
var _rules_t := {"step": 0, "time": 0.0}
var game_maker_test := false	# --gamemakertest: a game made in the workshop: its area marked on the M map (four clicks round you, Enter), its booth where you stand, 20 springbok and 10 zombies, only the .308; saved; its booth up; F1 there offers its ticket (the .308 only); played: 30 creatures out. Pictures with --shot
var _gm := {"step": 0, "time": 0.0}
var prop_test := false	# --proptest: a bench made in the workshop's props, saved; chosen, aimed at the ground, put there: solid, kept, back when its area's built again; right clicked twice: gone. Pictures with --shot
var _prop := {"step": 0, "time": 0.0}
var editor_test := false	# --editortest: the workshop's shape editor driven as a mouse would: a click picks a part, its z arrow dragged moves it (on the grid), in Size its y arrow makes it taller, the part dragged moves it across the view, made 40 cm long; pictures with --shot
var _editor := {"step": 0, "time": 0.0}
var zombie_test := false	# --zombietest (in George: the zombie pack's zone): a ticket at its booth; the zombies out of the buildings; one comes for you and catches you; up by the car; a chest shot doesn't stop one, a head shot does
var _zombie_test := {"step": 0, "time": 0.0}
var fence_test := false	# --fencetest (at a game farm): the free camera at its main gate once its fence is up
var arrive_test := false	# --arrivetest: on foot by the car at the start, and again after a teleport across the area
var _arrive_frame := 0
var stand_test := false	# --standtest (by an airport: --lat --lon): on foot to its plane stand, a plane chosen there, flying it
var _stand_step := -1
var _stand_time := 0.0
var arrive_on_foot := false	# (a teleport or a new area: once the car's down, you're beside it on foot; tests stay in it)
var arrive_stand := {}	# (an airport gone to: you arrive at its plane stand, the car on the road nearest)
var walk_test_time := 0.0
var walk_test_marks := [1.0, 4.0, 7.0, 9.5, 11.8, 12.9]
var high_view := false	# --high (pictures): the free camera 1.2 km up, looking out to the horizon
var race_pic := false	# --racepic (pictures): a second car beside yours, a length ahead, both held on the road; the camera low at the front quarter
var _rival: RigidBody3D
var clean_pic := false	# --clean (pictures): no numbers on screen (frame time, memory) and no key hints, for showing the game off
var look := Vector3(1200.0, -8.0, -60.0)	# --look=up,pitch,yaw (pictures): the free camera this high above the car, this way (degrees)
var f_step := 0
var f_start := Vector3.ZERO
var road_lines_node: MeshInstance3D = null
var road_lines_timer := 0.0
var seat_moving := false
var change_view_was_down := false


func _ready() -> void:
	# the game's name players see (the project keeps "Geodrive": its saves are kept under that name)
	get_window().title = "Open-Planet"
	var lat := -33.9375
	var lon := 18.44
	var live := ClassDB.class_exists("GeodriveWorld")
	var place_given := false
	var front_menu := false
	if "--creaturetest" in OS.get_cmdline_user_args():
		# (creature files read and written: nothing else needed)
		Creatures.self_test()
		get_tree().quit()
		return
	for arg in OS.get_cmdline_user_args():
		place_given = place_given or arg.begins_with("--lat=") or arg.begins_with("--lon=")
		if arg == "--menu":
			front_menu = true
		elif arg.begins_with("--menutest="):
			menu_test = arg.substr(11)
			front_menu = true
		if arg.begins_with("--area="):
			area_name = arg.substr(7)
		elif arg.begins_with("--shot="):
			shot_path = arg.substr(7)
		elif arg.begins_with("--lat="):
			lat = float(arg.substr(6))
		elif arg.begins_with("--lon="):
			lon = float(arg.substr(6))
		elif arg.begins_with("--km="):
			km = float(arg.substr(5))
		elif arg.begins_with("--view="):
			start_view = int(arg.substr(7))
		elif arg.begins_with("--car="):
			car_override = int(arg.substr(6))
		elif arg.begins_with("--teleport="):
			var parts := arg.substr(11).split(",")
			if parts.size() == 2:
				test_teleport = PackedFloat64Array([float(parts[0]), float(parts[1])])
			else:
				push_warning("--teleport wants lat,lon")
		elif arg.begins_with("--traffic="):
			traffic_override = int(arg.substr(10))
		elif arg == "--scan":
			scan_override = true
		elif arg.begins_with("--robot="):
			robot_mode = arg.substr(8)
		elif arg.begins_with("--ftest"):
			f_test = true
			f_km = float(arg.get_slice("=", 1)) if arg.contains("=") else 2.2
		elif arg == "--crashtest":
			crash_test = true
		elif arg == "--statuslog":
			status_log = true
		elif arg == "--worldmaptest":
			world_map_test = true
		elif arg.begins_with("--fetchtest="):
			var parts := arg.substr(12).split(",")
			fetch_test = PackedFloat64Array([float(parts[0]), float(parts[1])])
		elif arg.begins_with("--mptest="):
			mp_test = arg.substr(9)
		elif arg == "--carhittest":
			car_hit_test = true
		elif arg.begins_with("--sharetest="):
			share_test = arg.substr(12)
		elif arg == "--hunttest":
			hunt_test = true
		elif arg == "--zombietest":
			zombie_test = true
		elif arg == "--roadkilltest":
			roadkill_test = true
		elif arg == "--boothtest":
			booth_test = true
		elif arg == "--workshoptest":
			workshop_test = true
		elif arg == "--editortest":
			editor_test = true
		elif arg == "--proptest":
			prop_test = true
		elif arg == "--gamemakertest":
			game_maker_test = true
		elif arg == "--rulestest":
			rules_test = true
		elif arg == "--racetest":
			race_test = true
		elif arg == "--fencetest":
			fence_test = true
		elif arg == "--arrivetest":
			arrive_test = true
		elif arg == "--standtest":
			stand_test = true
			arrive_test = true	# (on foot at the start, as a player arrives)
		elif arg == "--walktest":
			walk_test = true
		elif arg == "--gpuprofile":
			gpu_profile = true
		elif arg.begins_with("--buildtest"):
			build_test = true
			build_hands = arg.ends_with("hands")
			build_stairs = arg.ends_with("stairs")
			build_remove = arg.ends_with("remove")
			build_trees = arg.ends_with("trees")
		elif arg == "--switchtest":
			switch_test = 0.0
		elif arg == "--behind":
			camera_behind = true
		elif arg.begins_with("--fly="):
			fly_request = arg.substr(6)
		elif arg == "--planetest":
			plane_test = true
		elif arg == "--cruisetest":
			cruise_test = true
		elif arg == "--clean":
			clean_pic = true
		elif arg == "--racepic":
			race_pic = true
		elif arg == "--high":
			high_view = true
		elif arg.begins_with("--look="):
			var parts := arg.substr(7).split(",")
			high_view = true
			for i in mini(parts.size(), 3):
				look[i] = float(parts[i])
		elif arg == "--host":
			net_host = true
		elif arg.begins_with("--join="):
			net_join = arg.substr(7)
		elif arg.begins_with("--nettest="):
			net_test = float(arg.substr(10))
		elif arg == "--map":
			test_map = true
		elif arg.begins_with("--speedtest"):
			speed_test = float(arg.get_slice("=", 1)) if arg.contains("=") else 120.0
		elif arg == "--rtest":
			r_test = true
		elif arg == "--resettest":
			reset_test = true
		elif arg == "--keytest":
			key_test = true
		elif arg == "--glb":
			live = false
		elif arg == "--fly":
			want_car = false
		elif arg == "--drivetest":
			drive_test = true
		elif arg == "--enginetest":
			engine_test = true
		elif arg.begins_with("--phonetest="):
			phone_test = arg.substr(12)
		elif arg.begins_with("--maketest="):
			make_test = arg.substr(11)
		elif arg == "--gentest":
			gen_test = true
		elif arg.begins_with("--eventtest="):
			event_test = arg.substr(12)
		elif arg.begins_with("--event="):
			event_name = arg.substr(8)
		elif arg.begins_with("--tunneltest="):
			tunnel_test = arg.substr(13)
		elif arg == "--golftest":
			golf_test = true
		elif arg.begins_with("--flytest"):
			fly_test = true
			DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)	# (to measure the frame rate)
			if arg.contains("="):
				fly_seconds = float(arg.get_slice("=", 1).get_slice(",", 0))
				fly_speed = float(arg.get_slice(",", 1)) if arg.contains(",") else 40.0
			want_car = false
		elif arg == "--wheeltest":
			wheel_test = true
		elif arg == "--side" or arg.begins_with("--side="):
			side_view = true
			if arg.begins_with("--side="):
				var parts := arg.substr(7).split(",")
				side_distance = float(parts[0])
				if parts.size() > 1:
					side_angle = float(parts[1])
		elif arg.begins_with("--vegetation="):
			vegetation_override = int(arg.substr(13))
		elif arg.begins_with("--textures="):
			textures_override = int(arg.substr(11))
		elif arg.begins_with("--lighting="):
			lighting_override = int(arg.substr(11))
		elif arg == "--verbose":
			verbose = true

	testing = crash_test or world_map_test or fetch_test.size() == 2 or mp_test != "" or share_test != "" or car_hit_test or hunt_test or zombie_test or roadkill_test or booth_test or workshop_test or editor_test or prop_test or game_maker_test or rules_test or race_test or fence_test or arrive_test or drive_test or engine_test or gpu_profile or build_test or switch_test >= 0.0 or walk_test or plane_test or cruise_test or fly_test or wheel_test or shot_path != "" or speed_test > 0.0 or key_test or reset_test or r_test or menu_test != "" or phone_test != "" or make_test != "" or gen_test or golf_test or event_test != "" or tunnel_test != "" or robot_mode != "" or net_join != "" or net_test > 0.0 \
		or f_test or test_teleport.size() == 2 or test_map or net_host or side_view or high_view or race_pic or traffic_override >= 0
	if testing:
		# (a test's workshop: its own, begun empty; the one in the Saved folder left alone)
		Packs.testing = true
		if net_join != "":
			Packs.test_name = "workshop_test_join"
		var shop := Packs.workshop()
		if DirAccess.dir_exists_absolute(shop):
			for kind in DirAccess.get_directories_at(shop):
				for file in DirAccess.get_files_at(shop.path_join(kind)):
					DirAccess.remove_absolute(shop.path_join(kind).path_join(file))
	UnrealSettings.in_memory_only = testing	# (a test's key presses and menu clicks don't change the player's settings)
	settings.load_all()
	if not testing and UnrealSettings.get_value(GameSettings.GODOT, "Preset") == "":
		# the first start: the preset for this PC's graphics card (as the Unreal game's first-start setup)
		settings.detect()
		settings.save_graphics()
		print("Geodrive: first start, chose ", settings.detected)
	_make_sky()
	camera = CarCamera.new()
	add_child(camera)
	hud = Hud.new()
	add_child(hud)
	menu = GameMenu.new()
	menu.settings = settings
	menu.game = self
	add_child(menu)
	phone = PHONE.new()
	phone.main = self
	add_child(phone)
	menu.drive_requested.connect(_drive_to)
	REGION_FETCH.finish_deletes()	# (places deleted while the game had them open)
	region_fetch = REGION_FETCH.new()
	region_fetch.main = self
	add_child(region_fetch)
	region_fetch.ready_to_go.connect(_drive_to)
	region_fetch.ready_to_join.connect(func(lat: float, lon: float, place_name: String): _start_area(lat, lon, place_name, true))
	menu.save_spot_requested.connect(_save_spot)
	menu.car_chosen.connect(_choose_car)
	menu.fly_requested.connect(func(k: int, airport: Dictionary): if aviation: aviation.request(k, airport))
	menu.airport_requested.connect(_go_to_airport)
	menu.settings_changed.connect(_apply_settings)
	menu.closed.connect(_menu_closed)
	loading = LoadingScreen.new()
	add_child(loading)
	# the M map's drawing over the view from above: the main roads, street names, the car (see _draw_map_overlay)
	map_overlay = Control.new()
	map_overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
	map_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	map_overlay.visible = false
	map_overlay.draw.connect(_draw_map_overlay)
	hud.add_child(map_overlay)
	world_map = WorldMap.new()
	add_child(world_map)
	net = GameNet.new()
	net.game = self
	net.player_name = UnrealSettings.get_value("Geodrive", "PlayerName", "Driver")
	add_child(net)
	net.status_changed.connect(func(text: String): if text != "": hud.set_status(text, 10.0))
	menu.world_map_requested.connect(open_world_map)
	world_map.travel_requested.connect(_travel_from_map)
	world_map.download_requested.connect(func(lat: float, lon: float, place_name: String): region_fetch.fetch(lat, lon, place_name))
	world_map.delete_requested.connect(func(stem: String): region_fetch.delete_place(stem))
	world_map.closed.connect(_world_map_closed)
	if live:
		menu.areas = ClassDB.class_call_static("GeodriveWorld", "areas")
		menu.spots = UnrealSettings.spots()
	_apply_settings(not testing)
	hud.set_shown(settings.hud_shown)
	hud.set_debug(settings.debug_info)

	want_car = want_car and live and ClassDB.class_exists("GeodriveCar")
	if not testing:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	if live:
		# the world builds round the start on a worker; the car goes in when it's ready (see _update_waiting)
		world = ClassDB.instantiate("GeodriveWorld")
		add_child(world)
		world.cache_dir = GameSettings.cache_dir()
		world.origin_shifted.connect(_on_origin_shifted)
		world.robot_place_car.connect(_robot_place_car)
		world.robot_screenshot.connect(_robot_screenshot)
		if km > 0.0:
			world.piece_km = km
		elif settings.world_size > 0:
			world.piece_km = settings.world_size
		world.detail_distance = settings.detail_metres()
		world.view_distance = settings.view_metres()
		world.follow_land = settings.follow_land
		world.kerbs = settings.kerbs
		world.scan_roads = settings.scan_roads or scan_override
		world.building_distance = settings.building_metres()
		world.verbose = verbose or testing
		# (the roads painted into the ground far off: the ground shader reads these, so they're there before it's made)
		for global in [["road_map", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null], ["road_origin", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO],
				["road_size", RenderingServer.GLOBAL_VAR_TYPE_FLOAT, 51200.0], ["world_offset", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO],
				["road_from", RenderingServer.GLOBAL_VAR_TYPE_FLOAT, 1800.0]]:
			RenderingServer.global_shader_parameter_add(global[0], global[1], global[2])
		world.set_ground_material(GroundLook.make())	# (the textured ground: ground.gd)
		if world.has_method("set_road_materials"):	# (an older library: plain roads)
			var roads := RoadLook.make()	# (the textured road surfaces: roads.gd)
			world.set_road_materials(roads["asphalt"], roads["paving"], roads["gravel"])
		if world.has_method("set_building_material"):
			world.set_building_material(BuildingLook.make())	# (walls, windows, roofs: buildings.gd)
		if world.has_method("make_plant_mesh"):
			vegetation = VegetationLook.new()	# (trees, bushes: vegetation.gd)
			vegetation.world = world
			vegetation.level = vegetation_override if vegetation_override >= 0 else settings.vegetation	# (the settings were applied before it was here)
			vegetation.shadow_reach = sun.directional_shadow_max_distance
			add_child(vegetation)
		if world.has_method("make_aircraft_mesh"):
			aviation = AVIATION.new()
			aviation.name = "Aviation"
			aviation.main = self
			aviation.world = world
			add_child(aviation)
		hunt = HUNT.new()
		hunt.name = "Hunt"
		hunt.main = self
		hunt.world = world
		add_child(hunt)
		if ClassDB.class_exists("GeodriveStructure"):
			builder = BUILDER.new()
			builder.name = "Builder"
			builder.main = self
			builder.world = world
			add_child(builder)
		game_gen = GAME_GEN.new()
		game_gen.name = "GameGen"
		game_gen.main = self
		game_gen.enabled = not testing or gen_test or golf_test or phone_test != "" or event_test != ""	# (the other tests: only their own games)
		add_child(game_gen)
		golf = GOLF.new()
		golf.name = "GolfRound"
		golf.main = self
		add_child(golf)
		if net_join != "":
			net.join(net_join)	# (the host's area comes by itself)
			return
		# the front menu (as the Unreal game opens), unless a place or a test was asked for
		if want_car:
			_make_wheel()
			menu.wheel = wheel
		if front_menu or not (place_given or testing or not want_car):
			menu.open("main")
			if menu_test != "":
				_run_menu_test()
			return
		_start_area(lat, lon, "the start")
		if phone_test != "":
			_run_phone_test()
		if make_test != "":
			_run_make_test()
		if gen_test:
			_run_gen_test()
		if golf_test:
			_run_golf_test()
		if event_test != "":
			_run_event_test()
		if tunnel_test != "":
			_run_tunnel_test()
		return
	if not _load_glb():
		return
	var spawn: Vector3 = info["spawn"]
	var heading: Vector3 = (info["heading"] as Vector3).normalized()
	camera.position = spawn - heading * 14.0 + Vector3.UP * 4.0
	camera.rotation = Vector3(-0.15, atan2(-heading.x, -heading.z), 0.0)
	camera.start_free()


# A new area round a place: the world builds its first piece on a worker (the game keeps running); the car waits
# (in a friend's game only the host's area: the host chooses, as in the Unreal game)
func _start_area(lat: float, lon: float, place_name: String, from_host: bool = false) -> void:
	if net.is_client() and not from_host:
		hud.set_status("In a friend's game the host chooses the area: leave the game (Esc > Multiplayer) to go elsewhere", 5.0)
		return
	# (no map of it here yet: a game with no places, or a friend's game somewhere we haven't got: offered first)
	if world and not world.has_map_at(lat, lon):
		region_fetch.offer(lat, lon, place_name, from_host)
		return
	_leave_free_camera()
	if car and world.is_ready() and not going_back:
		came_from = world.to_lat_lon(car.global_position)	# (if the new area can't be built, the car goes back there)
	if km <= 0.0:
		world.piece_km = settings.world_size	# (the World pieces setting, for each new area)
	world.start_area(lat, lon)
	if lighting:
		lighting.set_place(lat)	# (the sun as it is at this latitude)
	if aviation:
		aviation.reset_area()
	if builder:
		builder.reset_area()
	if hunt:
		hunt.reset_area()
	BuildingLook.set_place(ClassDB.class_call_static("GeodriveWorld", "is_uk", lat, lon))
	if vegetation:
		vegetation.set_place(lat)
	loading.set_target(lat, lon)
	net.set_area(lat, lon, place_name)	# (a host's friends follow it there)
	trip_km = 0.0
	waiting = "start" if car == null else "area"
	hold_lat = lat
	hold_lon = lon
	hold_name = place_name
	hold_have_target = false
	if car:
		hold_facing = -car.global_transform.basis.z
		car.freeze = true
	hud.set_status("Loading %s..." % place_name, 600.0)
	arrive_on_foot = not testing or arrive_test
	title = "Open-Planet: %.4f, %.4f" % [lat, lon]


# While a new area loads or a far teleport streams in: then the car goes to the start, or to the road nearest the place
func _update_waiting() -> void:
	var error: String = world.get_error()
	if error != "":
		hud.set_status("Couldn't build here: " + error, 10.0)
		if car and waiting == "area" and came_from.size() == 2:
			# driving: the old area was let go for the new one, so it's built again (from the cache: quick) and the car
			# put back on its road there (once: if that fails too, the areas menu)
			print("Geodrive: couldn't build the new area (", error, "): back where the car was")
			var back := came_from
			came_from = PackedFloat64Array()
			going_back = true
			_start_area(back[0], back[1], "where you were")
			going_back = false
			hud.set_status("Couldn't build there (%s): back where you were" % error, 10.0)
			return
		# the loading screen says why for a few seconds; then the areas menu (a car stays held still till an area is in)
		error_until = Time.get_ticks_msec() / 1000.0 + 5.0
		waiting = ""
		if car:
			car.freeze = true
		return
	if not world.is_ready():
		# the loading screen's up: the cars' looks made meanwhile, one kind a frame (each ~40 ms the first time: once
		# loaded, a jolt as the car or the first traffic car of its kind came in)
		var first: int = car_override if car_override > 0 else (4 if drive_test else settings.car_body)
		for body in [first, 1, 2, 3, 4]:
			if not _prepared.has(body):
				_prepared.append(body)
				GeodriveCar.prepare(body)
				break
		return
	var now := Time.get_ticks_msec() / 1000.0
	if waiting == "settle":
		# put on a road whose ground isn't solid yet (R, F, a short teleport): held still until it is
		world.set_focus(hold_check, Vector3.ZERO)
		if world.is_solid_at(hold_check) or now > hold_until:
			waiting = ""
			car.freeze = false
			car.reset_car()
		return
	if waiting == "start":
		var spawn: Dictionary = world.get_spawn()
		var at: Vector3 = spawn["position"]
		var heading: Vector3 = (spawn["heading"] as Vector3).normalized()
		camera.position = at - heading * 14.0 + Vector3.UP * 4.0
		camera.rotation = Vector3(-0.15, atan2(-heading.x, -heading.z), 0.0)
		title += " (start: %s)" % spawn["road"]
		if want_car:
			_make_car(Transform3D(Basis.looking_at(heading, Vector3.UP), at + Vector3.UP * 0.15))
		else:
			camera.start_free()
		waiting = ""
		hud.set_status("", 0.1)
		if car and not world.is_solid_at(at):
			# ready by the time limit, the ground under the start not solid yet: held still till it is
			car.freeze = true
			waiting = "settle"
			hold_check = at
			hold_until = now + 15.0
			hud.set_status("Waiting for the ground...", 4.0)
		return
	# a place: the ground and roads there stream in first, then the car goes on the road nearest it
	if not hold_have_target:
		hold_target = world.from_lat_lon(hold_lat, hold_lon)
		var road: Dictionary = world.nearest_road(hold_target, 3000.0)
		hold_check = road["position"] if road.get("found", false) else hold_target
		hold_have_target = true
		hold_until = now + 20.0
	world.set_focus(hold_check, Vector3.ZERO)
	if world.is_solid_at(hold_check) or now > hold_until:
		waiting = ""
		if car:
			car.freeze = false
			place_car(hold_target, hold_facing, "At " + hold_name)
		else:
			camera.position = hold_check + Vector3.UP * 30.0
			hud.set_status("At " + hold_name, 3.0)


func _load_glb() -> bool:
	var base := ProjectSettings.globalize_path("res://areas/" + area_name)
	var text := FileAccess.get_file_as_string(base + ".json")
	if text.is_empty():
		push_error("No area file: run geotile godot to make areas/%s.glb and .json" % area_name)
		return false
	var json: Dictionary = JSON.parse_string(text)
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(base + ".glb", state) != OK:
		push_error("Could not read " + base + ".glb")
		return false
	var area := document.generate_scene(state)
	add_child(area)
	_use_vertex_colours(area)
	info = { "spawn": Vector3(json["spawn"][0], json["spawn"][1], json["spawn"][2]),
		"heading": Vector3(json["heading"][0], 0.0, json["heading"][2]), "spawn_road": json["spawn_road"] }
	title = "Open-Planet (Core) on Godot %s: %s, %d roads (start: %s)" % [Engine.get_version_info()["string"], area_name.capitalize(), json["roads"], json["spawn_road"]]
	return true


# The car (the chosen body: Core's car, its wheels and physics as in the Unreal game) on a rigid body
func _make_car(at: Transform3D) -> void:
	car = ClassDB.instantiate("GeodriveCar")
	car.body = car_override if car_override > 0 else (4 if drive_test else settings.car_body)	# (the drive test in the bakkie unless --car says, so its numbers compare run to run)
	car.abs_level = settings.abs_level
	car.tc_level = settings.tc_level
	car.set_steering_ratio(settings.steering_ratio)
	car.transform = at
	add_child(car)
	car.contact_monitor = true	# (what it touches, for the knock log)
	car.max_contacts_reported = 6
	last_position = at.origin
	camera.car = car
	_load_seat()
	var saved_view := int(UnrealSettings.get_value("Geodrive", "CameraView", "0"))
	camera.set_view(clampi(start_view if start_view >= 0 else saved_view, 0, 3))

	_make_wheel()
	if wheel:
		wheel.set_car(car)
	# its sound (Core's synthesiser, as in the Unreal game); quiet in the automatic tests
	if car_sound == null and ClassDB.class_exists("GeodriveCarSound") and (not testing or menu_test != ""):
		car_sound = ClassDB.instantiate("GeodriveCarSound")
		add_child(car_sound)
		_apply_sound()
	if car_sound:
		car_sound.set_car(car)


# A steering wheel, if there is one (Core's wheel and force feedback, as in the Unreal game): it drives the car; made
# at the start, so its pedals and buttons can be set from the front menu too
func _make_wheel() -> void:
	# (never in a test run but the wheel's own: force feedback takes the wheel for itself, and a test started while the
	# game is being played had taken it from the game)
	if wheel == null and ClassDB.class_exists("GeodriveWheel") and (not testing or wheel_test):
		wheel = ClassDB.instantiate("GeodriveWheel")
		add_child(wheel)
		if wheel.load_settings(UnrealSettings.path()):
			print("Geodrive: wheel settings from the Unreal game (", UnrealSettings.path(), ")")
		wheel.force_enabled = wheel.force_enabled and not wheel_test


# The ground's colour is per corner (grass, forest, sand, rock on steep slopes): its material takes it as the albedo
func _use_vertex_colours(node: Node) -> void:
	if node is MeshInstance3D:
		var mesh: Mesh = node.mesh
		for surface in mesh.get_surface_count():
			var material := mesh.surface_get_material(surface) as BaseMaterial3D
			if material and material.resource_name == "Ground":
				material.vertex_color_use_as_albedo = true
	for child in node.get_children():
		_use_vertex_colours(child)


func _make_sky() -> void:
	lighting = GameLighting.new()	# (the sun, the sky, the haze: lighting.gd)
	add_child(lighting)
	environment = lighting.environment
	sun = lighting.sun


# --- the menu and settings ---

func open_menu(first_page: String = "main") -> void:
	if world == null or loading.is_showing():
		return
	_remember_spot()
	menu.has_area = car != null or (world.is_ready() and not want_car)
	menu.area_name = _area_name()
	menu.in_uk = _signs_mph()
	menu.spots = UnrealSettings.spots()
	menu.wheel = wheel
	if car:
		var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
		menu.here_lat = here[0]
		menu.here_lon = here[1]
	menu.open(first_page)


# Where you are, kept for the start screen's Continue (every 15 s, and as the menu opens); not a friend's game's
var _remember_spot_at := 0.0

# The places plants don't grow: the airports' (aviation.gd) and the golf courses' (game_gen.gd), for the pieces built
func update_plant_clearings() -> void:
	if world == null or not world.has_method("set_plant_clearings"):
		return
	var all: Array = []
	if aviation:
		all.append_array(aviation.clearings)
	if game_gen:
		all.append_array(game_gen.clearings)
	world.set_plant_clearings(all)


func _remember_spot() -> void:
	_remember_spot_at = 0.0
	var body: Node3D = walker if walker else car
	if body == null or world == null or waiting != "" or net.is_client():
		return
	var at: PackedFloat64Array = world.to_lat_lon(body.global_position)
	UnrealSettings.save_last_spot(_area_name(), at[0], at[1])


func open_world_map() -> void:
	world_map.has_car = car != null
	if car:
		var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
		world_map.car_lat = here[0]
		world_map.car_lon = here[1]
	world_map.area_boxes = world.get_built_boxes() if world and world.is_ready() else []	# (the built pieces, outlined)
	world_map.booths = hunt.booths() if hunt else []	# (the games' ticket booths)
	world_map.pause_game = car != null and not net.is_online()	# (online the game goes on, as with the menu)
	world_map.open()


# A place picked on the world map: a teleport (or a new area round it) while driving, else the first area
func _travel_from_map(lat: float, lon: float, place_name: String) -> void:
	_stop_flying()	# (flying, the flight ends first: the aircraft had flown on, its ground gone, crashed and gone back to its runway)
	if car:
		teleport(lat, lon, place_name)
	else:
		_drive_to(lat, lon, place_name)


func _world_map_closed() -> void:
	if car == null and waiting == "":
		menu.open("main")
	else:
		_menu_closed()


func _menu_closed() -> void:
	if not testing and (car or camera.free):
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


# The ready-made area the car is in (or the area was started in), else where it is
func _area_name() -> String:
	var at: PackedFloat64Array = world.to_lat_lon(car.global_position) if car else world.get_start()
	var found := "%.4f, %.4f" % [at[0], at[1]]
	var smallest := INF
	for area in menu.areas:
		var extent: float = (area["north"] - area["south"]) * (area["east"] - area["west"])
		if at[0] >= area["south"] and at[0] <= area["north"] and at[1] >= area["west"] and at[1] <= area["east"] and extent < smallest:
			smallest = extent
			found = area["name"]
	return found


# Whether the country being driven signs its limits in mph (its road rules: the UK, the US...)
func _signs_mph() -> bool:
	return bool(world.get_country().get("mph", false)) if world else _in_uk()


func _in_uk() -> bool:
	var at: PackedFloat64Array = world.to_lat_lon(car.global_position) if car else world.get_start()
	return ClassDB.class_call_static("GeodriveWorld", "is_uk", at[0], at[1])	# (the one box the roads' rules use too)


# The menu's Drive (an area, a search): a new area round the place (the car waits for it, or is made once it's in);
# with a car already in a built area that has the place, just a teleport there (as the Unreal game's geo.Go)
func _drive_to(lat: float, lon: float, place_name: String) -> void:
	if world == null or waiting != "":
		return
	# (no map of it yet: offered, made in the background, then here again)
	if not world.has_map_at(lat, lon):
		region_fetch.offer(lat, lon, place_name)
		return
	_stop_flying()
	if car and world.is_ready() and world.contains_lat_lon(lat, lon):
		teleport(lat, lon, place_name)
		return
	_start_area(lat, lon, place_name)


# A car chosen in the menu: saved, and if one is out, a new car of that kind in its place on the road
func _choose_car(body: int) -> void:
	_leave_free_camera()
	settings.car_body = body
	settings.save_quality("CarBody", body)
	# flying: the flight over, the new car on the road nearest where the aircraft was (the parked car's place may be
	# far behind, its ground let go)
	var from_flight: Variant = _stop_flying()
	if from_flight != null and car:
		var old := car
		var at := old.global_transform
		remove_child(old)
		old.queue_free()
		car = null
		_make_car(at)
		if wheel:
			wheel.set_car(car)
		camera.set_view(CarCamera.View.CHASE)
		teleport(from_flight[0], from_flight[1], GameSettings.CAR_NAMES[body])
		return
	if car == null or car.body == body:
		return
	var at := car.global_transform
	var facing := -at.basis.z
	var view := camera.view
	var old := car
	remove_child(old)
	old.queue_free()
	car = null
	_make_car(at)
	camera.set_view(view)	# (the view as it was, not the start's)
	if wheel:
		wheel.set_car(car)
	place_car(at.origin, facing, GameSettings.CAR_NAMES[body])


# Flying (or a flight waiting to start): over, the camera back on the car; where the aircraft was (lat, lon), or null
func _stop_flying() -> Variant:
	if aviation == null or (not aviation.flying and aviation.aircraft == null and aviation.pending.is_empty()):
		return null
	var at: Vector3 = aviation.aircraft.global_position if aviation.aircraft else (car.global_position if car else camera.global_position)
	var lat_lon: PackedFloat64Array = world.to_lat_lon(at)
	aviation.stop()
	if walker:
		_remove_walker()
	if car:
		car.freeze = false
	return lat_lon


# The settings as they are now: the picture (and the window, unless a test runs), the camera, the world, the HUD
func _apply_settings(window_too: bool = true) -> void:
	settings.apply_graphics(get_viewport(), sun, window_too and not testing)
	camera.cockpit_fov = settings.cockpit_fov
	apply_body_height()
	camera.view_far = settings.view_metres()
	# the land goes as far as the view (the far horizon beyond the built pieces), and the haze thickens to match
	if lighting:
		lighting.apply(lighting_override if lighting_override >= 0 else settings.lighting, settings.view_metres())
	var texture_level := textures_override if textures_override >= 0 else settings.textures
	GroundLook.apply(texture_level)
	RoadLook.apply(texture_level)
	BuildingLook.apply(texture_level)
	if vegetation:
		vegetation.set_shadow_reach(sun.directional_shadow_max_distance)
		vegetation.set_level(vegetation_override if vegetation_override >= 0 else settings.vegetation)
	if world:
		world.detail_distance = settings.detail_metres()
		world.view_distance = settings.view_metres()
		world.building_distance = settings.building_metres()
		world.follow_land = settings.follow_land
		world.kerbs = settings.kerbs
		world.scan_roads = settings.scan_roads or scan_override
	if car:
		car.abs_level = settings.abs_level
		car.tc_level = settings.tc_level
		car.set_steering_ratio(settings.steering_ratio)	# (the cockpit's steering wheel turns by it, wheel or keys)
	_apply_sound()
	hud.show_fps = settings.show_fps and not clean_pic
	hud.quality_name = GameSettings.PRESETS[settings.preset] if settings.is_preset() else "Custom"
	hud.show_memory = settings.show_memory
	hud.set_shown(settings.hud_shown)
	hud.set_debug(settings.debug_info)


# Settings > Game: the area built again round the car (with the road style and kerbs as they are now)
func rebuild_area() -> void:
	if world == null or car == null or waiting != "":
		return
	var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
	_start_area(here[0], here[1], _area_name())


# Settings > Game > Road centrelines: the roads' centrelines within 300 m, as the height solver left them (the Unreal
# game's geo.World.Debug), redrawn twice a second
func _update_road_lines(delta: float) -> void:
	if not settings.road_lines or world == null:
		if road_lines_node:
			road_lines_node.visible = false
		return
	road_lines_timer -= delta
	if road_lines_timer > 0.0:
		return
	road_lines_timer = 0.5
	if road_lines_node == null:
		road_lines_node = MeshInstance3D.new()
		road_lines_node.mesh = ImmediateMesh.new()
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.albedo_color = Color(1.0, 0.85, 0.1)
		road_lines_node.material_override = material
		road_lines_node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(road_lines_node)
	road_lines_node.visible = true
	var at := car.global_position if car and not camera.free else camera.global_position
	var points: PackedVector3Array = world.road_lines(at, 300.0)
	var mesh: ImmediateMesh = road_lines_node.mesh
	mesh.clear_surfaces()
	if points.size() >= 2:
		mesh.surface_begin(Mesh.PRIMITIVE_LINES)
		for p in points:
			mesh.surface_add_vertex(p)
		mesh.surface_end()


# Traffic round the car (Core's, as in the Unreal game): the Traffic setting's number of cars, a third parked
func _update_traffic(delta: float) -> void:
	var wanted := traffic_override if traffic_override >= 0 else settings.traffic_cars
	if net.is_online():
		wanted = 0	# (traffic is offline only, as in the Unreal game: each player would see different cars)
	# (round you, on foot too; new cars come in where the camera isn't looking)
	var you: Node3D = walker if walker else car
	var looking := -camera.global_transform.basis.z
	world.update_traffic(delta, you.global_position, looking if Vector2(looking.x, looking.z).length() > 0.1 else -car.global_transform.basis.z,
		car.linear_velocity if walker == null else walker.velocity, wanted)
	if traffic_override >= 0:
		traffic_log -= delta
		if traffic_log <= 0.0:
			traffic_log = 2.0
			var near := 0
			for child in world.get_children():
				if child is RigidBody3D and String(child.name).begins_with("Traffic") and child.global_position.distance_to(you.global_position) < 150.0:
					near += 1
			print("Geodrive traffic: ", world.get_traffic_status(), " (%d within 150 m) |" % near, world.get_traffic_debug())


# Multiplayer: the host's area, as it arrives or changes. In it already (and the host near): just beside the host;
# otherwise built (the loading screen), then beside the host
func join_area(lat: float, lon: float, place_name: String) -> void:
	if world == null:
		return
	menu.close()
	world_map.close()
	if car and world.contains_lat_lon(lat, lon) and car.global_position.distance_to(world.from_lat_lon(lat, lon)) < 3000.0:
		return
	waiting = ""
	_start_area(lat, lon, place_name + "'s area", true)


# Multiplayer: couldn't reach the host, or the host left before its area came: back to the front menu (with a car
# in the host's area already, it stays there, offline)
func join_failed() -> void:
	if car == null and waiting == "":
		menu.open("main")


# The robot test driver (Core's, as the Unreal game's geo.Robot): "n2", "town" or "all" (with Trips town trips), or "stop"
func start_robot(mode: String, trips: int = 10, shard: int = 0, shards: int = 1) -> void:
	if world == null or car == null:
		return
	if net.is_online() and mode != "stop":
		hud.set_status("The robot driver is for offline only (it would drive your car in front of your friends)", 4.0)
		return
	if mode == "stop":
		world.stop_robot()
		car.autopilot = false
		hud.set_status("Robot driver stopped: the report is in Saved/RoadReports", 6.0)
		return
	if world.start_robot(car, mode, trips, shard, shards):
		car.automatic = true
		car.autopilot = true
		hud.set_status(world.get_robot_status(), 4.0)
	else:
		hud.set_status("The robot found no routes to drive here", 4.0)


func _robot_place_car(at: Vector3, facing: Vector3) -> void:
	if car == null:
		return
	var before := car.global_position
	place_car(at, facing, "Robot driver")
	if robot_mode != "":
		print("Geodrive robot: placed from %s to %s (asked %s), freeze %s, waiting %s" % [before, car.global_position, at, car.freeze, waiting])


func _robot_screenshot(problem: int) -> void:
	if DisplayServer.get_name() == "headless":
		return	# (no picture to take)
	var folder := GameSettings.saved_dir().path_join("RoadReports")
	DirAccess.make_dir_recursive_absolute(folder)
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(folder.path_join("robot_%03d.png" % problem))


# The games' ticket booths on the M map: a flag and its name, and whether you've a ticket or are playing; one off the
# map at its edge, pointing the way
func _draw_booths() -> void:
	if hunt == null:
		return
	var size := map_overlay.size
	var middle := size * 0.5
	for booth in hunt.booths():
		var at: Vector3 = world.from_lat_lon(booth["lat"], booth["lon"])
		var spot := camera.unproject_position(at) if not camera.is_position_behind(at) else Vector2(-1.0, -1.0)
		var colour := Color(0.3, 1.0, 0.4) if booth["state"] == "playing" else (Color(1.0, 0.85, 0.2) if booth["state"] == "ticket" else Color(1.0, 0.45, 0.15))
		var state: String = {"playing": "PLAYING", "ticket": "you have a ticket", "tickets": "tickets here (E)"}[booth["state"]]
		var inside := Rect2(Vector2(40, 150), size - Vector2(80, 290)).has_point(spot)
		if not inside:
			# (off the map: at its edge, an arrow its way (the map's north up: east right, south down), how far)
			var flat := Vector2(at.x - camera.global_position.x, at.z - camera.global_position.z)
			var way := flat.normalized() if flat.length() > 0.1 else Vector2.UP
			var half := Vector2(size.x * 0.5 - 70.0, size.y * 0.5 - 150.0)
			var edge := middle + Vector2(0.0, 10.0) + way * minf(half.x / maxf(absf(way.x), 0.01), half.y / maxf(absf(way.y), 0.01))
			map_overlay.draw_colored_polygon(PackedVector2Array([edge + way * 16.0, edge - way * 6.0 + way.orthogonal() * 10.0, edge - way * 6.0 - way.orthogonal() * 10.0]), colour)
			var here: PackedFloat64Array = world.to_lat_lon(camera.global_position)
			var line := "%s, %s (%s)" % [booth["name"], hunt.way_to(here[0], here[1], booth["lat"], booth["lon"]), state]
			var width := map_font.get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT, -1, 14).x
			# (the words inside the map from the arrow, never past its sides)
			var words := edge - way * 30.0 - Vector2(width * (0.5 - way.x * 0.5), -5.0 - maxf(way.y, 0.0) * -10.0)
			words.x = clampf(words.x, 10.0, size.x - width - 10.0)
			map_overlay.draw_string_outline(map_font, words, line, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, 4, Color(0, 0, 0, 0.8))
			map_overlay.draw_string(map_font, words, line, HORIZONTAL_ALIGNMENT_LEFT, -1, 14, colour)
			continue
		map_overlay.draw_line(spot, spot + Vector2(0.0, -26.0), Color(0.1, 0.1, 0.1), 3.0)
		map_overlay.draw_colored_polygon(PackedVector2Array([spot + Vector2(0.0, -26.0), spot + Vector2(18.0, -20.0), spot + Vector2(0.0, -14.0)]), colour)
		map_overlay.draw_string_outline(map_font, spot + Vector2(22.0, -14.0), booth["name"], HORIZONTAL_ALIGNMENT_LEFT, -1, 16, 4, Color(0, 0, 0, 0.8))
		map_overlay.draw_string(map_font, spot + Vector2(22.0, -14.0), booth["name"], HORIZONTAL_ALIGNMENT_LEFT, -1, 16, colour)
		map_overlay.draw_string_outline(map_font, spot + Vector2(22.0, 4.0), state, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, 4, Color(0, 0, 0, 0.8))
		map_overlay.draw_string(map_font, spot + Vector2(22.0, 4.0), state, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, Color.WHITE)


# The M map, as the Unreal game draws it: the area's main roads (trunk orange, the rest white) and street names over
# the view from above, the car as an orange arrow, and how to use it. Scrolling out past 24 km opens the world map.
func _draw_map_overlay() -> void:
	var started := Time.get_ticks_usec()
	world.draw_map(map_overlay.get_canvas_item(), camera, map_font, 13)
	if verbose or testing:
		map_draw_ms = maxf(map_draw_ms, (Time.get_ticks_usec() - started) / 1000.0)
	var size := camera.map_height * 0.02
	var forward := -car.global_transform.basis.z
	forward.y = 0.0
	forward = forward.normalized()
	var right := Vector3(-forward.z, 0.0, forward.x)
	var at := car.global_position
	if not camera.is_position_behind(at):
		var tip := camera.unproject_position(at + forward * size * 1.5)
		var left := camera.unproject_position(at - forward * size - right * size)
		var right_point := camera.unproject_position(at - forward * size + right * size)
		map_overlay.draw_colored_polygon(PackedVector2Array([tip, left, right_point]), Color(1.0, 0.35, 0.0))
	_draw_booths()
	if builder and builder.game_maker.marking:
		builder.game_maker.draw(map_overlay, camera)
	map_overlay.draw_string(map_font, Vector2(20, 110), "MAP  (M to close, mouse wheel to zoom; scroll out for the world map)  height %d m  -  north is up" % roundi(camera.map_height),
		HORIZONTAL_ALIGNMENT_LEFT, -1, 15, Color(0.8, 0.8, 0.8))


func _apply_sound() -> void:
	if car_sound:
		car_sound.volume = settings.sound_levels["geo.Sound.Volume"]
		car_sound.engine_gain = settings.sound_levels["geo.Sound.Engine"]
		car_sound.rasp = settings.sound_levels["geo.Sound.Rasp"]
		car_sound.pipe = settings.sound_levels["geo.Sound.Pipe"]
		car_sound.crackle = settings.sound_levels["geo.Sound.Crackle"]


# The loading screen: up while a new area builds (what it's on, the globe turning to the place), or saying why it couldn't
func _update_loading() -> void:
	var now := Time.get_ticks_msec() / 1000.0
	var error: String = world.get_error()
	if error != "" and now < error_until:
		loading.loading = true
		loading.error = true
		loading.status = "Couldn't build here: " + error
		return
	if error != "" and error_until > 0.0:
		error_until = 0.0
		loading.loading = false
		menu.has_area = false	# (with a car too: its area is gone, it waits for the next)
		menu.open("go")
		return
	loading.error = false
	loading.loading = waiting in ["start", "area"]
	if loading.loading:
		var status: String = world.get_loading_status()
		if status != loading.status:
			print("Geodrive: loading: ", status)
		loading.status = status


# I: the key, the engine off or on (off: no power, quiet once it's stopped turning; on: started, to idle)
func _toggle_engine() -> void:
	car.engine_on = not car.engine_on
	hud.set_status("Engine on" if car.engine_on else "Engine off (I to start it)", 2.0)


# L / the gamepad's X: the diff lock, on the cars that have one (the bakkie)
func _toggle_diff_lock() -> void:
	if not car.has_diff_lock():
		hud.set_status("No diff lock on this car", 1.5)
		return
	car.diff_lock = not car.diff_lock
	hud.set_status("Diff lock " + ("on" if car.diff_lock else "off"), 1.5)


# --- the driver's seat (cockpit views), per car, as the Unreal game keeps it: [Geodrive] Seat<car>=X=<cm forward> Y=<cm up> ---

func _load_seat() -> void:
	var saved := UnrealSettings.get_value("Geodrive", "Seat%d" % settings.car_body, "")
	var x := 0.0
	var y := 0.0
	for part in saved.split(" ", false):
		if part.begins_with("X="):
			x = float(part.substr(2))
		elif part.begins_with("Y="):
			y = float(part.substr(2))
	set_seat(Vector2(x, y), false)


# The seat offset in cm (forward, up): within 40 cm forward or back, 25 up or down
func set_seat(cm: Vector2, save: bool = true) -> void:
	cm = Vector2(clampf(cm.x, -40.0, 40.0), clampf(cm.y, -25.0, 25.0))
	camera.seat = cm / 100.0
	if save:
		UnrealSettings.set_value("Geodrive", "Seat%d" % settings.car_body, "X=%.3f Y=%.3f" % [cm.x, cm.y])


func get_seat() -> Vector2:
	return camera.seat * 100.0


# The steering wheel's own buttons (set in Settings > Controls): looking round, moving the seat (15 cm a second, saved
# when you let go), changing the view
func _wheel_controls(delta: float) -> void:
	# (the wheel is for driving: on foot or flying, its buttons and look controls do nothing)
	if wheel == null or car == null or not wheel.is_wheel_connected() or walker or (aviation and aviation.flying):
		camera.wheel_look = Vector2.ZERO
		camera.wheel_look_back = false
		return
	var held := func(control: String) -> float: return 1.0 if wheel.is_control_down(control) else 0.0
	camera.wheel_look = Vector2(held.call("LookRight") - held.call("LookLeft"), held.call("LookUp") - held.call("LookDown"))
	camera.wheel_look_back = held.call("LookBack") > 0.0
	var nudge := Vector2(held.call("SeatForward") - held.call("SeatBack"), held.call("SeatUp") - held.call("SeatDown"))
	if nudge != Vector2.ZERO:
		set_seat(get_seat() + nudge * 15.0 * delta, false)
		seat_moving = true
	elif seat_moving:
		seat_moving = false
		set_seat(get_seat())
	var change_view: bool = held.call("ChangeView") > 0.0
	if change_view and not change_view_was_down:
		_key(KEY_C)
	change_view_was_down = change_view


# --- keys ---

func _unhandled_input(event: InputEvent) -> void:
	if menu.is_open() or phone.is_open() or loading.is_showing() or world_map.is_open() or (builder and builder.menu_open()) or (aviation and aviation.chooser_open()) or (hunt and hunt.chooser_open()):
		return
	if event is InputEventMouseMotion:
		camera.mouse_moved(event.relative)
	elif event is InputEventMouseButton and event.pressed and camera.map:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			camera.zoom_map(-1)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			if camera.map_height >= 24000.0 and world:
				# scrolled out as far as the area goes: on to the world map (every town you have), as in the Unreal game
				camera.toggle_map()
				open_world_map()
			else:
				camera.zoom_map(1)
	elif event is InputEventKey and event.pressed and not event.echo:
		_key(event.keycode)
	elif event is InputEventJoypadButton and event.pressed and walker and event.button_index == JOY_BUTTON_BACK and event.device == WALKER.gamepad():
		phone.open()	# (on foot, a gamepad's Back: the phone; driving, it puts the car upright)
	elif event is InputEventJoypadButton and event.pressed and aviation and aviation.flying:
		if event.device == WALKER.gamepad():
			aviation.pad(event.button_index)	# (flying: a gamepad's buttons; never the wheel's)
	elif event is InputEventJoypadButton and event.pressed and car and walker == null and (event.device == WALKER.gamepad() or (wheel and wheel.is_wheel_connected())):
		# (driving: a gamepad's buttons, or the wheel's; on foot, only the gamepad's Back above)
		match event.button_index:
			JOY_BUTTON_BACK:
				reset_in_place()
			JOY_BUTTON_RIGHT_SHOULDER:
				car.shift_up()
			JOY_BUTTON_LEFT_SHOULDER:
				car.shift_down()
			JOY_BUTTON_X:
				_toggle_diff_lock()
			JOY_BUTTON_START:
				open_menu()
			JOY_BUTTON_Y:
				if event.device == WALKER.gamepad():	# (a gamepad's Y, not a wheel's button)
					toggle_walk()


# The keys that do something now, for the HUD's bottom line: so a newcomer finds building, the phone and the map
func _key_hints() -> String:
	if not world.is_ready() or loading.is_showing() or ((high_view or race_pic or clean_pic) and shot_path != ""):
		return ""	# (not in a picture of the view)
	if camera.map:
		return "Map data (c) OpenStreetMap contributors    M close"	# (a map: its credit on it, as the licence asks)
	if camera.free:
		return "F back    Esc menu"
	if aviation and aviation.flying:
		return "M map    F free camera    Esc menu"
	if walker:
		return "B build    E use    Tab phone    M map    Enter get in    V park car here    Esc menu"
	return "Enter get out    E / Q gears    C view    Tab phone    M map    Esc menu"


func _key(key: Key) -> void:
	match key:
		KEY_ESCAPE:
			open_menu()
		KEY_TAB:
			if car and waiting == "" and not loading.is_showing():
				phone.open()
		KEY_H:
			settings.hud_shown = not settings.hud_shown	# (saved, as the setting is)
			settings.save_godot("bHUD", settings.hud_shown)
			hud.set_shown(settings.hud_shown)
		KEY_F1:
			if golf and golf.active:
				golf.stop()
			elif hunt:
				hunt.toggle_game()	# (the game here on or off: tickets offered here, or the one you're playing stopped)
		KEY_F12:
			_screenshot()
		KEY_F3:
			settings.debug_info = not settings.debug_info
			settings.save_godot("bDebugInfo", settings.debug_info)
			hud.set_debug(settings.debug_info)
		KEY_ENTER, KEY_KP_ENTER:
			toggle_walk()
		KEY_F:
			if aviation and aviation.flying:
				# flying: the free camera flies off and comes back to the aircraft (it flies on meanwhile)
				if camera.free:
					camera.stop_free()
				else:
					camera.start_free()
			elif walker:
				# on foot: the free camera flies off and comes back to them (they wait where they are)
				if camera.free:
					camera.stop_free()
					walker.set_physics_process(true)
				else:
					camera.start_free()
					walker.set_physics_process(false)
			elif car == null:
				pass	# (no car to drop: the free camera stays)
			elif waiting != "":
				hud.set_status("Wait a moment: the car is being put down", 2.0)
			elif camera.free:
				# back to the car, where it waited (not onto a road: it may be off one on purpose, out on a farm's
				# tracks; going somewhere far is the menu's Go somewhere)
				_leave_free_camera()
			else:
				camera.start_free()
				Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
				if car:
					# the car waits where it is, held still, as in the Unreal game: flying off, the ground under it streams
					# away, and it would drop through and be put back on its road, pulling the world back with it
					car.freeze = true
	if camera.free or car == null:
		return
	if aviation and aviation.flying:
		if key == KEY_M:
			camera.toggle_map()
		else:
			aviation.key(key)
		return
	if walker:
		if key == KEY_M:
			camera.toggle_map()
		elif key == KEY_B and builder:
			builder.toggle()
		elif key == KEY_E and hunt and not hunt.kiosk_near(walker.global_position).is_empty():
			hunt.open_tickets(hunt.kiosk_near(walker.global_position))
		elif key == KEY_E and aviation and not aviation.stand_near(walker.global_position).is_empty():
			aviation.open_chooser(aviation.stand_near(walker.global_position)["airport"])
		elif key == KEY_E and builder:
			builder.use()
		elif key == KEY_V:
			park_car(car.body)
		elif key == KEY_I and walker.global_position.distance_to(car.global_position) < 6.0:
			_toggle_engine()	# (reaching in through the window)
		elif key == KEY_C and not camera.map:
			camera.walker_behind = not camera.walker_behind
			hud.set_status("Behind you" if camera.walker_behind else "Your eyes", 1.5)
		return	# (the car's keys wait for its driver)
	match key:
		KEY_C:
			if camera.map:
				return	# (the map is the view: C after M)
			camera.cycle_view()
			UnrealSettings.set_value("Geodrive", "CameraView", camera.view)
			hud.set_status(camera.view_name() + " view", 1.5)
		KEY_M:
			camera.toggle_map()
		KEY_BACKSPACE:
			reset_in_place()
		KEY_L:
			_toggle_diff_lock()
		KEY_I:
			_toggle_engine()
		KEY_E:
			car.shift_up()
		KEY_Q:
			car.shift_down()
		KEY_G:
			car.automatic = true
			hud.set_status("Automatic gears", 1.5)
		KEY_B:
			report_spot()


# --- on foot ---

# Enter: out of the car (stopped) on the driver's side, or, within 4 m of it, back in
func toggle_walk() -> void:
	if car == null or camera.free or waiting != "":
		return
	if aviation and aviation.flying:
		# out of the aircraft (stopped on the ground), beside its door
		if not aviation.can_leave():
			hud.set_status("Stop on the ground to get out", 2.0)
			return
		aviation.leave()
		var at: Vector3 = aviation.door()
		var ray := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 5.0, at + Vector3.DOWN * 10.0)
		var hit := get_world_3d().direct_space_state.intersect_ray(ray)
		_spawn_walker(hit["position"] + Vector3.UP * 0.05 if hit else at, -aviation.aircraft.global_transform.basis.z)
		if car.global_position.distance_to(at) > 1000.0:
			# (the car was left where the flight began: it's brought to the road nearest the aircraft)
			place_car(at, -aviation.aircraft.global_transform.basis.z, "")
			car.freeze = true
			hud.set_status("On foot: your car's on the road nearest (%d m); Enter at the door to get back in the plane" % roundi(at.distance_to(car.global_position)), 5.0)
		else:
			hud.set_status("On foot: Enter at the door to get back in", 3.0)
		return
	if walker and aviation and aviation.near_door(walker.global_position):
		_remove_walker()
		aviation.board()
		hud.set_status("In the " + aviation.KINDS[aviation.kind].to_lower(), 1.5)
		return
	if walker:
		var gap: float = walker.global_position.distance_to(car.global_position)
		if gap > 4.0:
			hud.set_status("Walk back to the car to get in (%d m)" % roundi(gap), 2.0)
			return
		_stop_walking()
		hud.set_status("In the car", 1.5)
		return
	if absf(car.get_speed_kmh()) > 5.0:
		hud.set_status("Stop the car to get out", 2.0)
		return
	if camera.map:
		camera.toggle_map()
	# beside the driver's door (the right: the traffic keeps left), standing on whatever is under it there
	var body := car.global_transform
	var forward := -body.basis.z
	var at := body.origin + body.basis.x.normalized() * 1.7 + forward * 0.3
	var ray := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 3.0, at + Vector3.DOWN * 5.0, 0xFFFFFFFF, [car.get_rid()])
	var hit := get_world_3d().direct_space_state.intersect_ray(ray)
	if hit:
		at = hit["position"] + Vector3.UP * 0.05
	_spawn_walker(at, forward)
	hud.set_status("On foot: Enter to get back in the car", 3.0)


# An airport from Go somewhere: to its plane stand (on foot, the car parked on the road nearest)
func _go_to_airport(airport: Dictionary) -> void:
	var spot: PackedFloat64Array = aviation.stand_lat_lon(airport)
	if spot.is_empty():
		return
	_stop_flying()
	arrive_stand = airport
	_drive_to(spot[0], spot[1], aviation.label(airport))


# Arrived somewhere (a teleport, a new area, the game's start): on foot on the kerb side of the car (the left: the
# traffic keeps left), the car parked; Enter gets in. (A walker left where you were goes.) At an airport: at its stand.
func _arrive() -> void:
	if car == null or (aviation and aviation.flying):
		return
	_remove_walker()
	if not arrive_stand.is_empty():
		var airport := arrive_stand
		arrive_stand = {}
		var spot: PackedFloat64Array = aviation.stand_lat_lon(airport)
		aviation._draw_airport(airport)
		var stand: Vector3 = world.from_lat_lon(spot[0], spot[1])
		var down := get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(stand + Vector3.UP * 30.0, stand + Vector3.DOWN * 60.0))
		_spawn_walker((down["position"] if down else stand) + Vector3.UP * 0.05 + Vector3(3.0, 0.0, 0.0), -car.global_transform.basis.z)
		hud.set_status("%s: E at the stand to choose a plane (your car's on the road nearest, %d m)" % [aviation.label(airport), roundi(walker.global_position.distance_to(car.global_position))], 6.0)
		return
	if camera.map:
		camera.toggle_map()
	var body := car.global_transform
	var forward := -body.basis.z
	var at := body.origin - body.basis.x.normalized() * 1.9 + forward * 0.3
	var ray := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 3.0, at + Vector3.DOWN * 5.0, 0xFFFFFFFF, [car.get_rid()])
	var hit := get_world_3d().direct_space_state.intersect_ray(ray)
	if hit:
		at = hit["position"] + Vector3.UP * 0.05
	_spawn_walker(at, forward)
	hud.set_status("%s: on foot by your car (Enter to get in)" % hold_name if hold_name != "" else "On foot by your car (Enter to get in)", 4.0)


func _spawn_walker(at: Vector3, forward: Vector3) -> void:
	walker = WALKER.new(settings.body_height)
	walker.name = "Walker"
	walker.world = world
	add_child(walker)
	walker.global_position = at
	walker.rotation.y = atan2(-forward.x, -forward.z)
	car.freeze = true	# (the car waits, held still)
	camera.walker = walker
	camera.walker_behind = camera.walker_behind or camera_behind
	if not testing:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


# The walker gone (into the car or an aircraft)
func _remove_walker() -> void:
	if walker == null:
		return
	walker.queue_free()
	walker = null
	camera.walker = null


func _stop_walking() -> void:
	if walker == null:
		return
	_remove_walker()
	if camera.free:
		camera.stop_free()
	if car:
		car.freeze = false
		camera.set_view(camera.view)


# The player's height (settings): their eyes in the cockpit, as much higher or lower than the car's driver (1.75 m)
# as their seated eye height is; on foot, the walker's measures (taken when they next get out)
func apply_body_height() -> void:
	var eye: float = ClassDB.class_call_static("GeodriveWorld", "body_measures", float(settings.body_height))["eye_seated"]
	var usual: float = ClassDB.class_call_static("GeodriveWorld", "body_measures", 175.0)["eye_seated"]
	camera.body_eye = eye - usual


# --gpuprofile: once settled, 3 s with everything, then 3 s with each kind of thing hidden (its render layers off the
# camera, or the sun's shadows off), the graphics card's time averaged over each's last 2 s; what each costs printed
const PROFILE_PARTS := [["everything", 0], ["trees and bushes", 1 << 17], ["forest canopy", 1 << 18], ["city buildings", 1 << 13],
	["roads", (1 << 11) | (1 << 12)], ["road lines", 1 << 12], ["near ground", 1 << 10], ["distant ground", 1 << 14], ["far horizon", 1 << 15],
	["water", 1 << 16], ["shadows", -1], ["all the world", (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16) | (1 << 17) | (1 << 18)],
	["world node hidden", -2], ["ambient occlusion", -3], ["glow", -4], ["MSAA", -5], ["fog", -6], ["sky", -7], ["the car", -8], ["on-screen text", -9]]
var _profile_frame := 0
var _profile_sum := 0.0
var _profile_count := 0
var _profile_base := 0.0
func _tally(node: Node, tally: Dictionary, under: String) -> void:
	var here := under if under != "" else ""
	for child in node.get_children():
		var name_here: String = here if here != "" else String(child.name)
		if child is MeshInstance3D and child.mesh and child.is_visible_in_tree():
			var count := 0
			for s in child.mesh.get_surface_count():
				var arrays: Array = child.mesh.surface_get_arrays(s)
				var index: PackedInt32Array = arrays[Mesh.ARRAY_INDEX] if arrays[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
				count += index.size() / 3 if index.size() > 0 else (arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array).size() / 3
			tally[name_here] = tally.get(name_here, 0) + count
		elif child is MultiMeshInstance3D and child.multimesh and child.is_visible_in_tree():
			var mesh: Mesh = child.multimesh.mesh
			var per := 0
			for s in mesh.get_surface_count():
				var arrays: Array = mesh.surface_get_arrays(s)
				per += (arrays[Mesh.ARRAY_INDEX] as PackedInt32Array).size() / 3
			tally[name_here + " (copies)"] = tally.get(name_here + " (copies)", 0) + per * child.multimesh.instance_count
		_tally(child, tally, name_here if node == self else here)


# then each ring of plants hidden in turn (vegetation.gd's: by kind, detail and reach)
var _ring_names: Array = []
func _ring_profile_step(frame_in: int) -> void:
	if vegetation == null:
		get_tree().quit()
		return
	if _ring_names.is_empty():
		var seen := {}
		for node in vegetation.find_children("*", "MultiMeshInstance3D", true, false):
			if node.has_meta("ring"):
				seen[node.get_meta("ring")] = true
		_ring_names = seen.keys()
		_ring_names.sort()
	var part := frame_in / 180
	var into := frame_in % 180
	if part >= _ring_names.size():
		get_tree().quit()
		return
	var viewport := get_viewport().get_viewport_rid()
	if into == 0:
		camera.hidden_layers = 0
		sun.shadow_enabled = true
		world.visible = true
		vegetation.visible = true
		for node in vegetation.find_children("*", "MultiMeshInstance3D", true, false):
			if node.has_meta("ring"):
				node.visible = node.get_meta("ring") != _ring_names[part]
		_profile_sum = 0.0
		_profile_count = 0
	elif into >= 60:
		_profile_sum += RenderingServer.viewport_get_measured_render_time_gpu(viewport)
		_profile_count += 1
	if into == 179:
		var ms := _profile_sum / maxf(_profile_count, 1.0)
		var tiles := 0
		var triangles := 0
		for node in vegetation.find_children("*", "MultiMeshInstance3D", true, false):
			if node.has_meta("ring") and node.get_meta("ring") == _ring_names[part]:
				tiles += 1
				var arrays: Array = node.multimesh.mesh.surface_get_arrays(0)
				var per := 0
				for s in node.multimesh.mesh.get_surface_count():
					per += (node.multimesh.mesh.surface_get_arrays(s)[Mesh.ARRAY_INDEX] as PackedInt32Array).size() / 3
				triangles += per * node.multimesh.instance_count
		print("Geodrive GPU profile: plants %-58s %4.1f ms  (%d tiles, %.0f thousand triangles sent)" % [_ring_names[part], _profile_base - ms, tiles, triangles / 1000.0])


func _gpu_profile_step() -> void:
	if shown_frames < 300 or (_profile_frame == 0 and world.is_loading()):
		return	# (measured once everything round is in: a city keeps loading a minute)
	_profile_frame += 1
	var part := _profile_frame / 180
	var into := _profile_frame % 180
	var viewport := get_viewport().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(viewport, true)
	if part >= PROFILE_PARTS.size():
		_ring_profile_step(_profile_frame - PROFILE_PARTS.size() * 180)
		return
	if into == 0:
		# this part hidden (the last part's put back)
		camera.hidden_layers = int(PROFILE_PARTS[part][1]) if int(PROFILE_PARTS[part][1]) > 0 else 0
		sun.shadow_enabled = int(PROFILE_PARTS[part][1]) != -1
		world.visible = int(PROFILE_PARTS[part][1]) != -2
		var env: Environment = get_viewport().world_3d.environment if get_viewport().world_3d.environment else (get_viewport().get_camera_3d().environment if get_viewport().get_camera_3d().environment else null)
		if env == null:
			for node in find_children("*", "WorldEnvironment", true, false):
				env = (node as WorldEnvironment).environment
		if env:
			if not has_meta("env_saved"):
				set_meta("env_saved", [env.ssao_enabled, env.glow_enabled, env.fog_enabled])
			var saved: Array = get_meta("env_saved")
			env.ssao_enabled = saved[0] and int(PROFILE_PARTS[part][1]) != -3
			env.glow_enabled = saved[1] and int(PROFILE_PARTS[part][1]) != -4
			env.fog_enabled = saved[2] and int(PROFILE_PARTS[part][1]) != -6
			env.background_mode = Environment.BG_COLOR if int(PROFILE_PARTS[part][1]) == -7 else Environment.BG_SKY
		if car:
			car.visible = int(PROFILE_PARTS[part][1]) != -8
		hud.visible = int(PROFILE_PARTS[part][1]) != -9
		if not has_meta("msaa_saved"):
			set_meta("msaa_saved", get_viewport().msaa_3d)
		get_viewport().msaa_3d = Viewport.MSAA_DISABLED if int(PROFILE_PARTS[part][1]) == -5 else get_meta("msaa_saved")
		if vegetation:
			vegetation.visible = int(PROFILE_PARTS[part][1]) != -2
		_profile_sum = 0.0
		_profile_count = 0
		if part == 1:
			var counts := {}
			for child in world.get_children():
				if child is VisualInstance3D:
					counts[child.layers] = counts.get(child.layers, 0) + 1
			print("Geodrive GPU profile: the camera drawing is ours: %s, its mask %x; the world's layers %s" % [get_viewport().get_camera_3d() == camera, camera.cull_mask, counts])
			# everything else drawn, by the node it's under (its triangles, its draws)
			var tally := {}
			_tally(self, tally, "")
			var rows := tally.keys()
			rows.sort_custom(func(a, b): return tally[a] > tally[b])
			for row in rows.slice(0, 14):
				print("Geodrive GPU profile:   %8.0f thousand triangles under %s" % [tally[row] / 1000.0, row])
			# the ground squares by how far they are (their triangles)
			var rings := {}
			for child in world.get_children():
				if child is MeshInstance3D and String(child.name).begins_with("Ground_") and child.mesh:
					var arrays: Array = child.mesh.surface_get_arrays(0)
					var middle: Vector3 = child.global_transform * child.get_aabb().get_center()
					var d := int(Vector2(middle.x - camera.global_position.x, middle.z - camera.global_position.z).length() / 1000.0)
					var entry: Array = rings.get(d, [0, 0])
					entry[0] += 1
					entry[1] += (arrays[Mesh.ARRAY_INDEX] as PackedInt32Array).size() / 3
					rings[d] = entry
			var keys := rings.keys()
			keys.sort()
			for d in keys:
				print("Geodrive GPU profile:   ground %d-%d km away: %d squares, %.0f thousand triangles (%.0f each)" % [d, d + 1, rings[d][0], rings[d][1] / 1000.0, rings[d][1] / 1000.0 / rings[d][0]])
	elif into >= 60:
		_profile_sum += RenderingServer.viewport_get_measured_render_time_gpu(viewport)
		_profile_count += 1
	if into == 179:
		var ms := _profile_sum / maxf(_profile_count, 1.0)
		var triangles := Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME) / 1000.0
		var calls := Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME)
		if part == 0:
			_profile_base = ms
			print("Geodrive GPU profile: everything %.1f ms a frame (%.0f thousand triangles, %d draw calls)" % [ms, triangles, calls])
		else:
			print("Geodrive GPU profile: %-15s %4.1f ms  (without it %.1f ms; %.0f thousand triangles, %d draw calls)" % [PROFILE_PARTS[part][0], _profile_base - ms, ms, triangles, calls])


# --buildtest=stairs: a flight of stairs by the car, walked up at a walking pace (no jumping); how high they got
var _stairs: Dictionary = {}
var _stairs_frame := 0
var _stairs_top := -INF
func _build_stairs_step() -> void:
	_stairs_frame += 1
	if _stairs_frame == 120:
		_stairs = builder.build_test_stairs(car.global_position - car.global_transform.basis.z * 15.0, car.global_rotation.y + PI * 0.5)	# (on the road ahead: flat)
		toggle_walk()
	elif _stairs_frame == 180 and walker and not _stairs.is_empty():
		walker.global_position = _stairs["start"]
		var up: Vector3 = _stairs["up"]
		walker.rotation.y = atan2(-up.x, -up.z)
	elif _stairs_frame > 200 and _stairs_frame < 200 + 60 * 5 and walker and not _stairs.is_empty():
		walker.set_physics_process(false)
		walker.steer(1.0, 0.0, false, false, 1.0 / 60.0)
		_stairs_top = maxf(_stairs_top, walker.global_position.y - float(_stairs["floor"]))
		if _stairs_frame % 30 == 0:
			var along: float = (walker.global_position - (_stairs["start"] as Vector3)).dot(_stairs["up"])
			print("Geodrive build stairs: %.1f s  %.2f m along, %.2f m up, on the floor %s, speed %.2f" % [(_stairs_frame - 200) / 60.0, along,
				walker.global_position.y - float(_stairs["floor"]), walker.is_on_floor(), walker.ground_speed])
	elif _stairs_frame == 200 + 60 * 5:
		print("Geodrive build stairs: walked up %.2f m of the 3 m flight (no jumping)" % _stairs_top)
		get_tree().quit()


# --buildtest=hands: as a player would: out of the car, B, looking down at the ground ahead, a click (a foundation),
# then a wall (3) on it, a click; what each did printed
var _hands_frame := 0
# --buildtest=remove: the nearest building of the map's to the car found, a ray dropped on its roof; taken away; a
# while later the ray again (it should reach the ground now, and the building not be found)
# --hunttest: out of the car by the farm's kiosk, a .308 ticket, in to the farm's middle, the nearest animal that's
# legal today shot through the heart and lungs from 80 m (a level shot at its chest), then out again: the bag
# (a mouse's press, its moves, its release, on the shape editor's view, at points in it)
func _mouse_drag(editor: ShapeEditor, from: Vector2, to: Vector2) -> void:
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = from
	editor._view_input(press)
	for k in (10 if from.distance_to(to) > 0.5 else 0):	# (a click: no moves)
		var move := InputEventMouseMotion.new()
		move.button_mask = MOUSE_BUTTON_MASK_LEFT
		move.position = from.lerp(to, (k + 1) / 10.0)
		move.relative = (to - from) / 10.0
		editor._view_input(move)
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = to
	editor._view_input(release)


func _race_test_step(delta: float) -> void:
	if waiting != "" or car == null or builder == null or hunt == null:
		return
	var t: Dictionary = _race
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	var maker: GameMaker = builder.game_maker
	var drive_to := func(spot: Vector3) -> void:
		var forward: Vector3 = -car.global_transform.basis.z
		_put_car(Transform3D(car.global_transform.basis, Vector3(spot.x, world.ground_height(spot) + 0.8, spot.z)))
	if at.call(2.0):
		# made: four checkpoints along the way the car faces (the start 40 m on), point to point, no weapons, finish wins
		var forward: Vector3 = -car.global_transform.basis.z
		forward.y = 0.0
		forward = forward.normalized()
		maker.new_game()
		maker.game["name"] = "Test race"
		var spots := []
		for d in [40.0, 110.0, 180.0, 250.0]:
			var spot: Vector3 = car.global_position + forward * d
			maker.add_checkpoint(spot)
			spots.append(spot)
		t["spots"] = spots
		var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
		maker.game["booth"] = [here[0], here[1]]
		maker.game["weapons"] = ["none"]
		maker.add_preset("race", 0)
		var id := maker.save()
		t["id"] = id
		for i in hunt.reserves.size():
			if hunt.reserves[i].get("id", "") == id:
				t["index"] = i
	if at.call(4.0):
		hunt.toggle_game()
		t["offered"] = hunt.chooser_open()
		var take_part := false
		if t["offered"]:
			for child in hunt._chooser.get_child(0).get_children():
				take_part = take_part or (child is Button and child.text == "Take part")
		t["take_part"] = take_part
		hunt.close_tickets()
		hunt._buy(t["index"], {})
	if at.call(5.0):
		t["playing"] = hunt.hunting == t["index"] and hunt._course.size() == 4
		t["line_before"] = hud.game_label.text
		drive_to.call(t["spots"][0])
	# through them: the start, the third (too early), the second, the third, the finish
	var order := [[6.0, 2], [7.0, 1], [8.5, 2], [10.0, 3]]
	for step in order:
		if at.call(step[0]):
			if step[1] == 2 and not t.has("early"):
				t["early"] = true
			drive_to.call(t["spots"][step[1]])
	if at.call(6.5):
		# (the third reached before the second: it doesn't count)
		t["line"] = hud.game_label.text
		t["skipped"] = hunt.rules != null and hunt.rules.get_next_checkpoint() == 1
	if at.call(7.5):
		if shot_path != "":
			camera.start_free()
			camera.global_position = car.global_position + Vector3(0.0, 6.0, 0.0) - (-car.global_transform.basis.z) * 25.0
			camera.look_at(car.global_position + (-car.global_transform.basis.z) * 60.0, Vector3.UP)
			camera._free_pitch = camera.rotation.x
			camera._free_yaw = camera.rotation.y
	if at.call(8.0) and shot_path != "":
		_save(shot_path.get_basename() + "_course.png")
		camera.stop_free()
	if at.call(10.5):
		t["finished"] = hud.banner_label.visible and hud.banner_label.text.begins_with("Finished") and hunt.hunting < 0
		if shot_path != "":
			_save(shot_path.get_basename() + "_finished.png")
		print("Geodrive race test: offered %s (\"Take part\" %s); playing %s (before the start: \"%s\"); after the third early: \"%s\" (it didn't count %s); the banner \"%s\"" % [
			t["offered"], t["take_part"], t["playing"], t["line_before"], t["line"], t["skipped"], hud.banner_label.text])
		var ok: bool = t["offered"] and t["take_part"] and t["playing"] and String(t["line_before"]).begins_with("To the start") and String(t["line"]).begins_with("Checkpoint 2 / 4") \
			and t["skipped"] and t["finished"]
		print("Geodrive race test: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


# A zombie shot in the head from 12 m off, where nothing's between (the nearest standing): the shot's result
func _head_shot() -> Dictionary:
	var best := -1
	var nearest := INF
	for i in hunt.hunter.get_count():
		var a: Dictionary = hunt.hunter.animal(i)
		var d: float = (a["position"] as Vector3).distance_to(walker.global_position)
		if not a["dead"] and d < nearest:
			nearest = d
			best = i
	var z: Vector3 = hunt.hunter.animal(best)["position"]
	var tall: float = shoulder_of("zombie") / 0.83
	for k in 16:
		var from := z + Vector3.RIGHT.rotated(Vector3.UP, k * TAU / 16.0) * 12.0
		from.y = world.ground_height(from) + 1.6
		if get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(from, z + Vector3.UP * tall * 0.9)).is_empty() and not world.building_at(from)["found"]:
			walker.global_position = from - Vector3.UP * 1.5
			break
	var eye: Vector3 = walker.eyes()
	return hunt.shoot("308win", eye, (z + Vector3.UP * tall * 0.95 - eye).normalized())


func _rules_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null or hunt._kiosks.is_empty():
		return
	var t: Dictionary = _rules_t
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	var zone: int = hunt._kiosks[0]["reserve"]
	if at.call(2.0):
		toggle_walk()
		hunt.reserves[zone]["rules"] = [
			{"when": "kill", "of": "zombie", "then": [{"do": "score", "by": 1}]},
			{"when": "score", "reaches": 3, "then": [{"do": "win", "text": "3 zombies: you've won"}]},
			{"when": "start", "then": [{"do": "timer", "seconds": 60}]},
			{"when": "time_up", "then": [{"do": "lose", "text": "Time's up"}]}]
		walker.global_position = hunt._kiosks[0]["node"].global_position + Vector3(2.0, 1.0, 2.0)
		hunt._buy(zone, GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0])
	# three zombies, a second apart
	for k in 3:
		if at.call(3.5 + k):
			var shot := _head_shot()
			print("Geodrive rules test: shot %d: %s; the line (as the shot came): %s" % [k + 1, hunt.WEAPON.report(shot), hud.game_label.text])
			if k == 1:
				t["line"] = hud.game_label.text	# (the first's on it by now)
			if k == 1 and shot_path != "":
				_save(shot_path.get_basename() + "_score.png")
	if at.call(5.6):
		t["won"] = hud.banner_label.visible and hud.banner_label.text.contains("won") and hunt.hunting < 0
		print("Geodrive rules test: after the third: the banner \"%s\", playing %s" % [hud.banner_label.text, hunt.hunting >= 0])
		if shot_path != "":
			_save(shot_path.get_basename() + "_won.png")
	# a 3 s limit: lost when it's up
	if at.call(7.0):
		hud.banner_label.visible = false
		hunt.reserves[zone]["rules"] = [{"when": "start", "then": [{"do": "timer", "seconds": 3}]}, {"when": "time_up", "then": [{"do": "lose", "text": "Time's up"}]}]
		hunt._buy(zone, GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0])
	if at.call(11.0):
		t["lost"] = hud.banner_label.visible and hud.banner_label.text.contains("Time") and hunt.hunting < 0
		print("Geodrive rules test: a 3 s limit: the banner \"%s\", playing %s" % [hud.banner_label.text, hunt.hunting >= 0])
		var ok: bool = String(t.get("line", "")).begins_with("Score 1 / 3") and String(t.get("line", "")).contains("left") and t.get("won", false) and t["lost"]
		print("Geodrive rules test: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


func _game_maker_test_step(delta: float) -> void:
	if waiting != "" or car == null or builder == null or hunt == null:
		return
	var t: Dictionary = _gm
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	var maker: GameMaker = builder.game_maker
	if at.call(2.0):
		toggle_walk()
		builder.toggle()
		maker.new_game()
		maker.game["name"] = "Test game"
		maker.start_marking()
	if at.call(2.5):
		# four clicks on the map round where you are (the map's middle), as a mouse would
		var middle := get_viewport().get_visible_rect().size * 0.5
		for corner in [Vector2(-220, -160), Vector2(220, -160), Vector2(220, 160), Vector2(-220, 160)]:
			var click := InputEventMouseButton.new()
			click.button_index = MOUSE_BUTTON_LEFT
			click.pressed = true
			click.position = middle + corner
			maker._unhandled_input(click)
	if at.call(2.8):
		if shot_path != "":
			_save(shot_path.get_basename() + "_marking.png")
		var enter := InputEventKey.new()
		enter.keycode = KEY_ENTER
		enter.pressed = true
		maker._unhandled_input(enter)
		t["marked"] = maker.game["outline"].size() == 4 and maker.hectares() > 0.5
		print("Geodrive game maker test: marked %d corners, %.1f ha" % [maker.game["outline"].size(), maker.hectares()])
		builder._show_menu(false)
	if at.call(3.0):
		var lat_lon: PackedFloat64Array = world.to_lat_lon(walker.global_position)
		maker.game["booth"] = [lat_lon[0], lat_lon[1]]
		maker.game["stock"] = {"springbok": 20, "zombie": 10}
		maker.game["weapons"] = ["308win"]
		maker.game["pool"] = false
		maker.game["out"] = "playing"
		var id := maker.save()
		t["saved"] = id != "" and FileAccess.file_exists(Packs.workshop().path_join("reserves/%s.json" % id)) \
			and hunt.reserves.any(func(r): return r.get("id", "") == id)
		t["id"] = id
	if at.call(5.0):
		# its booth up (the area's within reach); F1: its ticket offered here, the .308 only
		var index := -1
		for i in hunt.reserves.size():
			if hunt.reserves[i].get("id", "") == t["id"]:
				index = i
		t["booth"] = hunt._kiosks.any(func(k): return k["reserve"] == index)
		hunt.toggle_game()
		t["offered"] = hunt.chooser_open()
		var buttons := 0
		if t["offered"]:
			for child in hunt._chooser.get_child(0).get_children():
				buttons += 1 if child is Button else 0
		t["weapons"] = buttons
		hunt.close_tickets()
		hunt._buy(index, GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0])
		t["index"] = index
	if at.call(6.5):
		var here_now: PackedFloat64Array = world.to_lat_lon(walker.global_position)
		print("Geodrive game maker test (why): ticket %d, inside %s, built %s, living %d, hunter %s, near %.0f m of %.0f" % [hunt.ticket, hunt._in(t["index"], here_now),
			hunt._built.has(t["index"]), hunt.living, hunt.hunter != null, hunt._metres(here_now[0], here_now[1], hunt._centre(hunt.reserves[t["index"]]).x, hunt._centre(hunt.reserves[t["index"]]).y), hunt._reach(hunt.reserves[t["index"]])])
		t["playing"] = hunt.hunting == t["index"]
		t["creatures"] = hunt.hunter.get_count() if hunt.living == t["index"] else 0
		if shot_path != "":
			camera.start_free()
			var booth: Vector3 = hunt._kiosks.filter(func(k): return k["reserve"] == t["index"])[0]["node"].global_position if t["booth"] else walker.global_position
			camera.global_position = booth + Vector3(8.0, 5.0, 8.0)
			camera.look_at(booth + Vector3.UP * 2.0, Vector3.UP)
			camera._free_pitch = camera.rotation.x
			camera._free_yaw = camera.rotation.y
	if at.call(7.0):
		if shot_path != "":
			_save(shot_path.get_basename() + "_booth.png")
		print("Geodrive game maker test: saved %s; its booth up %s; F1 offers its ticket %s (%d weapon); playing %s, %d creatures out" % [
			t["saved"], t["booth"], t["offered"], t["weapons"], t["playing"], t["creatures"]])
		var ok: bool = t["marked"] and t["saved"] and t["booth"] and t["offered"] and t["weapons"] == 1 and t["playing"] and t["creatures"] == 30
		print("Geodrive game maker test: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


func _prop_test_step(delta: float) -> void:
	if waiting != "" or car == null or builder == null:
		return
	var t: Dictionary = _prop
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	var props: PropKeeper = builder.props
	if at.call(2.0):
		# made: a crate with a back, named a bench; saved
		toggle_walk()
		builder.toggle()
		var shop := Workshop.new(builder)
		builder.workshop = shop
		shop.thing = "prop"
		shop.new_prop()
		shop.prop["name"] = "Test bench"
		shop.prop["look"]["parts"][0]["size"] = [1.2, 0.45, 0.4]
		shop.prop["look"]["parts"][0]["at"] = [0.0, 0.225, 0.0]
		shop.prop_editor.add("box")
		var back: Dictionary = shop.prop["look"]["parts"][-1]
		back["size"] = [1.2, 0.5, 0.06]
		back["at"] = [0.0, 0.7, 0.17]
		back["colour"] = "#7a4a26"
		back["finish"] = "wood"
		var id := shop.save_prop()
		t["id"] = id
		t["made"] = id != "" and props.kinds.has(id) and FileAccess.file_exists(Packs.workshop().path_join("props/%s.json" % id))
		# chosen, the eyes on the ground 4 m ahead
		props.chosen = id
		walker.pitch = -0.45
	if at.call(2.6):
		if shot_path != "":
			_save(shot_path.get_basename() + "_placing.png")
		t["aimed"] = not props._aim.is_empty() and props._aim["ok"]
		builder._put()
		t["put"] = props.placed.size() == 1 and props._shown.size() == 1
		props.stop()
	if at.call(3.0):
		# solid: a ray from over it lands on it
		var node: Node3D = props._shown.values()[0] if not props._shown.is_empty() else null
		if node:
			var over := node.global_position + Vector3.UP * 3.0
			var hit := get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(over, over + Vector3.DOWN * 6.0, 0xFFFFFFFF, [walker.get_rid()]))
			t["solid"] = not hit.is_empty() and (hit["collider"] as Node).get_parent() == node
			t["seat"] = (hit["position"] as Vector3).y - node.global_position.y if not hit.is_empty() else -1.0
		# kept, and back when its area's built again
		t["kept"] = FileAccess.get_file_as_string(props._path()).contains(String(t["id"]))
		props.reset_area()
		props.physics(1.1)
		t["back"] = props._shown.size() == 1
		if shot_path != "":
			camera.start_free()
			var bench: Vector3 = props._shown.values()[0].global_position if t["back"] else walker.global_position
			camera.global_position = bench + Vector3(2.0, 1.4, 2.0)
			camera.look_at(bench + Vector3.UP * 0.4, Vector3.UP)
			camera._free_pitch = camera.rotation.x
			camera._free_yaw = camera.rotation.y
	if at.call(3.6):
		if shot_path != "":
			_save(shot_path.get_basename() + "_placed.png")
			camera.stop_free()
		# right clicked twice: gone
		var node: Node3D = props._shown.values()[0] if not props._shown.is_empty() else null
		if node:
			var over := node.global_position + Vector3.UP * 3.0
			var hit := get_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(over, over + Vector3.DOWN * 6.0, 0xFFFFFFFF, [walker.get_rid()]))
			props.take(hit)
			props.take(hit)
		t["gone"] = props._shown.is_empty() and props.placed.filter(func(e): return not e.is_empty()).is_empty()
		print("Geodrive prop test: made and saved %s; aimed %s, put %s; solid %s (its top %.2f m up); kept %s; back %s; taken away %s" % [
			t["made"], t["aimed"], t["put"], t.get("solid", false), t.get("seat", -1.0), t["kept"], t["back"], t["gone"]])
		var ok: bool = t["made"] and t["aimed"] and t["put"] and t.get("solid", false) and t["kept"] and t["back"] and t["gone"]
		print("Geodrive prop test: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


func _editor_test_step(delta: float) -> void:
	if waiting != "" or car == null or builder == null:
		return
	var t: Dictionary = _editor
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	if at.call(2.0):
		# the workshop's shape editor open, on the submachine gun
		toggle_walk()
		builder.toggle()
		builder._page = builder.WORKSHOP_PAGE
		builder.workshop = Workshop.new(builder)
		builder.workshop.tab = 1
		builder._show_menu(true)
	if at.call(2.5):
		var editor: ShapeEditor = builder.workshop.editor
		var parts: Array = editor.look["parts"]
		# a click on the receiver (part 0, the biggest) picks it
		var middle: Vector3 = PartModel._vec(parts[0]["at"], Vector3.ZERO)
		var click: Vector2 = editor._camera.unproject_position(middle)
		_mouse_drag(editor, click, click)
		t["picked"] = editor.picked == 0
		# its z arrow (Move): grabbed near its head, dragged along it on the screen 60 px
		var arrows: Array = editor._arrows()
		var tail: Vector2 = editor._camera.unproject_position(arrows[2][0])
		var head: Vector2 = editor._camera.unproject_position(arrows[2][1])
		var z_before: float = float(parts[0]["at"][2])
		_mouse_drag(editor, tail.lerp(head, 0.8), tail.lerp(head, 0.8) + (head - tail).normalized() * 60.0)
		var z_after: float = float(parts[0]["at"][2])
		t["moved"] = z_after - z_before
		t["on_grid"] = is_equal_approx(snappedf(z_after, 0.01), z_after)
		# Size: its y arrow dragged 40 px up the screen: taller
		editor.mode = "size"
		arrows = editor._arrows()
		tail = editor._camera.unproject_position(arrows[1][0])
		head = editor._camera.unproject_position(arrows[1][1])
		var h_before: float = float(parts[0]["size"][1])
		_mouse_drag(editor, tail.lerp(head, 0.8), tail.lerp(head, 0.8) + (head - tail).normalized() * 40.0)
		t["taller"] = float(parts[0]["size"][1]) - h_before
		editor.mode = "move"
		# the part itself dragged 50 px right: moved across the view (a click on it, off its arrows)
		var x_before: Vector3 = PartModel._vec(parts[0]["at"], Vector3.ZERO)
		var on: Vector2 = editor._camera.unproject_position(x_before + Vector3(0.0, 0.0, -0.1))	# (its front end: no arrow there)
		t["grabbed"] = editor._part_at(on) == 0 and editor._arrow_at(on) < 0
		_mouse_drag(editor, on, on + Vector2(50.0, 0.0))
		t["across"] = PartModel._vec(parts[0]["at"], Vector3.ZERO).distance_to(x_before)
		# made 40 cm long
		editor.scale_to(0.40)
		t["length"] = editor.bounds().size.z
		print("Geodrive editor test: picked by a click %s; the z arrow moved it %.1f cm (on the grid %s); Size made it %.1f cm taller; dragged across %.1f cm (grabbed %s); made %.1f cm long; %s" % [
			t["picked"], t["moved"] * 100.0, t["on_grid"], t["taller"] * 100.0, t["across"] * 100.0, t["grabbed"], t["length"] * 100.0, editor._size_label.text])
		editor.pick(0)
	if at.call(2.4) and shot_path != "":
		_save(shot_path.get_basename() + "_editor_before.png")
	if at.call(3.2):
		if shot_path != "":
			_save(shot_path.get_basename() + "_editor.png")
		var ok: bool = t["picked"] and absf(t["moved"]) >= 0.01 and t["on_grid"] and t["taller"] > 0.005 and t["grabbed"] and t["across"] > 0.01 and absf(t["length"] - 0.40) < 0.002
		print("Geodrive editor test: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


func _workshop_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null or hunt._kiosks.is_empty() or builder == null:
		return
	var t: Dictionary = _workshop
	t["time"] += delta
	match int(t["step"]):
		0:
			if t["time"] > 2.0:
				# made in the workshop, as its page makes it (its first draft: a 9 mm submachine gun)
				var shop := Workshop.new(builder)
				# (and how it looks: a red box added on top, as the shape editor adds one)
				var parts_before: int = shop.draft["look"]["parts"].size()
				shop.editor.add("box")
				shop.draft["look"]["parts"][-1]["colour"] = "#a6302a"
				shop.draft["look"]["parts"][-1]["at"] = [0.0, 0.06, -0.05]
				builder.workshop = shop
				var carried: Dictionary = shop.carries()
				var id: String = shop.save()
				var path := Packs.workshop().path_join("weapons/%s.json" % id)
				t["saved"] = id != "" and FileAccess.file_exists(path)
				var gun: Array = GeodriveHunt.loads().filter(func(l): return l["id"] == id)
				t["look_kept"] = not gun.is_empty() and gun[0].get("look", {}).get("parts", []).size() == parts_before + 1
				print("Geodrive workshop test: its look: %d parts (one added), kept with it: %s" % [parts_before + 1, t["look_kept"]])
				print("Geodrive workshop test: made %s: %d ft-lb at the muzzle, %d at 100 m; saved %s; at the booths %s" % [shop.draft["name"],
					roundi(carried.get("energy", 0.0)), roundi(carried.get("energy_100", 0.0)), t["saved"], not gun.is_empty()])
				toggle_walk()
				walker.global_position = hunt._kiosks[0]["node"].global_position + Vector3(2.0, 1.0, 2.0)
				hunt._buy(hunt._kiosks[0]["reserve"], gun[0])
				t["step"] = 1
				t["time"] = 0.0
		1:
			# (with pictures: the workshop's page in the build menu, as it made the gun)
			if shot_path != "" and t["time"] >= 0.3 and t["time"] - delta < 0.3:
				builder.toggle()
				builder._page = builder.WORKSHOP_PAGE
				builder._show_menu(true)
			if shot_path != "" and t["time"] >= 0.8 and t["time"] - delta < 0.8:
				_save(shot_path.get_basename() + "_workshop.png")
				# (how it looks: the shape editor, the red box picked)
				builder.workshop.tab = 1
				builder.workshop.editor.picked = builder.workshop.draft["look"]["parts"].size() - 1
				builder._fill_menu()
			if shot_path != "" and t["time"] >= 1.4 and t["time"] - delta < 1.4:
				_save(shot_path.get_basename() + "_looks.png")
				builder._show_menu(false)
				builder.toggle()
			if t["time"] > (1.6 if shot_path != "" else 1.0):
				# the nearest zombie, from 15 m off on open ground, the gun at its chest
				var best := -1
				var nearest := INF
				for i in hunt.hunter.get_count():
					var d: float = (hunt.hunter.animal(i)["position"] as Vector3).distance_to(walker.global_position)
					if d < nearest:
						nearest = d
						best = i
				var z: Vector3 = hunt.hunter.animal(best)["position"]
				for k in 16:
					var spot := z + Vector3.RIGHT.rotated(Vector3.UP, k * TAU / 16.0) * 15.0
					spot.y = world.ground_height(spot) + 1.6
					var ray := PhysicsRayQueryParameters3D.create(spot, z + Vector3.UP * 1.2)
					if get_world_3d().direct_space_state.intersect_ray(ray).is_empty() and not world.building_at(spot)["found"]:
						walker.global_position = spot - Vector3.UP * 1.5
						break
				t["target"] = best
				t["loaded"] = hunt.weapon.loaded
				t["step"] = 2
				t["time"] = 0.0
		2:
			# (aimed at its chest each frame, as a player holds it on; the trigger held a second)
			var z: Vector3 = hunt.hunter.animal(t["target"])["position"] + Vector3.UP * 1.25
			var to: Vector3 = z - walker.eyes()
			walker.rotation.y = atan2(-to.x, -to.z)
			walker.pitch = atan2(to.y, Vector2(to.x, to.z).length())
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
			hunt.weapon._trigger = t["time"] < 1.0
			if shot_path != "" and t["time"] >= 0.5 and t["time"] - delta < 0.5:
				hunt.weapon.shown_for_test = true
				_save(shot_path.get_basename() + "_firing.png")
			# (and aiming along its sights, up at the eye)
			if shot_path != "":
				hunt.weapon.aiming = t["time"] > 1.3 and t["time"] < 2.0
				if t["time"] >= 1.8 and t["time"] - delta < 1.8:
					_save(shot_path.get_basename() + "_aiming.png")
			if t["time"] > 3.0:
				var fired: int = int(t["loaded"]) - int(hunt.weapon.loaded)
				var hits: int = hunt._shots.filter(func(r): return r.get("hit", false) and int(r.get("index", -1)) == int(t["target"])).size()
				var down: bool = hunt.hunter.animal(t["target"])["dead"]
				print("Geodrive workshop test: a second on the trigger: %d rounds, %d hits on the zombie, it's %s" % [fired, hits, "down" if down else "STANDING"])
				var ok: bool = t["saved"] and t["look_kept"] and fired >= 12 and fired <= 15 and down
				print("Geodrive workshop test: %s" % ("PASS" if ok else "FAIL"))
				get_tree().quit()


func _booth_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null or hunt._kiosks.is_empty():
		return
	var t: Dictionary = _booth
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	match int(t["step"]):
		0:
			# on foot 300 m from the booth, in George, no ticket
			if at.call(2.0):
				toggle_walk()
				var booth: Vector3 = hunt._kiosks[0]["node"].global_position
				var spot := booth + Vector3(220.0, 0.0, 200.0)
				walker.global_position = Vector3(spot.x, world.ground_height(spot) + 1.0, spot.z)
				hunt._booth_hint = 0.0	# (said now: the first, as the game came in, was spoken over by getting out)
			if at.call(3.0):
				t["no_zombies"] = hunt.living < 0
				t["hint"] = hud.status_label.text
				print("Geodrive booth test: no ticket: creatures out %s; the hint: %s" % [hunt.living >= 0, t["hint"]])
				camera.toggle_map()
			if at.call(7.0):
				if shot_path != "":
					_save(shot_path.get_basename() + "_mmap.png")
				camera.toggle_map()
				open_world_map()
				get_tree().paused = false
				var booth: Dictionary = hunt.booths().filter(func(b): return b["area"] == hunt._kiosks[0]["reserve"])[0]
				world_map.centre_lat = booth["lat"] - 0.05
				world_map.centre_lon = booth["lon"]
				world_map.pixels_per_degree = 2000.0
				world_map._changed()
			if at.call(9.0):
				if shot_path != "":
					_save(shot_path.get_basename() + "_worldmap.png")
				world_map.close()
				# a ticket at the booth
				walker.global_position = hunt._kiosks[0]["node"].global_position + Vector3(2.0, 1.0, 2.0)
				hunt._buy(hunt._kiosks[0]["reserve"], GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0])
			if at.call(11.0):
				t["zombies"] = hunt.hunter.get_count() if hunt.living >= 0 else 0
				print("Geodrive booth test: a ticket: playing %s, %d zombies out" % [hunt.hunting >= 0, t["zombies"]])
				# a zombie on a road within 150 m: the camera beside it, looking at its feet
				for i in hunt.hunter.get_count():
					var z: Vector3 = hunt.hunter.animal(i)["position"]
					var road: Dictionary = world.nearest_road(z, 3.0)
					if road.get("found", false) and z.distance_to(walker.global_position) < 150.0:
						t["on_road"] = i
						break
				if t.has("on_road"):
					var z: Vector3 = hunt.hunter.animal(t["on_road"])["position"]
					camera.start_free()
					camera.global_position = z + Vector3(4.0, 1.4, 4.0)
					camera.look_at(z + Vector3.UP * 0.6, Vector3.UP)
					camera._free_pitch = camera.rotation.x
					camera._free_yaw = camera.rotation.y
			if at.call(12.5):
				if shot_path != "" and t.has("on_road"):
					_save(shot_path.get_basename() + "_onroad.png")
				t["playing"] = hunt.hunting >= 0
				# F1: stopped; F1 again: the tickets here
				hunt.toggle_game()
			if at.call(14.0):
				t["stopped"] = hunt.hunting < 0 and hunt.living < 0
				hunt.toggle_game()
				t["offered"] = hunt.chooser_open()
				hunt.close_tickets()
				print("Geodrive booth test: F1: stopped %s (the zombies gone); F1 again: the tickets offered %s" % [t["stopped"], t["offered"]])
				var ok: bool = t["no_zombies"] and String(t["hint"]).contains("the booth:") and t["zombies"] > 0 and t["playing"] and t["stopped"] and t["offered"]
				print("Geodrive booth test: %s" % ("PASS" if ok else "FAIL"))
				get_tree().quit()


func _roadkill_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null or hunt.living < 0:
		return
	var t: Dictionary = _roadkill
	t["time"] += delta
	match int(t["step"]):
		0, 2:
			if t["time"] > 3.0 and not t.has("waiting"):
				# the nearest animal (not the first one): the car held still 30 m off it till the ground's solid there
				var best := -1
				var nearest := INF
				for i in hunt.hunter.get_count():
					var c: Dictionary = hunt.hunter.animal(i)
					var d: float = (c["position"] as Vector3).distance_to(car.global_position)
					if not c["dead"] and i != t.get("first", -1) and d < nearest:
						nearest = d
						best = i
				var near_it: Vector3 = hunt.hunter.animal(best)["position"] + Vector3(30.0, 0.0, 0.0)
				_put_car(Transform3D(Basis(), Vector3(near_it.x, world.ground_height(near_it) + 1.0, near_it.z)))
				car.freeze = true
				t["target"] = best
				t["waiting"] = true
			elif t.has("waiting"):
				# the ground in: the car behind it, coming at it: slowly from 4 m, fast from 6 m (farther, it's off out
				# of the way first: it sees the car coming)
				var slow := int(t["step"]) == 0
				var run_up := 4.0 if slow else 6.0
				var a: Dictionary = hunt.hunter.animal(t["target"])
				var at: Vector3 = a["position"]
				var way := -(a["facing"] as Vector3)
				var from := at - way * (run_up + 2.6)
				if not (world.is_solid_at(at) and world.is_solid_at(from)):
					return
				t.erase("waiting")
				from.y = world.ground_height(from) + 0.6
				car.freeze = false
				_put_car(Transform3D(Basis.looking_at(way, Vector3.UP), from))
				var speed := 1.5 if slow else 16.7
				car.linear_velocity = way * speed
				t["speed"] = speed
				t["hits"] = hunt.car_hits.size()
				print("Geodrive roadkill test: the car at a %s, %.0f m off, at %.0f km/h" % [a["name"], run_up, speed * 3.6])
				t["step"] = int(t["step"]) + 1
				t["time"] = 0.0
		1, 3:
			var slow := int(t["step"]) == 1
			# (the car kept coming: as a driver holds the speed)
			if hunt.car_hits.size() == t["hits"]:
				var way: Vector3 = -car.global_transform.basis.z
				way.y = 0.0
				car.linear_velocity = Vector3(way.normalized().x * t["speed"], car.linear_velocity.y, way.normalized().z * t["speed"])
				t["before"] = Vector2(car.linear_velocity.x, car.linear_velocity.z).length()
			elif not t.has("hit%d" % t["step"]):
				var hit: Dictionary = hunt.car_hits[t["hits"]]
				t["hit%d" % t["step"]] = hit
				t["lost%d" % t["step"]] = t["before"] - Vector2(car.linear_velocity.x, car.linear_velocity.z).length()
				print("Geodrive roadkill test: hit the %s at %.0f km/h: %s; the car %.2f m/s slower (%.2f expected)" % [hit["name"], hit["speed"] * 3.6,
					"killed" if hit["killed"] else ("wounded" if hit["wounded"] else "bumped"), t["lost%d" % t["step"]], hit["car_loses"]])
			if t["time"] > (8.0 if slow else 4.0):
				var a: Dictionary = hunt.hunter.animal(t["target"])
				t["after%d" % t["step"]] = a["doing"]
				print("Geodrive roadkill test: %.0f s on, the %s is %s" % [t["time"], a["name"], a["doing"]])
				if slow:
					t["first"] = t["target"]
					t["step"] = 2
					t["time"] = 0.0
				else:
					var bump: Dictionary = t.get("hit1", {})
					var kill: Dictionary = t.get("hit3", {})
					var ok: bool = not bump.is_empty() and not bump["killed"] and t["after1"] == "running" 						and not kill.is_empty() and kill["killed"] and t["after3"] == "dead" and t["lost3"] > 0.05
					print("Geodrive roadkill test: %s" % ("PASS" if ok else "FAIL"))
					get_tree().quit()


func _zombie_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null:
		return
	var t: Dictionary = _zombie_test
	t["time"] += delta
	match int(t["step"]):
		0:
			if not hunt._kiosks.is_empty() and t["time"] > 2.0:
				toggle_walk()
				walker.global_position = hunt._kiosks[0]["node"].global_position + Vector3(2.0, 1.0, 2.0)
				var kiosk: Dictionary = hunt.kiosk_near(walker.global_position)
				print("Geodrive zombie test: at the booth (%s): %s" % [hunt.reserves[hunt._kiosks[0]["reserve"]]["name"], "E buys a ticket" if not kiosk.is_empty() else "NOT NEAR IT"])
				hunt._buy(hunt._kiosks[0]["reserve"], GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0])
				t["step"] = 1
				t["time"] = 0.0
		1:
			if t["time"] > 1.0:
				# (on: you're in the zone already) the zombies out: how many stand in a building (none should)
				var count: int = hunt.hunter.get_count()
				var in_buildings := 0
				var round_you := 0
				for i in count:
					var at: Vector3 = hunt.hunter.animal(i)["position"]
					in_buildings += 1 if world.building_at(at + Vector3.UP * 0.5)["found"] else 0
					round_you += 1 if Vector2(at.x - walker.global_position.x, at.z - walker.global_position.z).length() < 255.0 else 0
				t["round_you"] = round_you
				t["playing"] = hunt.hunting >= 0
				t["in_buildings"] = in_buildings
				t["count"] = count
				print("Geodrive zombie test: playing %s; %d zombies, %d of them round you (within 250 m), %d in a building" % [t["playing"], count, round_you, in_buildings])
				# the nearest zombie, and you 25 m from it on open ground (in its reach: 40 m)
				var best := -1
				var nearest := INF
				for i in count:
					var d: float = (hunt.hunter.animal(i)["position"] as Vector3).distance_to(walker.global_position)
					if d < nearest:
						nearest = d
						best = i
				var z: Vector3 = hunt.hunter.animal(best)["position"]
				for k in 16:
					var spot := z + Vector3.RIGHT.rotated(Vector3.UP, k * TAU / 16.0) * 25.0
					spot.y = world.ground_height(spot)
					if not world.building_at(spot + Vector3.UP * 0.5)["found"]:
						walker.global_position = spot + Vector3.UP * 0.2
						break
				t["target"] = best
				t["step"] = 2
				t["time"] = 0.0
		2:
			var z: Dictionary = hunt.hunter.animal(t["target"])
			# (with pictures: the zombie coming, from 6 m off its side, at 1.5 s)
			if shot_path != "" and t["time"] < 1.6:
				var at: Vector3 = z["position"]
				var side: Vector3 = (z["facing"] as Vector3).cross(Vector3.UP)
				if not camera.free:
					camera.start_free()
				camera.global_position = at + side * 4.5 + (z["facing"] as Vector3) * 4.0 + Vector3.UP * 1.7
				camera.look_at(at + Vector3.UP * 1.2, Vector3.UP)
				camera._free_pitch = camera.rotation.x
				camera._free_yaw = camera.rotation.y
				if t["time"] >= 1.5 and t["time"] - delta < 1.5:
					_save(shot_path.get_basename() + "_zombie.png")
					camera.stop_free()
			if z["doing"] == "coming for you" and not t.has("coming"):
				t["coming"] = t["time"]
				print("Geodrive zombie test: it's coming for you (%.1f s, %.0f m off)" % [t["time"], (z["position"] as Vector3).distance_to(walker.global_position)])
			if walker.down_wanted:
				t["caught"] = t["time"]
				print("Geodrive zombie test: caught after %.1f s: down" % t["time"])
				t["step"] = 3
				t["time"] = 0.0
			elif t["time"] > 60.0:
				print("Geodrive zombie test: not caught in 60 s (the zombie %s, %.0f m off)" % [z["doing"], (z["position"] as Vector3).distance_to(walker.global_position)])
				t["step"] = 3
				t["time"] = 0.0
		3:
			if t.has("caught") and not walker.down_wanted and not t.has("up"):
				t["up"] = walker.global_position.distance_to(car.global_position)
				var near := 0
				for i in hunt.hunter.get_count():
					var a: Dictionary = hunt.hunter.animal(i)
					near += 1 if not a["dead"] and (a["position"] as Vector3).distance_to(walker.global_position) < 60.0 else 0
				t["near_up"] = near
				print("Geodrive zombie test: up again after %.1f s, %.1f m from the car; %d zombies within 60 m" % [t["time"], t["up"], near])
			if t.has("up") and walker.down_wanted:
				t["caught_again"] = true
			if t["time"] > 8.0:
				# a zombie shot from 12 m, where nothing's between: in the chest (it keeps coming), then the head (down)
				var best := -1
				var nearest := INF
				for i in hunt.hunter.get_count():
					var a: Dictionary = hunt.hunter.animal(i)
					var d: float = (a["position"] as Vector3).distance_to(walker.global_position)
					if not a["dead"] and d < nearest:
						nearest = d
						best = i
				var at: Vector3 = hunt.hunter.animal(best)["position"]
				var tall: float = shoulder_of("zombie") / 0.83
				for k in 16:
					var from := at + Vector3.RIGHT.rotated(Vector3.UP, k * TAU / 16.0) * 12.0
					from.y = world.ground_height(from) + 1.6
					var ray := PhysicsRayQueryParameters3D.create(from, at + Vector3.UP * tall * 0.7)
					if get_world_3d().direct_space_state.intersect_ray(ray).is_empty() and not world.building_at(from)["found"]:
						walker.global_position = from - Vector3.UP * 1.5
						break
				var eye: Vector3 = walker.eyes()
				var chest: Dictionary = hunt.shoot("308win", eye, (at + Vector3.UP * tall * 0.70 - eye).normalized())
				var head: Dictionary = hunt.shoot("308win", eye, (at + Vector3.UP * tall * 0.95 - eye).normalized())
				t["chest"] = chest
				t["head"] = head
				print("Geodrive zombie test: the chest: %s; the head: %s" % [hunt.weapon.report(chest), hunt.weapon.report(head)])
				var ok: bool = t.get("playing", false) and t.get("count", 0) > 0 and t.get("in_buildings", 99) == 0 and t.get("round_you", 0) >= t.get("count", 0) * 9 / 10 \
					and t.has("coming") and t.has("caught") and t.has("up") and t["up"] < 6.0 and t["near_up"] == 0 and not t.has("caught_again") \
					and chest.get("hit", false) and chest.get("zone", "") == "heart and lungs" and not chest.get("down", false) and not chest.get("dies", false) \
					and head.get("hit", false) and head.get("zone", "") == "brain" and head.get("down", false)
				print("Geodrive zombie test: %s" % ("PASS" if ok else "FAIL"))
				get_tree().quit()


func _hunt_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null:
		return
	var t: Dictionary = _hunt_test
	t["time"] += delta
	match int(t["step"]):
		0:
			if not hunt._kiosks.is_empty() and t["time"] > 2.0:
				toggle_walk()
				walker.global_position = hunt._kiosks[0]["node"].global_position + Vector3(2.0, 1.0, 2.0)
				print("Geodrive hunt test: at the kiosk: %s" % ("E buys a ticket" if not hunt.kiosk_near(walker.global_position).is_empty() else "NOT NEAR IT"))
				hunt.open_tickets(hunt.kiosk_near(walker.global_position) if not hunt.kiosk_near(walker.global_position).is_empty() else hunt._kiosks[0])
				var rifle: Dictionary = GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0]
				hunt._buy(hunt._kiosks[0]["reserve"], rifle)
				t["step"] = 1
				t["time"] = 0.0
		1:
			if t["time"] > 1.0:
				var c: Vector2 = hunt._centre(hunt.reserves[hunt.ticket])
				var inside: Vector3 = world.from_lat_lon(c.x, c.y)
				walker.global_position = Vector3(inside.x, world.ground_height(inside) + 1.0, inside.z)
				t["step"] = 2
				t["time"] = 0.0
		2:
			# (with pictures: first a close look at one of each kind on the farm)
			if shot_path != "" and hunt.hunting >= 0 and t["time"] > 3.0 and not t.has("gallery"):
				t["gallery"] = []
				var seen := {}
				for i in hunt.hunter.get_count():
					var sp: String = hunt.hunter.animal(i)["species"]
					if not seen.has(sp):
						seen[sp] = true
						t["gallery"].append(i)
				t["step"] = 10
				t["time"] = 0.0
			elif hunt.hunting >= 0 and t["time"] > 3.0:
				# the nearest animal that's legal now (not protected, in season, the bag not full)
				var month := int(Time.get_date_dict_from_system()["month"])
				var legal := {}
				for sp in GeodriveHunt.species_list():
					legal[sp["id"]] = not sp["protected"] and sp["season"][0] > 0 and month >= sp["season"][0] and month <= sp["season"][1]
				var best := -1
				var nearest := INF
				for i in hunt.hunter.get_count():
					var a: Dictionary = hunt.hunter.animal(i)
					var d: float = (a["position"] as Vector3).distance_to(walker.global_position)
					if legal.get(a["species"], false) and not a["dead"] and d < nearest:
						nearest = d
						best = i
				var a: Dictionary = hunt.hunter.animal(best)
				t["target"] = best
				print("Geodrive hunt test: %d animals; the nearest legal one a %s, %s, %.0f m off" % [hunt.hunter.get_count(), a["name"], a["doing"], nearest])
				# (80 m from it, where nothing's between: the farm's hilly)
				var at: Vector3 = a["position"]
				var from := at + (walker.global_position - at).normalized() * 80.0
				# (broadside first, as a hunter waits for: from either side, then a little round to the front and back)
				var side_way: Vector3 = (a["facing"] as Vector3).cross(Vector3.UP).normalized()
				for k in 16:
					var turn := (PI if k % 2 == 1 else 0.0) + (k / 2 % 2 * 2 - 1) * (k / 4) * PI / 10.0
					var try := at + side_way.rotated(Vector3.UP, turn) * 80.0
					try.y = world.ground_height(try) + 1.7
					var ray := PhysicsRayQueryParameters3D.create(try, at + Vector3.UP * minf(shoulder_of(a["species"]) * 0.66, 0.8))	# (to its chest)
					if get_world_3d().direct_space_state.intersect_ray(ray).is_empty():
						from = try
						break
				from.y = world.ground_height(from)
				walker.global_position = from + Vector3.UP * 0.1
				# (a picture of it, from 10 m off its side, before the shot)
				if shot_path != "":
					camera.start_free()
					var side: Vector3 = (a["facing"] as Vector3).cross(Vector3.UP)
					camera.global_position = at + side * 10.0 + Vector3.UP * 1.6
					camera.global_position.y = world.ground_height(camera.global_position) + 1.6
					camera.look_at(at + Vector3.UP * 0.9, Vector3.UP)
					camera._free_pitch = camera.rotation.x
					camera._free_yaw = camera.rotation.y
				t["step"] = 3
				t["time"] = 0.0
		3:
			if shot_path != "" and absf(t["time"] - 1.5) < delta:
				_save(shot_path.get_basename() + "_herd.png")
			if t["time"] > 2.0:
				if camera.free:
					camera.stop_free()
				var a: Dictionary = hunt.hunter.animal(t["target"])
				# (its heart and lungs: low in the chest, ~two thirds of its shoulder height up)
				var shoulder := 1.0
				for sp in GeodriveHunt.species_list():
					shoulder = sp["shoulder"] if sp["id"] == a["species"] else shoulder
				# (and forward of its middle, behind the front leg)
				var chest: Vector3 = (a["position"] as Vector3) + Vector3.UP * (shoulder * 0.66) + (a["facing"] as Vector3) * (shoulder * 0.3)
				var eye: Vector3 = walker.eyes()
				var result: Dictionary = hunt.shoot("308win", eye, (chest - eye).normalized())
				hunt.weapon.shown_for_test = shot_path != ""
				hunt.weapon._effects(result)	# (heard and seen as a shot from the hands is)
				print("Geodrive hunt test: shot (the %s %.0f m off, %s): %s" % [a["name"], eye.distance_to(chest), a["doing"], hunt.weapon.report(result)])
				t["step"] = 4
				t["time"] = 0.0
		4:
			# (pictures: the rifle kicking, then back at rest)
			if shot_path != "" and t["time"] >= 0.08 and t["time"] - delta < 0.08:
				_save(shot_path.get_basename() + "_kick.png")
			if shot_path != "" and t["time"] >= 1.2 and t["time"] - delta < 1.2:
				_save(shot_path.get_basename() + "_inhand.png")
			# (aiming: the rifle coming up to the eye, then through its scope)
			if shot_path != "" and t["time"] > 2.0 and t["time"] < 3.0:
				hunt.weapon.aiming = true
				if t["time"] >= 2.08 and t["time"] - delta < 2.08:
					_save(shot_path.get_basename() + "_raising.png")
				if t["time"] >= 2.6 and t["time"] - delta < 2.6:
					_save(shot_path.get_basename() + "_scope.png")
			elif shot_path != "" and t["time"] >= 3.0 and t["time"] - delta < 3.0:
				hunt.weapon.aiming = false
			# (the graphics card's time a frame with the animals and without: once settled (15 s), five times each way, 2 s
			# at a time, every frame's averaged; the first half second after each switch left out)
			if t["time"] > 15.0 and t["time"] < 35.0:
				var phase := int((t["time"] - 15.0) / 2.0)
				hunt.hunter.visible = phase % 2 == 0
				if fmod(t["time"] - 15.0, 2.0) > 0.5:
					var key := "with" if phase % 2 == 0 else "without"
					t[key] = t.get(key, 0.0) + RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
					t[key + "_n"] = t.get(key + "_n", 0) + 1
			if t["time"] >= 35.0 and not t.has("told"):
				t["told"] = true
				hunt.hunter.visible = true
				print("Geodrive hunt test: the graphics card %.2f ms a frame with the animals, %.2f without (averaged)" % [t.get("with", 0.0) / maxi(t.get("with_n", 1), 1), t.get("without", 0.0) / maxi(t.get("without_n", 1), 1)])
			# (and the farm's far bushes: the same, with and without)
			if t["time"] > 37.0 and t["time"] < 57.0:
				var phase := int((t["time"] - 37.0) / 2.0)
				vegetation.set_wide(phase % 2 == 0)
				if fmod(t["time"] - 37.0, 2.0) > 0.5:
					var key := "wide" if phase % 2 == 0 else "narrow"
					t[key] = t.get(key, 0.0) + RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
					t[key + "_n"] = t.get(key + "_n", 0) + 1
			if t["time"] >= 57.0 and not t.has("told_wide"):
				t["told_wide"] = true
				print("Geodrive hunt test: the graphics card %.2f ms a frame with the farm's far bushes, %.2f without (averaged)" % [t.get("wide", 0.0) / maxi(t.get("wide_n", 1), 1), t.get("narrow", 0.0) / maxi(t.get("narrow_n", 1), 1)])
			if t["time"] > 58.0:
				var a: Dictionary = hunt.hunter.animal(t["target"])
				print("Geodrive hunt test: 20 s on, the %s is %s; today's bag %s" % [a["name"], "dead" if a["dead"] else "still " + str(a["doing"]), hunt.taken])
				if camera.free:
					camera.stop_free()
				var gate: Vector3 = world.from_lat_lon(hunt.reserves[hunt.hunting]["gates"][0]["lat"], hunt.reserves[hunt.hunting]["gates"][0]["lon"])
				var out: Vector3 = hunt._kiosks[0]["node"].global_position
				walker.global_position = out + Vector3(0.0, 1.0, 0.0)
				t["step"] = 5
				t["time"] = 0.0
		10:
			# (each kind in turn: the camera from its side, 2.5 body lengths off, a picture, the next)
			var shown: int = t.get("shown", 0)
			if shown >= t["gallery"].size():
				camera.stop_free()
				t["step"] = 2
				t["time"] = 3.0
			elif t["time"] > 0.2 and not t.has("placed"):
				var a: Dictionary = hunt.hunter.animal(t["gallery"][shown])
				var length := 2.0
				for sp in GeodriveHunt.species_list():
					length = sp["length"] if sp["id"] == a["species"] else length
				var at: Vector3 = a["position"]
				var side: Vector3 = (a["facing"] as Vector3).cross(Vector3.UP).normalized()
				camera.start_free()
				camera.global_position = at + side * length * 2.5 + (a["facing"] as Vector3) * length * 0.8
				camera.global_position.y = maxf(world.ground_height(camera.global_position), at.y) + length * 0.5
				camera.look_at(at + Vector3.UP * length * 0.35, Vector3.UP)
				camera._free_pitch = camera.rotation.x
				camera._free_yaw = camera.rotation.y
				t["placed"] = true
			elif t["time"] > 0.6:
				_save(shot_path.get_basename() + "_%s.png" % hunt.hunter.animal(t["gallery"][shown])["species"])
				t["shown"] = shown + 1
				t.erase("placed")
				t["time"] = 0.0
		5:
			if t["time"] > 2.0:
				# (out of the gate: the hunt over, the ticket handed back; the animals still out, seen from the kiosk)
				print("Geodrive hunt test: out: hunting %s, the animals %d, a ticket %s; %s" % [hunt.hunting >= 0, hunt.hunter.get_count(), hunt.ticket >= 0,
					"PASS" if hunt.hunting < 0 and hunt.ticket < 0 and hunt.hunter.get_count() > 0 and not hunt.taken.is_empty() else "FAIL"])
				get_tree().quit()


func _car_hit_test_step(delta: float) -> void:
	if waiting != "" or car == null:
		return
	var t: Dictionary = _car_hit
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	if at.call(2.0):
		toggle_walk()
	var forward: Vector3 = -car.global_transform.basis.z
	forward.y = 0.0
	forward = forward.normalized()
	if at.call(3.0):
		# slow: the car at walking pace, you 3.7 m ahead of its middle (a metre off its nose)
		car.freeze = false
		var spot: Vector3 = car.global_position + forward * 3.7
		walker.global_position = Vector3(spot.x, world.ground_height(spot) + 0.05, spot.z)
		walker.velocity = Vector3.ZERO
		t["from"] = walker.global_position
	if t["time"] > 3.0 and t["time"] < 6.0:
		car.linear_velocity = forward * 1.0
	if at.call(6.0):
		t["pushed"] = walker.global_position.distance_to(t["from"])
		t["slow_up"] = not walker.down_wanted
		car.linear_velocity = Vector3.ZERO
	if at.call(7.0):
		# fast: 40 km/h from 15 m off
		var spot: Vector3 = car.global_position + forward * 15.0
		walker.global_position = Vector3(spot.x, world.ground_height(spot) + 0.05, spot.z)
		walker.velocity = Vector3.ZERO
		car.linear_velocity = forward * (40.0 / 3.6)
	if t["time"] > 7.0 and walker.down_wanted and not t.has("down_at"):
		t["down_at"] = t["time"]
		t["car_after"] = car.linear_velocity.length() * 3.6
		t["line"] = hud.status_label.text
		car.linear_velocity = Vector3.ZERO
	if t.has("down_at") and not walker.down_wanted and not t.has("up"):
		t["up"] = walker.global_position.distance_to(car.global_position)
	if t.has("up") or Time.get_ticks_msec() > 240000 or (t["time"] > 12.0 and not t.has("down_at")):
		print("Geodrive car hit test: at 3.6 km/h pushed %.1f m, still up %s; at 40 km/h down %s (the car %.0f km/h after: \"%s\"); up again %s m from the car" % [
			t.get("pushed", 0.0), t.get("slow_up", false), t.has("down_at"), t.get("car_after", 0.0), t.get("line", ""), str(snappedf(t.get("up", -1.0), 0.1))])
		var ok: bool = t.get("pushed", 0.0) > 0.5 and t.get("slow_up", false) and t.has("down_at") and t.get("car_after", 99.0) < 40.0 and t.has("up") and t["up"] < 5.0
		print("Geodrive car hit test: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


func _share_test_step(delta: float) -> void:
	if waiting != "" or car == null or builder == null or hunt == null:
		return
	var t: Dictionary = _share
	var props: PropKeeper = builder.props
	if share_test == "host":
		if not net.is_host():
			return
		t["time"] += delta
		var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
		if at.call(1.0):
			# a crate, made and put 6 m ahead
			var shop := Workshop.new(builder)
			builder.workshop = shop
			shop.thing = "prop"
			shop.new_prop()
			shop.prop["name"] = "Share crate"
			shop.prop["look"]["parts"][0]["size"] = [1.0, 1.0, 1.0]
			shop.prop["look"]["parts"][0]["at"] = [0.0, 0.5, 0.0]
			var id := shop.save_prop()
			var spot: Vector3 = car.global_position - car.global_transform.basis.z * 6.0
			props.chosen = id
			props._aim = {"at": Vector3(spot.x, world.ground_height(spot), spot.z), "yaw": 0.0, "ok": true}
			props.put()
			props.stop()
			# a race (two checkpoints ahead), and a gun
			var maker: GameMaker = builder.game_maker
			maker.new_game()
			maker.game["name"] = "Share race"
			for d in [30.0, 90.0]:
				maker.add_checkpoint(car.global_position - car.global_transform.basis.z * d)
			maker.game["weapons"] = ["none"]
			maker.save()
			var gun := Workshop.new(builder)
			gun.draft["name"] = "Share gun"
			gun.save()
		if t["time"] > 1.0 and props.placed.size() >= 2 and not t.has("got"):
			t["got"] = t["time"]
			print("Geodrive share test: host: the joiner's crate put here too (%d on the map)" % props.placed.size())
		if (t.has("got") and multiplayer.get_peers().is_empty()) or Time.get_ticks_msec() > 300000:	# (5 minutes by the clock: a headless host's game time runs ahead of it)
			print("Geodrive share test: host: %s" % ("PASS" if t.has("got") else "FAIL"))
			get_tree().quit()
		return
	# the joiner: once it's beside the host, what came
	if (not net.is_client() or net._join_pending) and not t.has("both"):
		return	# (till it's left on purpose)
	t["time"] += delta
	var at := func(seconds: float) -> bool: return t["time"] >= seconds and t["time"] - delta < seconds
	if at.call(8.0):
		var race: Array = hunt.reserves.filter(func(r): return r.get("id", "") == "shared_my_share_race")
		t["props"] = props.sharing and props.kinds.has("my_share_crate") and props.placed.size() == 1 and props._shown.size() == 1
		t["race"] = race.size() == 1 and race[0].get("checkpoints", []).size() == 2
		t["booth"] = hunt.booths().any(func(b): return hunt.reserves[b["area"]].get("id", "") == "shared_my_share_race")
		t["gun"] = GeodriveHunt.loads().any(func(l): return l["id"] == "my_share_gun")
		var node: Node3D = props._shown.values()[0] if not props._shown.is_empty() else null
		t["crate_above"] = (node.global_position.y - world.ground_height(node.global_position)) if node else -99.0
		print("Geodrive share test: join: the host's crate %s (%.2f m above the ground), its race %s (booth %s), its gun %s" % [t["props"], t["crate_above"], t["race"], t["booth"], t["gun"]])
		# a crate of its own, 4 m to the side: the host puts it
		var spot: Vector3 = car.global_position + car.global_transform.basis.x * 4.0
		props.chosen = "my_share_crate"
		props._aim = {"at": Vector3(spot.x, world.ground_height(spot), spot.z), "yaw": 0.5, "ok": true}
		props.put()
		props.stop()
	if at.call(11.0):
		t["both"] = props.placed.size() == 2 and props._shown.size() == 2
		t["kept"] = not FileAccess.file_exists(props._path())	# (the host's: not kept here)
		net.leave()
	if at.call(12.0):
		var own_back: bool = not props.sharing and props.placed.is_empty() and not hunt.reserves.any(func(r): return r.get("shared", false))
		print("Geodrive share test: join: its crate on the map %s (not kept here %s); out again, its own back %s" % [t["both"], t["kept"], own_back])
		var ok: bool = t["props"] and t["race"] and t["booth"] and t["gun"] and absf(t["crate_above"]) < 0.3 and t["both"] and t["kept"] and own_back
		print("Geodrive share test: join: %s" % ("PASS" if ok else "FAIL"))
		get_tree().quit()


# --mptest: two players (a host and a joiner, both here) at the farm: tickets, in, side by side 30 m apart; the joiner
# shoots the host in the chest; the host goes down (and gets up by their car), hears the shot; both see the same animals
func _mp_test_step(delta: float) -> void:
	if waiting != "" or car == null or hunt == null:
		return
	var t: Dictionary = _mp_test
	t["time"] += delta
	var joiner := mp_test == "join"
	match int(t["step"]):
		0:
			if not hunt._kiosks.is_empty() and t["time"] > 4.0 and (net.is_host() or (net.is_client() and not net._join_pending)):
				net.player_name = "Joiner" if joiner else "Host"
				toggle_walk()
				walker.global_position = hunt._kiosks[0]["node"].global_position + Vector3(2.0, 1.0, 2.0)
				var rifle: Dictionary = GeodriveHunt.loads().filter(func(l): return l["id"] == "308win")[0]
				hunt._buy(hunt._kiosks[0]["reserve"], rifle)
				t["step"] = 1
				t["time"] = 0.0
		1:
			if t["time"] > 1.0:
				var c: Vector2 = hunt._centre(hunt.reserves[hunt.ticket])
				var spot: Vector3 = world.from_lat_lon(c.x, c.y) + Vector3(30.0 if joiner else 0.0, 0.0, 0.0)
				walker.global_position = Vector3(spot.x, world.ground_height(spot) + 1.0, spot.z)
				t["step"] = 2
				t["time"] = 0.0
		2:
			if int(t["time"]) != int(t["time"] - delta):
				print("Geodrive mp test: %s: %.0f s: my feet %.2f, the ground %.2f, on the floor %s, held %s, solid %s" % [mp_test, t["time"], walker.global_position.y, world.ground_height(walker.global_position), walker.is_on_floor(), walker.held, world.is_solid_at(walker.global_position)])
			if t["time"] > 8.0:
				# (both: the first animal, where each sees it)
				if hunt.hunter.get_count() > 0:
					var a: Dictionary = hunt.hunter.animal(0)
					var at: PackedFloat64Array = world.to_lat_lon(a["position"])
					print("Geodrive mp test: %s: hunting %s, %d animals, following the host's %s; the first a %s at %.6f,%.6f" % [mp_test, hunt.hunting >= 0, hunt.hunter.get_count(), hunt.hunter.is_follower(), a["name"], at[0], at[1]])
				if joiner:
					if not net._people.has(1):
						print("Geodrive mp test: join: the host's body isn't here: FAIL")
					else:
						var host: Node3D = net._people[1]["node"]
						print("Geodrive mp test: join: the host's body %d m off, %s" % [roundi(host.global_position.distance_to(walker.global_position)), "seen" if host.visible else "hidden"])
						var chest: Vector3 = host.global_position + Vector3.UP * (host.height_cm / 100.0) * 0.72
						var dir: Vector3 = (chest - walker.eyes()).normalized()
						walker.rotation.y = atan2(-dir.x, -dir.z)
						walker.pitch = asin(dir.y)
				t["step"] = 3
				t["time"] = 0.0
		3:
			if joiner and t["time"] > 0.3 and not t.has("fired"):
				t["fired"] = true
				if net._people.has(1):
					var host: Node3D = net._people[1]["node"]
					print("Geodrive mp test: join: aiming from %s along %s; the host's chest at %s (its feet %s, ground there %.2f); my eyes %s" % [camera.global_position, -camera.global_transform.basis.z,
						host.global_position + Vector3.UP * (host.height_cm / 100.0) * 0.72, host.global_position, world.ground_height(host.global_position), walker.eyes()])
				hunt.weapon._fire()
			if not joiner and walker.down_wanted and not t.has("down"):
				t["down"] = true
				print("Geodrive mp test: host: down (shot)")
			if not joiner and t.has("down") and not walker.down_wanted and not t.has("up"):
				t["up"] = true
				print("Geodrive mp test: host: up again, %d m from the car" % roundi(walker.global_position.distance_to(car.global_position)))
			# (the host: till it's shot and up again, or 5 minutes by the clock: a headless game's time runs ahead of it)
			if t["time"] > 14.0 and (joiner or t.has("up") or Time.get_ticks_msec() > 300000):
				if not joiner:
					print("Geodrive mp test: host: %s" % ("PASS" if t.has("down") and t.has("up") else "FAIL"))
				get_tree().quit()


# --worldmaptest: the map opened over the whole world, then zoomed in on Cape Town 30 steps (a step a frame), out again,
# each frame's time kept (the slowest told); a part of the world chosen as a click would
func _world_map_test_step(delta: float) -> void:
	var t: Dictionary = _wm
	t["frames"] += 1
	match int(t["step"]):
		0:
			if t["frames"] > 30:
				open_world_map()
				get_tree().paused = false	# (the test goes on: the map pauses the game behind it)
				world_map.centre_lat = 0.0
				world_map.centre_lon = 15.0
				world_map.pixels_per_degree = 4.5
				world_map._changed()
				t["step"] = 1
				t["frames"] = 0
		1:
			if t["frames"] == 90 and shot_path != "":
				_save(shot_path.get_basename() + "_world.png")
			if t["frames"] > 100:
				t["step"] = 2
				t["frames"] = 0
		2, 3:
			# (a step a frame: in towards Cape Town, then out)
			if t["frames"] > 1:
				t["worst"] = maxf(t["worst"], delta * 1000.0)
			var factor := 1.3 if int(t["step"]) == 2 else 1.0 / 1.3
			world_map.pixels_per_degree = clampf(world_map.pixels_per_degree * factor, 1.5, 60000.0)
			world_map.centre_lat = lerpf(world_map.centre_lat, -33.92, 0.15)
			world_map.centre_lon = lerpf(world_map.centre_lon, 18.42, 0.15)
			world_map._changed()
			if shot_path != "" and int(t["step"]) == 2 and t["frames"] in [20, 24, 28]:
				_save(shot_path.get_basename() + "_zooming_%d.png" % t["frames"])	# (mid-zoom: the roads and labels not drawn again yet)
			if t["frames"] >= 30:
				t["step"] += 1
				t["frames"] = 0
				if int(t["step"]) == 3:
					print("Geodrive world map test: zoomed in to %.0f px a degree; slowest frame %.1f ms" % [world_map.pixels_per_degree, t["worst"]])
					t["zoomed_worst"] = t["worst"]
				else:
					print("Geodrive world map test: out again; slowest frame %.1f ms; its parts' slowest %s" % [t["worst"], world_map.timings])
		4:
			# Cape Town close, then the Western Cape: pictures
			if t["frames"] == 1:
				world_map.centre_lat = -33.95
				world_map.centre_lon = 18.6
				world_map.pixels_per_degree = 900.0
				world_map._changed()
			if t["frames"] == 60 and shot_path != "":
				_save(shot_path.get_basename() + "_town.png")
			if t["frames"] == 61:
				world_map.centre_lat = -32.0
				world_map.centre_lon = 22.0
				world_map.pixels_per_degree = 90.0
				world_map._changed()
			if t["frames"] == 120:
				# (a part of the world chosen: somewhere in Germany, as a click there would)
				world_map.centre_lat = 50.0
				world_map.centre_lon = 10.0
				world_map.pixels_per_degree = 40.0
				world_map._changed()
				world_map._chosen_at = Vector2(48.137, 11.575)
				world_map._chosen = world_map._extract_at(48.137, 11.575)
				world_map._show_choice()
				world_map._canvas.queue_redraw()
				print("Geodrive world map test: %d parts of the world; Munich's: %s" % [world_map._extracts.size(), world_map._extracts[world_map._chosen]["name"] if world_map._chosen >= 0 else "none"])
			if t["frames"] == 180 and shot_path != "":
				_save(shot_path.get_basename() + "_germany.png")
			if t["frames"] == 181:
				# dragged from Africa up to Norway as a mouse would: the map's scale mustn't change as it goes
				world_map.centre_lat = 0.0
				world_map.centre_lon = 15.0
				world_map.pixels_per_degree = 20.0
				var before: float = world_map._to_screen(0.0, 25.0).x - world_map._to_screen(0.0, 15.0).x
				var press := InputEventMouseButton.new()
				press.button_index = MOUSE_BUTTON_LEFT
				press.pressed = true
				press.position = Vector2(400, 100)
				world_map._map_input(press)
				for i in 60:
					var move := InputEventMouseMotion.new()
					move.position = Vector2(400, 100 + (i + 1) * 20)
					world_map._map_input(move)
				var release := press.duplicate()
				release.pressed = false
				release.position = Vector2(400, 1300)
				world_map._map_input(release)
				var after: float = world_map._to_screen(world_map.centre_lat, 25.0).x - world_map._to_screen(world_map.centre_lat, 15.0).x
				t["drag_ok"] = absf(after - before) < 0.01 and world_map.centre_lat > 50.0
				print("Geodrive world map test: dragged to %.1f N; 10 degrees across %.1f px before, %.1f after" % [world_map.centre_lat, before, after])
			if t["frames"] == 185 and shot_path != "":
				_save(shot_path.get_basename() + "_dragged.png")
			if t["frames"] > 190:
				print("Geodrive world map test: %s" % ("PASS" if t.get("zoomed_worst", 99.0) < 34.0 and t["worst"] < 34.0 and t.get("drag_ok", false) else "FAIL"))
				get_tree().quit()


func _crash_test_step(delta: float) -> void:
	var t: Dictionary = _crash
	t["time"] += delta
	var plane: Node3D = aviation.aircraft
	match int(t["step"]):
		0:
			if t["time"] > 3.0:
				# (60 m up, 70 m/s, nose hard down)
				var heading: float = plane.readout()["heading"]
				plane.place(aviation.start["ground"] + Vector3.UP * 60.0, heading, 70.0)
				aviation.force_controls = {"throttle": 1.0, "pitch": -1.0, "gear": true}
				t["step"] = 1
				t["time"] = 0.0
		1:
			if plane.is_crashed():
				t["at"] = plane.global_position
				print("Geodrive crash test: crashed: %s" % plane.readout()["crash"])
				t["step"] = 2
				t["time"] = 0.0
			elif t["time"] > 20.0:
				print("Geodrive crash test: never crashed: FAIL")
				get_tree().quit()
		2:
			if t["time"] > 6.0:
				var moved := plane.global_position.distance_to(t["at"])
				print("Geodrive crash test: 6 s on, still crashed %s, the wreck %.1f m from where it hit (no runway)" % [plane.is_crashed(), moved])
				t["kept"] = plane.is_crashed() and moved < 2.0
				aviation.force_controls = {"throttle": 0.0, "brakes": 1.0, "gear": true}
				aviation.key(KEY_R)
				t["step"] = 3
				t["time"] = 0.0
		3:
			if t["time"] > 2.0:
				var r: Dictionary = plane.readout()
				print("Geodrive crash test: R: crashed %s, on the ground %s, %.1f m from the wreck" % [plane.is_crashed(), r["on_ground"], plane.global_position.distance_to(t["at"])])
				t["recovered"] = not plane.is_crashed() and r["on_ground"]
				toggle_walk()
				t["step"] = 4
				t["time"] = 0.0
		_:
			pass


func _crash_test_walk(delta: float) -> void:
	var t: Dictionary = _crash
	t["time"] += delta
	if int(t["step"]) == 4 and t["time"] > 1.0:
		car_here()
		t["step"] = 5
		t["time"] = 0.0
	elif int(t["step"]) == 5 and t["time"] > 1.0:
		var gap := car.global_position.distance_to(walker.global_position) if car and walker else INF
		print("Geodrive crash test: out on foot %s; V: the car %.1f m away, the aircraft still whole %s; %s" % [walker != null, gap, not aviation.aircraft.is_crashed(), "PASS" if t.get("kept", false) and t.get("recovered", false) and gap < 25.0 and not aviation.aircraft.is_crashed() else "FAIL"])
		get_tree().quit()


static func shoulder_of(species: String) -> float:
	for sp in GeodriveHunt.species_list():
		if sp["id"] == species:
			return sp["shoulder"]
	return 1.0


# --arrivetest: once the start's in, on foot by the car? Then a teleport 800 m along the area: on foot by it there?
func _stand_test_step(delta: float) -> void:
	if waiting != "" or car == null or aviation == null:
		return
	_stand_time += delta
	if _stand_step == -1 and walker and _stand_time > 1.0:
		# (Go somewhere > Cape Town's airport, from wherever the game started)
		_go_to_airport(aviation.search("FACT")[0]["airport"])
		_stand_step = 0
		_stand_time = 0.0
	elif _stand_step == 0 and walker and not aviation._stands.is_empty() and _stand_time > 2.0:
		var stand: Dictionary = aviation._stands[0]
		var at: Vector3 = stand["node"].global_position
		print("Geodrive stand test: arrived %.0f m from the stand at %s (the car %.0f m off)" % [walker.global_position.distance_to(at), aviation.label(stand["airport"]), walker.global_position.distance_to(car.global_position)])
		walker.global_position = at + (walker.global_position - at).normalized() * 4.0 + Vector3.UP * 0.5
		_stand_step = 1
		_stand_time = 0.0
	elif _stand_step == 1 and _stand_time > 2.0:
		var near: Dictionary = aviation.stand_near(walker.global_position)
		print("Geodrive stand test: at the stand: %s" % ("E chooses a plane" if not near.is_empty() else "NOT NEAR IT"))
		if shot_path != "":
			_save(shot_path.get_basename() + "_stand.png")
		aviation.open_chooser(near["airport"] if not near.is_empty() else aviation._stands[0]["airport"])
		aviation._choose(2)
		_stand_step = 2
		_stand_time = 0.0
	elif _stand_step == 2 and (aviation.flying or _stand_time > 30.0):
		print("Geodrive stand test: %s" % ("flying the %s at %s; on foot: %s; PASS" % [aviation.KINDS[aviation.kind].to_lower(), aviation.label(aviation.start["airport"]), walker != null] if aviation.flying else "never in the plane: FAIL"))
		_stand_step = 3
		_stand_time = 0.0
	elif _stand_step == 3 and _stand_time > 4.0:
		if shot_path != "":
			_save(shot_path.get_basename() + "_flying.png")
		get_tree().quit()


func _arrive_test_step() -> void:
	if stand_test:
		_stand_test_step(get_process_delta_time())
		return
	if not arrive_test or waiting != "" or car == null:
		return
	_arrive_frame += 1
	var report := func(when: String) -> void:
		var gap: float = walker.global_position.distance_to(car.global_position) if walker else -1.0
		print("Geodrive arrive test %s: %s, %.1f m from the car, the car %s" % [when, "on foot" if walker else "IN THE CAR", gap, "parked" if car.freeze else "free"])
	if _arrive_frame == 60:
		report.call("at the start")
		var here: PackedFloat64Array = world.to_lat_lon(car.global_position + Vector3(800.0, 0.0, 0.0))
		teleport(here[0], here[1], "800 m east")
	elif _arrive_frame == 240:
		report.call("after the teleport")
		get_tree().quit()


# Whether a plant's drawn anywhere (any ring)
func _plant_drawn(channel: int, fx: int, fz: int) -> bool:
	for spec in vegetation._specs:
		if int(spec["channel"]) != channel:
			continue
		for node in spec["nodes"].values():
			var buffer: PackedFloat32Array = node.multimesh.buffer
			for k in node.multimesh.instance_count:
				if int(buffer[k * 16 + 14]) == fx and int(buffer[k * 16 + 15]) == fz:
					return true
	return false


func _build_trees_step() -> void:
	var t: Dictionary = _trees_test
	t["frame"] = t.get("frame", 0) + 1
	if t["frame"] == 240:
		# the nearest tree drawn, aimed at from 6 m off, at a man's eyes
		toggle_walk()
		var best := {}
		var nearest := INF
		for spec in vegetation._specs:
			if int(spec["channel"]) != 0:
				continue
			for node in spec["nodes"].values():
				var buffer: PackedFloat32Array = node.multimesh.buffer
				for k in node.multimesh.instance_count:
					var at: Vector3 = node.position + Vector3(buffer[k * 16 + 3], buffer[k * 16 + 7], buffer[k * 16 + 11])
					var d := at.distance_to(car.global_position)
					if d < nearest:
						nearest = d
						best = {"at": at, "fx": int(buffer[k * 16 + 14]), "fz": int(buffer[k * 16 + 15])}
		t["tree"] = best
		var from: Vector3 = best["at"] + Vector3(6.0, 1.6, 0.0)
		var plant: Dictionary = vegetation.plant_along(from, best["at"] + Vector3.UP * 1.6 - from, 14.0)
		t["picked"] = plant.get("fx", -1) == best["fx"] and plant.get("fz", -1) == best["fz"]
		print("Geodrive build test: a tree %.0f m from the car; aimed at from 6 m: %s" % [nearest, "picked" if t["picked"] else "NOT PICKED"])
		if t["picked"]:
			builder._cut(plant)	# (the first right click: outlined)
			builder._cut(plant)	# (the second: cut down)
	if t["frame"] == 300:
		t["gone"] = not _plant_drawn(0, t["tree"]["fx"], t["tree"]["fz"])
		t["kept"] = builder._cut_list.size() == 1 and FileAccess.file_exists(builder._cut_path())
		print("Geodrive build test: a second later the tree's %s; kept in the file: %s" % ["gone" if t["gone"] else "STILL THERE", t["kept"]])
		# (and from its lat, lon in the file, as a new game would: found in the same square)
		world.set_removed_plants([])
		builder._load_cut()
	if t["frame"] == 360:
		t["from_file"] = not _plant_drawn(0, t["tree"]["fx"], t["tree"]["fz"])
		print("Geodrive build test: from the file again: the tree's %s" % ("gone" if t["from_file"] else "BACK"))
		builder.bring_back()
	if t["frame"] == 420:
		var back := _plant_drawn(0, t["tree"]["fx"], t["tree"]["fz"])
		print("Geodrive build test: brought back: the tree's %s" % ("there" if back else "STILL GONE"))
		print("Geodrive build test: %s" % ("PASS" if t["picked"] and t["gone"] and t["kept"] and t["from_file"] and back else "FAIL"))
		get_tree().quit()


func _build_remove_step() -> void:
	_hands_frame += 1
	var space := get_world_3d().direct_space_state
	var drop := func(at: Vector3) -> float:
		var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(at + Vector3.UP * 200.0, at + Vector3.DOWN * 200.0))
		return (hit["position"] as Vector3).y if not hit.is_empty() else NAN
	if _hands_frame == 120:
		var best := 1.0e9
		for i in range(-30, 31):
			for j in range(-30, 31):
				var at := car.global_position + Vector3(i * 4.0, 0.0, j * 4.0)
				var found: Dictionary = world.building_at(at)
				if found.get("found", false) and float(found["height"]) > 4.0 and at.distance_to(car.global_position) < best:
					best = at.distance_to(car.global_position)
					_remove_test = {"id": found["id"], "at": at, "height": found["height"]}
		if _remove_test.is_empty():
			print("Geodrive remove test: no building of the map's within 120 m")
			get_tree().quit()
			return
		_remove_test["roof"] = drop.call(_remove_test["at"])
		print("Geodrive remove test: building %d, %.0f m off, %.1f m high; the ray down meets %.1f m" % [_remove_test["id"], best, _remove_test["height"], _remove_test["roof"]])
		world.remove_building(_remove_test["id"])
	if _hands_frame == 420:
		var now: float = drop.call(_remove_test["at"])
		var gone: bool = not world.building_at(_remove_test["at"]).get("found", false) or world.building_at(_remove_test["at"])["id"] != _remove_test["id"]
		print("Geodrive remove test: after: the ray meets %.1f m (%.1f m lower); found there now: %s; taken away: %s" % [now, _remove_test["roof"] - now, "no" if gone else "yes", world.get_removed_buildings()])
		print("Geodrive remove test: %s" % ("PASS" if gone and _remove_test["roof"] - now > 2.0 else "FAIL"))
		get_tree().quit()


func _build_hands_step() -> void:
	_hands_frame += 1
	match _hands_frame:
		120:
			toggle_walk()
		200:
			builder.toggle()
			camera.mouse_moved(Vector2(0.0, 250.0))	# (looking down ahead)
		260:
			print("Geodrive build hands: aiming at %s: %s (ground %s)" % [builder._aim.get("slot", "nothing"), builder._aim.get("why", ""), builder._aim.get("ground", "")])
			builder._put()
			print("Geodrive build hands: buildings now %d" % builder._root.get_child_count())
		320:
			builder.kind = 2
			builder.size = 2	# (half wide)
		360:
			print("Geodrive build hands: a half-wide wall at %s: %s" % [builder._aim.get("slot", "nothing"), builder._aim.get("why", "")])
			builder._put()
			var b: Node = builder._root.get_child(0) if builder._root.get_child_count() > 0 else null
			print("Geodrive build hands: the building has %d pieces: %s" % [b.get_piece_count() if b else 0, b.get_pieces() if b else []])
			builder._show_menu(true)	# (Q: the build menu, on the walls' page)
			print("Geodrive build hands: the menu is %s: %s" % ["open" if builder.menu_open() else "shut", builder._piece_name()])
		390:
			if shot_path != "":
				_save(shot_path.get_basename() + "_menu.png")
		400:
			get_tree().quit()


# --walktest: out of the car, walking ahead for 6 s, running for 3 (a jump), sprinting for 2, then back to the car and in
func _walk_test_step(delta: float) -> void:
	if not walk_test:
		return
	walk_test_time += delta
	walker.set_physics_process(false)	# (the test walks them, not the keys)
	var t := walk_test_time
	if t < 1.0:
		walker.steer(0.0, 0.0, false, false, delta)
	elif t < 7.0:
		walker.steer(1.0, 0.0, false, false, delta)
	elif t < 10.0:
		walker.steer(1.0, 0.0, true, t < 7.1 + delta and t >= 7.1, delta)
	elif t < 12.0:
		walker.steer(1.0, 0.0, false, false, delta, true)
	elif t < 13.0:
		walker.steer(0.0, 0.0, false, false, delta)
	if walk_test_marks.size() > 0 and t >= walk_test_marks[0]:
		walk_test_marks.pop_front()
		var from_car: float = walker.global_position.distance_to(car.global_position)
		var level: float = walker.ground_speed
		var ground: float = world.ground_height(walker.global_position) if world else 0.0
		print("Geodrive walk test at %.1f s: %.1f m from the car, %.1f km/h, %s, feet %.2f m over the ground" % [t, from_car, level * 3.6, "on the ground" if walker.is_on_floor() else "in the air", walker.global_position.y - ground])
		if shot_path != "" and (t >= 4.0 and t < 4.1 or t >= 9.5 and t < 9.6):	# (walking, running)
			_save(shot_path.get_basename() + ("_walking.png" if t < 5.0 else "_running.png"))
	if t >= 13.0:
		walker.global_position = car.global_position + car.global_transform.basis.x * 1.7
		toggle_walk()
		print("Geodrive walk test: back in the car: %s" % ("yes" if walker == null else "no"))
		walk_test = false
		if shot_path == "":
			get_tree().quit()


# --- putting the car somewhere ---

# On the road nearest a point, facing along it the way closest to Facing
# (as the Unreal game's PlaceCar: the nearest road by distance along the ground, on its centreline 60 cm up, facing the
# way along it nearest Facing; then, if the ground and road there aren't solid yet, the car waits for them)
func place_car(near: Vector3, facing: Vector3, message: String) -> void:
	var road: Dictionary = world.nearest_road(near, 20000.0) if world else {}
	var at := near
	var along := Vector3(facing.x, 0.0, facing.z)
	if road.get("found", false):
		at = road["position"]
		var direction: Vector3 = road["direction"]
		along = direction if direction.dot(along) >= 0.0 else -direction
	if along.length() < 0.01:
		along = Vector3.FORWARD
	_put_car(Transform3D(Basis.looking_at(along.normalized(), Vector3.UP), at + Vector3.UP * 0.6))
	last_position = car.global_position
	hud.set_status(message, 3.0)
	if world and not world.is_solid_at(at):
		car.freeze = true
		waiting = "settle"
		hold_check = at
		hold_until = Time.get_ticks_msec() / 1000.0 + 15.0


# Back from the free camera to the car (F, or the car put somewhere by a teleport, a new area, a new car): the car
# is let go and the mouse looks round again
func _leave_free_camera() -> void:
	if not camera.free or car == null:
		return	# (with no car, the free camera is all there is)
	camera.stop_free()
	if car:
		car.freeze = false
	if not testing:
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


# R: onto the nearest road, facing the way the car faces (the Unreal game's RecoverCar)
func back_on_road() -> void:
	place_car(car.global_position, -car.global_transform.basis.z, "Back on the road")


# Upright where it is, half a metre up, facing the way it was
func reset_in_place() -> void:
	var forward := -car.global_transform.basis.z
	forward.y = 0.0
	if forward.length() < 0.1:
		forward = Vector3.FORWARD
	_put_car(Transform3D(Basis.looking_at(forward.normalized(), Vector3.UP), car.global_position + Vector3.UP * 0.5))


# On foot, V (or the menu): your car brought to you, on the ground a few metres ahead and to your right, facing the way
# you face, wherever you are (off a road too: a farm's track, beside a wrecked aircraft); Enter to get in
func car_here() -> void:
	if walker == null or world == null:
		return
	var forward := -walker.global_transform.basis.z
	forward = Vector3(forward.x, 0.0, forward.z).normalized()
	var right := Vector3(-forward.z, 0.0, forward.x)
	var at := walker.global_position + forward * 4.0 + right * 2.5
	# (clear of an aircraft by you: its wings reach well past its door)
	if aviation and aviation.aircraft:
		var plane: Vector3 = aviation.aircraft.global_position
		var clear: float = aviation.aircraft.get_chase_distance() * 0.6 + 4.0
		var away := Vector3(at.x - plane.x, 0.0, at.z - plane.z)
		if away.length() < clear:
			at = plane + (away.normalized() if away.length() > 0.1 else right) * clear
	at.y = world.ground_height(at) + 0.6
	var place := Transform3D(Basis.looking_at(forward, Vector3.UP), at)
	if car == null:
		_make_car(place)
	else:
		knock_skip = 3
		car.global_transform = place
		car.linear_velocity = Vector3.ZERO
		car.angular_velocity = Vector3.ZERO
		car.reset_car()
	car.freeze = true	# (waits, held still, till you get in)
	hud.set_status("Your car's here: Enter to get in", 3.0)


# The phone's Cars (and V): the car you pick, waiting for you parked on the road nearest you (within 60 m, at its
# side nearest you), marked; with no road that near, beside you. Driving or flying, you're simply in it.
func park_car(body: int) -> void:
	if walker == null or world == null:
		_choose_car(body)
		return
	if car == null or car.body != body:
		settings.car_body = body
		settings.save_quality("CarBody", body)
		if car:
			var old := car
			remove_child(old)
			old.queue_free()
			car = null
		_make_car(walker.global_transform)
		if wheel:
			wheel.set_car(car)
	var road: Dictionary = world.nearest_road(walker.global_position, 60.0)
	if not road.get("found", false):
		car_here()
	else:
		var direction: Vector3 = road["direction"]
		var middle: Vector3 = road["position"]
		var side := Vector3(-direction.z, 0.0, direction.x).normalized()
		var to_you := walker.global_position - middle
		side = side if side.dot(to_you) >= 0.0 else -side
		var at := middle + side * minf(2.2, Vector2(to_you.x, to_you.z).length())	# (its side of the road, the kerb's)
		at.y = maxf(middle.y, world.ground_height(at)) + 0.6
		var along := direction if direction.dot(-walker.global_transform.basis.z) >= 0.0 else -direction
		knock_skip = 3
		car.global_transform = Transform3D(Basis.looking_at(along.normalized(), Vector3.UP), at)
		car.linear_velocity = Vector3.ZERO
		car.angular_velocity = Vector3.ZERO
		car.reset_car()
		car.freeze = true	# (waits, held still, till you get in)
		hud.set_status("Your %s is parked %d m away, on %s: walk to it, Enter to get in" % [car.get_car_name(),
			roundi(walker.global_position.distance_to(at)), road["name"] if str(road["name"]) != "" else "the road"], 5.0)
	_mark_car()


# A marker over the parked car (seen through things, its size the same however far), gone once you're at it or in it
var _car_mark: Label3D

func _mark_car() -> void:
	if is_instance_valid(_car_mark):
		_car_mark.queue_free()
	_car_mark = Label3D.new()
	_car_mark.text = "Your car
▼"
	_car_mark.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	_car_mark.no_depth_test = true
	_car_mark.fixed_size = true
	_car_mark.pixel_size = 0.0012
	_car_mark.font_size = 28
	_car_mark.outline_size = 8
	_car_mark.modulate = Color(1.0, 0.55, 0.15)
	_car_mark.position = Vector3.UP * 2.6
	car.add_child(_car_mark)


func _put_car(at: Transform3D) -> void:
	if walker:
		_stop_walking()	# (the car moved somewhere: its driver goes with it)
	knock_skip = 3	# (put down: the speed it loses isn't a knock)
	car.global_transform = at
	car.linear_velocity = Vector3.ZERO
	car.angular_velocity = Vector3.ZERO
	car.reset_car()


# Teleport: in the built area the car goes to the road nearest the place; further, a new area is built round it first
func teleport(lat: float, lon: float, place_name: String) -> void:
	if world == null or waiting != "":
		return
	_leave_free_camera()
	if not world.contains_lat_lon(lat, lon):
		_start_area(lat, lon, place_name)
		return
	arrive_on_foot = not testing or arrive_test
	var facing := -car.global_transform.basis.z if car else Vector3.FORWARD
	var target: Vector3 = world.from_lat_lon(lat, lon)
	var from := car.global_position if car else camera.global_position
	if from.distance_to(target) > 1500.0:
		# far across the built area: the car waits while the ground there comes in
		waiting = "hop"
		hold_lat = lat
		hold_lon = lon
		hold_name = place_name
		hold_facing = facing
		hold_have_target = false
		if car:
			car.freeze = true
		hud.set_status("Going to %s..." % place_name, 30.0)
	elif car:
		place_car(target, facing, "At " + place_name)
	else:
		camera.position = target + Vector3.UP * 30.0
		hud.set_status("At " + place_name, 3.0)
	trip_km = 0.0


func _save_spot(spot_name: String) -> void:
	if world == null or car == null:
		return
	var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
	UnrealSettings.save_spot(spot_name, here[0], here[1], menu.areas)
	menu.spots = UnrealSettings.spots()
	hud.set_status("Spot saved", 2.0)


# B: the hardest jolt of the last 5 s, a picture and a line in Saved/RoadReports/spots.txt (as the Unreal game does)
func report_spot() -> void:
	if world == null:
		return
	var now := Time.get_ticks_msec() / 1000.0
	var jolt := 0.0
	var jolt_at := car.global_position
	for i in range(1, ride.size()):
		var step: float = ride[i][0] - ride[i - 1][0]
		if ride[i][0] < now - 5.0 or step <= 0.0001:
			continue
		var acceleration: float = abs(ride[i][1] - ride[i - 1][1]) / step
		if acceleration > jolt:
			jolt = acceleration
			jolt_at = ride[i][2]
	var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
	var there: PackedFloat64Array = world.to_lat_lon(jolt_at)
	var road: Dictionary = world.nearest_road(jolt_at, 25.0)
	var road_text := "%s (%s)" % [road["name"] if road["name"] != "" else "unnamed", road["highway"]] if road.get("found", false) else "no road nearby"
	var forward := -car.global_transform.basis.z
	var heading := fposmod(rad_to_deg(atan2(forward.x, -forward.z)), 360.0)
	var folder := GameSettings.saved_dir().path_join("RoadReports")
	DirAccess.make_dir_recursive_absolute(folder)
	var list := folder.path_join("spots.txt")
	var existing := FileAccess.get_file_as_string(list).strip_edges()
	var number := (existing.count("\n") + 1 if existing != "" else 0) + 1
	get_viewport().get_texture().get_image().save_png(folder.path_join("Spot_%03d.png" % number))
	var line := "Spot %d | %s | here %.6f,%.6f | jolt %.6f,%.6f (%.1f g) | %s | %.0f km/h | heading %.0f (Godot)" % [number,
		Time.get_datetime_string_from_system(false, true), here[0], here[1], there[0], there[1], jolt / 9.81, road_text,
		abs(car.get_speed_kmh()), heading]
	var file := FileAccess.open(list, FileAccess.READ_WRITE) if FileAccess.file_exists(list) else FileAccess.open(list, FileAccess.WRITE)
	if file:
		file.seek_end()
		file.store_string(line + "\r\n")
	print("Geodrive: ", line)
	hud.set_status("Bad spot %d saved (screenshot and position). Thanks!" % number, 4.0)


# --- driving ---

# The keys and pad drive (and look round) unless the menu, the world map or the loading screen has them: online those
# don't pause the game, and typing a name or an address mustn't drive the car
func _keys_drive() -> bool:
	return not (menu.is_open() or world_map.is_open() or loading.is_showing())


func _physics_process(delta: float) -> void:
	if switch_test >= 0.0 and aviation and (aviation.aircraft or switch_test > 0.0):
		var was := switch_test
		switch_test += delta
		if was < 15.0 and switch_test >= 15.0 and aviation.aircraft:
			print("Geodrive switch test: the aircraft sunk 200 m under the ground")
			aviation.aircraft.place(aviation.aircraft.global_position + Vector3.DOWN * 200.0, 0.0, 0.0)
		if was < 30.0 and switch_test >= 30.0:
			var r: Dictionary = aviation.aircraft.readout() if aviation.aircraft else {}
			print("Geodrive switch test: back on the runway: ", "yes" if r.get("on_ground", false) and not r.get("crashed", true) else "NO")
		if was < 45.0 and switch_test >= 45.0:
			print("Geodrive switch test: a car chosen mid-flight")
			_choose_car(4)
		if was < 75.0 and switch_test >= 75.0:
			var road: Dictionary = world.nearest_road(car.global_position, 25.0) if car else {}
			print("Geodrive switch test: flying %s; the car %s, on a road: %s, %.0f m above the ground" % [aviation.flying, "out" if car else "missing",
				"yes" if not road.is_empty() else "NO", car.global_position.y - world.ground_height(car.global_position) if car else -1.0])
			get_tree().quit()
	if builder:
		builder.physics(delta)
	if hunt:
		hunt.physics(delta)
	if aviation:
		aviation.physics(delta)
		if aviation.flying:
			if car:
				car.freeze = true	# (the car waits where it was)
			return
	if walker:
		walker.keys = _keys_drive() and not camera.free and not camera.map
		_walk_test_step(delta)
		return	# (the car waits, held still, for its driver)
	if car == null or waiting != "" or camera.free:
		return
	if walk_test:
		# (the walk test: settled for 1.5 s, then out of the car)
		walk_test_time += delta
		if walk_test_time >= 1.5:
			walk_test_time = 0.0
			toggle_walk()
			if walker == null:
				print("Geodrive walk test: couldn't get out (%.1f km/h)" % car.get_speed_kmh())
				get_tree().quit()
			return
	_watch_car(delta)
	# the robot test driver has the car
	if world and world.is_robot_driving():
		var drive: Dictionary = world.update_robot(delta, car)
		car.set_input(drive.get("steer", 0.0), drive.get("throttle", 0.0), drive.get("brake", 1.0), 0.0)
		if not drive.get("driving", false):
			car.autopilot = false
			hud.set_status("Robot driver finished: the report is in Saved/RoadReports", 8.0)
			if robot_mode != "":
				get_tree().quit()
		return
	if wheel and wheel.is_wheel_connected() and not drive_test:
		# the wheel and pedals drive; the handbrake stays on the keyboard
		car.set_handbrake(float(Input.is_key_pressed(KEY_SPACE)))
		return
	var throttle := 0.0
	var brake := 0.0
	var handbrake := 0.0
	var steer_target := 0.0
	if drive_test:
		# settle on the handbrake for 1.5 s, full throttle for 2.5 s, then the brakes
		test_time += delta
		throttle = 1.0 if test_time > 1.5 and test_time <= 4.0 else 0.0
		brake = 1.0 if test_time > 4.0 else 0.0
		handbrake = 1.0 if test_time < 1.5 else 0.0
		var marks := [0.5, 1.5, 4.0, 5.5]
		if test_step < marks.size() and test_time >= marks[test_step]:
			print("Geodrive drive test at %.1f s: %s\n%s" % [test_time, car.get_status(), car.get_debug()])
			test_step += 1
			if test_step == marks.size() and shot_path == "":
				get_tree().quit()
	elif engine_test:
		# 1.5 s on the handbrake; the throttle 1.5-4 s (on), 4-8 s (switched off at 4), 9-11 s (on again at 8)
		test_time += delta
		var e: Dictionary = _engine
		handbrake = 1.0 if test_time < 1.5 else 0.0
		throttle = 0.6 if (test_time > 1.5 and test_time <= 8.0) or test_time > 9.0 else 0.0
		var kmh: float = absf(car.get_speed_kmh())
		if test_time >= 4.0 and not e.has("off"):
			e["off"] = kmh
			car.engine_on = false
		if test_time >= 7.9 and not e.has("off_end"):
			e["off_end"] = kmh
			e["off_rpm"] = car.get_rpm()
			print("Geodrive engine test: switched off at %.0f km/h; 4 s of throttle later %.0f km/h, the engine at %.0f rpm" % [e["off"], kmh, e["off_rpm"]])
		if test_time >= 8.0 and not e.has("on"):
			e["on"] = true
			car.engine_on = true
		if test_time >= 9.0 and not e.has("on_rpm"):
			e["on_rpm"] = car.get_rpm()
			e["on_kmh"] = kmh
		if test_time >= 11.0:
			print("Geodrive engine test: on again: %.0f rpm a second later; 2 s of throttle: %.0f to %.0f km/h" % [e["on_rpm"], e["on_kmh"], kmh])
			var ok: bool = e["off"] > 5.0 and e["off_end"] <= e["off"] + 0.5 and e["off_rpm"] < 250.0 and e["on_rpm"] > 600.0 and kmh > e["on_kmh"] + 3.0
			print("Geodrive engine test: %s" % ("PASS" if ok else "FAIL"))
			get_tree().quit()
	elif speed_test > 0.0:
		var pilot := _autopilot(delta)
		throttle = pilot.x
		brake = pilot.y
		steer_target = pilot.z
		steer = steer_target
	elif not camera.free and _keys_drive():
		throttle = max(float(Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP)), Input.get_joy_axis(0, JOY_AXIS_TRIGGER_RIGHT))
		brake = max(float(Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN)), Input.get_joy_axis(0, JOY_AXIS_TRIGGER_LEFT))
		handbrake = float(Input.is_key_pressed(KEY_SPACE) or Input.is_joy_button_pressed(0, JOY_BUTTON_A))
		steer_target = float(Input.is_key_pressed(KEY_D) or Input.is_key_pressed(KEY_RIGHT)) - float(Input.is_key_pressed(KEY_A) or Input.is_key_pressed(KEY_LEFT))
		var stick := Input.get_joy_axis(0, JOY_AXIS_LEFT_X)
		if abs(stick) > 0.08:
			steer_target = stick

	# keys turn the wheels at a steady rate, with less lock at speed (as in the Unreal game)
	steer = move_toward(steer, steer_target, settings.steer_rate() * delta)
	var speed_ms: float = abs(car.get_speed_kmh()) / 3.6
	var lock: float = lerp(1.0, settings.high_speed_lock(), clamp(speed_ms / 40.0, 0.0, 1.0))
	car.set_input(steer * lock, throttle, brake, handbrake)


# A hard knock (as the Unreal game logs them): over 2 m/s of the car's speed taken out in one step, at most every
# 0.5 s: where, on which road, what it hit and how high, in the log, for finding map problems
func _check_knock(now: float) -> void:
	var velocity := car.linear_velocity
	var change := (velocity - knock_velocity).length()
	knock_velocity = velocity
	if knock_skip > 0:
		knock_skip -= 1
		return
	if change < 2.0 or now - last_knock < 0.5:
		return
	last_knock = now
	var hit := PackedStringArray()
	for body in car.get_colliding_bodies():
		var what := str(body.name).rstrip("0123456789")
		if not hit.has(what):
			hit.append(what)
	var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
	var road: Dictionary = world.nearest_road(car.global_position, 25.0, false)
	print("Geodrive: [Knock] %.1f m/s at %.6f,%.6f on %s, hitting %s, %.1f m up, %d km/h" % [change, here[0], here[1],
		("%s (%s)" % [road["name"] if road["name"] != "" else "unnamed", road["highway"]]) if road.get("found", false) else "no road",
		", ".join(hit) if hit.size() > 0 else "nothing (landed)", car.global_position.y, roundi(abs(car.get_speed_kmh()))])


# Every physics step: the trip, the last few seconds' ride (for B), and the car upside down or under the ground
func _watch_car(delta: float) -> void:
	var at := car.global_position
	trip_km += at.distance_to(last_position) / 1000.0 if at.distance_to(last_position) < 50.0 else 0.0
	last_position = at
	var now := Time.get_ticks_msec() / 1000.0
	ride.append([now, car.linear_velocity.y, at, abs(car.get_speed_kmh())])
	while ride.size() > 0 and ride[0][0] < now - 6.0:
		ride.pop_front()
	_check_knock(now)

	# on its roof, or stuck on its side, for two checks in a row (3 s apart): back on its wheels
	flip_timer += delta
	if flip_timer >= 3.0:
		flip_timer = 0.0
		var up := car.global_transform.basis.y.dot(Vector3.UP)
		var stuck := up < -0.2 or (up < 0.3 and car.linear_velocity.length() < 1.0)
		flip_count = flip_count + 1 if stuck else 0
		if flip_count >= 2:
			flip_count = 0
			reset_in_place()
	# fallen through the ground: back on the road. Under the ground isn't enough (in a tunnel the mountain is above the
	# road): it must be well below the nearest road too, or have no road near at all
	if world and at.y < world.ground_height(at) - 20.0:
		var road: Dictionary = world.nearest_road(at, 30.0, false)
		if not road.get("found", false) or at.y < (road["position"] as Vector3).y - 10.0:
			print("Geodrive: fell through the ground at ", world.to_lat_lon(at), ": back on the road")
			back_on_road()


func _process(delta: float) -> void:
	if camera == null:
		return
	if walk_test and Time.get_ticks_msec() > 120000:
		print("Geodrive walk test: gave up after 2 minutes (never out of the car)")
		get_tree().quit()
	camera.keys = _keys_drive()
	if net_test > 0.0 and Time.get_ticks_msec() / 1000.0 > net_test:
		net.leave()
		get_tree().quit()
	# slow frames (once things have settled), to find stutters: by the clock, not delta (--fixed-fps makes every delta 1/60)
	var now_us := Time.get_ticks_usec()
	var real_ms := (now_us - last_frame_us) / 1000.0
	last_frame_us = now_us
	if frame > 300 and real_ms > 25.0 and (testing or verbose):
		print("Geodrive: slow frame %.0f ms at %.1f s" % [real_ms, now_us / 1000000.0])
	_remember_spot_at += delta
	if is_instance_valid(_car_mark) and (walker == null or walker.global_position.distance_to(car.global_position) < 4.0):
		_car_mark.queue_free()
	if _remember_spot_at > 15.0:
		_remember_spot()

	# fly test: straight on along the start road at 40 m/s for 60 s (the ground streams as it would driving), then quit
	if fly_test and waiting == "":
		fly_test_time += delta
		if fly_test_time > 5.0 and fly_test_time <= fly_seconds + 5.0:
			fly_frames.append(delta)
		if fly_test_time > 5.0:
			var ahead := -camera.global_transform.basis.z
			ahead.y = 0.0
			camera.position += ahead.normalized() * fly_speed * delta
		if fly_test_time > fly_seconds + 5.0:
			var sorted := fly_frames.duplicate()
			sorted.sort()
			var worst := sorted.slice(int(sorted.size() * 0.99)) if sorted.size() > 100 else sorted
			var total := 0.0
			for f in fly_frames:
				total += f
			var low := 0.0
			for f in worst:
				low += f
			print("Geodrive fly test: %.0f fps average, %.0f fps in the slowest 1%% of frames, slowest %.0f ms (%d frames)" % [fly_frames.size() / total, worst.size() / low, sorted[-1] * 1000.0, fly_frames.size()])
			get_tree().quit()

	_wheel_controls(delta)
	_update_road_lines(delta)
	camera.update(delta)
	map_overlay.visible = camera.map and world != null and car != null
	if map_overlay.visible:
		map_overlay.queue_redraw()
	var side_of: Node3D = aviation.aircraft if aviation and aviation.flying and aviation.aircraft else car
	if side_view and side_of:
		var forward := -side_of.global_transform.basis.z
		var a := deg_to_rad(side_angle)
		var off := (forward * cos(a) + side_of.global_transform.basis.x * sin(a)) * side_distance
		camera.global_position = side_of.global_position + off + Vector3.UP * 1.6
		camera.look_at(side_of.global_position + Vector3.UP * 0.9, Vector3.UP)

	if world:
		_update_loading()
		# the keys that do things now, and what's running (robot, traffic), twice a second (the data's credits are on the
		# loading screen, the map screens and in the README)
		info_timer -= delta
		if info_timer <= 0.0:
			info_timer = 0.5
			hud.set_hints(_key_hints())
			hud.set_info(world.get_robot_status() if world.is_robot_driving() else world.get_traffic_status())
		if waiting != "":
			_update_waiting()
		elif arrive_on_foot and car and not camera.free:
			arrive_on_foot = false
			_arrive()
		elif camera.free or car == null:
			world.set_focus(camera.global_position, (camera.global_position - last_camera) / max(delta, 0.001))
			last_camera = camera.global_position
		elif aviation and aviation.pending.has("at"):
			world.set_focus(aviation.pending["at"], Vector3.ZERO)	# (a flight asked for: the runway's ground first)
			car.freeze = true	# (held where it is: its ground goes as the world turns to the runway)
		elif aviation and aviation.flying:
			world.set_focus(aviation.aircraft.global_position, aviation.velocity)
		elif walker:
			world.set_focus(walker.global_position, walker.velocity)
		else:
			# which way the world should grow: the way the car goes, or, nearly stopped (just put down with F, say), the way it
			# faces, as the Unreal game does; the next piece is built when less than 4 km is built that way
			var going := car.linear_velocity
			if Vector2(going.x, going.z).length() < 3.0:
				var facing := -car.global_transform.basis.z
				going = Vector3(facing.x, 0.0, facing.z).normalized() * 3.5
			world.set_focus(car.global_position, going)
		_update_road_globals()
		if aviation and world.is_ready():
			runway_timer -= delta
			if runway_timer <= 0.0:
				runway_timer = 2.0
				aviation.draw_near(camera.global_position, 8.0)
			if fly_request != "" and car and waiting == "" and shown_frames >= 30:
				var parts := fly_request.split(",")
				var found: Array = aviation.search(parts[1] if parts.size() > 1 else "FACT")
				fly_request = ""
				if found.is_empty():
					print("Geodrive: no airport ", parts)
				else:
					aviation.request(int(parts[0]), found[0]["airport"])
		if car and waiting == "":
			_update_traffic(delta)
			if net_host and shown_frames == 30:
				net.host()
			if robot_mode != "" and shown_frames >= 30 and not robot_started:
				robot_started = true
				var parts := robot_mode.split(",")
				start_robot(parts[0], int(parts[1]) if parts.size() > 1 else 10, int(parts[2]) if parts.size() > 3 else 0, int(parts[3]) if parts.size() > 3 else 1)
			if net_test > 0.0:
				net_log -= delta
				if net_log <= 0.0:
					net_log = 2.0
					var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
					print("Geodrive net test: %s, me at %.5f,%.5f (%d km/h); others: %s" % ["host" if net.is_host() else ("joined" if net.is_client() else "offline"), here[0], here[1], roundi(car.get_speed_kmh()), net.describe()])
					print("Geodrive net test: my car at ", car.global_position)
			robot_status_timer -= delta
		if car and waiting == "":
			# the street you're on (and whether it's in the UK, for mph), four times a second
			street_timer -= delta
			if street_timer <= 0.0:
				street_timer = 0.25
				mph = settings.uses_mph(_signs_mph())
				var road: Dictionary = {} if aviation and aviation.flying else world.nearest_road(walker.global_position if walker else car.global_position, 25.0)
				street = ""
				if road.get("found", false):
					street = road["name"] if road["name"] != "" else "Unnamed %s road" % str(road["highway"]).replace("_", " ")	# (as the Unreal game names them)
	if car:
		var gear: int = car.get_gear()
		var debug := ""
		if hud.debug:
			debug = "\n".join([title, car.get_status(), car.get_debug(), (wheel.get_debug() if wheel else "wheel: none"),
				world.get_status() if world else "", world.get_traffic_status() if world else "", "%.1f fps" % Engine.get_frames_per_second()])
		hud.update(car.get_speed_kmh(), "R" if gear < 0 else ("N" if gear == 0 else str(gear)), street, trip_km, debug, mph)
		hud.set_on_foot(walker != null or (aviation != null and aviation.flying))
		hud.set_flight(aviation.instruments() if aviation and aviation.flying else "")
		hud.set_car_flags(car.diff_lock, car.is_stalled() and car.engine_on)	# (switched off: not stalled)
	elif hud.debug and world:
		hud.update(0.0, "-", "", 0.0, title + "\n" + world.get_status())

	# wheel test: what the wheel says after 3 s (no forces), then quit
	if wheel_test and frame == 180:
		print("Geodrive wheel test: ", wheel.get_status() if wheel else "no wheel node", ", connected ", wheel.is_wheel_connected() if wheel else false)
		get_tree().quit()

	# test hooks: teleport at frame 60, the map at frame 200
	if shown_frames == 60 and test_teleport.size() == 2:
		var place := test_teleport
		test_teleport = PackedFloat64Array()	# (once: a new area counts its frames again)
		teleport(place[0], place[1], "the test place")
	if shown_frames == 200 and test_map and car:
		camera.toggle_map()
	if test_map and car and shown_frames in [205, 305]:
		print("Geodrive map test: %d m up, first drawing %.2f ms" % [roundi(camera.map_height), map_draw_ms])
		map_draw_ms = 0.0
	if test_map and car and shown_frames in [300, 400]:
		# the M map's slowest drawing at 400 m up, then zoomed out to 2 km
		print("Geodrive map test: %d m up, slowest map drawing after that %.2f ms" % [roundi(camera.map_height), map_draw_ms])
		map_draw_ms = 0.0
		camera.map_height = 2000.0
		if shown_frames == 400:
			get_tree().quit()
	if r_test and car and shown_frames in [100, 200, 260]:
		# R off the road: the car put 60 m to the side, then R: it should be back on the nearest road
		var road: Dictionary = world.nearest_road(car.global_position, 20000.0)
		var off: float = car.global_position.distance_to(road["position"]) if road.get("found", false) else -1.0
		print("Geodrive R test: frame %d, car at %s, %.1f m from the nearest road (%s)" % [shown_frames, car.global_position, off, road.get("name", "")])
		if shown_frames == 100:
			var side := car.global_transform.basis.x
			var aside := car.global_position + side * 60.0
			_put_car(Transform3D(car.global_transform.basis, Vector3(aside.x, world.ground_height(aside) + 1.0, aside.z)))
		elif shown_frames == 200:
			back_on_road()	# (no key now: the speed test and the fell-through check use it)
		else:
			get_tree().quit()
	if reset_test and car and frame in [100, 101, 110, 140]:
		if frame == 100:
			print("Geodrive reset test: before ", car.global_position)
			_put_car(Transform3D(car.global_transform.basis, car.global_position + Vector3(0.0, 5.0, 20.0)))
		print("Geodrive reset test: frame ", frame, " car at ", car.global_position, " server ", PhysicsServer3D.body_get_state(car.get_rid(), PhysicsServer3D.BODY_STATE_TRANSFORM).origin)
	if key_test and car and shown_frames == 180:
		camera.position += Vector3(300.0, 40.0, 0.0)	# (flown off 300 m east and up; F brings the camera back to the car)
	if key_test and car:
		var keys := { 130: KEY_BACKSPACE, 160: KEY_F, 200: KEY_F, 230: KEY_C, 260: KEY_C, 290: KEY_M, 320: KEY_M }
		if keys.has(shown_frames):
			_key(keys[shown_frames])
			print("Geodrive key test: pressed ", OS.get_keycode_string(keys[shown_frames]), " -> view ", camera.view_name(), ", free ", camera.free, ", map ", camera.map, ", car at ", car.global_position)

	# (traffic test pictures: the second one from behind the nearest traffic car)
	if (traffic_override > 0 or net_test > 0.0) and shot_path != "" and shown_frames == 470 and world:
		var nearest: Node3D = null
		for child in world.get_children() + get_children():
			if child is Node3D and (str(child.name).begins_with("Traffic") or str(child.name).begins_with("Player")) and (nearest == null or child.global_position.distance_to(car.global_position) < nearest.global_position.distance_to(car.global_position)):
				nearest = child
		if nearest:
			camera.start_free()
			var behind: Vector3 = nearest.global_transform.basis.z
			camera.global_position = nearest.global_position + behind * 7.0 + Vector3.UP * 2.5 + nearest.global_transform.basis.x * 2.0
			camera.look_at(nearest.global_position + Vector3.UP * 0.8, Vector3.UP)
			camera._free_yaw = camera.rotation.y
			camera._free_pitch = camera.rotation.x
	if f_test and car and waiting == "":
		f_test_time += get_process_delta_time()
		if f_step == 0 and f_test_time > 1.0:
			f_step = 1
			f_start = car.global_position
			_key(KEY_F)
			print("Geodrive F test: flying from ", camera.global_position)
		if f_step == 1:
			camera.position += Vector3(150.0, 0.0, -100.0) * get_process_delta_time()	# 180 m/s
			if camera.global_position.distance_to(car.global_position) > f_km * 1000.0:
				f_step = 2
				var road: Dictionary = world.nearest_road(camera.global_position, 20000.0)
				print("Geodrive F test: camera at ", camera.global_position, " (car ", car.global_position, ", moved ", car.global_position - f_start, " while flying), nearest road ", road.get("position", "none"), "; pressing F")
				_key(KEY_F)
				f_test_time = 0.0
		if f_step == 2 and f_test_time > 5.0 and f_test_time - get_process_delta_time() <= 5.0:
			print("Geodrive F test: 5 s later the camera is %.0f m from the car, the car at %.1f m/s: %s" % [camera.global_position.distance_to(car.global_position), car.linear_velocity.length(),
				"PASS" if not camera.free and camera.global_position.distance_to(car.global_position) < 30.0 and car.linear_velocity.length() < 3.0 else "FAIL"])	# (an automatic creeps at idle, ~8 km/h)
		if f_step == 2 and f_test_time > 60.0:
			print("Geodrive F test: a minute later: ", world.get_status())
			get_tree().quit()
	_arrive_test_step()
	if hunt_test:
		_hunt_test_step(delta)
	if zombie_test:
		_zombie_test_step(delta)
	if roadkill_test:
		_roadkill_test_step(delta)
	if booth_test:
		_booth_test_step(delta)
	if workshop_test:
		_workshop_test_step(delta)
	if editor_test:
		_editor_test_step(delta)
	if prop_test:
		_prop_test_step(delta)
	if game_maker_test:
		_game_maker_test_step(delta)
	if rules_test:
		_rules_test_step(delta)
	if race_test:
		_race_test_step(delta)
	if mp_test != "":
		_mp_test_step(delta)
	if share_test != "":
		_share_test_step(delta)
	if car_hit_test:
		_car_hit_test_step(delta)
	if crash_test and aviation and aviation.aircraft and aviation.flying and waiting == "":
		_crash_test_step(delta)
	elif crash_test and walker and int(_crash["step"]) >= 4:
		_crash_test_walk(delta)
	if status_log and world:
		_status_log_timer -= delta
		if _status_log_timer <= 0.0:
			_status_log_timer = 2.0
			var here: PackedFloat64Array = world.to_lat_lon(car.global_position) if car else PackedFloat64Array([0.0, 0.0])
			print("Geodrive status %.0f s: at %.3f,%.3f %s; %s; memory %.0f MB" % [Time.get_ticks_msec() / 1000.0, here[0], here[1], ("waiting: " + waiting) if waiting != "" else "", world.get_status(), OS.get_static_memory_usage() / 1048576.0])
	if world_map_test and car and waiting == "":
		_world_map_test_step(delta)
	# (a game with no places: the offer comes at the start, before there's a car; pressed as a player would)
	if fetch_test.size() == 2 and region_fetch._panel.visible and region_fetch._yes.visible and not has_meta("fetch_pressed"):
		set_meta("fetch_pressed", true)
		set_meta("fetch_asked", true)
		print("Geodrive fetch test: offered at the start: %s" % region_fetch._title.text)
		region_fetch._start()
	if fetch_test.size() == 2 and car and waiting == "":
		if not has_meta("fetch_asked") and shown_frames > 60:
			set_meta("fetch_asked", true)
			print("Geodrive fetch test: a map at %.4f,%.4f: %s" % [fetch_test[0], fetch_test[1], world.has_map_at(fetch_test[0], fetch_test[1])])
			_drive_to(fetch_test[0], fetch_test[1], "the test place")
			if region_fetch._panel.visible:
				region_fetch._start()
		elif has_meta("fetch_asked") and not region_fetch.busy() and not has_meta("fetch_there"):
			var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
			if absf(here[0] - fetch_test[0]) < 0.05 and absf(here[1] - fetch_test[1]) < 0.05:
				set_meta("fetch_there", true)
				print("Geodrive fetch test: there (%.4f,%.4f), %s: PASS" % [here[0], here[1], world.get_country()])
				get_tree().quit()
	if build_test and build_trees and car and builder and vegetation:
		_build_trees_step()
	elif build_test and build_remove and car and builder:
		_build_remove_step()
	elif build_test and build_stairs and car and builder:
		_build_stairs_step()
	elif build_test and build_hands and car and builder:
		_build_hands_step()
	elif build_test and car and builder and shown_frames == 60:
		# a double-storey house 14 m off to the car's right, and the free camera looking at it from its front corner
		var right := car.global_transform.basis.x
		var house: Node3D = builder.build_test_house(car.global_position + right * 14.0, car.global_rotation.y)
		if house:
			camera.start_free()
			var corner: Vector3 = house.global_transform * Vector3(-6.0, 4.0, 9.0)
			camera.global_position = corner
			camera.look_at(house.global_transform * Vector3(3.0, 2.5, -3.0), Vector3.UP)
			camera._free_pitch = camera.rotation.x
			camera._free_yaw = camera.rotation.y
	if clean_pic and hud.shown:
		hud.set_shown(false)	# (a picture for showing off: nothing over the view)
	if race_pic and car and shown_frames == 100:
		_stage_race()
	if high_view and car and shown_frames == 100:
		if shot_path != "":
			hud.set_shown(false)	# (a picture of the view: the HUD not in it; not saved)
		camera.start_free()
		camera.position = car.global_position + Vector3(0.0, look.x, 0.0)
		camera._free_pitch = deg_to_rad(look.y)
		camera._free_yaw = deg_to_rad(look.z)
	# picture mode: once the ground round it is in, then a while later, then quit
	frame += 1
	if waiting == "" and (car or world == null or camera.free):
		shown_frames += 1
	if gpu_profile:
		_gpu_profile_step()
	if shot_path != "":
		if shown_frames == 300 and build_test and not build_hands and not build_stairs and builder:
			# (the build test's later picture from behind: into the upstairs room through its glass wall)
			for house in builder._root.get_children():
				camera.global_position = house.global_transform * Vector3(2.2, 5.0, -10.0)
				camera.look_at(house.global_transform * Vector3(1.5, 4.0, -3.0), Vector3.UP)
				camera._free_pitch = camera.rotation.x
				camera._free_yaw = camera.rotation.y
		if shown_frames == 240:
			_save(shot_path)
		elif shown_frames == 480:
			_save(shot_path.get_basename() + "_later.png")
			if not (plane_test or walk_test or cruise_test or hunt_test or zombie_test or roadkill_test or booth_test or workshop_test or editor_test or prop_test or game_maker_test or rules_test or race_test):
				get_tree().quit()	# (those tests end themselves)


# --racepic: two cars racing, staged: a rival (another body) in the next lane a length ahead, both held where they are
# (a picture can't tell held from flat out), and the camera low by the road ahead of them, looking back at both
func _stage_race() -> void:
	var road: Dictionary = world.nearest_road(car.global_position, 60.0)
	if not road.get("found", false):
		return
	var direction: Vector3 = road["direction"]
	var along := (direction if direction.dot(-car.global_transform.basis.z) >= 0.0 else -direction).normalized()
	var side := Vector3(-along.z, 0.0, along.x).normalized()
	var middle: Vector3 = road["position"]
	if side.dot(middle - car.global_position) < 0.0:
		side = -side	# (the rival on the road's side of you, not off its edge)
	car.freeze = true
	_rival = ClassDB.instantiate("GeodriveCar")
	_rival.body = 1 if car.body != 1 else 4
	var at := car.global_position + side * 3.0 + along * 4.0
	at.y = world.ground_height(at) + 0.6
	_rival.transform = Transform3D(Basis.looking_at(along, Vector3.UP), at)
	add_child(_rival)
	_rival.freeze = true
	hud.set_shown(false)
	camera.start_free()
	var both := (car.global_position + at) * 0.5
	camera.global_position = both + along * 9.0 - side * 4.5 + Vector3.UP * 1.4
	camera.look_at(both + Vector3.UP * 0.5, Vector3.UP)
	camera._free_pitch = camera.rotation.x
	camera._free_yaw = camera.rotation.y


# --menutest=<folder>: pictures of the front menu, the settings tabs, the cars and areas, the loading screen part way,
# then driving with the menu open (main and teleport pages), then quit
func _run_menu_test() -> void:
	DirAccess.make_dir_recursive_absolute(menu_test)
	var shot := func(picture: String):
		await RenderingServer.frame_post_draw
		_save(menu_test.path_join(picture + ".png"))
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	await wait.call(2.0)
	await shot.call("menu_front")
	UnrealSettings.save_last_spot("Long Street, Cape Town", -33.9206, 18.4182)	# (the test's settings are in memory only)
	menu.show_page("main")
	await wait.call(0.5)
	await shot.call("menu_continue")
	menu.show_page("multiplayer")
	await wait.call(0.5)
	await shot.call("menu_multiplayer_front")
	menu.show_page("places")
	await wait.call(0.5)
	await shot.call("menu_places")
	menu.show_page("main")
	open_world_map()
	while not WorldMap.index.is_loaded():
		await wait.call(0.25)
	await wait.call(1.0)
	await shot.call("world_map")
	world_map._look_at(-33.92, 18.42)
	await wait.call(1.0)
	await shot.call("world_map_cape_town")
	world_map.close()
	await wait.call(0.5)
	for tab in 4:
		menu.settings_tab = tab
		menu.show_page("settings")
		await wait.call(0.5)
		await shot.call("menu_settings_%d" % tab)
		if tab == 1:
			for part in [1, 2]:
				menu._scroll.scroll_vertical = part * 640
				await wait.call(0.3)
				await shot.call("menu_settings_1_part%d" % part)
	menu.show_page("go")
	await wait.call(0.5)
	await shot.call("menu_go")
	menu.chosen_area = menu.areas[0]
	menu.show_page("start_point")
	await wait.call(0.5)
	await shot.call("menu_start_point")
	# by name, through OpenStreetMap's search (as a player would type it)
	menu.show_page("go")
	menu._search_and_drive("Plettenberg Bay Main Street")
	while waiting == "":
		await wait.call(0.25)
	await wait.call(3.0)
	await shot.call("loading_3s")
	await wait.call(3.0)
	await shot.call("loading_6s")
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(4.0)
	await shot.call("driving")
	settings.road_lines = true
	hud.set_debug(true)
	await wait.call(1.5)
	await shot.call("debug")
	settings.road_lines = false
	hud.set_debug(settings.debug_info)
	print("Geodrive menu test: bakkie sound level ", car_sound.get_level() if car_sound else -1.0, " at ", car.get_rpm(), " rpm")
	var body := settings.car_body
	_choose_car(3)
	await wait.call(3.0)
	print("Geodrive menu test: sound level ", car_sound.get_level() if car_sound else -1.0, " at ", car.get_rpm(), " rpm")
	print("Geodrive menu test: car now ", car.get_car_name(), " at ", car.global_position, ", ", car.get_status())
	await shot.call("car_changed")
	_choose_car(body)
	await wait.call(1.0)
	open_menu()
	await wait.call(0.5)
	await shot.call("menu_main")
	menu.show_page("go")
	await wait.call(0.5)
	await shot.call("menu_go_driving")
	var last := UnrealSettings.last_spot()
	print("Geodrive menu test: Continue goes to %s (%.4f, %.4f)" % [last.get("name", "nowhere"), last.get("lat", 0.0), last.get("lon", 0.0)])
	print("Geodrive menu test: pictures in ", menu_test)
	get_tree().quit()


func _run_tunnel_test() -> void:
	DirAccess.make_dir_recursive_absolute(tunnel_test)
	var shot := func(picture: String):
		await RenderingServer.frame_post_draw
		_save(tunnel_test.path_join(picture + ".png"))
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(6.0)
	var spots := [["louis_ii", 43.73908, 7.42918], ["larvotto", 43.7375, 7.4245], ["rainier_iii", 43.73044, 7.4071],
		["dorsale", 43.73338, 7.41759], ["aureglia_roundabout", 43.73441, 7.41812], ["pont_cadre", 43.72736, 7.4185]]
	# (--tunnelspots=name:lat:lon,...: others)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--tunnelspots="):
			spots = []
			for spot in arg.substr(14).split(","):
				var parts := spot.split(":")
				spots.append([parts[0], float(parts[1]), float(parts[2])])
	car.freeze = true
	camera.start_free()
	for spot in spots:
		var at: Vector3 = world.from_lat_lon(spot[1], spot[2])
		var road: Dictionary = world.nearest_road(at, 40.0, false)
		if road.get("found", false) and road["highway"] == "service":
			road = world.nearest_road(at, 40.0, true)	# (the main road there, not a lane beside it)
		if not road.get("found", false):
			print("Geodrive tunnel test: %s: no road there" % spot[0])
			continue
		var on: Vector3 = road["position"]
		var way: Vector3 = (road["direction"] as Vector3).normalized()
		world.set_focus(on, Vector3.ZERO)
		await wait.call(3.0)
		var ground: float = world.ground_height(on)
		print("Geodrive tunnel test: %s: %s (%s), the road %.1f m %s the ground there" % [spot[0], road["name"], road["highway"],
			absf(on.y - ground), "under" if on.y < ground else "over"])
		# from the driver's seat, along it; then from above its mouth
		camera.global_position = on + Vector3.UP * 1.3 - way * 6.0
		camera.look_at(on + Vector3.UP * 1.2 + way * 30.0, Vector3.UP)
		camera._free_pitch = camera.rotation.x
		camera._free_yaw = camera.rotation.y
		await wait.call(1.5)
		await shot.call(spot[0] + "_inside")
		camera.global_position = on + Vector3.UP * 45.0 - way * 35.0
		camera.look_at(on, Vector3.UP)
		camera._free_pitch = camera.rotation.x
		camera._free_yaw = camera.rotation.y
		await wait.call(1.5)
		await shot.call(spot[0] + "_above")
	print("Geodrive tunnel test: pictures in ", tunnel_test)
	get_tree().quit()


func _run_event_test() -> void:
	DirAccess.make_dir_recursive_absolute(event_test)
	var shot := func(picture: String):
		await RenderingServer.frame_post_draw
		_save(event_test.path_join(picture + ".png"))
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(2.0)
	menu.has_area = false
	menu.open("events")
	await wait.call(0.5)
	await shot.call("start_events")
	menu.close()
	phone.events_kind = "track"
	phone.open("games")
	await wait.call(0.5)
	await shot.call("phone_tracks")
	var here: PackedFloat64Array = world.to_lat_lon(car.global_position)
	var found: Array = game_gen.events("track", here, event_name, 1)
	if found.is_empty():
		print("Geodrive event test: FAIL (no %s)" % event_name)
		get_tree().quit()
		return
	print("Geodrive event test: %s, %.1f km away" % [found[0]["name"], found[0]["km"]])
	(_first_phone_button(phone, found[0]["name"]) as Button).pressed.emit()
	await wait.call(1.0)
	while waiting != "" or car == null:
		await wait.call(0.5)
	await wait.call(8.0)
	var you: Node3D = walker if walker else car
	var at: PackedFloat64Array = world.to_lat_lon(you.global_position)
	var booth: Array = hunt.booths().filter(func(b): return String(b["name"]).contains(event_name))
	var away: float = hunt._metres(at[0], at[1], booth[0]["lat"], booth[0]["lon"]) if not booth.is_empty() else -1.0
	await shot.call("arrived")
	# (its checkpoints on the track: each one's nearest road, and what it is)
	var on_track := 0
	var points: Array = []
	for reserve in hunt.reserves:
		if String(reserve["name"]).contains(event_name) and reserve.has("checkpoints"):
			points = reserve["checkpoints"]
	for point in points:
		var road: Dictionary = world.nearest_road(world.from_lat_lon(point["lat"], point["lon"]), 30.0)
		var good: bool = road.get("found", false) and ((road["highway"] == "raceway" and road["distance"] < 8.0) or road["distance"] < 1.0)	# (or where a road joins it)
		on_track += 1 if good else 0
		if not good:
			print("Geodrive event test:   checkpoint at %.5f, %.5f: the nearest road %s (%s), %.1f m" % [point["lat"], point["lon"], road.get("name", "none"), road.get("highway", ""), road.get("distance", -1.0)])
	print("Geodrive event test: arrived; its game %s, its booth %.0f m from you; %d of %d checkpoints on the race track" % [
		booth[0]["name"] if not booth.is_empty() else "none", away, on_track, points.size()])
	print("Geodrive event test: ", "PASS" if not booth.is_empty() and away < 300.0 and on_track == points.size() else "FAIL")
	get_tree().quit()


func _run_golf_test() -> void:
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(2.0)
	car.linear_velocity = Vector3.ZERO
	car.freeze = true
	await wait.call(0.5)
	toggle_walk()
	for c in GOLF.CLUBS.size() - 1:
		print("Geodrive golf test: %s carries %d m" % [GOLF.CLUBS[c][0], roundi(golf.carry_of(c))])
	var course := {}
	for i in 60:
		await wait.call(1.0)
		var found: Array = hunt.reserves.filter(func(r): return r.has("golf"))
		if not found.is_empty():
			course = found[0]
			break
	if course.is_empty() or not golf.start(course):
		print("Geodrive golf test: FAIL (no course here)")
		get_tree().quit()
		return
	var holed := 0
	for h in 3:
		while golf.hole == h and golf.active and not golf.holed:
			# (the robot golfer: to its ball, the club the game picks, full power; on the green, a putt as hard as it needs)
			walker.global_position = golf._root.to_global(golf._ball_at + Vector3(1.2, 0.3, 0.0))
			await wait.call(0.2)
			golf._address()
			var left := Vector2(golf._line[-1].x - golf._ball_at.x, golf._line[-1].z - golf._ball_at.z).length()
			var on_green: bool = golf.club == GOLF.CLUBS.size() - 1
			var rolls: float = GOLF.SURFACES[golf._surface(golf._ball_at)][2]
			var strength := clampf(sqrt(2.0 * rolls * (left + 0.3)) / GOLF.CLUBS[-1][1], 0.05, 1.0) if on_green else 1.0
			if not on_green and golf.club >= 4 and golf.carry_of(golf.club) > left:
				strength = clampf(left / golf.carry_of(golf.club), 0.1, 1.0)	# (a short iron, not a full one)
			if shot_path != "" and h == 0 and golf.strokes[0] == 0:
				golf.power = 0.8
				await wait.call(0.6)
				_save(shot_path.get_basename() + "_address.png")
			golf.hit(golf.club, strength, golf._heading_to(golf._line[-1]) if left < 260.0 else golf.aim)
			if shot_path != "" and h == 0 and golf.strokes[0] <= 2:
				golf._camera.current = true
				await wait.call(1.2)
				_save(shot_path.get_basename() + "_flight%d.png" % golf.strokes[0])
			if shot_path != "" and h == 0 and golf._surface(golf._ball_at) == "green" and golf.club == GOLF.CLUBS.size() - 1:
				_save(shot_path.get_basename() + "_putt.png")
			while golf.state == "flight":
				await wait.call(0.25)
			print("Geodrive golf test: hole %d stroke %d (%s, %d%%): %s, %.2f m to go (passed %.2f m from the cup); the slope there %.1f%%" % [h + 1, golf.strokes[h],
				GOLF.CLUBS[golf.club][0], roundi(strength * 100.0), golf._surface(golf._ball_at), Vector2(golf._line[-1].x - golf._ball_at.x, golf._line[-1].z - golf._ball_at.z).length(),
				golf.closest, golf._slope(golf._ball_at).length() * 100.0])
			if golf.strokes[h] >= 12:
				break
		if golf.holed:
			holed += 1
		while golf.active and golf.hole == h:
			await wait.call(0.5)
			if golf.strokes[h] >= 12:
				break
	print("Geodrive golf test: %s, strokes %s" % [golf.course.get("name", "?"), golf.strokes])
	print("Geodrive golf test: ", "PASS" if holed == 3 else "FAIL")
	get_tree().quit()


func _run_gen_test() -> void:
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(30.0 if game_gen._is_covered(world.get_start()) else 120.0)	# (asked of Overpass: ~40 s the first time)
	var made: Array = hunt.reserves.filter(func(r): return r.get("generated", false))
	var races := 0
	for reserve in made:
		var points: int = reserve.get("checkpoints", []).size()
		races += 1 if points >= 4 else 0
		print("Geodrive gen test: %s: %d corners, %d checkpoints, %s, booth at %.5f, %.5f" % [reserve["name"], reserve["outline"].size(), points,
			reserve.get("stock", []), reserve["gates"][0]["lat"], reserve["gates"][0]["lon"]])
	var booths: int = hunt.booths().filter(func(b): return hunt.reserves[b["area"]].get("generated", false)).size()
	print("Geodrive gen test: %d games made, %d races, %d booths up" % [made.size(), races, booths])
	print("Geodrive gen test: ", "PASS" if made.size() >= 2 and booths >= 1 else "FAIL")
	get_tree().quit()


func _run_make_test() -> void:
	DirAccess.make_dir_recursive_absolute(make_test)
	var shot := func(picture: String):
		await RenderingServer.frame_post_draw
		_save(make_test.path_join(picture + ".png"))
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(3.0)
	car.linear_velocity = Vector3.ZERO
	car.freeze = true
	await wait.call(0.5)
	toggle_walk()
	await wait.call(1.0)
	_key(KEY_B)
	await wait.call(1.0)
	var bar: bool = builder._bar.visible and builder._bar.get_child_count() >= 14
	await shot.call("build_bar")
	builder._show_menu(true)
	await wait.call(0.5)
	await shot.call("place_building")
	builder._page = builder.PROPS_PAGE
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("place_things")
	builder._page = builder.GAMES_PAGE
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("place_games")
	builder.make_new()
	await wait.call(0.5)
	await shot.call("make_what")
	builder.workshop.thing = "prop"
	builder.workshop.step = "start"
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("make_start")
	# (the Table card, as a click would)
	var shop: Workshop = builder.workshop
	shop.prop = {"id": "", "name": "My table", "look": {"parts": Workshop.OBJECTS["Table"].duplicate(true)}}
	shop.prop_editor = ShapeEditor.new(shop.prop["look"], builder, "prop")
	shop.step = "edit"
	builder._fill_menu()
	await wait.call(1.0)
	await shot.call("make_table")
	var table_parts: int = shop.prop["look"]["parts"].size()
	var saved_prop := shop.save_prop()
	shop.change_kind("weapon")
	builder._fill_menu()
	await wait.call(1.0)
	await shot.call("make_table_gun")
	var gun_parts: int = shop.draft["look"]["parts"].size()
	shop.tab = 0
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("make_gun_does")
	var saved_gun := shop.save()
	builder._page = builder.PROPS_PAGE
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("place_things_after")
	# a creature: from the kudu, a bigger one with a rhino's horn, saved; then the zombie (upright)
	shop.thing = "creature"
	shop.step = "start"
	builder._page = builder.WORKSHOP_PAGE
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("creature_start")
	shop.creature = Workshop.CREATURE_MAKER.new(builder)
	shop.creature.start_from("kudu")
	shop.step = "edit"
	builder._fill_menu()
	await wait.call(1.0)
	await shot.call("creature_kudu")
	var beast: Dictionary = shop.creature.data
	beast["name"] = "Test beast"
	beast["mass"] = 900.0
	beast["colour"] = "#5a3a2a"
	beast["shape"]["horns"] = "nose"
	beast["shape"]["horn_length"] = 0.7
	beast["shape"]["coat"] = "stripes"
	shop.creature._rebuild()
	builder._fill_menu()
	await wait.call(1.0)
	await shot.call("creature_beast")
	var parts: int = shop.creature._model.get_child_count()
	var saved_beast: String = shop.creature.save()
	var kept: Dictionary = GeodriveHunt.species_data(saved_beast)
	var file_there := FileAccess.file_exists(Packs.workshop().path_join("creatures/%s.json" % saved_beast))
	shop.creature.tab = 0
	builder._fill_menu()
	await wait.call(0.5)
	await shot.call("creature_does")
	shop.creature = Workshop.CREATURE_MAKER.new(builder)
	shop.creature.start_from("zombie")
	shop.creature.tab = 1
	builder._fill_menu()
	await wait.call(1.0)
	await shot.call("creature_zombie")
	print("Geodrive make test: bar %s; table %d parts, saved %s; made a gun: %s, %d parts, saved %s" % [bar, table_parts, saved_prop, shop.thing, gun_parts, saved_gun])
	print("Geodrive make test: a creature of %d parts saved as %s (%s kg, its horn %s; the file %s)" % [parts, saved_beast, kept.get("mass"), kept.get("shape", {}).get("horns"), file_there])
	var ok: bool = bar and table_parts == 5 and saved_prop != "" and gun_parts == 5 and saved_gun != "" \
		and parts > 10 and saved_beast != "" and float(kept.get("mass", 0.0)) == 900.0 and kept.get("shape", {}).get("horns") == "nose" and file_there
	print("Geodrive make test: ", "PASS" if ok else "FAIL")
	get_tree().quit()


func _run_phone_test() -> void:
	DirAccess.make_dir_recursive_absolute(phone_test)
	var shot := func(picture: String):
		await RenderingServer.frame_post_draw
		_save(phone_test.path_join(picture + ".png"))
	var wait := func(seconds: float):
		await get_tree().create_timer(seconds, true).timeout
	while car == null or waiting != "":
		await wait.call(0.5)
	await wait.call(3.0)
	car.linear_velocity = Vector3.ZERO	# (stopped, held, as you'd stop to get out)
	car.freeze = true
	await wait.call(0.5)	# (its speed read again, held)
	toggle_walk()
	await wait.call(2.0)
	if walker == null:
		print("Geodrive phone test: FAIL (couldn't get out: %.1f km/h, free camera %s, waiting \"%s\", status %s)" % [car.get_speed_kmh(), camera.free, waiting, hud.status_label.text])
		get_tree().quit()
		return
	# (walked off the road a little, as you would be)
	walker.global_position += walker.global_transform.basis.x * -8.0
	await wait.call(1.0)
	_key(KEY_TAB)
	await wait.call(0.5)
	await shot.call("phone_home")
	var opened: bool = phone.is_open()
	phone.show_page("cars")
	await wait.call(0.5)
	await shot.call("phone_cars")
	phone.show_page("games")
	await wait.call(0.5)
	await shot.call("phone_games")
	phone.show_page("cars")
	(_first_phone_button(phone, "Hatchback 1.6") as Button).pressed.emit()
	await wait.call(2.0)
	var road: Dictionary = world.nearest_road(car.global_position, 50.0)
	var off_road: float = Vector2(car.global_position.x - road["position"].x, car.global_position.z - road["position"].z).length() if road.get("found", false) else 99.0
	var away: float = walker.global_position.distance_to(car.global_position)
	await shot.call("car_parked")
	print("Geodrive phone test: phone opened %s; now a %s, %.1f m from the road's middle, %.0f m from you, marked %s, phone shut %s" % [
		opened, car.get_car_name(), off_road, away, is_instance_valid(_car_mark), not phone.is_open()])
	_key(KEY_ESCAPE)
	await wait.call(0.5)
	await shot.call("pause")
	menu.close()
	phone.open()
	phone._open_app("Map")
	await wait.call(0.5)
	await shot.call("phone_map")
	var map_open: bool = menu.is_open() and menu.page == "go"
	menu.back()	# (Back on the phone's page: the menu shuts)
	var ok: bool = opened and car.body == 3 and off_road < 4.0 and away < 60.0 and is_instance_valid(_car_mark) and not phone.is_open() and map_open and not menu.is_open()
	print("Geodrive phone test: ", "PASS" if ok else "FAIL")
	get_tree().quit()


# (the phone's card with that name: its button, behind its text)
func _first_phone_button(node: Node, text: String) -> Node:
	for label in node.find_children("*", "Label", true, false):
		if label.text == text:
			return label.get_parent().get_parent().get_parent().get_child(0)
	return null


# F12: a picture of the game as it is now (H first for none of the HUD), into the Pictures folder's Open-Planet
func _screenshot() -> void:
	await RenderingServer.frame_post_draw
	var folder := OS.get_system_dir(OS.SYSTEM_DIR_PICTURES).path_join("Open-Planet")
	DirAccess.make_dir_recursive_absolute(folder)
	var path := folder.path_join("Open-Planet %s.png" % Time.get_datetime_string_from_system().replace(":", "-").replace("T", " "))
	get_viewport().get_texture().get_image().save_png(path)
	hud.set_status("Picture saved, in Pictures, Open-Planet", 2.0)


func _save(path: String) -> void:
	get_viewport().get_texture().get_image().save_png(path)
	var gpu_ms := RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
	print("Geodrive: picture saved to ", path, " (graphics card %.1f ms a frame, %d thousand triangles, %d draw calls)" % [gpu_ms,
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME) / 1000,
		RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME)])


# --- the speed test: an autopilot that follows the roads as fast as the bakkie goes (slowing for bends), logging how
# high the car sits over the road and anything going wrong ---

func _autopilot(delta: float) -> Vector3:
	test_time += delta
	var at := car.global_position
	var forward := -car.global_transform.basis.z
	forward.y = 0.0
	forward = forward.normalized()
	var speed: float = abs(car.get_speed_kmh()) / 3.6
	var ahead: Dictionary = world.nearest_road(at + forward * max(12.0, speed * 0.9) + Vector3.UP * 2.0, 40.0)
	var steer_to := 0.0
	var bend := 0.0
	if ahead.get("found", false):
		var to: Vector3 = ahead["position"] - at
		to.y = 0.0
		var angle := forward.signed_angle_to(to.normalized(), Vector3.UP)
		steer_to = clamp(-angle * 2.5, -1.0, 1.0)
		bend = abs(angle)
	# as fast as it goes on the straight, slower the sharper the road bends ahead
	var limit: float = lerp(45.0, 6.0, clamp(bend / 0.6, 0.0, 1.0))
	var throttle := 1.0 if speed < limit else 0.0
	var brake: float = clamp((speed - limit) / 8.0, 0.0, 1.0)

	# how the car sits: its origin (the ground under its wheels) against the road and the ground under it
	var road: Dictionary = world.nearest_road(at, 10.0)
	var over_road: float = at.y - (road["position"] as Vector3).y if road.get("found", false) else 99.0
	var over_ground: float = at.y - world.ground_height(at)
	speed_log -= delta
	var broken := not (at.is_finite() and car.linear_velocity.is_finite())
	var sinking := broken or (over_road < -0.4 and over_ground < -0.4)
	if speed_log <= 0.0 or sinking:
		speed_log = 0.5
		print("Geodrive speed test %.1f s: %d km/h, %s above the road, %.2f m above the ground, up %.2f, %s" % [test_time,
			roundi(speed * 3.6), ("%.2f m" % over_road) if over_road < 90.0 else "no road", over_ground,
			car.global_transform.basis.y.dot(Vector3.UP), car.get_status()])
		if fmod(test_time, 10.0) < 0.49 and hud.perf_label.visible:
			print("Geodrive speed test overlay: ", hud.perf_label.text.replace("\n", " | "))
		stuck_time = stuck_time + 0.5 if speed < 0.3 and test_time > 10.0 else 0.0
		if stuck_time >= 5.0:
			var stuck_at := car.global_position
			back_on_road()
			print("Geodrive speed test: stuck 5 s, R: from ", stuck_at, " to ", car.global_position, " (", world.to_lat_lon(car.global_position), ")")
			stuck_time = 0.0
		if speed < 0.3 and test_time > 10.0 and int(test_time * 2.0) % 10 == 0:
			print("Geodrive speed test: stopped. sleeping ", car.sleeping, ", freeze ", car.freeze, ", velocity ", car.linear_velocity, ", at ", at, " (", world.to_lat_lon(at), ")")
			print(car.get_debug())
	if sinking:
		print("Geodrive speed test: SINKING at ", at, " (", world.to_lat_lon(at), ")\n", car.get_debug())
		var space := get_world_3d().direct_space_state
		var ray := PhysicsRayQueryParameters3D.create(at + Vector3.UP * 30.0, at - Vector3.UP * 30.0, 0xFFFFFFFF, [car.get_rid()])
		var hit := space.intersect_ray(ray)
		print("Geodrive speed test: straight down from 30 m up: ", (str(hit["collider"].name) + " at " + str(hit["position"])) if hit else "nothing",
			"; the ground mesh is at ", world.ground_height(at), "; ", world.collision_status(at))
		var before := car.global_position
		back_on_road()
		print("Geodrive speed test: R moved the car from ", before, " to ", car.global_position)
		speed_test = 0.0
		get_tree().create_timer(2.0).timeout.connect(func():
			print("Geodrive speed test: 2 s after R: ", car.global_position, ", up ", car.global_transform.basis.y.dot(Vector3.UP), ", ", car.get_status())
			get_tree().quit())
	elif test_time > speed_test:
		print("Geodrive speed test: done, %.1f km driven" % trip_km)
		get_tree().quit()
	return Vector3(throttle, brake, steer_to)


# Godot's 0 has moved along with the car (positions stay small and exact): everything kept in Godot's positions moves too
# The ground's shared shader values (where Godot's 0 is, the painted roads): every frame, and at once when Godot's 0 moves
func _update_road_globals() -> void:
	RenderingServer.global_shader_parameter_set("world_offset", world.get_render_offset())	# (the ground's patterns stay put)
	# the painted roads from 1.8 km out on the ground, from right under you high up (where the world brings in no 3D
	# roads: GeodriveWorld, from 300 m over the ground, none from 500)
	var over: float = camera.global_position.y - world.ground_height(camera.global_position)
	RenderingServer.global_shader_parameter_set("road_from", 1800.0 * (1.0 - clampf((over - 250.0) / 250.0, 0.0, 1.0)))
	if world.has_method("get_road_texture") and world.get_road_texture():
		RenderingServer.global_shader_parameter_set("road_map", world.get_road_texture())
		RenderingServer.global_shader_parameter_set("road_origin", world.get_road_origin())
		RenderingServer.global_shader_parameter_set("road_size", world.get_road_size())


func _on_origin_shifted(offset: Vector3) -> void:
	if car:
		car.global_position += offset
	if walker:
		walker.global_position += offset
	if aviation:
		aviation.shift(offset)
	if builder:
		builder.shift(offset)
	if hunt:
		hunt.shift(offset)
	if golf:
		golf.shift(offset)
	camera.position += offset
	# (the shaders' shared positions now, in this same frame: set earlier in it, they'd be a frame out of step)
	_update_road_globals()
	if vegetation:
		vegetation.shift(offset)
	if vegetation and vegetation.visible:
		vegetation.update_globals()
	last_position += offset
	last_camera += offset
	hold_target += offset
	hold_check += offset
	for entry in ride:
		entry[2] += offset
