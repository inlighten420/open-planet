# Tile format test (2026-10-12)

**Question:** how small and how fast can map data be, if the world is streamed from a server instead of downloaded as one big file per area?

**Answer:** the Garden Route (108 × 24 km) goes from **54 MB of JSON to 2.14 MB**, with nothing the game uses lost. Reading it takes **0.2 s instead of 3.7 s**, and the whole load dropped from about 16 s to 12.3 s. The game builds exactly the same roads.

## Results

Same Garden Route box (`osm2_-34.1200_22.3300_-33.9000_23.5000.json`): 59,873 ways and 454,955 nodes.

| Version | Size | Notes |
|---|---|---|
| JSON as cached today | 54.10 MB | |
| JSON, minified | 41.15 MB | |
| JSON, minified + zstd 3 | 7.04 MB | |
| JSON, minified + zstd 19 | 3.87 MB | still one file, still needs the full JSON parse |
| JSON, game tags only + zstd 19 | 3.84 MB | dropping unused tags barely helps; points are most of the data |
| OSM PBF (standard OSM format), all tags | 2.65 MB | one file, can't be streamed by area |
| OSM PBF, game tags only | 2.60 MB | |
| Planetiler → PMTiles, same layers, z14 | 1.63 MB | **lossy:** ~0.5 m grid, simplified lines, **junction/bridge links lost** |
| **Ours, exact (~1 cm)** | **2.14 MB** | 547 tiles, average 3.9 KB, largest 94 KB |
| **Ours, ~10 cm** | **1.69 MB** | all links kept |
| **Ours, ~50 cm** (Planetiler's grid) | **1.35 MB** | all links kept, no simplifying; 17% smaller than Planetiler |
| Ours + trained zstd dictionary | 2.04 MB | only 5% better, so not used |

**Speed (ours, exact):** decoding all 547 tiles takes 0.02 s, joining them into whole roads 0.15 s, and the largest tile ~1 ms.

**In the game** (headless `-GeoWorldTest` load, same box):

| | JSON | Packed tiles |
|---|---|---|
| Read + parse map data | 3.69 s (3.61 s on a rerun) | **0.20 s** |
| Total load | 16.1 s (15.8 s on a rerun) | **12.3 s** |
| Road segments / junctions / buildings / road triangles | 30,275 / 18,385 / 42,522 / 3,996,873 | identical |
| Road scanner (jolts, launches, grass, holes) and all height lines | 14,793 / 44 / 72 / 44 | identical |

The only differences between runs are in the short live car test and the streaming order. Those depend on real time, and they differ just as much between two JSON runs.

## What we learned

- **Most of the size is node positions, not tags.** Dropping unused tags saves almost nothing (3.87 → 3.84 MB).
- **The standard OSM PBF is already compact** (2.65 MB). Ours is 20% smaller and, more importantly, **split into small tiles**, so a client can fetch only what's near the car.
- **Planetiler's map tiles are smaller only because they throw data away.** They keep no shared node ids, which is what joins roads at junctions and bridges, so the gaps and jolts we fixed would come back. At the same 50 cm grid, ours is smaller anyway.
- **We probably don't need 1 cm.** OSM roads are only accurate to about a metre. 10 cm saves 21% and changes nothing you could see. This should be tested with the road scanner and robot before switching.
- **Integers everywhere.** Points are whole numbers of 1e-7 degrees, so every machine reads exactly the same positions. That matters for multiplayer and for "same roads on every PC".

## How the format works (v0)

- **Tiles:** web-mercator zoom 14, about 2 km across at this latitude, the same size the game streams.
- **Roads that cross tiles** are cut into pieces. The cut node is in both pieces, so they join again exactly.
- **Node ids** are kept only where they matter: nodes used more than once (junctions, bridges, closed loops) and cut points. 68k of 455k nodes keep their id; the rest get made-up ids on load.
- **Columns:** each tile stores strings, points, ids, way ids, pieces, tags and node references in separate blocks, then zstd squeezes it. Similar numbers sit together, which is why it compresses well.
- **Container:** PMTiles v3, one file served by any web server using byte ranges. That's the self-hosting story: put the file on any static host.
- **Only the tags the game reads are kept** (list in `Core/GeoTiles/OsmTypes.cpp`).

Code: the format is in `Core/GeoTiles` (engine-free C++, no exceptions, shared by the tool and the game). The packer is `Tools/geotile`. The game side is `Plugins/GeoWorld/.../Parse/OsmTiles.cpp`.

## How to repeat it

```
Tools\geotile\build.bat
Tools\geotile\bin\geotile.exe pack  <osm2_box.json> <osm2_box.pmtiles> [zstd level] [precision]
Tools\geotile\bin\geotile.exe check <osm2_box.json> <osm2_box.pmtiles>
Tools\geotile\bin\geotile.exe roads <roads_box.roadbin> <roads_box.pmtiles>   (or just: powershell -File Tools\bake_roads.ps1)
Tools\geotile\bin\geotile.exe bench <osm2_box.json> <osm2_box.pmtiles>
Tools\geotile\bin\geotile.exe pbf   <osm2_box.json> <osm2_box.osm.pbf>
```

Put the `.pmtiles` next to the `.json` in `%LOCALAPPDATA%\Geodrive\Cache` with the same name. The game uses it automatically; `-GeoUseJson` forces the old path.

Planetiler (Java 21) used a small custom profile with the same layers at z14, reading the PBF from `geotile pbf`:
`java -jar planetiler.jar generate-custom --schema=geodrive.yml --bounds=22.33,-34.12,23.50,-33.90 --minzoom=14 --maxzoom=14 --output=planetiler.pmtiles`.
The standard OpenMapTiles profile needs about 1.3 GB of worldwide ocean data and makes all zoom levels, so it wasn't a fair comparison.

## Next

1. Test 10 cm precision with the road scanner and robot. If nothing changes, make it the default.
2. ~~Bake road fixes into the tiles~~ **Done (2026-10-12):** solved road heights per road piece in whole centimetres, in `roads_<box>.pmtiles` (`Core/GeoTiles/RoadBake.h`, `geotile roads`, `Tools/bake_roads.ps1`). Players skip the solver.
3. **Stream by tile** instead of loading a whole area: load tiles near the car, prefetch ahead, and "plan your trip" fetches the route's tiles in advance.
4. Lower-detail zoom levels for far away (main roads only at 8 km+), in the same file.
5. Terrain in the same archive (quantised mesh / Martini), replacing the Terrarium PNGs.
