// Geodrive tile tool - GPL (see Core/README.md)
//
// England's LiDAR ground heights (Environment Agency, National LIDAR Programme composite DTM, 1 m, Open Government
// Licence v3) for an area, packed as fine height tiles (Core/GeoTiles/HeightTiles.h) for the game to use instead of
// the ~25 m SRTM heights. Downloaded 1 km square by 1 km square from the Agency's WCS with curl, kept next to the
// output so an interrupted run carries on, averaged to CellMetres.

#pragma once

#include <string>

namespace geotile
{
	/**
	 *  A state survey's 1 m heights (Germany's DGM1: Rheinland-Pfalz's, the Nuerburgring's) as fine heights: its 1 km
	 *  GeoTIFFs of 32-bit floats (uncompressed: Tools/trackpack/dgm_plain.py makes them so), each with its world file
	 *  (.tfw), in UTM 32, averaged to CellMetres. The game uses them where they are, as England's LiDAR.
	 */
	int PackDgm(const std::string& OutPath, const std::string& Folder, double CellMetres, const std::string& Attribution);

	int PackLidar(const std::string& OutPath, double Lat, double Lon, double Km, double CellMetres, int Jobs);
}

namespace geotile
{
	/**
	 *  The ~25 m SRTM heights (AWS Terrain Tiles, Terrarium z12 PNGs) for every part of a region that has map data, plus
	 *  a tile round it, downloaded ahead into the game's terrain cache: a whole country is then playable without waiting
	 *  on AWS (from South Africa a tile can take 10-50 s). Existing tiles are skipped, so it can be stopped and rerun.
	 */
	int PrefetchTerrain(const std::string& RegionPath, const std::string& CacheDir, int Jobs);
}
