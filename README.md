# Open-Planet

**Welcome to your virtual planet: a place where we can all connect, play, build and create.**

Open-Planet is the whole real Earth, made from open map data. Pick anywhere on the globe and you arrive there: your
own street, your own town, the real roads and the ground around them. Walk, drive, fly, hunt, race and play golf on
the real courses. Build a house. Make your own games, creatures and props right inside the world, and play them all
with friends online.

It's a game where you make the games. Welcome to a new era of gaming.

It's early. A lot is still to do, and that's the point of opening it: come and build in it.

(Inside the code it's still called Geodrive, its first name: the classes, files and folders.)

- **The game:** `Godot/` (Godot 4.7). How to build and run it: [Godot/README.md](Godot/README.md).
- **The engine-free core** (roads, ground, car physics, games): `Core/`. See [Core/README.md](Core/README.md).
- **The map tools** (OpenStreetMap to tiles): `Tools/geotile/`. Adding a region: [docs/ADD_A_REGION.md](docs/ADD_A_REGION.md).
- **Making creatures and game packs:** [docs/MAKE_A_CREATURE.md](docs/MAKE_A_CREATURE.md).

## Contributing

Open-Planet is open source, and everyone is welcome to help build it. There's a lot to do, and that's the point:

- **Play it and report what's wrong.** Open an issue with where you were (the place or its numbers) and what happened.
- **Make things for it.** Game packs, creatures, weapons, props and races are plain data files. See
  [docs/MAKE_A_CREATURE.md](docs/MAKE_A_CREATURE.md).
- **Bring your part of the world in.** Add a region and check its roads look right: [docs/ADD_A_REGION.md](docs/ADD_A_REGION.md).
- **Improve the code.** Fork it, make your change, and open a pull request. Small, focused changes are easiest to take in.

By contributing you agree that your work is shared under the same licence (AGPL-3.0).

## Licence

Open-Planet is free software under the **GNU Affero General Public License, version 3** (AGPL-3.0). The full text is in
[LICENSE](LICENSE).

In plain words: you may use it, change it, share it and run it, for free or for money. If you share it, or a changed
version of it, you must share the source too, under the same licence. Because Geodrive is an online game, the AGPL also
covers servers: if you run a changed Open-Planet server that other people play on, you must offer them your changed source.
That keeps every region and server open, however it grows.

Game packs, creatures, props and maps people make with the game are theirs; the licence covers the game's code.

## Credits

The world comes from open data. Thank you to everyone who made it:

- **Map data** © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors, under the Open Database
  License (ODbL). Places are found with OpenStreetMap's search (Nominatim) and the games' places with Overpass.
- **Heights:** SRTM courtesy of the U.S. Geological Survey; ETOPO1, NOAA; Terrain Tiles via AWS Open Data.
- Produced using **Copernicus WorldDEM-30** © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018
  provided under COPERNICUS by the European Union and ESA.
- Contains **Environment Agency** information © Environment Agency and/or database right (Open Government Licence v3).
- **Heights in Rhineland-Palatinate** © GeoBasis-DE / LVermGeoRP, dl-de/by-2-0, www.lvermgeo.rlp.de.
- **Land cover** © ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA
  WorldCover consortium (CC BY 4.0).
- **World map:** Natural Earth (public domain).
- **Default speed limits** (`Godot/rules/`): from the OpenStreetMap wiki's "Default speed limits" (CC BY-SA 2.0), as
  machine-read by osm-legal-default-speeds (Tobias Zwick and contributors, BSD-3-Clause); driving sides from REST
  Countries (MPL 2.0).
- **Code by others:** [Godot Engine](https://godotengine.org) (MIT); [Zstandard](Core/third_party/zstd/LICENSE)
  (BSD); [nlohmann/json](https://github.com/nlohmann/json) and miniz (MIT); the tyre model is a port of Project Chrono's TMeasy tyre (BSD-3-Clause, see
  [Core/GeoCar/ThirdPartyNotices.md](Core/GeoCar/ThirdPartyNotices.md)).

Real places, roads and brands appear because they're in the open map data; Open-Planet isn't connected with or endorsed
by any of them.
