# How Open-Planet works

The ideas behind it, for anyone who wants to help build it. For the detail of each part, follow the links to the code.

The short version: **open map data goes in, a walkable, drivable, flyable world comes out, built round the player as they
move, and everything people make in it is plain files that can be shared.**

**Nothing is pre-rendered.** The files hold only compact data (road and building outlines, ground heights, land cover).
The game builds the 3D world from them as you move, once for each piece you get near, and throws the pieces away when you
leave. That is why a region is a few hundred MB and why changing a setting never means making files again. For the longer
version, for developers: [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md).

## 1. The layers

```
 open data                 Tools/geotile            Core (C++, no engine)        Godot
 ─────────                 ─────────────            ──────────────────────       ─────────────────────
 OpenStreetMap  ──┐
 Copernicus 30 m ─┼──►  packed map files  ──►  roads, heights, ground,  ──►  Godot/extension: streams it in,
 SRTM, WorldCover ┘     (one per region)         buildings, cars, traffic       puts it on screen
                                                                              Godot/*.gd: menus, games, packs,
                                                                              building, multiplayer
```

- **`Tools/geotile`** turns an OpenStreetMap extract into a packed map: the ways are cut into zoom-14 tiles, each
  compressed with zstd, in one PMTiles file ([Core/README.md](../Core/README.md), `Core/GeoTiles`). It also fetches the
  heights. `geotile region <name>` does it all ([ADD_A_REGION.md](ADD_A_REGION.md)). The game runs the same tool itself
  when you go somewhere new (`Godot/region_fetch.gd`).
- **`Core/`** is plain C++ with no game engine in it: the road network, the ground, the buildings, the cars and their
  physics, traffic, people, the games' rules. Because it has no engine in it, another engine (or a server with no
  graphics at all) could use it.
- **`Godot/extension/`** is the thin layer that lets Godot run Core (a GDExtension), and streams the world in.
- **`Godot/*.gd`** is the game: menus, the phone, building, hunting, racing, the workshop, multiplayer.

## 2. The road is the boss

Roads are decided first, by the legal design rules of the country they're in (widths, lanes, grades, curve
smoothness, junction shapes). Then the ground is shaped to fit the roads, not the other way round.

1. **Roads from the map.** Each OpenStreetMap way becomes a road with its widths, lanes and speed from that
   country's rules (`Godot/rules/*.json`, one per country or state; `Core/GeoRoads/RoadDesign`).
2. **Heights solved for the whole network.** A smooth solve with grade limits, and bridges lifted to clear the roads they
   cross (`Core/GeoRoads/RoadHeights`). One shared road surface gives a height at any point (`RoadSurface`).
3. **Corridors.** The ground grid is sunk under each road and shaped with verges, kerbs, cuttings and embankments
   (`Core/GeoGround/Terrain`). Without this you get trenches, grass growing through roads, and "Scalextric" junctions.
4. **Everything else sits on that:** painted lines by each country's rules, buildings fitted round the roads, water,
   tunnels and bridges.

Heights themselves come in layers: Copernicus 30 m as the base, finer LiDAR blended in where it exists (England, and
Rhineland-Palatinate in Germany), SRTM for the far terrain, and ESA WorldCover for what's growing on it.

## 3. How the world loads (the camshaft rule)

The world is built in **pieces** round the player, and streamed in as you move, nearest first:

- A new area builds its first piece (about 12 km across, depending on the RAM) on a worker thread. The next piece starts
  when less than 4 km is built ahead, and joins the last at a seam where the roads are matched up.
- Round you, the ground is detailed within about 2.5 km, coarser out to 8, and plain distant blocks out to 30 km.
  Roads and buildings come in as 1 km tiles within about 7 km.

The hard part isn't building it, it's **not stuttering while you do**. The rule that fixed it is called the **camshaft
rule**, and it's the project creator's idea. Picture the cams on an engine's camshaft: each valve opens at its own
moment, so they never all open at once.

> Workers do all the heavy lifting in the background. Anything that has to touch the main thread takes its own small,
> timed slot each frame. The main thread **never waits** for a worker.

Why it matters: the world stuttered, and we hunted for one slow thing, but there wasn't one. Many jobs were each fine
alone and **all finishing in the same frame**. Spread across frames, they cost nothing you can feel.

