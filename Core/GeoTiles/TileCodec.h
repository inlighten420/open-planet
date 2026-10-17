// Geodrive Core - GPL (see Core/README.md)
//
// Tile format v0: the OSM ways of one z14 web-mercator tile (about 2 km across at Garden Route latitudes).
//
// A way that crosses tiles is cut into pieces. The node where it's cut is in both pieces, so the pieces join again.
// Nodes keep their global OSM id only when it matters: used more than once (junctions, bridges, closed rings)
// or at a cut. Every other node gets a made-up id when read back, which is all the game needs.
//
// Layout (before zstd): magic "GT", version, then 7 blocks, each a varint length + bytes:
//   strings, points (precision, count, deltas), node ids, way ids, pieces, tags, refs.
// Keeping similar numbers together in their own block is what lets zstd squeeze them well.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "OsmTypes.h"

namespace geotile
{
	constexpr int TileZoom = 14;

	struct TileXY
	{
		uint32_t X = 0;
		uint32_t Y = 0;

		bool operator<(const TileXY& O) const { return Y != O.Y ? Y < O.Y : X < O.X; }
		bool operator==(const TileXY& O) const { return X == O.X && Y == O.Y; }
	};

	/** Which tile a position falls in at a zoom level (web mercator, like every web map) */
	TileXY TileOf(const LatLon& P, int Zoom);

	/** One piece of a way, as stored in a tile */
	struct WayPiece
	{
		int64_t WayId = 0;
		uint32_t Piece = 0;
		bool bLast = true;
		std::vector<Tag> Tags;
		std::vector<int64_t> Nodes; // OSM node ids while packing
	};

	/** A decoded tile: node table plus pieces that refer to it by index */
	struct DecodedTile
	{
		std::vector<LatLon> Points;
		std::vector<int64_t> GlobalIds; // 0 = no global id
		int64_t Quantum = 1;            // point precision in 1e-7 degrees
		struct TilePiece
		{
			int64_t WayId = 0;
			uint32_t Piece = 0;
			bool bLast = true;
			std::vector<Tag> Tags;
			std::vector<uint32_t> Refs;
		};
		std::vector<TilePiece> Pieces;
	};

	struct PackStats
	{
		size_t Ways = 0;
		size_t Pieces = 0;
		size_t MissingNodeRefs = 0;
		size_t GlobalIdNodes = 0;
	};

	/** Cuts all ways into per-tile pieces. Tags must already be filtered. */
	std::map<TileXY, std::vector<WayPiece>> SplitIntoTiles(const OsmSet& Set, PackStats& Stats);

	/** Ids that must survive packing: nodes referenced more than once, or at a cut between pieces */
	std::unordered_map<int64_t, bool> NodesNeedingIds(const std::map<TileXY, std::vector<WayPiece>>& Tiles);

	/**
	 *  Encodes one tile (uncompressed). KeepId says which node ids must be kept.
	 *  Quantum is the point precision in 1e-7 degrees: 1 keeps OSM exactly (~1 cm), 10 is ~10 cm.
	 */
	std::vector<uint8_t> EncodeTile(const std::vector<WayPiece>& Pieces, const OsmSet& Set, int64_t Quantum,
		const std::unordered_map<int64_t, bool>& KeepId);

	/** Decodes one uncompressed tile; false if the data is broken */
	bool DecodeTile(const uint8_t* Data, size_t Size, DecodedTile& Out);

	/** Joins decoded tiles back into whole ways. Nodes without a global id get unique negative ids. */
	bool Assemble(const std::vector<DecodedTile>& Tiles, OsmSet& Out, std::string& Error);

	/** The same for some of an archive's tiles (an area): ways are cut where they leave it */
	bool Assemble(const std::vector<DecodedTile>& Tiles, OsmSet& Out, std::string& Error, bool bPartial);
}
