// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "HeightRaster.h"
#include "Mesh.h"
#include "BritishGrid.h"
#include "HeightTiles.h"
#include "PMTiles.h"
#include "TileArchive.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <map>

namespace geoground
{
	double HeightRaster::LonToTileX(double Lon, int32_t Z) { return (Lon + 180.0) / 360.0 * (1 << Z); }

	double HeightRaster::LatToTileY(double Lat, int32_t Z)
	{
		const double LatRad = Lat * (Pi / 180.0);
		return (1.0 - std::log(std::tan(LatRad) + 1.0 / std::cos(LatRad)) / Pi) * 0.5 * (1 << Z);
	}

	void HeightRaster::SetBox(double South, double West, double North, double East, int32_t InZoom)
	{
		Zoom = InZoom;
		MinTileX = static_cast<int32_t>(std::floor(LonToTileX(West, Zoom)));
		MinTileY = static_cast<int32_t>(std::floor(LatToTileY(North, Zoom)));
		TilesX = static_cast<int32_t>(std::floor(LonToTileX(East, Zoom))) - MinTileX + 1;
		TilesY = static_cast<int32_t>(std::floor(LatToTileY(South, Zoom))) - MinTileY + 1;
		Heights.assign(size_t(TilesX) * TileSize * TilesY * TileSize, 0.0f);
	}

	void HeightRaster::SetTile(int32_t Index, const uint8_t* Pixels, int32_t Channels)
	{
		const int32_t Width = TilesX * TileSize;
		const int32_t TileX = Index % TilesX;
		const int32_t TileY = Index / TilesX;
		for (int32_t Y = 0; Y < TileSize; ++Y)
		{
			for (int32_t X = 0; X < TileSize; ++X)
			{
				const uint8_t* Pixel = &Pixels[(size_t(Y) * TileSize + X) * Channels];
				// Terrarium encoding
				Heights[size_t(TileY * TileSize + Y) * Width + TileX * TileSize + X] = Pixel[0] * 256.0f + Pixel[1] + Pixel[2] / 256.0f - 32768.0f;
			}
		}
	}

	float HeightRaster::SampleCoarse(double Lat, double Lon) const
	{
		if (!IsValid())
		{
			return 0.0f;
		}
		const int32_t Width = TilesX * TileSize;
		const int32_t Height = TilesY * TileSize;
		// pixel centres sit at +0.5
		const double PX = (LonToTileX(Lon, Zoom) - MinTileX) * TileSize - 0.5;
		const double PY = (LatToTileY(Lat, Zoom) - MinTileY) * TileSize - 0.5;

		const int32_t X0 = std::clamp(static_cast<int32_t>(std::floor(PX)), 0, Width - 1);
		const int32_t Y0 = std::clamp(static_cast<int32_t>(std::floor(PY)), 0, Height - 1);
		const int32_t X1 = std::min(X0 + 1, Width - 1);
		const int32_t Y1 = std::min(Y0 + 1, Height - 1);
		const float FX = std::clamp(static_cast<float>(PX - std::floor(PX)), 0.0f, 1.0f);
		const float FY = std::clamp(static_cast<float>(PY - std::floor(PY)), 0.0f, 1.0f);

		const float H00 = Heights[size_t(Y0) * Width + X0], H10 = Heights[size_t(Y0) * Width + X1];
		const float H01 = Heights[size_t(Y1) * Width + X0], H11 = Heights[size_t(Y1) * Width + X1];
		return LerpF(LerpF(H00, H10, FX), LerpF(H01, H11, FX), FY);
	}

	float HeightRaster::Sample(double Lat, double Lon) const
	{
		const float Coarse = SampleCoarse(Lat, Lon);
		// at or below sea level in SRTM is water the game draws with its sea plane at 0 m (tidal rivers such as the Thames,
		// whose OSM water areas are multipolygons the map data doesn't carry yet). LiDAR has the water's real surface there,
		// a metre or two up, which would put grass over the river: the coarse height stays
		if (Fine.empty() || Coarse <= 0.5f || bIgnoreFine)
		{
			return Coarse;
		}
		double E, N;
		geotile::ToFineGrid(FineGridKind, Lat, Lon, E, N);
		// cell centres sit at +0.5
		const double PX = (E - FineWestE) / FineCell - 0.5;
		const double PY = (FineNorthN - N) / FineCell - 0.5;
		const int32_t X0 = static_cast<int32_t>(std::floor(PX)), Y0 = static_cast<int32_t>(std::floor(PY));
		if (X0 < 0 || Y0 < 0 || X0 + 1 >= FineWidth || Y0 + 1 >= FineHeight)
		{
			return Coarse;
		}
		const size_t I = size_t(Y0) * FineWidth + X0;
		const float H00 = Fine[I], H10 = Fine[I + 1], H01 = Fine[I + FineWidth], H11 = Fine[I + FineWidth + 1];
		if (std::isnan(H00) || std::isnan(H10) || std::isnan(H01) || std::isnan(H11))
		{
			return Coarse;
		}
		const float FX = static_cast<float>(PX - X0), FY = static_cast<float>(PY - Y0);
		const float Height = LerpF(LerpF(H00, H10, FX), LerpF(H01, H11, FX), FY);
		// near the edge of the fine heights, eased into the coarse ones so there's no step
		const int32_t Edge = std::min(std::min(FineEdge[I], FineEdge[I + 1]), std::min(FineEdge[I + FineWidth], FineEdge[I + FineWidth + 1]));
		return Edge >= FadeCells ? Height : LerpF(Coarse, Height, SmoothStepF(0.0f, 1.0f, float(Edge) / FadeCells));
	}

