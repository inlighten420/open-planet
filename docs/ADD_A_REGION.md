# Adding a region (any country, state or province)

Geodrive builds its world from three free, open sources: **OpenStreetMap** (roads, buildings, places, airports),
**Copernicus GLO-30** heights (and SRTM for the far terrain), and **ESA WorldCover** land cover. Nobody has to ship the
whole world: anyone can make the part they want, in one command, and share it.

## One command

```
geotile region <name> [data folder] [--memory=GB] [--no-heights]
```

`<name>` is any of [Geofabrik's extracts](https://download.geofabrik.de/): a country (`germany`, `japan`, `kenya`), a
state or province (`california`, `bayern`, `ontario`) or a small country (`monaco`). Its id or its name works; a name
that isn't found lists the close ones.

It does, in order, skipping whatever is already done (so a stopped run carries on where it was):

1. **Geofabrik's list** of extracts (fetched once a week): which download, and the country's code.
2. **The OpenStreetMap extract** (`downloads/<name>-latest.osm.pbf`), resumed if the download stops.
3. **The map**, packed: `regions/<name>.pmtiles` (roads, buildings, land use, places: a few times smaller than the
   extract), `<name>.world.json` (towns and main roads for the world map), `<name>.airports.json` (runways, taxiways
   and aprons for flying), and `<name>.region.json` (its name, its country's code, where it came from).
4. **The heights**: Copernicus 30 m for every 1-degree square with map data in it (`heights/cop_<name>.pmtiles`),
   from AWS Open Data.
5. **The far terrain**: the SRTM tiles the game blends the heights with and reaches the horizon with (`terrain/`).

The **land cover** (bush, grass, farmland, forest) is fetched by the game itself as you go, into the same folder.

**In the game:** go somewhere no map covers yet (Go somewhere, or the world map) and the game offers to download it:
the same, run in the background (`geotile region --at=lat,lon`: the smallest extract that covers the spot, so a city
in Germany fetches its Regierungsbezirk, not all of Germany), then it takes you there.

**No waste:** a place with a map is never fetched again; each step is skipped if it's done; a stopped download carries
on; the terrain tiles are shared between neighbouring regions and never fetched twice. Once packed, the downloaded
extract (a few times the map's size) and the raw height tiles are deleted (`--keep-download` keeps the extract, to
pack it again).

**Where it goes:** beside a packaged game, its `data` folder (geotile.exe sits next to Geodrive.exe); otherwise the
game's cache (`%LOCALAPPDATA%\Geodrive\Cache`). Give a folder to put it elsewhere.

**How long, how big:** Monaco takes 40 seconds from nothing. A big country takes much longer, mostly for its heights.
South Africa was a 0.4 GB download, packed into 0.2 GB of map and 2.0 GB of heights.
- `--memory=GB` keeps the packing within that much RAM: a big country is packed band by band.
- `--no-heights` makes just the map, and the game fetches rougher heights as you drive.

Then start the game and go there: Go somewhere, and type a place or click it on the world map.

## Road rules: a file per country

How a country's roads are laid out and driven comes from `Godot/rules/<code>.json`, by the region's country code:
- which side traffic keeps to;
- mph or km/h;
- the limit of a road with no speed sign;
- lane and shoulder widths;
- the style of its lines.

A country without a file still works. It keeps to its real side (the list is in `Core/GeoRoads/Country.cpp`), uses
km/h (mph for the US, Liberia and Myanmar), South Africa's lane sizes, and white lines. Writing a file for your country
is one of the easiest and most useful things to contribute; [Godot/rules/README.md](../Godot/rules/README.md) explains
how. A `rules/<code>.json` in the data folder is read last, to try changes without touching the game.

## Sharing regions

Ready-made regions can be shared (zip the files from `regions/` and `heights/` for one name), with their credits:
- **OpenStreetMap**: © OpenStreetMap contributors, under the [ODbL](https://opendatacommons.org/licenses/odbl/).
  The packed map is a derived database, so it is shared under the ODbL too.
- **Copernicus DEM GLO-30**: © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018, provided under
  COPERNICUS by the European Union and ESA.
- **SRTM** via the AWS Terrain Tiles; **ESA WorldCover** 2021, CC BY 4.0.

The game shows these on screen while you play.

## What's still South African or British

These are the next steps for a truly worldwide game:
- **Markings:** right-hand countries get mirrored white lines, but not their own markings: the US's yellow centre
  lines, or France's lines.
- **Traffic cars:** they look South African (hatches and bakkies).
- **Your car:** its steering wheel is on the right.
- **Towns:** buildings and towns are drawn the same everywhere.
