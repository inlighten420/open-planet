// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "TileArchive.h"

#include <algorithm>

#include "zstd.h"

namespace geotile
{
	namespace
	{
		/** No real tile comes near this; it stops a broken file from asking for a huge buffer */
		constexpr unsigned long long MaxTileBytes = 64ull << 20;
	}

	bool UnzipArchiveTile(const uint8_t* File, const PmHeader& Header, const PmEntry& Entry, std::vector<uint8_t>& Raw, std::string& Error)
	{
		const uint8_t* Packed = File + Header.DataOffset + Entry.Offset;
		const unsigned long long RawSize = ZSTD_getFrameContentSize(Packed, Entry.Length);
		if (RawSize == ZSTD_CONTENTSIZE_ERROR || RawSize == ZSTD_CONTENTSIZE_UNKNOWN || RawSize > MaxTileBytes)
		{
			Error = "tile " + std::to_string(Entry.TileId) + " isn't a zstd frame";
			return false;
		}
		Raw.resize(static_cast<size_t>(RawSize));
		const size_t Got = ZSTD_decompress(Raw.data(), Raw.size(), Packed, Entry.Length);
		if (ZSTD_isError(Got) || Got != Raw.size())
		{
			Error = "tile " + std::to_string(Entry.TileId) + " didn't unzip";
			return false;
		}
		return true;
	}

	bool ReadArchiveTile(const uint8_t* File, const PmHeader& Header, const PmEntry& Entry, DecodedTile& Out, std::string& Error)
	{
		std::vector<uint8_t> Raw;
		if (!UnzipArchiveTile(File, Header, Entry, Raw, Error))
		{
			return false;
		}
		if (!DecodeTile(Raw.data(), Raw.size(), Out))
		{
			Error = "tile " + std::to_string(Entry.TileId) + " is broken";
			return false;
		}
		return true;
	}

	bool ArchiveCovers(const uint8_t* File, size_t Size, int32_t MinLat, int32_t MinLon, int32_t MaxLat, int32_t MaxLon)
	{
		// only the header is needed (the first 127 bytes): bounds at byte 102 as min lon, min lat, max lon, max lat
		if (Size < 127 || std::string(reinterpret_cast<const char*>(File), 7) != "PMTiles")
		{
			return false;
		}
		auto At = [File](size_t Offset) { return int32_t(uint32_t(File[Offset]) | (uint32_t(File[Offset + 1]) << 8) | (uint32_t(File[Offset + 2]) << 16) | (uint32_t(File[Offset + 3]) << 24)); };
		return MinLon >= At(102) && MinLat >= At(106) && MaxLon <= At(110) && MaxLat <= At(114);
	}

	bool ArchiveBounds(const uint8_t* File, size_t Size, int32_t& MinLat, int32_t& MinLon, int32_t& MaxLat, int32_t& MaxLon)
	{
		if (Size < 127 || std::string(reinterpret_cast<const char*>(File), 7) != "PMTiles")
		{
			return false;
		}
		auto At = [File](size_t Offset) { return int32_t(uint32_t(File[Offset]) | (uint32_t(File[Offset + 1]) << 8) | (uint32_t(File[Offset + 2]) << 16) | (uint32_t(File[Offset + 3]) << 24)); };
		MinLon = At(102);
		MinLat = At(106);
		MaxLon = At(110);
		MaxLat = At(114);
		return true;
	}

	bool ReadArchiveArea(const uint8_t* File, size_t Size, int32_t MinLat, int32_t MinLon, int32_t MaxLat, int32_t MaxLon,
		OsmSet& Out, std::string& Error)
	{
		PmHeader Header;
		std::vector<PmEntry> Entries;
		if (!ReadPmTiles(File, Size, Header, Entries, Error))
		{
			return false;
		}
		std::sort(Entries.begin(), Entries.end(), [](const PmEntry& A, const PmEntry& B) { return A.TileId < B.TileId; });

		// the z14 tiles the box touches (y grows southwards)
		LatLon SouthWest, NorthEast;
		SouthWest.Lat = MinLat;
		SouthWest.Lon = MinLon;
		NorthEast.Lat = MaxLat;
		NorthEast.Lon = MaxLon;
		const TileXY A = TileOf(SouthWest, TileZoom), B = TileOf(NorthEast, TileZoom);
		std::vector<DecodedTile> Tiles;
		for (uint32_t Y = std::min(A.Y, B.Y); Y <= std::max(A.Y, B.Y); ++Y)
		{
			for (uint32_t X = std::min(A.X, B.X); X <= std::max(A.X, B.X); ++X)
			{
				const uint64_t Id = ZxyToTileId(TileZoom, X, Y);
				const auto It = std::lower_bound(Entries.begin(), Entries.end(), Id, [](const PmEntry& E, uint64_t V) { return E.TileId < V; });
				if (It == Entries.end() || It->TileId != Id)
				{
					continue;	// nothing mapped there (sea, empty veld)
				}
				DecodedTile& Tile = Tiles.emplace_back();
				if (!ReadArchiveTile(File, Header, *It, Tile, Error))
				{
					return false;
				}
			}
		}
		return Assemble(Tiles, Out, Error, true);
	}

