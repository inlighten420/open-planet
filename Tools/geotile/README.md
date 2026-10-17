# geotile

Packs the game's cached OSM data (`osm2_*.json`) into Geodrive map tiles (`.pmtiles`), and checks and measures them.

```
build.bat                                      builds bin\geotile.exe (Visual Studio 2026, no CMake)
geotile region <name> [data folder] [--memory=GB] [--no-heights]   any country, state or province ready to play (docs/ADD_A_REGION.md)
geotile selftest
geotile pack  <osm2.json> <out.pmtiles> [level] [precision]   zstd level (default 19); precision in 1e-7 degrees (1 = exact)
geotile check <osm2.json> <in.pmtiles>         proves nothing the game uses was lost
geotile bench <osm2.json> <in.pmtiles>         sizes and timings against JSON and OSM PBF
geotile pbf   <osm2.json> <out.osm.pbf>        the same area as a standard OSM PBF (e.g. for Planetiler)
geotile packpbf <country.osm.pbf> <out.pmtiles> [level] [precision] [bands] [--around=lat,lon,km] [--memory=GB]
                                               a country (or an area of it) as a region, band by band to fit in RAM
geotile world <country.osm.pbf> <out.world.json>   places and main roads for the in-game world map
geotile airports <out.airports.json> <in.osm.pbf>...   runways (centrelines, width, surface), taxiways and aprons, by airport, for flying
geotile lidar <out.pmtiles> --around=lat,lon,km [--cell=4] [--jobs=8]
                                               England's LiDAR ground heights for an area (put it in Cache/heights)
geotile height <heights.pmtiles> lat lon       one height out of it, as the game reads it
geotile sections <region> <heights-1m> --around=lat,lon,km <out.csv>   real road cross-sections from 1 m LiDAR
geotile copernicus <out.pmtiles> --around=lat,lon,km   Copernicus 30 m base heights (the game takes the lower of it and SRTM)
```

**England's LiDAR heights:** the Environment Agency's National LIDAR Programme composite DTM (1 m, bare earth), fetched
1 km square by 1 km square from its WCS with curl (about 2.7 s a square, 8 at a time), averaged to 4 m and packed as
height tiles (`Core/GeoTiles/HeightTiles.h`, about 45 KB a km2). Downloads are kept next to the output, so a stopped run
carries on. Contains Environment Agency information © Environment Agency and/or database right, Open Government
Licence v3.0. The game uses any archive in `%LOCALAPPDATA%\Geodrive\Cache\heights` instead of the ~25 m SRTM heights.

The format itself lives in [Core/GeoTiles](../../Core/GeoTiles). Results are in [docs/TILE_FORMAT_TEST.md](../../docs/TILE_FORMAT_TEST.md).

**Third-party code:**
- `third_party/json`: nlohmann/json 3.11.3 (MIT)
- `third_party/miniz`: miniz 3.0.2 (MIT), zlib for the PBF writer
- zstd: from `Core/third_party`
