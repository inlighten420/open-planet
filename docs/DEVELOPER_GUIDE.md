# Developer guide

For anyone who wants to work on Open-Planet. [HOW_IT_WORKS.md](HOW_IT_WORKS.md) is the short tour; this is the longer one:
where things live, the rules the code follows, and where to plug a new thing in. If something here disagrees with the code,
the code wins: please fix this page.

(Inside the code the project is still called **Geodrive**, its first name: `Geodrive*` classes, `Core/Geo*` folders.)

## 1. The one idea: nothing is pre-rendered

The files hold **compact data**: road and building outlines, ground heights, land cover. The game **builds the 3D world
from that data, live, round the player**, and throws the pieces away when the player leaves. No pictures of the world and
no finished 3D models of it are stored anywhere.

- A *piece* of the world is built **once** when you get near it (meshes made from the numbers), kept in memory while you
  are near, and drawn by the graphics card like any normal 3D model. Nothing is recomputed every frame.
- That is why a whole region is a few hundred MB, why every player builds their own copy from the same files, why the
  network only sends *where things are*, and why changing a setting (view distance, road style) never means re-making files.

Everything below follows from that.

## 2. The layers

```
 open data         Tools/geotile           Core (C++, no engine)          Godot/extension         Godot/*.gd
 ---------         --------------          ---------------------          ---------------         ----------
 OpenStreetMap -+
 Copernicus 30m +-> packed region files -> roads, heights, ground,  -----> GeodriveWorld, ...  -> menus, phone, games,
 SRTM, WorldCover+   (data/regions,         buildings, cars, people,        (a GDExtension:        building, workshop,
                      data/heights)          aircraft, traffic, rules        thin wrappers)         multiplayer
```

| Layer | Folder | Rule |
|---|---|---|
| Packer | `Tools/geotile/` | A command-line tool. Turns an OpenStreetMap extract (and heights) into packed files. The game runs it for you when you go somewhere new (`Godot/region_fetch.gd`). |
| Core | `Core/` | Plain C++. **No engine types, no exceptions.** Everything that is *the game's logic* (roads, ground, cars, physics, people, rules) lives here, so another engine, or a server with no graphics, could use it. |
| Extension | `Godot/extension/src/` | Thin classes that let Godot call Core and put what it builds into the scene. Registered in `register_types.cpp`. |
| Game | `Godot/*.gd` | GDScript: the menus, phone, controls, camera, games, building, workshop, multiplayer. |

If you are adding *logic*, it probably goes in Core. If you are adding *a screen or a button*, it goes in GDScript.

## 3. Data: from OpenStreetMap to a file

- **Map:** `geotile` reads an `.osm.pbf` and keeps only the ways the game builds from (drivable roads, buildings, water,
  woods, sand, forest, plus leisure, protected areas and airports for the games). They are cut into **zoom-14
  web-mercator tiles** (about 2 km across), each compressed with zstd, in one **PMTiles** file per region
  (`Core/GeoTiles/`, format in [TILE_FORMAT_TEST.md](TILE_FORMAT_TEST.md)). A way that crosses a tile is cut into pieces
  that share the cut node, so they join again.
- **Other files per region** (`data/regions/<stem>.*`): `.world.json` (towns and main roads, for the world map),
  `.airports.json`, `.games.json` and `.golf/` (places for games), `.region.json` (name, country, source).
- **Heights:** Copernicus 30 m as the base (`data/heights/cop_<stem>.pmtiles`), finer LiDAR blended in where it
  exists (England, part of Germany), SRTM / AWS Terrain Tiles for the far terrain (fetched as needed into `terrain/`),
  ESA WorldCover for what is growing on it (`landcover/`).
- **Size:** a region's map is about 0.35-0.45 times the size of its `.osm.pbf`. Heights are most of the bytes
  (South Africa: 220 MB map, 2 GB heights).
- Adding a region by hand, and sharing one: [ADD_A_REGION.md](ADD_A_REGION.md).