How it looks in the code (`Godot/extension/src/geodrive_world.cpp`):
- **A time budget per frame.** Finished chunks and tiles are put into the scene nearest first, until the budget is spent
  (4 ms a frame while playing, 30 ms while the loading screen is up). At least one goes in each frame, so nothing starves.
- **Meshes are built on the worker**, not on the main thread (Godot's `AddSurface` / `ArrayMesh` on main costs
  milliseconds).
- **Never `wait()` for a worker on the main thread.** Ask whether it's ready, and come back next frame if not.
- **Collision is the same:** 62.5 m tiles near the player, a few built per frame, because Godot builds a concave
  shape's search tree on the main thread.
- **Other systems follow the rule too:** `Godot/game_gen.gd` looks at one place per turn, for example.

**Measuring it:** time by the real clock, not the frame count (with `--fixed-fps` every frame is 1/60 s on paper).
`--gpuprofile` and the log's slow-frame lines show where the milliseconds go. The budget it's aimed at: about 8 ms a frame
on a GTX 1070, a new area loaded in under 10 seconds.

## 4. Games made of files

Everything a player makes is a plain file, so it can be shared, edited and mixed:

- **Packs** (`Godot/packs/`, and a player's own in their Saved folder): a pack is a folder with `pack.json`, and any of
  `creatures/`, `weapons/`, `reserves/` (the areas a game is played in). Later packs and files replace earlier ones by id,
  so a pack can change a creature without copying the game ([Godot/packs.gd](../Godot/packs.gd),
  [MAKE_A_CREATURE.md](MAKE_A_CREATURE.md)). The built-in hunting and zombie games are just packs; the engine has no
  animals or weapons of its own.
- **Rules.** A game is *when* something happens, *then* something else (`Core/GeoGame`): a checkpoint is passed, a
  creature is shot, the clock runs out. The game maker writes these for you.
- **Games made by the world.** Real places suit real games: a golf course gets golf, a race track a race, a game
  reserve a hunt, a stadium an outbreak. Where the map has no such place, now and then a square of land gets one. They're
  seeded by where they are, so **friends find the same games** (`Godot/game_gen.gd`).
- **The workshop** (`Godot/workshop.gd`, `shape_editor.gd`): a 3D editor in the game. Shapes (box, wedge, cylinder, cone,
  sphere, tube), colours, finishes, cuts and a grid. What you make is saved as files and becomes a gun, a prop or a
  creature.

## 5. Playing together, and sharing

There are no servers yet; one player hosts (`Godot/net.gd`, Godot's ENet, UDP port 7777; setup in
[MULTIPLAYER.md](MULTIPLAYER.md)).

- **Each player's world is their own copy**, built from the same open data. So the network never sends terrain or
  roads, only **where things are**, as latitude, longitude and height (so each player's own local origin doesn't matter).
- **About 20 times a second** each player sends where they are and how they're moving. Everyone else is shown a
  little in the past (0.12 s), moving smoothly between two updates, which hides lag.
- **The host decides the shared things**: the area (when the host goes somewhere new, everyone does), the animals (sent
  about 10 times a second), what the games are.
- **What the host makes is shared with whoever joins**: their packs are sent over (and again when something new is
  made), and so are the props they've placed. Nothing is kept after leaving.
- **Regions can be shared** as files: a region is a few files in `regions/` and `heights/`, shareable with their
  credits ([ADD_A_REGION.md](ADD_A_REGION.md)).

## 6. Where it could go (ideas, not built yet)

These are open questions, and good places to help:

- **Always-on servers.** Someone hosts a region, and it stays up. Since everyone builds their own copy of the world
  from the same files, a server only needs to track people, creatures and games, not draw anything.
- **Hosting regions for download.** A place to download ready-made regions, so a new player doesn't wait for a big
  country to pack on their own PC.
- **Connecting servers.** Linking regions so you can travel between them, and sharing packs between them.
- **More of the world looking like itself.** Road markings for each country, local building styles, local traffic
  (see "What's still South African or British" in [ADD_A_REGION.md](ADD_A_REGION.md)).
- **Better heights** where finer open data exists (for example, [Mapterhorn](https://mapterhorn.com/)).
- **Cars made in the workshop**, with their own physics file.

If you want to take one on, open an Issue first to say so, so we don't both build it.
