// Geodrive Core - GPL (see Core/README.md)
//
// Baked roads: the solved height profile of every road piece, so a game doesn't have to run the road solver itself
// (the slowest part of building an area). The ways themselves stay in the OSM tiles; this only adds heights.
//
// A road piece is one stretch of an OSM way between two junctions (or ends), named by the way's id and its number
// along the way (Part, counting from the way's start in the direction traffic goes). Its heights are at the road
// points the game makes along it: the way's own points with extra ones every few metres, so a game rebuilds the same
// points from the same OSM data and then takes their heights from here.
//
// Layout of a road tile (before zstd): magic "GR", format version, rules version, road style, canopy height, then the
// pieces sorted by way and part. Numbers are varints; ids, positions and heights are stored as steps from the one
// before, which keeps them small. Heights are whole centimetres above sea level.
//
// Roads go in the z14 tile of their first point, in a PMTiles archive of their own (roads_<box>.pmtiles next to the
// osm2_<box>.pmtiles), each tile zstd-squeezed like the map tiles.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "OsmTypes.h"
#include "TileCodec.h"

namespace geotile
{
	constexpr uint8_t RoadBakeFormat = 1;

	struct BakedRoad
	{
		int64_t WayId = 0;
		uint32_t Part = 0;
		LatLon Start;                 // the piece's first point, which decides its tile (and lets a reader check it)
		std::vector<int32_t> Heights; // cm above sea level, one per road point
	};

	struct RoadBake
	{
		uint32_t Rules = 0;      // the game's own version of its road rules: a bake made under other rules isn't used
		uint32_t Style = 0;      // the game's road style when baked (follow the land / legal grades)
		int32_t CanopyCm = 0;    // how far the elevation data reads above the ground in forest, as measured when baked
		std::vector<BakedRoad> Roads;
	};

	/** Encodes roads (in any order) as one uncompressed road tile */
	std::vector<uint8_t> EncodeRoads(const RoadBake& Bake);

	/** Decodes one uncompressed road tile, appending its roads; false if the data is broken */
	bool DecodeRoads(const uint8_t* Data, size_t Size, RoadBake& Out);

	/** Groups roads by the z14 tile of their first point, each group encoded as one tile (uncompressed) */
	std::map<TileXY, std::vector<uint8_t>> SplitRoadsIntoTiles(const RoadBake& Bake);

	/** Reads every tile of a road archive in memory (roads_<box>.pmtiles) */
	bool ReadRoadArchive(const uint8_t* File, size_t Size, RoadBake& Out, std::string& Error);
}