## 4. Coordinates and projections (read this before touching positions)

Several frames are in use. Most "it's in the wrong place" bugs are a missed conversion.

| Where | Frame |
|---|---|
| Core roads, ground, pieces (`GeoRoads`, `GeoGround`, `GeoWorld`) | **centimetres**, X north, Y east, Z up (left-handed) |
| Core physics (`GeoBody`, `GeoFlight`) | **metres**, x east, y north, z up (right-handed); a body's own x forward, y left, z up |
| Core cars (`GeoCar`) | metres, kg, N; a world frame chosen by the host ("the same one throughout"); see `CarSim.h` |
| People (`GeoPeople`) | cm and cm/s, x north, y east, z up |
| Godot | Y up, metres (the extension converts at its edge) |
| On the network | **latitude, longitude, height**, never local positions |

Projections:

1. **Local tangent plane** (`Core/GeoRoads/Projection.h`): latitude/longitude to world cm around an origin, using the
   WGS84 radii at that origin. About 0.2 m error at 1 km, fine for an area. `ToWorld` and `ToLatLon`.
2. **A shifted projection per piece** (`Projection::Shifted`): a piece far from the drive's origin is measured from its
   own point, so its single-precision mesh corners stay exact. Do not mix positions from two pieces without converting.
3. **Floating origin in Godot** (`GeodriveWorld::get_render_offset`): Godot's 0 is not the drive's 0, so the car never
   gets far from the origin. A world-fixed position is a Godot position plus this offset. For anything that must survive
   a re-origin, store lat/lon (`to_lat_lon` / `from_lat_lon`).
4. **Web mercator tiles** (zoom 14) for addressing map tiles in the PMTiles file. The **world map screen** also draws in
   mercator (`world_map.gd`).

Rule of thumb: store and send **lat/lon**; convert to a local frame only to compute or draw.

## 5. Pieces, chunks and tiles (how the world streams in)

The world is built in **pieces** round the player, nearest first (numbers from `HOW_IT_WORKS.md`, `GeodriveWorld`):

1. **A piece** is a box about 12 km across (8-16 km, by the RAM). `Core/GeoWorld/Piece.cpp` builds it in one go on a
   **worker thread**: reads the map data out of the region archive, loads heights, solves the road network (joined to the
   previous piece at a seam, `GeoRoads/RoadSeams`), prepares the ground builder, cuts roads, lines and buildings into 1 km
   tiles, and, for a brand new area, finds where to start. `PieceRequest::Stage` says which step it is on (the loading
   screen shows it).
2. **Chunks and tiles** come from the piece on demand: the ground in detailed chunks within about 2.5 km, coarser out to
   8 km, plain distant blocks to 30 km; roads and buildings as 1 km tiles within about 7 km. Each is made **on a worker**
   (mesh included) and put into the scene a few milliseconds per frame.
3. **When less than 4 km is built ahead** of the car, the next piece starts. Old pieces go when nothing near needs them.
4. **Settings change how far**, not what: view distance, detail distance, building distance, piece size
   (`GeodriveWorld` setters, Settings > Graphics).
5. **Missing height tiles** are downloaded as they are needed.

The road is decided first, and the ground is shaped to fit it: roads from the map and the country's legal design rules,
heights solved for the whole network, corridors sunk and shaped in the ground, then everything else sits on that
(HOW_IT_WORKS section 2).

## 6. The camshaft rule (the most important rule)

> Workers do all the heavy lifting in the background. Anything that has to touch the main thread takes its own small,
> timed slot each frame. The main thread **never waits** for a worker.

It is the project creator's idea: like the cams on a camshaft, each job opens at its own moment so they never all open at
once. The world used to stutter; there was no single slow thing, many jobs each fine alone were *finishing in the same
frame*. Spread across frames they cost nothing you can feel.

Checklist when you add anything that loads or builds:

