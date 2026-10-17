// Geodrive tile tool - AGPL-3.0 (see LICENSE)

#include "Copernicus.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "json.hpp"
#include "miniz.h"
#include "zstd.h"
#include "HeightTiles.h"
#include "OsmJson.h"
#include "PMTiles.h"

namespace geotile
{
	namespace
	{
		constexpr int SquareArcsec = 120;	// one height tile: 120 x 120 arc-seconds (about 3.7 x 3.1 km in South Africa)
		constexpr int LatLonZoom = 14;

		/** A 1-degree Copernicus tile: heights row by row from the north, and where its first pixel's centre is */
		struct FSource
		{
			int Width = 0, Height = 0;
			double NorthLat = 0.0, WestLon = 0.0;	// centre of pixel (0, 0)
			double StepLat = 0.0, StepLon = 0.0;	// degrees per pixel
			std::vector<float> Heights;
			bool IsEmpty() const { return Heights.empty(); }

			float At(double Lat, double Lon) const
			{
				const double X = (Lon - WestLon) / StepLon, Y = (NorthLat - Lat) / StepLat;
				const int X0 = int(std::floor(X)), Y0 = int(std::floor(Y));
				if (X0 < 0 || Y0 < 0 || X0 + 1 >= Width || Y0 + 1 >= Height)
				{
					// the last row/column: hold the edge value
					const int CX = std::clamp(int(std::lround(X)), 0, Width - 1), CY = std::clamp(int(std::lround(Y)), 0, Height - 1);
					return (std::abs(X - CX) <= 1.0 && std::abs(Y - CY) <= 1.0) ? Heights[size_t(CY) * Width + CX] : NoHeight;
				}
				const float FX = float(X - X0), FY = float(Y - Y0);
				const float* R0 = &Heights[size_t(Y0) * Width + X0];
				const float* R1 = R0 + Width;
				return (R0[0] + (R0[1] - R0[0]) * FX) * (1.0f - FY) + (R1[0] + (R1[1] - R1[0]) * FX) * FY;
			}
		};

