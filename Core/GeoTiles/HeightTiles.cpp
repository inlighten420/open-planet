// Geodrive Core - GPL (see Core/README.md)

#include "HeightTiles.h"

#include <cmath>

#include "Bytes.h"

namespace geotile
{
	namespace
	{
		constexpr int64_t Offset = 100001;	// 0 means no data; -1000 m is far below any ground
	}

	uint64_t HeightTileId(int EastingKm, int NorthingKm)
	{
		return ZxyToTileId(HeightTileZoom, uint32_t(EastingKm), uint32_t(NorthingKm));
	}

	std::vector<uint8_t> EncodeHeightTile(const HeightTile& Tile)
	{
		ByteWriter W;
		W.U8('G');
		W.U8('D');
		W.U8('H');
		W.U8('1');
		W.Fixed(uint64_t(Tile.Size), 2);
		W.Fixed(uint64_t(std::llround(Tile.CellMetres * 100.0)), 2);
		W.Fixed(uint64_t(uint32_t(Tile.SouthWestE)), 4);
		W.Fixed(uint64_t(uint32_t(Tile.SouthWestN)), 4);
		int64_t Previous = 0;
		for (const float H : Tile.Heights)
		{
			const int64_t Value = std::isnan(H) ? 0 : std::llround(double(H) * 100.0) + Offset;
			W.Svarint(Value - Previous);
			Previous = Value;
		}
		return W.Data;
	}

	bool DecodeHeightTile(const uint8_t* Data, size_t Size, HeightTile& Out)
	{
		ByteReader R(Data, Size);
		if (R.U8() != 'G' || R.U8() != 'D' || R.U8() != 'H' || R.U8() != '1')
		{
			return false;
		}
		Out.Size = int(R.Fixed(2));
		Out.CellMetres = double(R.Fixed(2)) / 100.0;
		Out.SouthWestE = int32_t(uint32_t(R.Fixed(4)));
		Out.SouthWestN = int32_t(uint32_t(R.Fixed(4)));
		if (!R.Ok() || Out.Size <= 0 || Out.Size > 4096 || Out.CellMetres <= 0.0)
		{
			return false;
		}
		Out.Heights.resize(size_t(Out.Size) * Out.Size);
		int64_t Value = 0;
		for (float& H : Out.Heights)
		{
			Value += R.Svarint();
			H = Value == 0 ? NoHeight : float(double(Value - Offset) / 100.0);
		}
		return R.Ok();
	}
}