- [ ] Do the heavy work on a worker (`WorkerThreadPool` in GDScript, a thread in the extension).
- [ ] Build meshes on the worker, not on the main thread (`ArrayMesh` / `AddSurface` on main costs milliseconds).
- [ ] On the main thread, only ask "is it ready?" and come back next frame. **Never `wait_for_task_completion` on a task
      that is not finished** (waiting on a finished one is fine).
- [ ] Put finished things into the scene under a **time budget** (4 ms a frame while playing, 30 ms on the loading
      screen), nearest first, **at least one per frame** so nothing starves.
- [ ] If many things finish together, give each a timed turn (see `Godot/game_gen.gd`: one place per turn).
- [ ] Collision too: 62.5 m tiles near the player, a few per frame (Godot builds a concave shape's tree on main).
- [ ] Measure with the **real clock**, not frame counts (`--fixed-fps` makes every frame 1/60 s on paper).

Budget to aim at: about **8 ms a frame on a GTX 1070**, a new area loaded in under 10 s. Tools: `--gpuprofile`, and the
log's slow-frame lines (`--verbose`).

## 7. Classes for everything: how things are modelled

Every kind of thing in the world has the same shape of code: **an engine-free Core class that owns the rules, a thin
wrapper that puts it in Godot, and GDScript that decides when and where.** The Core class never knows about Godot; the host
tells it what the world looks like through a small interface.

| Thing | Core (rules, engine-free) | Godot wrapper | GDScript |
|---|---|---|---|
| The world | `GeoWorld/Piece`, `GeoRoads/*`, `GeoGround/*`, `GeoMesh/*` | `GeodriveWorld` | `main.gd` |
| Cars | `GeoCar/CarSim` (physics), `CarDesigns` (the bodies), `CarSound`, `Tire` | `GeodriveCar` (a `RigidBody3D`), `GeodriveCarSound` | `main.gd`, `car_camera.gd` |
| Steering wheels | `GeoFFB/*` | `GeodriveWheel` | `menu.gd` (setup) |
| Traffic | `GeoTraffic/Traffic`, `RobotDriver` | via `GeodriveWorld` (`update_traffic`) | `main.gd` |
| People on foot | `GeoPeople/Body` (measures from height, how they walk) | `GeodriveWorld::walk` | `walker.gd` |
| Aircraft | `GeoFlight/Flight`, `GeoMesh/Aircraft` | `GeodriveAircraft` | `aviation.gd` |
| Buildings you make | `GeoBuild/Build` (grid, support rule, real materials) | `GeodriveStructure` | `builder.gd` |
| Animals and weapons | `GeoHunt/Hunt`, `CreatureFile` (files in, real figures) | `GeodriveHunt` | `hunt.gd`, `weapon.gd`, `creatures.gd` |
| Game rules | `GeoGame/Rules` ("when X, then Y", from a file) | `GeodriveGame` | `game_gen.gd`, `game_maker.gd` |

### The host interfaces

Core asks the host for the little it needs; that is what keeps it engine-free:

- `CarWorld` (`GeoCar`): sphere sweeps and "push at a point". The host owns the rigid body and collision; each physics
  step it hands the car the body state.
- `TrafficHost` (`GeoTraffic`): the host puts traffic cars in its world and moves them.
- `Surroundings` (`GeoBody`): gravity, air density, water density and a `Water(x, y)` height function.
- `ParallelFor` and a log, passed in by the host (`ThreadsParallelFor` is there for hosts without a job system).

### Shared physics: what is and isn't shared

`GeoBody` is the shared rigid-body physics: **a body is a list of parts** (box, cylinder, sphere; solid or a shell; of a
real material with a real density, or a weighed lump). Mass, centre of mass and inertia are *measured from the parts*, never
typed in. **One motion function** moves every body; things that push it are `ForcePart`s (tyre, wing, engine,
propeller) that call `Body::Push`. Water lift (Archimedes) comes from the parts' volumes, so things float as deep and as
level as they should.

- `GeoFlight` and `GeoBuild` are built on `GeoBody`.
- **Cars are the exception for now:** `GeoCar/CarSim` has its own detailed physics (suspension sweeps, TMeasy tyres,
  drivetrain) and Godot's `RigidBody3D` owns the car's body. Bringing cars onto the `GeoBody` parts model, so a car built in
  the workshop weighs what it should, is an open idea.

If you add a new vehicle or object, the intended route is: describe it as `GeoBody` parts, give it `ForcePart`s, write a
Core class for its rules, wrap it, then script its behaviour.

## 8. Where to plug a new thing in

| I want to add... | Do this |
|---|---|
| **A country's road rules** | A JSON file `Godot/rules/<code>.json` (a country or state; `AU-NSW`, `US-CA`...) and see what is still South African or British in [ADD_A_REGION.md](ADD_A_REGION.md). Logic lives in `Core/GeoRoads/Country`, `RoadDesign`. |
| **A region (map data)** | `geotile region <geofabrik id>` or use *Play somewhere new* in the game. See [ADD_A_REGION.md](ADD_A_REGION.md). |
| **A creature, weapon or game** | A **pack**: a folder in `Godot/packs/` (or a player's Saved folder) with `pack.json` and `creatures/`, `weapons/`, `reserves/`. No code. [MAKE_A_CREATURE.md](MAKE_A_CREATURE.md). Later files replace earlier ones by id. |
| **A game's rules** | JSON "rules" in a pack's area (`GeoGame/Rules.h` lists every *when* and *then*). New kinds of when/then are Core changes. |
| **A new car** | `Core/GeoCar/CarDesigns.cpp` (body, wheels, cameras, setup, engine sound), then the car picker in `settings.gd` / `menu.gd`. |
| **A new vehicle type** | The route in section 7: `GeoBody` parts and `ForcePart`s, a Core class, a wrapper in `Godot/extension/src`, registered in `register_types.cpp`, scripted in GDScript. |
| **A key or mouse action** | An entry in `Godot/controls.gd`'s `ACTIONS` table (name, label, group, defaults). Read it with `Controls.down("name")` or `Controls.hit(event, "name")`. The Settings > Controls page lists it by itself. |
| **A setting** | `Godot/settings.gd` (`GameSettings`: a variable, loaded and saved in the ini; Graphics settings go in `[Godot]`), then a row in `menu.gd`. Settings the Unreal game shares are read and written through `unreal_settings.gd`. |
| **A menu screen** | `menu.gd` (`GameMenu`), a `_..._page` function; helpers `_button`, `_stepper`, `_note`, `_heading`, `_text_action`. |
| **A game made by the world** | `Godot/game_gen.gd` (looks at one place per turn: camshaft). |
| **Something you can build in the workshop** | `workshop.gd`, `shape_editor.gd`, `part_model.gd`. |

### Controls, in detail

`Controls` is a single table of actions, each with two slots (a key or a mouse button). Keys are stored by their **place on
the keyboard** (`physical_keycode`), so WASD is the same four keys on AZERTY or Dvorak, and shown by what is printed on
them. Mouse buttons are stored as minus their number. Saved as `Key_<action>` in the settings file. Not rebindable on
purpose: Esc, F1, F3, closing menus, the builder's number keys, mouse look. A wheel's or gamepad's button asks for an
action by name (`main.gd` `_do([...])`) rather than pretending to press a key.

## 9. Testing

- **Syntax-check a script:** `Godot_..._console.exe --headless --path Godot --check-only --script res://main.gd`
  (run `--import` once after adding a new `class_name`).
- **Behaviour tests run with command-line flags**, after `--`: `--drivetest`, `--walktest`, `--enginetest`, `--flytest`,
  `--speedtest`, `--hunttest`, `--racetest`, and more (the list is at the top of `Godot/main.gd`). Run them
  `--headless --fixed-fps 60`; open a window only when you need pictures (`--shot=`, `--menutest=<folder>`).
- **Tools:** `geotile selftest`, and the road scan (`Settings > Game > Advanced > Scan the roads`, CSVs in
  `Saved/RoadReports`).
- **Measure by the real clock.** `--fixed-fps` makes every frame 1/60 s on paper.
- **Debug recording (F4):** press F4 (or Settings > Game > *Start a recording*), do what goes wrong, press F4 again. It
  saves a zip with `recording.txt` (every key press and which actions it became, whether a menu ate it, what recover and
  teleport decided, and the car, camera, held keys, screens up and world state twice a second) plus the log, settings and
  a picture. Add `rec("...")` where a decision is made and it appears in the recording.
- **Bug reports from players:** Settings > Game > *Something wrong?* writes a zip (log, settings, controllers, game state,
  picture) to Documents/Open-Planet/Bug reports.
- **The road diagram:** `--dumproads=<file>` writes an area's roads as solved (heights, levels, bridges, tunnels, the
  elevation under each point) and quits; `python Tools/road_levels.py <file> <page.html>` makes a page of them by height,
  with a side view of any road clicked and a list of what looks wrong (levels too close, roads on top of each other, ends
  short of a road, too steep, tilted junctions). `--roadsonly` draws only the roads in the game.
- **The truth test (are the roads right?):** `powershell Tools/truth_areas.ps1` solves the roads of every area with LiDAR
  (Monaco, the Nordschleife, five English places) as if there were none (`--truth`), and `Tools/road_truth.py` scores them
  against the LiDAR: the height error by road class, the grade error, pieces too steep for their class, tilted junctions,
  and one SCORE line. Run it before and after any change to the road heights; the last results are in `docs/truth_all.csv`.
- **1 m heights for a place in France or Monaco:** `python Tools/trackpack/ign_fetch.py <name> <lat> <lon> <km>` (IGN's
  RGE ALTI, on the UTM 32 grid the game's fine heights use), then `geotile dgm <Cache/heights/<name>.pmtiles> <folder>
  --cell=1 --credit="..."`. Germany's: `Tools/trackpack/rlp_fetch.py`.

## 10. Building and packaging

- **The extension** (needs Python 3 and CMake): [Godot/README.md](../Godot/README.md). Build the **release** library too
  for exported games.
- **geotile:** `Tools/geotile/build.bat` (Visual Studio C++ compiler).
- **A shareable game:** `pwsh -File Tools/package_godot.ps1 -Places none|south_africa|all` exports the game, adds the
  data folder, `geotile.exe` and the how-to files, and zips it.
- Player data: settings and buildings in `%APPDATA%\Godot\app_userdata\Geodrive`; maps in the `data` folder beside the game
  (a new version keeps using the earlier folder's maps if its own has none).

## 11. Conventions

- **Licence:** AGPL-3.0; the source files say so in their header. Map data is ODbL: credit OpenStreetMap, and keep
  attribution for the height data (`GeodriveWorld::get_attribution`).
- **Core:** no engine types, no exceptions, SI units (metres, kilograms, seconds) except where a module says otherwise
  (the roads and ground are in cm: check the header). Say the frame at the top of every header.
- **Comments say why**, not what: short, plain, and in the code's own voice. Asides go in (brackets).
- **Real values, not "game feel":** real densities, real speeds, real laws (the hunting rules are the real ones).
  Add extras as options; don't change the realistic baseline.
- **No artificial limits** (stamina bars, cooldowns): leave them to mods.
- **Never block the main thread** (section 6).
- **Open an Issue first** to say you are taking something on, so two people do not build the same thing.

## 12. Rough edges (honest list)

- Core mixes **cm and metres** and **two handedness conventions** (section 4). Check a header before trusting a number.
- `Godot/main.gd` is very large: it wires everything and holds many of the test modes. Splitting it is welcome.
- Cars do not use `GeoBody` yet (section 7).
- Some names are the old ones (Geodrive, Unreal-era settings file and section names).
- Controls: the builder's, golf's and phone's own keys are still fixed.
- Small areas can be downloaded from OpenStreetMap's public Overpass servers, which are sometimes busy; ready-made
  regions to download (section 6 of HOW_IT_WORKS) would be better.
