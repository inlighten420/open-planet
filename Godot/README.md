# Geodrive on Godot

Geodrive's engine-free Core (`Core/`) running in Godot 4.7: the real roads (junctions, painted lines, heights solved
as the game does), buildings and the ground shaped round the roads, from the same map data and caches as the game.

## Live (the GeodriveWorld extension)

`extension/` is a GDExtension: a `GeodriveWorld` node whose `start_area(lat, lon)` streams the world in as the Unreal game does: the
first piece (Core/GeoWorld, ~12 km by the RAM there is) builds on a worker, then the ground (detailed within 2.5 km,
coarse to 8, distant blocks to 30), roads and buildings (1 km tiles within 7 km) come in round the car, nearest first, a
few ms a frame; the next piece builds when less than 4 km is built ahead and joins the last at the seam; old pieces go.
Missing height tiles are downloaded (AWS Terrain Tiles). Teleports in the built area stream the place in first; further
ones start a new area (Cape Town centre ~19 s, Bloemfontein ~10 s, Knysna ~5 s).
A `GeodriveCar` (a RigidBody3D) runs Core's car physics (`Core/GeoCar`: suspension sweeps, TMeasy tyres, engine, clutch,
gearbox, diff, ABS and traction control): its body, wheels and setup come from Core (`Core/GeoCar/CarDesigns.cpp`), the same as the Unreal game's: for now the
bakkie, the Isuzu KB300 TDi.

Build it once (needs Python 3 and CMake, which comes with Visual Studio):

```
git clone --depth 1 https://github.com/godotengine/godot-cpp.git <somewhere>/godot-cpp
Godot_v4.7.2-stable_win64_console.exe --headless --dump-extension-api        (writes extension_api.json)
cmake -S Godot/extension -B <build dir> -DGODOT_CPP_DIR=<somewhere>/godot-cpp -DGODOTCPP_CUSTOM_API_FILE=<path>/extension_api.json
cmake --build <build dir> --config Release
```

The library goes to `Godot/bin`. For an exported game (no editor) build the release library too, in a second build folder:
`cmake -S Godot/extension -B <release build dir> ... -DGODOTCPP_TARGET=template_release` (same options as above), then
`cmake --build <release build dir> --config Release`; it's `bin/geodrive.windows.template_release.x86_64.dll`.

Open the project in the Godot editor once (or run it with `--headless --import`) so Godot registers the extension.

## Run

```
Godot_v4.7.2-stable_win64.exe --path Godot -- --lat=-33.9375 --lon=18.44 --km=6
```

(any place with map data works, England too: the region and the UK rules are found from where it is; areas come out of `%LOCALAPPDATA%/Geodrive/Cache/regions`, heights from
its `terrain` and `heights` folders, so the game must have cached the area's height tiles.)

Started with no place given, it opens on the front menu, as the Unreal game does (`menu.gd`): Drive (choose your car,
then an area and where in it to start, or type a latitude, longitude), Car, Settings, Quit. While a new area builds, the
loading screen (`loading_screen.gd`, `globe.gd`) shows the Unreal game's turning globe with a pin on the place and what
Core is building ("Reading the map", "Road heights", ..., "Putting in the ground and roads"), then fades out.

You start on a main road near the place, with the Unreal game's controls (the full list is at the top
of `main.gd`): W/S or the arrows throttle and brake (hold the brake when stopped to reverse), A/D steer, Space
handbrake, L diff lock, E/Q gears, G automatic; C cycles the view (chase, cockpit, cockpit without the steering wheel,
bonnet), Z/X/V and the mouse look round; F free camera (F again puts the car on the road there); M map; Backspace back on its wheels; H HUD, F3 numbers, B report a bad road spot; Esc or Tab the menu (resume, teleport to the
area's places or your saved spots, save a spot, change car, drive somewhere else, settings, quit). As in the Unreal game the HUD
shows DIFF LOCK and STALLED, the map and height-data credits are always along the bottom (even with the HUD off), friends'
names show how far away they are, and a hard knock (over 2 m/s lost in one step) logs a `[Knock]` line with where, which
road and what was hit (Ground or RoadsAndBuildings), for finding map problems.

Settings (`settings.gd`, saved as they change): Graphics (preset, find the best for this PC, render resolution with FSR,
anti-aliasing, shadows, lighting, textures, view distance, building distance, window, window size, VSync, frame limit, FPS / memory
overlay), Controls (keyboard steering speed, wheel force, brake lock / scrub / road effects, clutch pedal or automatic,
cockpit field of view), Game (car, world piece size, speed units, on-screen text, debug info). What means the same in
both games is shared with the Unreal game through its GameUserSettings.ini (the car, units, steering speed, cockpit FOV,
world pieces, building distance, the wheel's forces, the camera view, saved spots); Godot's graphics go in a [Godot]
section of it. Test options: `--drivetest`, `--flytest`, `--keytest`,
`--wheeltest`, `--walktest` (with `--behind`: the camera behind them), `--fly=kind,airport` (0 fighter, 1 airliner, 2 regional jet; a code or name), `--planetest`, `--view=`, `--teleport=lat,lon`, `--map`, `--side`, `--high`, `--look=up,pitch,yaw`, `--shot=<file.png>`, `--menu`, `--menutest=<folder>`. `--verbose` logs everything (slow frames and the world's detail
lines: heights lifted, ground checks, slow chunks), as the tests do; normal play logs only the main lines.

## From a file (no extension)

`geotile godot <region.pmtiles> <Cache/terrain> --box=s,w,n,e Godot/areas/<name>.glb [--uk]` writes an area as a glTF
file; `-- --glb --area=<name>` shows it.

A `GeodriveWheel` node drives the car from a steering wheel and pedals with force feedback (Core's `GeoFFB`, the same as
the Unreal game's; settings as properties: gain, wheel_rotation, steering_ratio, max_force_nm, axes...). Space stays the
handbrake. `-- --wheeltest` reports the wheel (no forces) and quits.

Road scan (Settings > Game > Debug > Scan the roads, or `--scan`): each piece built is scanned by Core's `RoadScan`; the summary goes to the log and every fault to Saved/RoadReports/scan_<time>_piece<n>.csv.
Multiplayer (`net.gd`, Esc > Multiplayer): host or join by address over Tailscale, as in the Unreal game; Godot players only.
Traffic (Settings > Game > Traffic, 0-150 cars, shared with the Unreal game; Core's `GeoTraffic`, run by `GeodriveWorld.update_traffic`): GeodriveCars in their own colours on the roads round you, a third parked; bumps they feel go to Saved/RoadReports/traffic_<time>.csv. `--traffic=<n>` for tests.
The world map (`world_map.gd`, from the menu: every region's towns and main roads out of Cache/regions/*.world.json,
read by `GeodriveWorldIndex`; scroll to zoom, drag, click a town to go there, search, quick buttons for the cities).
Sound (`GeodriveCarSound`: Core's synthesiser, as in the Unreal game; Settings > Sound). Settings > Controls sets the
pedals (press one all the way down) and the wheel's buttons for looking round, the seat and the view (Core's
`WheelSetup`, shared with the Unreal game), and the seat per car.