		/** A Copernicus COG: little-endian, tiled, DEFLATE with the floating-point predictor (or none), 32-bit floats */
		bool ReadCopernicus(const std::vector<uint8_t>& File, FSource& Out)
		{
			if (File.size() < 8 || File[0] != 'I')
			{
				return false;
			}
			auto U16 = [&](size_t At) -> uint32_t { return At + 2 > File.size() ? 0 : uint32_t(File[At] | File[At + 1] << 8); };
			auto U32 = [&](size_t At) -> uint32_t { return At + 4 > File.size() ? 0 : uint32_t(File[At] | File[At + 1] << 8 | File[At + 2] << 16 | uint32_t(File[At + 3]) << 24); };
			auto F64 = [&](size_t At) { double D = 0.0; if (At + 8 <= File.size()) { std::memcpy(&D, &File[At], 8); } return D; };
			const uint32_t Ifd = U32(4);
			const uint32_t Tags = U16(Ifd);
			uint32_t Compression = 1, Predictor = 1, TileW = 0, TileH = 0, Bits = 0;
			std::vector<uint32_t> Offsets, Counts;
			double Scale[2] = { 0.0, 0.0 }, Tie[6] = {};
			bool bPixelIsPoint = false;
			for (uint32_t t = 0; t < Tags; ++t)
			{
				const size_t Entry = Ifd + 2 + t * 12;
				const uint32_t Tag = U16(Entry), Type = U16(Entry + 2), Count = U32(Entry + 4);
				const size_t Each = Type == 3 ? 2 : Type == 12 ? 8 : 4;
				const size_t At = Count * Each <= 4 ? Entry + 8 : U32(Entry + 8);
				auto Value = [&](uint32_t i) -> uint32_t { return Each == 2 ? U16(At + i * 2) : U32(At + i * 4); };
				switch (Tag)
				{
				case 256: Out.Width = int(Value(0)); break;
				case 257: Out.Height = int(Value(0)); break;
				case 258: Bits = Value(0); break;
				case 259: Compression = Value(0); break;
				case 317: Predictor = Value(0); break;
				case 322: TileW = Value(0); break;
				case 323: TileH = Value(0); break;
				case 324: for (uint32_t i = 0; i < Count; ++i) { Offsets.push_back(Value(i)); } break;
				case 325: for (uint32_t i = 0; i < Count; ++i) { Counts.push_back(Value(i)); } break;
				case 33550: Scale[0] = F64(At); Scale[1] = F64(At + 8); break;
				case 33922: for (int i = 0; i < 6; ++i) { Tie[i] = F64(At + i * 8); } break;
				case 34735:
					// GeoKey directory: GTRasterTypeGeoKey (1025) = 2 is pixel-is-point
					for (uint32_t k = 4; k + 3 < Count; k += 4)
					{
						if (Value(k) == 1025 && Value(k + 1) == 0)
						{
							bPixelIsPoint = Value(k + 3) == 2;
						}
					}
					break;
				default: break;
				}
			}
			if (Out.Width <= 0 || Out.Height <= 0 || Bits != 32 || TileW == 0 || TileH == 0 || Offsets.size() != Counts.size() || Scale[0] <= 0.0)
			{
				return false;
			}
			Out.StepLon = Scale[0];
			Out.StepLat = Scale[1];
			// the tie point is pixel (Tie[0], Tie[1]) at (Tie[3], Tie[4]); its corner, unless pixels are points
			Out.WestLon = Tie[3] - Tie[0] * Scale[0] + (bPixelIsPoint ? 0.0 : 0.5 * Scale[0]);
			Out.NorthLat = Tie[4] + Tie[1] * Scale[1] - (bPixelIsPoint ? 0.0 : 0.5 * Scale[1]);
			Out.Heights.assign(size_t(Out.Width) * Out.Height, NoHeight);
			const uint32_t Across = (uint32_t(Out.Width) + TileW - 1) / TileW;
			std::vector<uint8_t> Raw(size_t(TileW) * TileH * 4), Row(size_t(TileW) * 4);
			for (size_t Block = 0; Block < Offsets.size(); ++Block)
			{
				if (size_t(Offsets[Block]) + Counts[Block] > File.size())
				{
					return false;
				}
				if (Compression == 8 || Compression == 32946)
				{
					mz_ulong Size = mz_ulong(Raw.size());
					if (mz_uncompress(Raw.data(), &Size, &File[Offsets[Block]], mz_ulong(Counts[Block])) != MZ_OK || Size != Raw.size())
					{
						return false;
					}
				}
				else if (Compression == 1 && Counts[Block] == Raw.size())
				{
					std::memcpy(Raw.data(), &File[Offsets[Block]], Raw.size());
				}
				else
				{
					return false;
				}
				const uint32_t BX = uint32_t(Block % Across) * TileW, BY = uint32_t(Block / Across) * TileH;
				for (uint32_t Y = 0; Y < TileH; ++Y)
				{
					uint8_t* Bytes = &Raw[size_t(Y) * TileW * 4];
					float Values[4096];
					if (Predictor == 3)
					{
						// floating-point predictor: bytes differenced along the row, then the four bytes of each value
						// stored as planes, most significant first
						for (size_t i = 1; i < Row.size(); ++i)
						{
							Bytes[i] = uint8_t(Bytes[i] + Bytes[i - 1]);
						}
						for (uint32_t X = 0; X < TileW; ++X)
						{
							const uint32_t Bitsv = uint32_t(Bytes[X]) << 24 | uint32_t(Bytes[TileW + X]) << 16 | uint32_t(Bytes[2 * TileW + X]) << 8 | Bytes[3 * TileW + X];
							std::memcpy(&Values[X], &Bitsv, 4);
						}
					}
					else
					{
						std::memcpy(Values, Bytes, size_t(TileW) * 4);
					}
					for (uint32_t X = 0; X < TileW && BX + X < uint32_t(Out.Width); ++X)
					{
						if (BY + Y < uint32_t(Out.Height))
						{
							const float H = Values[X];
							Out.Heights[size_t(BY + Y) * Out.Width + BX + X] = (std::isnan(H) || H < -1000.0f || H > 9000.0f) ? NoHeight : H;
						}
					}
				}
			}
			return true;
		}

		std::string TileName(int LatFloor, int LonFloor)
		{
			char Name[96];
			std::snprintf(Name, sizeof(Name), "Copernicus_DSM_COG_10_%c%02d_00_%c%03d_00_DEM", LatFloor < 0 ? 'S' : 'N', std::abs(LatFloor), LonFloor < 0 ? 'W' : 'E', std::abs(LonFloor));
			return Name;
		}
	}


