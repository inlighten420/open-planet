// Geodrive Core - GPL (see Core/README.md)
//
// A Geodrive map archive: Geodrive tiles (TileCodec.h), each squeezed with zstd, in one PMTiles file.

#pragma once

#include <functional>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "OsmTypes.h"
#include "PMTiles.h"
#include "TileCodec.h"

namespace geotile
{
	/** Unzips one tile of an archive (any Geodrive tile kind) */
	bool UnzipArchiveTile(const uint8_t* File, const PmHeader& Header, const PmEntry& Entry, std::vector<uint8_t>& Raw, std::string& Error);

	/** Unzips and decodes one tile of an archive */
	bool ReadArchiveTile(const uint8_t* File, const PmHeader& Header, const PmEntry& Entry, DecodedTile& Out, std::string& Error);

	/** Whether an archive covers a lat/lon box (1e-7 degrees); only its first 127 bytes (the header) are needed */
	bool ArchiveCovers(const uint8_t* File, size_t Size, int32_t MinLat, int32_t MinLon, int32_t MaxLat, int32_t MaxLon);
	/** Its bounds (1e-7 degrees), from the header: false if it isn't an archive */
	bool ArchiveBounds(const uint8_t* File, size_t Size, int32_t& MinLat, int32_t& MinLon, int32_t& MaxLat, int32_t& MaxLon);

	/** Reads only the tiles of an archive that a lat/lon box touches (1e-7 degrees): an area out of a country.
	 *  Ways that leave the box are cut there. */
	bool ReadArchiveArea(const uint8_t* File, size_t Size, int32_t MinLat, int32_t MinLon, int32_t MaxLat, int32_t MaxLon,
		OsmSet& Out, std::string& Error);

	/** Every road (a way tagged highway) in the tiles a lat/lon box touches (1e-7 degrees), as each tile has it (cut at
	 *  the tile's edges, not joined up): its highway value and points, one tile at a time (TileId: which, to keep it) */
	bool ForEachRoadTile(const uint8_t* File, size_t Size, int32_t MinLat, int32_t MinLon, int32_t MaxLat, int32_t MaxLon,
		const std::function<bool(uint64_t TileId)>& Wanted,
		const std::function<void(uint64_t TileId, const std::string& Highway, const std::vector<LatLon>& Line)>& Do, std::string& Error);

	/** Reads every tile of an archive in memory and joins them back into whole ways */
	bool ReadArchive(const uint8_t* File, size_t Size, OsmSet& Out, std::string& Error);
}
