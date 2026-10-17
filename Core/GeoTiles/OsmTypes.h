// Geodrive Core - GPL (see Core/README.md)
//
// Plain OSM data the way Geodrive uses it: ways with their tags and node lists, and node positions.
// Positions are whole numbers of 1e-7 degrees (OSM's own precision, about 1 cm), never floats.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace geotile
{
	/** A position in 1e-7 degrees */
	struct LatLon
	{
		int32_t Lat = 0;
		int32_t Lon = 0;

		bool operator==(const LatLon& O) const { return Lat == O.Lat && Lon == O.Lon; }
	};

	using Tag = std::pair<std::string, std::string>;

	struct Way
	{
		int64_t Id = 0;
		std::vector<int64_t> Nodes;
		std::vector<Tag> Tags;
	};

	struct OsmSet
	{
		std::unordered_map<int64_t, LatLon> Nodes;
		std::vector<Way> Ways;
	};

	/**
	 *  The tag keys the game reads (Parse/OsmData.cpp, Build/GeoMeshBuilder.cpp, Elevation/GeoTerrain.cpp).
	 *  The position in this list is the key's number in the tile format, so only ever add to the end.
	 */
	const std::vector<std::string>& KeptKeys();

	/** Index into KeptKeys(), or -1 */
	int KeptKeyIndex(const std::string& Key);

	/** Drops tags the game doesn't use and sorts the rest by key number, so output is the same on every machine */
	void FilterTags(std::vector<Tag>& Tags);
}
