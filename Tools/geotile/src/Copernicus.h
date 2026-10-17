// Geodrive tile tool - AGPL-3.0 (see LICENSE)
//
// Copernicus GLO-30 heights (ESA/DLR/Airbus WorldDEM, ~30 m, 2011-2015; free on AWS Open Data) for an area, packed as
// height tiles on a latitude/longitude grid of 1 arc-second (HeightTiles.h, zoom 14 ids: 120 arc-second squares), for
// the game to use as its base heights instead of SRTM (the older 2000 survey, smoothed into the AWS Terrain Tiles).
// The 1-degree COG tiles are fetched with curl and kept next to the output.

#pragma once

#include <string>

namespace geotile
{
	int PackCopernicus(const std::string& OutPath, double Lat, double Lon, double Km, int Jobs);

	/** The same for every 1-degree tile that has some of a region's map data in it (a whole country) */
	int PackCopernicusRegion(const std::string& OutPath, const std::string& RegionPath, int Jobs);
}
