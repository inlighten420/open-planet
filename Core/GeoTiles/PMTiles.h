// Geodrive Core - GPL (see Core/README.md)
//
// PMTiles v3: many tiles in one file, found by a small directory at the start.
// Any plain web server (or free static hosting) can serve it, because a reader only asks for byte ranges.
// Spec: https://github.com/protomaps/PMTiles/blob/main/spec/v3/spec.md

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace geotile
{
	/** Hilbert-curve tile number, as the PMTiles spec defines it */
	uint64_t ZxyToTileId(int Z, uint32_t X, uint32_t Y);

	enum class PmCompression : uint8_t { Unknown = 0, None = 1, Gzip = 2, Brotli = 3, Zstd = 4 };

	struct PmHeader
	{
		uint64_t RootOffset = 0, RootLength = 0;
		uint64_t MetadataOffset = 0, MetadataLength = 0;
		uint64_t LeafOffset = 0, LeafLength = 0;
		uint64_t DataOffset = 0, DataLength = 0;
		uint64_t AddressedTiles = 0, TileEntries = 0, TileContents = 0;
		uint8_t Clustered = 1;
		PmCompression InternalCompression = PmCompression::None;
		PmCompression TileCompression = PmCompression::None;
		uint8_t TileType = 0; // 0 = unknown/custom
		uint8_t MinZoom = 0, MaxZoom = 0;
		int32_t MinLonE7 = 0, MinLatE7 = 0, MaxLonE7 = 0, MaxLatE7 = 0;
		uint8_t CenterZoom = 0;
		int32_t CenterLonE7 = 0, CenterLatE7 = 0;
	};

	struct PmEntry
	{
		uint64_t TileId = 0;
		uint64_t Offset = 0; // within the tile data section
		uint32_t Length = 0;
		uint32_t RunLength = 1;
	};

	/** Writes tiles (keyed by tile id) into one archive */
	bool WritePmTiles(const std::map<uint64_t, std::vector<uint8_t>>& Tiles, PmHeader Header, const std::string& MetadataJson,
		std::vector<uint8_t>& Out, std::string& Error);

	/**
	 *  Reads the header and root directory of a whole archive in memory, and checks every tile lies inside the file.
	 *  Only uncompressed directories without leaf directories are supported so far (what WritePmTiles makes).
	 */
	bool ReadPmTiles(const uint8_t* File, size_t Size, PmHeader& Header, std::vector<PmEntry>& Entries, std::string& Error);
}
