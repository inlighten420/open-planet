// Geodrive Core - GPL (see Core/README.md)
//
// Fine ground heights in 1 km squares of a national grid (England's LiDAR in the British National Grid), packed
// one zstd frame per square in a PMTiles file like the map tiles. A square's tile id is ZxyToTileId(11, easting km,
// northing km), so every square of Great Britain (700 x 1300 km) has its own id.
//
// A square: "GDH1", cells per side (u16), cell size in cm (u16), its south-west corner in grid metres (2 x i32), then
// the heights row by row from the north edge, west to east, in centimetres above sea level: each the varint
// difference from the one before (zigzag), with "no data" (sea, gaps in the survey) as 0 and heights as cm + 100001.

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "PMTiles.h"

namespace geotile
{
	constexpr int HeightTileZoom = 11;

	/** A square's heights in metres, row 0 at the north edge; NaN where there is no data */
	struct HeightTile
	{
		int Size = 0;				// cells per side
		double CellMetres = 0.0;
		int32_t SouthWestE = 0;		// grid metres
		int32_t SouthWestN = 0;
		std::vector<float> Heights;

		bool IsEmpty() const { return Heights.empty(); }
	};

	constexpr float NoHeight = std::numeric_limits<float>::quiet_NaN();

	std::vector<uint8_t> EncodeHeightTile(const HeightTile& Tile);
	bool DecodeHeightTile(const uint8_t* Data, size_t Size, HeightTile& Out);

	/** The tile id of the 1 km square whose south-west corner is at (EastingKm, NorthingKm) */
	uint64_t HeightTileId(int EastingKm, int NorthingKm);
}