	namespace
	{
		/**
		 *  The heights of every 120 arc-second square inside the box, one 1-degree tile at a time (each square lies in one
		 *  tile, so only that tile is ever in memory: a whole country fits), packed into one archive
		 */
		int PackCells(const std::string& OutPath, const std::vector<std::pair<int, int>>& Cells, double South, double West, double North, double East, int Jobs)
		{
			const auto Start = std::chrono::steady_clock::now();
			auto Elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(); };
			const std::string Cache = OutPath + ".download";
			std::filesystem::create_directories(Cache);
			std::map<uint64_t, std::vector<uint8_t>> Tiles;
			std::mutex Lock;
			size_t Done = 0;
			for (const auto& [LatF, LonF] : Cells)
			{
				++Done;
				const std::string Name = TileName(LatF, LonF);
				const std::string Path = Cache + "/" + Name + ".tif";
				if (!std::filesystem::exists(Path))
				{
					const std::string Url = "https://copernicus-dem-30m.s3.amazonaws.com/" + Name + "/" + Name + ".tif";
					const std::string Command = "curl.exe -s -f -L --retry 3 -m 900 -o \"" + Path + ".part\" \"" + Url + "\"";
					if (std::system(Command.c_str()) != 0)
					{
						std::error_code Ignored;
						std::filesystem::remove(Path + ".part", Ignored);
						std::printf("  %zu of %zu: %s not there (sea)\n", Done, Cells.size(), Name.c_str());
						continue;
					}
					std::filesystem::rename(Path + ".part", Path);
				}
				FSource Source;
				if (!ReadCopernicus(ReadFile(Path), Source))
				{
					throw std::runtime_error("can't read " + Path);
				}
				// the squares of this tile inside the box
				std::vector<std::pair<int, int>> Squares;
				const int CX0 = (LonF + 180) * 30, CY0 = (LatF + 90) * 30;
				for (int Y = CY0; Y < CY0 + 30; ++Y)
				{
					for (int X = CX0; X < CX0 + 30; ++X)
					{
						const double SqWest = X / 30.0 - 180.0, SqSouth = Y / 30.0 - 90.0;
						if (SqWest + 1.0 / 30.0 >= West && SqWest <= East && SqSouth + 1.0 / 30.0 >= South && SqSouth <= North)
						{
							Squares.emplace_back(X, Y);
						}
					}
				}
				std::atomic<size_t> Next{ 0 };
				std::vector<std::thread> Workers;
				for (int j = 0; j < std::max(1, Jobs); ++j)
				{
					Workers.emplace_back([&]()
					{
						for (size_t i = Next++; i < Squares.size(); i = Next++)
						{
							const auto [X, Y] = Squares[i];
							HeightTile T;
							T.Size = SquareArcsec;
							T.CellMetres = 1.0;	// (here: one arc-second)
							T.SouthWestE = X * SquareArcsec;	// arc-seconds east of -180
							T.SouthWestN = Y * SquareArcsec;	// arc-seconds north of -90
							T.Heights.resize(size_t(SquareArcsec) * SquareArcsec);
							bool bLand = false;
							for (int R = 0; R < SquareArcsec; ++R)
							{
								for (int C = 0; C < SquareArcsec; ++C)
								{
									const double CLat = (T.SouthWestN + SquareArcsec - R - 0.5) / 3600.0 - 90.0;
									const double CLon = (T.SouthWestE + C + 0.5) / 3600.0 - 180.0;
									const float H = Source.At(CLat, CLon);
									T.Heights[size_t(R) * SquareArcsec + C] = H;
									bLand |= !std::isnan(H) && H != 0.0f;
								}
							}
							if (!bLand)
							{
								continue;	// open sea (Copernicus has it at 0): SRTM's sea floor stays
							}
							const std::vector<uint8_t> Encoded = EncodeHeightTile(T);
							std::vector<uint8_t> Packed(ZSTD_compressBound(Encoded.size()));
							Packed.resize(ZSTD_compress(Packed.data(), Packed.size(), Encoded.data(), Encoded.size(), 19));
							std::lock_guard<std::mutex> Guard(Lock);
							Tiles[ZxyToTileId(LatLonZoom, uint32_t(X), uint32_t(Y))] = std::move(Packed);
						}
					});
				}
				for (std::thread& T : Workers)
				{
					T.join();
				}
				std::printf("  %zu of %zu: %s, %zu squares so far, %.0fs, about %.0f min to go\n", Done, Cells.size(), Name.c_str(), Tiles.size(), Elapsed(),
					Elapsed() / Done * (Cells.size() - Done) / 60.0);
				std::fflush(stdout);
			}

