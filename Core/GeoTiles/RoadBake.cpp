// Geodrive Core - GPL (see Core/README.md)

#include "RoadBake.h"

#include <algorithm>

#include "Bytes.h"
#include "PMTiles.h"
#include "TileArchive.h"

namespace geotile
{
	namespace
	{
		/** No real piece comes near this; it stops a broken file from asking for a huge buffer */
		constexpr uint64_t MaxHeightsPerRoad = 1u << 20;
	}

	std::vector<uint8_t> EncodeRoads(const RoadBake& Bake)
	{
		std::vector<const BakedRoad*> Sorted;
		Sorted.reserve(Bake.Roads.size());
		for (const BakedRoad& Road : Bake.Roads)
		{
			Sorted.push_back(&Road);
		}
		std::sort(Sorted.begin(), Sorted.end(), [](const BakedRoad* A, const BakedRoad* B)
		{
			return A->WayId != B->WayId ? A->WayId < B->WayId : A->Part < B->Part;
		});

		ByteWriter W;
		W.U8('G');
		W.U8('R');
		W.U8(RoadBakeFormat);
		W.Varint(Bake.Rules);
		W.Varint(Bake.Style);
		W.Svarint(Bake.CanopyCm);
		W.Varint(Sorted.size());

		int64_t PrevWay = 0;
		LatLon PrevStart;
		int32_t PrevFirst = 0;
		for (const BakedRoad* Road : Sorted)
		{
			W.Svarint(Road->WayId - PrevWay);
			W.Varint(Road->Part);
			W.Svarint(int64_t(Road->Start.Lat) - PrevStart.Lat);
			W.Svarint(int64_t(Road->Start.Lon) - PrevStart.Lon);
			W.Varint(Road->Heights.size());
			// the first height from the previous piece's first, the rest from the one before
			int32_t Prev = PrevFirst;
			for (const int32_t Height : Road->Heights)
			{
				W.Svarint(int64_t(Height) - Prev);
				Prev = Height;
			}
			if (!Road->Heights.empty())
			{
				PrevFirst = Road->Heights[0];
			}
			PrevWay = Road->WayId;
			PrevStart = Road->Start;
		}
		return W.Data;
	}

	bool DecodeRoads(const uint8_t* Data, size_t Size, RoadBake& Out)
	{
		ByteReader R(Data, Size);
		if (R.U8() != 'G' || R.U8() != 'R' || R.U8() != RoadBakeFormat)
		{
			return false;
		}
		Out.Rules = uint32_t(R.Varint());
		Out.Style = uint32_t(R.Varint());
		Out.CanopyCm = int32_t(R.Svarint());
		const uint64_t Count = R.Varint();
		if (!R.Ok() || Count > Size)
		{
			return false;
		}

		int64_t PrevWay = 0;
		LatLon PrevStart;
		int32_t PrevFirst = 0;
		for (uint64_t I = 0; I < Count && R.Ok(); ++I)
		{
			BakedRoad Road;
			Road.WayId = PrevWay + R.Svarint();
			Road.Part = uint32_t(R.Varint());
			Road.Start.Lat = int32_t(PrevStart.Lat + R.Svarint());
			Road.Start.Lon = int32_t(PrevStart.Lon + R.Svarint());
			const uint64_t NumHeights = R.Varint();
			if (!R.Ok() || NumHeights > MaxHeightsPerRoad)
			{
				return false;
			}
			Road.Heights.resize(size_t(NumHeights));
			int32_t Prev = PrevFirst;
			for (int32_t& Height : Road.Heights)
			{
				Height = int32_t(Prev + R.Svarint());
				Prev = Height;
			}
			if (!Road.Heights.empty())
			{
				PrevFirst = Road.Heights[0];
			}
			PrevWay = Road.WayId;
			PrevStart = Road.Start;
			Out.Roads.push_back(std::move(Road));
		}
		return R.Ok() && R.AtEnd();
	}

	std::map<TileXY, std::vector<uint8_t>> SplitRoadsIntoTiles(const RoadBake& Bake)
	{
		std::map<TileXY, RoadBake> Groups;
		for (const BakedRoad& Road : Bake.Roads)
		{
			RoadBake& Group = Groups[TileOf(Road.Start, TileZoom)];
			Group.Rules = Bake.Rules;
			Group.Style = Bake.Style;
			Group.CanopyCm = Bake.CanopyCm;
			Group.Roads.push_back(Road);
		}
		std::map<TileXY, std::vector<uint8_t>> Tiles;
		for (const auto& [Tile, Group] : Groups)
		{
			Tiles[Tile] = EncodeRoads(Group);
		}
		return Tiles;
	}

	bool ReadRoadArchive(const uint8_t* File, size_t Size, RoadBake& Out, std::string& Error)
	{
		PmHeader Header;
		std::vector<PmEntry> Entries;
		if (!ReadPmTiles(File, Size, Header, Entries, Error))
		{
			return false;
		}
		bool bFirst = true;
		for (const PmEntry& Entry : Entries)
		{
			std::vector<uint8_t> Raw;
			if (!UnzipArchiveTile(File, Header, Entry, Raw, Error))
			{
				return false;
			}
			RoadBake Tile;
			if (!DecodeRoads(Raw.data(), Raw.size(), Tile))
			{
				Error = "road tile " + std::to_string(Entry.TileId) + " is broken";
				return false;
			}
			if (!bFirst && (Tile.Rules != Out.Rules || Tile.Style != Out.Style || Tile.CanopyCm != Out.CanopyCm))
			{
				Error = "road tiles were baked under different rules";
				return false;
			}
			bFirst = false;
			Out.Rules = Tile.Rules;
			Out.Style = Tile.Style;
			Out.CanopyCm = Tile.CanopyCm;
			for (BakedRoad& Road : Tile.Roads)
			{
				Out.Roads.push_back(std::move(Road));
			}
		}
		return true;
	}
}
