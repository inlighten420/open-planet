# Open-Planet

**Welcome to your virtual planet: a place where we can all connect, play, build and create.**

### [⬇ Download Open-Planet 0.2.1 for Windows (free, about 130 MB)](https://github.com/inlighten420/open-planet/releases/latest)

Unzip it anywhere and run `Open-Planet.exe`; nothing to install. (Windows may say it's an unknown app: More info, Run
anyway.) **Try a demo** to play at once: Honolulu on Oahu (the city, its airport, the mountains and tunnels) or Monte
Carlo. Or choose **Play somewhere new**, click anywhere on the world map, and the game downloads that place (just a town,
a few MB, or the whole region) and takes you there.

Already have an earlier version? Keep your maps: unzip the new one over the old folder, or just start it: it finds the
maps your last version downloaded. What's new: [docs/RELEASE_NOTES_0.2.1.md](docs/RELEASE_NOTES_0.2.1.md). What we know
is still rough, and what we're doing about it: the [known issues](https://github.com/inlighten420/open-planet/issues?q=is%3Aissue+label%3A%22known+issue%22).

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
- **How it works** (nothing is pre-rendered: the world is built live from compact data; how it loads without stuttering, how games and sharing work, where it could go): [docs/HOW_IT_WORKS.md](docs/HOW_IT_WORKS.md).
- **Developer guide** (the layers, coordinates and projections, how pieces stream in, the camshaft rule, how each kind of thing is modelled, where to plug things in): [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md).
- **Playing with friends** (no servers yet: one of you hosts): [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md).
- **Making creatures and game packs:** [docs/MAKE_A_CREATURE.md](docs/MAKE_A_CREATURE.md).

## Help fix the world (no coding needed)

The map is drawn by millions of people, mostly by hand, so some roads come out wrong. You can put them right from inside
the game: press **F2** for the road editor, drag the road to where it really is, **Ctrl+C**, then **F6**: your browser
opens a form here with your edit filled in; say what's wrong and Submit. Pictures (**F12**) and recordings of what goes
wrong (**F4**) go the same way, with **F6**. Every road edit becomes a test the game has to pass, so it builds roads
like it right everywhere. The whole guide: **[CONTRIBUTING.md](CONTRIBUTING.md)**.

## Contributing

Open-Planet is open source, and everyone is welcome to help build it. There's a lot to do, and that's the point:

- **Play it and report what's wrong.** In the game: **F6** sends your last picture, recording or road edit here (see
  [CONTRIBUTING.md](CONTRIBUTING.md)). Or open an issue with where you were and what happened.
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
- **Heights in Monaco and France:** IGN RGE ALTI © IGN, Licence Ouverte Etalab 2.0.
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
