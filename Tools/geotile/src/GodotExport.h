// Geodrive tile tool - AGPL-3.0 (see LICENSE)
//
// An area's roads at their real heights as a glTF binary (.glb) for a Godot client to load, built entirely by the
// engine-free Core (Core/GeoTiles to read the map, Core/GeoRoads for the network, heights and road surface) with
// SRTM ground heights from the game's terrain cache. The first step of running Geodrive on Godot.

#pragma once

#include <string>

namespace geotile
{
	/**
	 *  geotile godot <region.pmtiles> <Cache/terrain> --box=s,w,n,e <out.glb> [--uk]
	 *  Writes the roads (ribbons coloured by surface) and the ground (a 20 m grid) in Godot's axes (x east, y up,
	 *  z south, metres, origin at the box centre), and <out>.json with where to start (a main road near the middle).
	 */
	int ExportGodot(const std::string& RegionPath, const std::string& TerrainDir, double South, double West, double North, double East,
		const std::string& OutPath, bool bUK);
}
