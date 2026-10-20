# Open-Planet 0.2.1 (early)

A small update to 0.2: it's easier to tell us when something's wrong.

- **Report a problem**, right on the start screen (next to Settings and Quit): write a few words and the game opens our
  bug form on GitHub, filled in, with the report's folder beside it to drag in. No place needs to be loaded first.
- **A map that won't download?** The download box now has a **Report this** button: the report says what was being
  downloaded, where it stopped and why, so you don't have to explain it.
- **Known issues are on GitHub** now, so you can see what we're working on and follow it: the main one is
  [Roads, junctions and tunnels: what we're working on](https://github.com/inlighten420/open-planet/issues/1)
  (tunnels, gaps in junctions, off-ramps, sharp bends), and the
  [known issues](https://github.com/inlighten420/open-planet/issues?q=is%3Aissue+label%3A%22known+issue%22) list.
  We're working on better procedural code for drawing the roads, and on fixing the map (OpenStreetMap, or our own local
  copy of it) where it's wrong.

Already have 0.2? Unzip 0.2.1 over it, or start it from a new folder: your maps carry over either way.

---

# Open-Planet 0.2 (early)

The real world to drive, walk, fly and build in, made from open map data. This is a big one: thank you to everyone who
tried 0.1 and told us what broke.

**Windows.** Unzip anywhere and run Open-Planet.exe: nothing to install. Windows may warn that it's an unknown app:
click More info, then Run anyway.

## Already have 0.1 and downloaded maps?

Keep them, don't download them again. Either unzip 0.2 over your old Open-Planet folder (your "data" folder isn't
touched), or unzip it somewhere new and start it: 0.2 finds the maps your last version downloaded and uses them. To
move them by hand, copy the old "data" folder into the new Open-Planet folder. "KEEP YOUR MAPS.txt" in the zip walks you
through it.

Playing with friends: everyone needs 0.2 (0.1 and 0.2 can't play together).

## Try it straight away

Two places now come with the game, ready the moment it starts: **Try a demo** on the start screen.

- **Honolulu, Oahu**: the city, the airport (walk to a plane on its stand and fly), the mountains and the H-3's tunnels.
- **Monte Carlo, Monaco**: steep streets, the tunnels under the town and the Grand Prix circuit.

Anywhere else: **Play somewhere new**, click the world map, and the game downloads that place while you play. You can
now download **just a town** (a few MB) instead of a whole country, choose Download (and stay) or Download and play, and
search the world map for towns you haven't downloaded yet.

## Help fix the world (no coding needed)

Found a road that's wrong, or something broken? Make something, then press **F6** to send it to us:

- **A road edit**: F2 opens the road editor; drag the road to where it really is, Ctrl+C, then F6.
- **A picture**: F12, then F6.
- **A recording of what goes wrong**: F4 to start, F4 to stop, then F6.
- **A bug report**: Settings > Game > Something wrong?

F6 opens a form on our GitHub page, already filled in by the game (the version, where you are, your road edit), with
a box for **why, and what to look for**. For pictures and recordings, the folder with the file opens beside it: drag it
into the form, and Submit. You need a free GitHub account. Every road edit you send becomes a test the game has to pass,
so it builds roads like it right everywhere. The whole guide: **CONTRIBUTING.md** on our GitHub page.

## What's new

**Roads, tunnels and junctions**
- Tunnels, bridges and roads on different levels stack with real headroom: a bus fits under every road.
- Roads underground are laid as smooth curves instead of the kinks they're drawn with.
- Ramps get a longer line where they need one to climb, and tunnel roads drawn on top of each other are moved apart.
- Short open gaps between pieces of a tunnel are closed, so roads don't pop out and back in.
- Roundabouts are made wide enough for the roads joining them, and every road meets a roundabout level.
- Busy junctions are paved as one surface; forks and off-ramps split cleanly; kerb corners never lie on another road.
- Street widths come from the buildings either side, with parking lanes where the country has them.
- US roads have yellow centre lines.
- The ground follows the roads everywhere: no grass through the tarmac, no gaps at junctions.

**Buildings**
- Buildings no longer overlap the roads or each other, and no longer flicker.

**Fix the world: the road editor (F2)**
- Found a road that's wrong? Press F2. Hide the ground, buildings, trees and water to see just the roads, then drag
  them to where they really are: sideways, up and down, wider or narrower. Add points, smooth a line or make it flow,
  colour the roads by height or steepness, undo and redo. Ctrl+C copies what you drew (it's saved in
  Pictures\Open-Planet too): send it to us, and we use it to make the game build roads like it right, everywhere.

**Controls**
- Every key and mouse button can be changed (Settings > Controls), two keys per action.
- Keys go by where they are on the keyboard, so WASD works on AZERTY and Dvorak.
- The steering wheel has its own settings tab, gamepads have a dead zone, a held key always beats the wheel, and loose
  pedals or a flight stick can no longer drive the car by themselves.
- F: the free camera, to fly around anywhere (F again teleports you there, Backspace takes you back). R puts the car
  back on the nearest road.
- F5: reload the world where you are.
- F12: a picture, saved with where it was taken.
- F4: record what goes wrong; Settings > Game > Something wrong? also writes a bug report. F6 sends either (or your last
  picture or road edit) to us on GitHub.
- The bar at the bottom of the screen always shows Tab (your phone), F5 (reload the world) and F6 (send to us).

**Loading**
- Much faster in big cities (Honolulu went from minutes to under 20 seconds), with a timer on the loading screen.
- New versions keep the maps your last one downloaded.

## Known issues

- **Some roads, junctions and tunnels are still rough:** sharp bends, bumps underground, off-ramps that bend into their
  main road, and tangled junctions that don't join cleanly. Monaco shows it most.
- **Spikes in the distance:** far-off hills sometimes show spikes that aren't really there.
- **Lag after moving very fast for a long time:** flying a jet, or the free camera flat out in one direction (or straight
  up and down), can outrun the world being built ahead of you, and the game stutters for a while. Wait a moment, or
  press F5 (or Settings > Game > Reload the world here) to rebuild the world where you are.
- **The road editor's letter keys** (H, K, L, G and the rest) follow the letters printed on your keys, so on AZERTY
  keyboards they're in different places.

## What we're working on next

Roads, junctions and tunnels. OpenStreetMap is a wonderful map, but much of it is drawn by hand: tunnels are drawn from
below ground, where nobody can see them from the air, so their lines are often a little off. To us the map is a
guideline, not the law: the law is whether a car can really drive it. Next we're building procedural code that finds,
by itself, the closest layout to the map that a car can drive and that makes sense: widening a roundabout that can't
fit its roads, letting a ramp run on straight until it has room to climb, easing a road out from under another. Where
the map itself is wrong, we'll suggest small fixes to OpenStreetMap, so they help everyone who uses it. Every road you
fix in the editor and send us becomes a test of that work, so the more of the world you fix, the better every map
gets.

---
Map data (c) OpenStreetMap contributors (ODbL); heights Copernicus DEM GLO-30 (c) DLR e.V. 2010-2014 and (c) Airbus
Defence and Space GmbH 2014-2018; terrain SRTM; land cover ESA WorldCover. Open-Planet is free software (AGPL-3.0).