	bool HeightRaster::SampleFine(double Lat, double Lon, float& OutMetres) const
	{
		if (Fine.empty())
		{
			return false;
		}
		double E, N;
		geotile::ToFineGrid(FineGridKind, Lat, Lon, E, N);
		const double PX = (E - FineWestE) / FineCell - 0.5;
		const double PY = (FineNorthN - N) / FineCell - 0.5;
		const int32_t X0 = static_cast<int32_t>(std::floor(PX)), Y0 = static_cast<int32_t>(std::floor(PY));
		if (X0 < 0 || Y0 < 0 || X0 + 1 >= FineWidth || Y0 + 1 >= FineHeight)
		{
			return false;
		}
		const size_t I = size_t(Y0) * FineWidth + X0;
		const float H00 = Fine[I], H10 = Fine[I + 1], H01 = Fine[I + FineWidth], H11 = Fine[I + FineWidth + 1];
		if (std::isnan(H00) || std::isnan(H10) || std::isnan(H01) || std::isnan(H11))
		{
			return false;
		}
		const float FX = static_cast<float>(PX - X0), FY = static_cast<float>(PY - Y0);
		OutMetres = LerpF(LerpF(H00, H10, FX), LerpF(H01, H11, FX), FY);
		return true;
	}

	int64_t ApplyBase(HeightRaster& Raster, const std::vector<Archive>& Archives)
	{
		constexpr int32_t TileSize = HeightRaster::TileSize;
		const int32_t Width = Raster.TilesX * TileSize, Height = Raster.TilesY * TileSize;
		const double N = double(1 << Raster.Zoom);
		auto PixelLon = [&](int32_t X) { return (Raster.MinTileX + (X + 0.5) / TileSize) / N * 360.0 - 180.0; };
		auto PixelLat = [&](int32_t Y) { return std::atan(std::sinh(Pi * (1.0 - 2.0 * (Raster.MinTileY + (Y + 0.5) / TileSize) / N))) * (180.0 / Pi); };
		const double West = PixelLon(0), East = PixelLon(Width - 1), North = PixelLat(0), South = PixelLat(Height - 1);
		int64_t Replaced = 0;
		for (const Archive& File : Archives)
		{
			geotile::PmHeader Header;
			std::vector<geotile::PmEntry> Entries;
			std::string Error;
			if (!File.Data || !geotile::ReadPmTiles(File.Data, File.Size, Header, Entries, Error) || Header.MinZoom != 14
				|| Header.MaxLatE7 < South * 1e7 || Header.MinLatE7 > North * 1e7 || Header.MaxLonE7 < West * 1e7 || Header.MinLonE7 > East * 1e7)
			{
				continue;
			}
			std::sort(Entries.begin(), Entries.end(), [](const geotile::PmEntry& A, const geotile::PmEntry& B) { return A.TileId < B.TileId; });
			// the squares under the raster, decoded once
			std::map<std::pair<int32_t, int32_t>, geotile::HeightTile> Squares;
			const int32_t X0 = static_cast<int32_t>(std::floor((West + 180.0) * 30.0)), X1 = static_cast<int32_t>(std::floor((East + 180.0) * 30.0));
			const int32_t Y0 = static_cast<int32_t>(std::floor((South + 90.0) * 30.0)), Y1 = static_cast<int32_t>(std::floor((North + 90.0) * 30.0));
			for (int32_t Y = Y0 - 1; Y <= Y1 + 1; ++Y)
			{
				for (int32_t X = X0 - 1; X <= X1 + 1; ++X)
				{
					const uint64_t Id = geotile::ZxyToTileId(14, uint32_t(X), uint32_t(Y));
					const auto It = std::lower_bound(Entries.begin(), Entries.end(), Id, [](const geotile::PmEntry& E, uint64_t V) { return E.TileId < V; });
					std::vector<uint8_t> Raw;
					geotile::HeightTile Tile;
					if (It != Entries.end() && It->TileId == Id && geotile::UnzipArchiveTile(File.Data, Header, *It, Raw, Error)
						&& geotile::DecodeHeightTile(Raw.data(), Raw.size(), Tile))
					{
						Squares.emplace(std::make_pair(X, Y), std::move(Tile));
					}
				}
			}
			if (Squares.empty())
			{
				continue;
			}
			// one arc-second cell by its column and row counted from -180 / -90
			auto Cell = [&](int64_t Col, int64_t Row) -> float
			{
				const auto T = Squares.find({ static_cast<int32_t>(std::floor(Col / 120.0)), static_cast<int32_t>(std::floor(Row / 120.0)) });
				if (T == Squares.end())
				{
					return geotile::NoHeight;
				}
				const int64_t X = Col - T->second.SouthWestE, Y = T->second.Size - 1 - (Row - T->second.SouthWestN);
				return (X >= 0 && Y >= 0 && X < T->second.Size && Y < T->second.Size) ? T->second.Heights[size_t(Y) * T->second.Size + X] : geotile::NoHeight;
			};
			for (int32_t Y = 0; Y < Height; ++Y)
			{
				const double PY = (PixelLat(Y) + 90.0) * 3600.0 - 0.5;
				const int64_t R0 = int64_t(std::floor(PY));
				const float FY = float(PY - R0);
				for (int32_t X = 0; X < Width; ++X)
				{
					const double PX = (PixelLon(X) + 180.0) * 3600.0 - 0.5;
					const int64_t C0 = int64_t(std::floor(PX));
					const float FX = float(PX - C0);
					const float H00 = Cell(C0, R0), H10 = Cell(C0 + 1, R0), H01 = Cell(C0, R0 + 1), H11 = Cell(C0 + 1, R0 + 1);
					if (!std::isnan(H00) && !std::isnan(H10) && !std::isnan(H01) && !std::isnan(H11))
					{
						float& Here = Raster.Heights[size_t(Y) * Width + X];
						Here = std::min(Here, LerpF(LerpF(H00, H10, FX), LerpF(H01, H11, FX), FY));
						++Replaced;
					}
				}
			}
		}
		return Replaced;
	}

