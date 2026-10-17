// Geodrive tile tool - GPL (see Core/README.md)
//
// Real road cross-sections from 1 m LiDAR (geotile lidar --cell=1): every road of an area, every 10 m away from
// junctions, from 15 m left to 15 m right of its middle, by road class and setting (town: buildings close by; country:
// none). Also where the kerbs are (a rise of 7 cm or more within half a metre), their height, the carriageway width
// between them and its crossfall. The basis for road templates: what each kind of road really looks like.

#pragma once

#include <string>

namespace geotile
{
	int RoadSections(const std::string& RegionPath, const std::string& HeightsPath, double Lat, double Lon, double Km, const std::string& OutCsv);
}