			PmHeader H;
			H.TileCompression = PmCompression::None;
			H.MinZoom = H.MaxZoom = H.CenterZoom = LatLonZoom;	// 14: a lat/lon heights grid (11 is the British National Grid)
			H.MinLatE7 = int32_t(std::llround(South * 1e7));
			H.MaxLatE7 = int32_t(std::llround(North * 1e7));
			H.MinLonE7 = int32_t(std::llround(West * 1e7));
			H.MaxLonE7 = int32_t(std::llround(East * 1e7));
			H.CenterLatE7 = int32_t(std::llround((South + North) * 0.5 * 1e7));
			H.CenterLonE7 = int32_t(std::llround((West + East) * 0.5 * 1e7));
			const std::string Meta = nlohmann::json{
				{ "format", "geodrive-heights" }, { "format_version", 1 }, { "grid", "EPSG:4326 arc-seconds" }, { "tile_arcsec", SquareArcsec }, { "cell_arcsec", 1 },
				{ "role", "base" },
				{ "attribution", "Produced using Copernicus WorldDEM-30 (c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved" },
			}.dump();
			std::vector<uint8_t> Archive;
			std::string Error;
			if (!WritePmTiles(Tiles, H, Meta, Archive, Error))
			{
				throw std::runtime_error(Error);
			}
			Tiles.clear();
			WriteFile(OutPath, Archive);
			std::printf("copernicus: -> %s (%.1f MB), %.0fs\n", OutPath.c_str(), Archive.size() / 1e6, Elapsed());
			return 0;
		}
	}

	int PackCopernicus(const std::string& OutPath, double Lat, double Lon, double Km, int Jobs)
	{
		const double HalfLat = Km / 111.32, HalfLon = Km / (111.32 * std::cos(Lat * 3.14159265358979323846 / 180.0));
		const double South = Lat - HalfLat, North = Lat + HalfLat, West = Lon - HalfLon, East = Lon + HalfLon;
		// the 1-degree tiles the area touches (a tile named S35 covers -35..-34: its floor)
		std::vector<std::pair<int, int>> Cells;
		for (int LatF = int(std::floor(South)); LatF <= int(std::floor(North)); ++LatF)
		{
			for (int LonF = int(std::floor(West)); LonF <= int(std::floor(East)); ++LonF)
			{
				Cells.emplace_back(LatF, LonF);
			}
		}
		return PackCells(OutPath, Cells, South, West, North, East, Jobs);
	}

	int PackCopernicusRegion(const std::string& OutPath, const std::string& RegionPath, int Jobs)
	{
		// every 1-degree tile with some of the region's map data in it (z14 tiles), all of it
		const std::vector<uint8_t> File = ReadFile(RegionPath);
		PmHeader Header;
		std::vector<PmEntry> Entries;
		std::string Error;
		if (!ReadPmTiles(File.data(), File.size(), Header, Entries, Error))
		{
			throw std::runtime_error(Error);
		}
		std::vector<uint64_t> Ids;
		for (const PmEntry& E : Entries)
		{
			Ids.push_back(E.TileId);
		}
		std::sort(Ids.begin(), Ids.end());
		const double South = Header.MinLatE7 / 1e7, North = Header.MaxLatE7 / 1e7, West = Header.MinLonE7 / 1e7, East = Header.MaxLonE7 / 1e7;
		const double N = double(1 << 14);
		auto TileX = [&](double Lon) { return int(std::floor((Lon + 180.0) / 360.0 * N)); };
		auto TileY = [&](double Lat)
		{
			const double R = Lat * 3.14159265358979323846 / 180.0;
			return int(std::floor((1.0 - std::log(std::tan(R) + 1.0 / std::cos(R)) / 3.14159265358979323846) * 0.5 * N));
		};
		std::vector<std::pair<int, int>> Cells;
		for (int LatF = int(std::floor(South)); LatF <= int(std::floor(North)); ++LatF)
		{
			for (int LonF = int(std::floor(West)); LonF <= int(std::floor(East)); ++LonF)
			{
				bool bAny = false;
				for (int X = TileX(LonF); X <= TileX(LonF + 1.0) && !bAny; ++X)
				{
					for (int Y = TileY(LatF + 1.0); Y <= TileY(LatF) && !bAny; ++Y)
					{
						bAny = std::binary_search(Ids.begin(), Ids.end(), ZxyToTileId(14, uint32_t(X), uint32_t(Y)));
					}
				}
				if (bAny)
				{
					Cells.emplace_back(LatF, LonF);
				}
			}
		}
		std::printf("copernicus: %zu 1-degree tiles have the region's map data in them\n", Cells.size());
		return PackCells(OutPath, Cells, South, West, North, East, Jobs);
	}
}