	int32_t LoadFine(HeightRaster& Raster, const std::vector<Archive>& Archives, double South, double West, double North, double East, std::string& OutLog)
	{
		// (fine heights in a grid: Britain's (England's LiDAR), or UTM 32 (Germany's states' 1 m heights))
		const geotile::FineGrid Grid = geotile::FineGridAt(South, West);
		if (Grid == geotile::FineGrid::None || geotile::FineGridAt(North, East) != Grid || Archives.empty())
		{
			return 0;
		}

		// the area's corners in the grid, out to whole kilometres
		double MinE = 1e12, MinN = 1e12, MaxE = -1e12, MaxN = -1e12;
		for (const double Lat : { South, North })
		{
			for (const double Lon : { West, East })
			{
				double E, N;
				geotile::ToFineGrid(Grid, Lat, Lon, E, N);
				MinE = std::min(MinE, E);
				MinN = std::min(MinN, N);
				MaxE = std::max(MaxE, E);
				MaxN = std::max(MaxN, N);
			}
		}
		const int32_t E0 = static_cast<int32_t>(std::floor(MinE / 1000.0)), E1 = static_cast<int32_t>(std::floor(MaxE / 1000.0));
		const int32_t N0 = static_cast<int32_t>(std::floor(MinN / 1000.0)), N1 = static_cast<int32_t>(std::floor(MaxN / 1000.0));

		// the squares that have fine heights, decoded first, so the grid only spans them
		std::vector<geotile::HeightTile> Found;
		for (const Archive& File : Archives)
		{
			geotile::PmHeader Header;
			std::vector<geotile::PmEntry> Entries;
			std::string Error;
			if (!File.Data || !geotile::ReadPmTiles(File.Data, File.Size, Header, Entries, Error) || Header.MinZoom == 14)
			{
				continue;	// (a lat/lon base-heights archive: ApplyBase)
			}
			if (Header.MinZoom != geotile::HeightTileZoom)
			{
				OutLog += "heights archive " + File.Name + " can't be read (" + Error + "); ";
				continue;
			}
			std::sort(Entries.begin(), Entries.end(), [](const geotile::PmEntry& A, const geotile::PmEntry& B) { return A.TileId < B.TileId; });
			for (int32_t N = N0; N <= N1; ++N)
			{
				for (int32_t E = E0; E <= E1; ++E)
				{
					const uint64_t Id = geotile::HeightTileId(E, N);
					const auto It = std::lower_bound(Entries.begin(), Entries.end(), Id, [](const geotile::PmEntry& Entry, uint64_t V) { return Entry.TileId < V; });
					std::vector<uint8_t> Raw;
					geotile::HeightTile Tile;
					if (It == Entries.end() || It->TileId != Id || !geotile::UnzipArchiveTile(File.Data, Header, *It, Raw, Error)
						|| !geotile::DecodeHeightTile(Raw.data(), Raw.size(), Tile))
					{
						continue;
					}
					if (Found.empty() || std::abs(Tile.CellMetres - Found[0].CellMetres) <= 1.0e-8)
					{
						Found.push_back(std::move(Tile));	// (another archive at another cell size: the first one's squares only)
					}
				}
			}
		}
		const int32_t Squares = static_cast<int32_t>(Found.size());
		if (Squares == 0)
		{
			return 0;
		}

		// the grid: the squares found, and one empty cell round them so the blend sees where they end
		int32_t FoundE0 = INT32_MAX, FoundE1 = INT32_MIN, FoundN0 = INT32_MAX, FoundN1 = INT32_MIN;
		for (const geotile::HeightTile& Tile : Found)
		{
			FoundE0 = std::min(FoundE0, Tile.SouthWestE);
			FoundE1 = std::max(FoundE1, Tile.SouthWestE + 1000);
			FoundN0 = std::min(FoundN0, Tile.SouthWestN);
			FoundN1 = std::max(FoundN1, Tile.SouthWestN + 1000);
		}
		Raster.FineGridKind = Grid;
		Raster.FineCell = Found[0].CellMetres;
		Raster.FineWestE = FoundE0 - Raster.FineCell;
		Raster.FineNorthN = FoundN1 + Raster.FineCell;
		Raster.FineWidth = RoundToInt((FoundE1 - FoundE0) / Raster.FineCell) + 2;
		Raster.FineHeight = RoundToInt((FoundN1 - FoundN0) / Raster.FineCell) + 2;
		Raster.Fine.assign(size_t(Raster.FineWidth) * Raster.FineHeight, geotile::NoHeight);
		for (const geotile::HeightTile& Tile : Found)
		{
			const int32_t X0 = RoundToInt((Tile.SouthWestE - Raster.FineWestE) / Raster.FineCell);
			const int32_t Y0 = RoundToInt((Raster.FineNorthN - (Tile.SouthWestN + 1000.0)) / Raster.FineCell);
			for (int32_t Y = 0; Y < Tile.Size; ++Y)
			{
				for (int32_t X = 0; X < Tile.Size; ++X)
				{
					const float H = Tile.Heights[size_t(Y) * Tile.Size + X];
					if (!std::isnan(H) && X0 + X < Raster.FineWidth && Y0 + Y < Raster.FineHeight)
					{
						Raster.Fine[size_t(Y0 + Y) * Raster.FineWidth + X0 + X] = H;
					}
				}
			}
		}
		Found.clear();

		// cells from the nearest one without fine heights (two chamfer passes), for the blend into the ~25 m heights
		const int32_t W = Raster.FineWidth, H = Raster.FineHeight;
		Raster.FineEdge.resize(size_t(W) * H);
		for (size_t i = 0; i < Raster.FineEdge.size(); ++i)
		{
			Raster.FineEdge[i] = std::isnan(Raster.Fine[i]) ? 0 : HeightRaster::FadeCells;
		}
		auto Relax = [&](int32_t X, int32_t Y, int32_t DX, int32_t DY)
		{
			const int32_t NX = X + DX, NY = Y + DY;
			if (NX >= 0 && NX < W && NY >= 0 && NY < H)
			{
				uint8_t& Here = Raster.FineEdge[size_t(Y) * W + X];
				Here = uint8_t(std::min<int32_t>(Here, Raster.FineEdge[size_t(NY) * W + NX] + 1));
			}
		};
		for (int32_t Y = 0; Y < H; ++Y)
		{
			for (int32_t X = 0; X < W; ++X)
			{
				Relax(X, Y, -1, 0); Relax(X, Y, 0, -1); Relax(X, Y, -1, -1); Relax(X, Y, 1, -1);
			}
		}
		for (int32_t Y = H - 1; Y >= 0; --Y)
		{
			for (int32_t X = W - 1; X >= 0; --X)
			{
				Relax(X, Y, 1, 0); Relax(X, Y, 0, 1); Relax(X, Y, 1, 1); Relax(X, Y, -1, 1);
			}
		}
		char Line[256];
		std::snprintf(Line, sizeof(Line), "fine heights for %d of %d km squares (%.0f m cells, %d x %d, %.0f MB) from %zu archive(s)",
			Squares, (E1 - E0 + 1) * (N1 - N0 + 1), Raster.FineCell, W, H, (double(W) * H * 5) / 1e6, Archives.size());
		OutLog += Line;
		return Squares;
	}
}