	bool ForEachRoadTile(const uint8_t* File, size_t Size, int32_t MinLat, int32_t MinLon, int32_t MaxLat, int32_t MaxLon,
		const std::function<bool(uint64_t TileId)>& Wanted,
		const std::function<void(uint64_t TileId, const std::string& Highway, const std::vector<LatLon>& Line)>& Do, std::string& Error)
	{
		PmHeader Header;
		std::vector<PmEntry> Entries;
		if (!ReadPmTiles(File, Size, Header, Entries, Error))
		{
			return false;
		}
		std::sort(Entries.begin(), Entries.end(), [](const PmEntry& A, const PmEntry& B) { return A.TileId < B.TileId; });
		LatLon SouthWest, NorthEast;
		SouthWest.Lat = MinLat;
		SouthWest.Lon = MinLon;
		NorthEast.Lat = MaxLat;
		NorthEast.Lon = MaxLon;
		const TileXY A = TileOf(SouthWest, TileZoom), B = TileOf(NorthEast, TileZoom);
		std::vector<LatLon> Line;
		for (uint32_t Y = std::min(A.Y, B.Y); Y <= std::max(A.Y, B.Y); ++Y)
		{
			for (uint32_t X = std::min(A.X, B.X); X <= std::max(A.X, B.X); ++X)
			{
				const uint64_t Id = ZxyToTileId(TileZoom, X, Y);
				if (Wanted && !Wanted(Id))
				{
					continue;
				}
				const auto It = std::lower_bound(Entries.begin(), Entries.end(), Id, [](const PmEntry& E, uint64_t V) { return E.TileId < V; });
				if (It == Entries.end() || It->TileId != Id)
				{
					Do(Id, std::string(), {});	// (nothing mapped there: said, so it's known)
					continue;
				}
				DecodedTile Tile;
				if (!ReadArchiveTile(File, Header, *It, Tile, Error))
				{
					return false;
				}
				Do(Id, std::string(), {});	// (the tile read: even with no roads)
				for (const DecodedTile::TilePiece& Piece : Tile.Pieces)
				{
					const std::string* Highway = nullptr;
					bool bUnderground = false;	// (a tunnel: not seen from above; an open gallery is)
					for (const Tag& T : Piece.Tags)
					{
						if (T.first == "highway")
						{
							Highway = &T.second;
						}
						else if (T.first == "tunnel")
						{
							bUnderground = T.second != "no" && T.second != "avalanche_protector";
						}
					}
					if (!Highway || bUnderground || Piece.Refs.size() < 2)
					{
						continue;
					}
					Line.clear();
					for (const uint32_t Ref : Piece.Refs)
					{
						if (Ref < Tile.Points.size())
						{
							Line.push_back(Tile.Points[Ref]);
						}
					}
					Do(Id, *Highway, Line);
				}
			}
		}
		return true;
	}

	bool ReadArchive(const uint8_t* File, size_t Size, OsmSet& Out, std::string& Error)
	{
		PmHeader Header;
		std::vector<PmEntry> Entries;
		if (!ReadPmTiles(File, Size, Header, Entries, Error))
		{
			return false;
		}
		std::vector<DecodedTile> Tiles(Entries.size());
		for (size_t I = 0; I < Entries.size(); ++I)
		{
			if (!ReadArchiveTile(File, Header, Entries[I], Tiles[I], Error))
			{
				return false;
			}
		}
		return Assemble(Tiles, Out, Error);
	}
}
