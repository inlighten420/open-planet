// Geodrive tile tool - GPL (see Core/README.md)

#include "Lidar.h"

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
#include "zstd.h"
#include "BritishGrid.h"
#include "HeightTiles.h"
#include "OsmJson.h"
#include "PMTiles.h"

namespace geotile
{
	namespace
	{
		const char* Service = "https://environment.data.gov.uk/spatialdata/lidar-composite-digital-terrain-model-dtm-1m/wcs";
		const char* Coverage = "13787b9a-26a4-4775-8523-806d13af58fc__Lidar_Composite_Elevation_DTM_1m";
		constexpr int SquareMetres = 1000;	// one request: a 1 km square at 1 m, 4 MB, ~12 s

		/** A GeoTIFF of 32-bit floats as the WCS sends it (either byte order, uncompressed, in tiles or strips) */
		bool ReadFloatTiff(const std::vector<uint8_t>& File, int& Width, int& Height, std::vector<float>& Out)
		{
			if (File.size() < 8)
			{
				return false;
			}
			const bool bBig = File[0] == 'M';
			auto U16 = [&](size_t At) -> uint32_t { return At + 2 > File.size() ? 0 : bBig ? (File[At] << 8 | File[At + 1]) : (File[At] | File[At + 1] << 8); };
			auto U32 = [&](size_t At) -> uint32_t
			{
				if (At + 4 > File.size()) { return 0; }
				return bBig ? (uint32_t(File[At]) << 24 | uint32_t(File[At + 1]) << 16 | uint32_t(File[At + 2]) << 8 | File[At + 3])
					: (File[At] | uint32_t(File[At + 1]) << 8 | uint32_t(File[At + 2]) << 16 | uint32_t(File[At + 3]) << 24);
			};
			// one value of a tag, or a list of them (offsets/counts are stored elsewhere when they don't fit in 4 bytes)
			auto Values = [&](size_t Entry) -> std::vector<uint32_t>
			{
				const uint32_t Type = U16(Entry + 2), Count = U32(Entry + 4);
				const size_t Each = Type == 3 ? 2 : 4;
				const size_t At = Count * Each <= 4 ? Entry + 8 : U32(Entry + 8);
				std::vector<uint32_t> V(Count);
				for (uint32_t i = 0; i < Count; ++i)
				{
					V[i] = Each == 2 ? U16(At + i * 2) : U32(At + i * 4);
				}
				return V;
			};
			const uint32_t Ifd = U32(4);
			const uint32_t Tags = U16(Ifd);
			uint32_t Bits = 0, Compression = 1, Format = 1, TileW = 0, TileH = 0, RowsPerStrip = 0;
			std::vector<uint32_t> Offsets, Counts;
			Width = Height = 0;
			for (uint32_t t = 0; t < Tags; ++t)
			{
				const size_t Entry = Ifd + 2 + t * 12;
				const uint32_t Tag = U16(Entry);
				const std::vector<uint32_t> V = Values(Entry);
				const uint32_t First = V.empty() ? 0 : V[0];
				switch (Tag)
				{
				case 256: Width = int(First); break;
				case 257: Height = int(First); break;
				case 258: Bits = First; break;
				case 259: Compression = First; break;
				case 278: RowsPerStrip = First; break;
				case 273: case 324: Offsets = V; break;
				case 279: case 325: Counts = V; break;
				case 322: TileW = First; break;
				case 323: TileH = First; break;
				case 339: Format = First; break;
				default: break;
				}
			}
			if (Width <= 0 || Height <= 0 || Bits != 32 || Format != 3 || Compression != 1 || Offsets.empty())
			{
				return false;
			}
			auto Float = [&](size_t At) { const uint32_t Raw = U32(At); float F; std::memcpy(&F, &Raw, 4); return F; };
			Out.assign(size_t(Width) * Height, NoHeight);
			if (TileW == 0)
			{
				TileW = uint32_t(Width);
				TileH = RowsPerStrip ? RowsPerStrip : uint32_t(Height);
			}
			const uint32_t Across = (uint32_t(Width) + TileW - 1) / TileW;
			for (size_t Block = 0; Block < Offsets.size(); ++Block)
			{
				const uint32_t BX = uint32_t(Block % Across) * TileW, BY = uint32_t(Block / Across) * TileH;
				for (uint32_t Y = 0; Y < TileH && BY + Y < uint32_t(Height); ++Y)
				{
					for (uint32_t X = 0; X < TileW && BX + X < uint32_t(Width); ++X)
					{
						const size_t At = Offsets[Block] + (size_t(Y) * TileW + X) * 4;
						if (At + 4 > File.size())
						{
							return false;
						}
						const float H = Float(At);
						Out[size_t(BY + Y) * Width + BX + X] = (std::isnan(H) || H < -1000.0f || H > 10000.0f) ? NoHeight : H;
					}
				}
			}
			return true;
		}

		/** Averages the 1 m heights into cells; a cell with fewer than half its points measured has no data */
		HeightTile Average(const std::vector<float>& Metre, int Width, int Height, int E, int N, double CellMetres)
		{
			const int Step = std::max(1, int(std::lround(CellMetres)));
			HeightTile T;
			T.Size = SquareMetres / Step;
			T.CellMetres = Step;
			T.SouthWestE = E;
			T.SouthWestN = N;
			T.Heights.assign(size_t(T.Size) * T.Size, NoHeight);
			bool bAny = false;
			for (int CY = 0; CY < T.Size; ++CY)
			{
				for (int CX = 0; CX < T.Size; ++CX)
				{
					double Sum = 0.0;
					int Count = 0;
					for (int Y = CY * Step; Y < (CY + 1) * Step && Y < Height; ++Y)
					{
						for (int X = CX * Step; X < (CX + 1) * Step && X < Width; ++X)
						{
							const float H = Metre[size_t(Y) * Width + X];
							if (!std::isnan(H))
							{
								Sum += H;
								++Count;
							}
						}
					}
					if (Count * 2 >= Step * Step)
					{
						T.Heights[size_t(CY) * T.Size + CX] = float(Sum / Count);
						bAny = true;
					}
				}
			}
			if (!bAny)
			{
				T.Heights.clear();	// all sea, or no survey
			}
			return T;
		}

		bool Download(const std::string& Url, const std::string& Path)
		{
			const std::string Part = Path + ".part";
			const std::string Command = "curl.exe -s -f -L --retry 3 --retry-delay 5 -m 300 -o \"" + Part + "\" \"" + Url + "\"";
			if (std::system(Command.c_str()) != 0)
			{
				std::error_code Ignored;
				std::filesystem::remove(Part, Ignored);
				return false;
			}
			std::error_code Error;
			std::filesystem::rename(Part, Path, Error);
			return !Error;
		}
	}

	int PackLidar(const std::string& OutPath, double Lat, double Lon, double Km, double CellMetres, int Jobs)
	{
		const auto Start = std::chrono::steady_clock::now();
		auto Elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(); };

		// the area's corners in the grid, out to whole kilometres
		const double HalfLat = Km / 111.32, HalfLon = Km / (111.32 * std::cos(Lat * 3.14159265358979323846 / 180.0));
		double MinE = 1e12, MinN = 1e12, MaxE = -1e12, MaxN = -1e12;
		for (const double CLat : { Lat - HalfLat, Lat + HalfLat })
		{
			for (const double CLon : { Lon - HalfLon, Lon + HalfLon })
			{
				if (!NearBritishGrid(CLat, CLon))
				{
					throw std::runtime_error("LiDAR heights are for England: this area is outside Great Britain");
				}
				double E, N;
				ToBritishGrid(CLat, CLon, E, N);
				MinE = std::min(MinE, E);
				MinN = std::min(MinN, N);
				MaxE = std::max(MaxE, E);
				MaxN = std::max(MaxN, N);
			}
		}
		const int E0 = int(std::floor(MinE / 1000.0)), E1 = int(std::floor(MaxE / 1000.0));
		const int N0 = int(std::floor(MinN / 1000.0)), N1 = int(std::floor(MaxN / 1000.0));
		std::vector<std::pair<int, int>> Squares;
		for (int N = N0; N <= N1; ++N)
		{
			for (int E = E0; E <= E1; ++E)
			{
				Squares.emplace_back(E, N);
			}
		}
		const std::string Cache = OutPath + ".download";
		std::filesystem::create_directories(Cache);
		std::printf("lidar: %zu km squares (E %d-%d km, N %d-%d km), %d at a time; downloads kept in %s\n", Squares.size(), E0, E1, N0, N1, Jobs, Cache.c_str());
		std::fflush(stdout);

		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		std::mutex Lock;
		std::atomic<size_t> Next{ 0 }, Done{ 0 }, Fetched{ 0 }, Failed{ 0 }, Empty{ 0 };
		std::vector<std::thread> Workers;
		for (int j = 0; j < std::max(1, Jobs); ++j)
		{
			Workers.emplace_back([&]()
			{
				for (size_t i = Next++; i < Squares.size(); i = Next++)
				{
					const auto [E, N] = Squares[i];
					const std::string Path = Cache + "/" + std::to_string(E) + "_" + std::to_string(N) + ".tif";
					if (!std::filesystem::exists(Path))
					{
						char Url[1024];
						std::snprintf(Url, sizeof(Url), "%s?service=WCS&version=2.0.1&request=GetCoverage&CoverageId=%s&format=image/tiff&subset=E(%d,%d)&subset=N(%d,%d)",
							Service, Coverage, E * 1000, E * 1000 + SquareMetres, N * 1000, N * 1000 + SquareMetres);
						if (!Download(Url, Path))
						{
							++Failed;
							++Done;
							continue;
						}
						++Fetched;
					}
					std::vector<float> Metre;
					int Width = 0, Height = 0;
					std::vector<uint8_t> Raw;
					try { Raw = ReadFile(Path); } catch (const std::exception&) {}
					if (!ReadFloatTiff(Raw, Width, Height, Metre))
					{
						std::error_code Ignored;
						std::filesystem::remove(Path, Ignored);	// broken: fetched again next time
						++Failed;
						++Done;
						continue;
					}
					const HeightTile Tile = Average(Metre, Width, Height, E * 1000, N * 1000, CellMetres);
					if (Tile.IsEmpty())
					{
						++Empty;
					}
					else
					{
						const std::vector<uint8_t> Encoded = EncodeHeightTile(Tile);
						std::vector<uint8_t> Packed(ZSTD_compressBound(Encoded.size()));
						Packed.resize(ZSTD_compress(Packed.data(), Packed.size(), Encoded.data(), Encoded.size(), 19));
						std::lock_guard<std::mutex> Guard(Lock);
						Tiles[HeightTileId(E, N)] = std::move(Packed);
					}
					const size_t Count = ++Done;
					if (Count % 20 == 0 || Count == Squares.size())
					{
						const double S = Elapsed();
						std::printf("  %zu of %zu squares (%zu downloaded, %zu sea or unsurveyed, %zu failed), %.0fs, about %.0fs to go\n", Count,
							Squares.size(), size_t(Fetched), size_t(Empty), size_t(Failed), S, S / Count * (Squares.size() - Count));
						std::fflush(stdout);
					}
				}
			});
		}
		for (std::thread& T : Workers)
		{
			T.join();
		}

		PmHeader H;
		H.TileCompression = PmCompression::None;	// each tile is its own zstd frame
		H.MinZoom = H.MaxZoom = H.CenterZoom = HeightTileZoom;
		H.MinLatE7 = int32_t(std::llround((Lat - HalfLat) * 1e7));
		H.MaxLatE7 = int32_t(std::llround((Lat + HalfLat) * 1e7));
		H.MinLonE7 = int32_t(std::llround((Lon - HalfLon) * 1e7));
		H.MaxLonE7 = int32_t(std::llround((Lon + HalfLon) * 1e7));
		H.CenterLatE7 = int32_t(std::llround(Lat * 1e7));
		H.CenterLonE7 = int32_t(std::llround(Lon * 1e7));
		const std::string Meta = nlohmann::json{
			{ "format", "geodrive-heights" }, { "format_version", 1 }, { "grid", "EPSG:27700" }, { "tile_km", 1 }, { "cell_m", CellMetres },
			{ "attribution", "Contains Environment Agency information (c) Environment Agency and/or database right. Open Government Licence v3.0" },
		}.dump();
		std::vector<uint8_t> Archive;
		std::string Error;
		if (!WritePmTiles(Tiles, H, Meta, Archive, Error))
		{
			throw std::runtime_error(Error);
		}
		WriteFile(OutPath, Archive);
		std::printf("lidar: %zu squares with heights, %zu sea or unsurveyed, %zu failed -> %s (%.1f MB), %.0fs\n", Tiles.size(), size_t(Empty),
			size_t(Failed), OutPath.c_str(), Archive.size() / 1e6, Elapsed());
		return Failed > 0 ? 4 : 0;
	}
}

namespace geotile
{
	int PackDgm(const std::string& OutPath, const std::string& Folder, double CellMetres, const std::string& Attribution)
	{
		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		size_t Read = 0, Failed = 0;
		for (const auto& Entry : std::filesystem::directory_iterator(Folder))
		{
			if (Entry.path().extension() != ".tif")
			{
				continue;
			}
			// where it sits: its world file's top left pixel's middle, a pixel's size
			std::FILE* World = std::fopen((Entry.path().parent_path() / (Entry.path().stem().u8string() + ".tfw")).u8string().c_str(), "r");
			double W[6] = {};
			const bool bWorld = World && std::fscanf(World, "%lf %lf %lf %lf %lf %lf", &W[0], &W[1], &W[2], &W[3], &W[4], &W[5]) == 6;
			if (World)
			{
				std::fclose(World);
			}
			std::vector<float> Metre;
			int Width = 0, Height = 0;
			std::vector<uint8_t> Raw;
			try { Raw = ReadFile(Entry.path().u8string()); } catch (const std::exception&) {}
			if (!bWorld || std::abs(W[0] - 1.0) > 1e-6 || !ReadFloatTiff(Raw, Width, Height, Metre) || Width != SquareMetres || Height != SquareMetres)
			{
				std::printf("  %s: not a 1 km square of 1 m heights with its world file, left out\n", Entry.path().filename().u8string().c_str());
				++Failed;
				continue;
			}
			const int E = int(std::lround(W[4] - 0.5)), N = int(std::lround(W[5] + 0.5)) - SquareMetres;
			const HeightTile Tile = Average(Metre, Width, Height, E, N, CellMetres);
			if (Tile.IsEmpty())
			{
				continue;
			}
			const std::vector<uint8_t> Encoded = EncodeHeightTile(Tile);
			std::vector<uint8_t> Packed(ZSTD_compressBound(Encoded.size()));
			Packed.resize(ZSTD_compress(Packed.data(), Packed.size(), Encoded.data(), Encoded.size(), 19));
			Tiles[HeightTileId(E / 1000, N / 1000)] = std::move(Packed);
			++Read;
		}
		PmHeader H;
		H.TileCompression = PmCompression::None;
		H.MinZoom = H.MaxZoom = H.CenterZoom = HeightTileZoom;
		const std::string Meta = nlohmann::json{
			{ "format", "geodrive-heights" }, { "format_version", 1 }, { "grid", "EPSG:25832" }, { "tile_km", 1 }, { "cell_m", CellMetres },
			{ "attribution", Attribution },
		}.dump();
		std::vector<uint8_t> Archive;
		std::string Error;
		if (!WritePmTiles(Tiles, H, Meta, Archive, Error))
		{
			throw std::runtime_error(Error);
		}
		WriteFile(OutPath, Archive);
		std::printf("dgm: %zu squares of %.0f m heights (%zu left out) -> %s (%.1f MB)\n", Tiles.size(), CellMetres, Failed, OutPath.c_str(), Archive.size() / 1e6);
		return Tiles.empty() ? 4 : 0;
	}

	int PrefetchTerrain(const std::string& RegionPath, const std::string& CacheDir, int Jobs)
	{
		const auto Start = std::chrono::steady_clock::now();
		auto Elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(); };

		const std::vector<uint8_t> File = ReadFile(RegionPath);
		PmHeader Header;
		std::vector<PmEntry> Entries;
		std::string Error;
		if (!ReadPmTiles(File.data(), File.size(), Header, Entries, Error))
		{
			throw std::runtime_error(Error);
		}
		std::vector<uint64_t> Ids;
		Ids.reserve(Entries.size());
		for (const PmEntry& E : Entries)
		{
			Ids.push_back(E.TileId);
		}
		std::sort(Ids.begin(), Ids.end());

		// z12 tiles with map data in any of their z14 children, and the ring round them
		constexpr int Zoom = 12;
		const double N = double(1 << Zoom);
		auto TileX = [&](int32_t LonE7) { return int(std::floor((LonE7 / 1e7 + 180.0) / 360.0 * N)); };
		auto TileY = [&](int32_t LatE7)
		{
			const double Lat = LatE7 / 1e7 * 3.14159265358979323846 / 180.0;
			return int(std::floor((1.0 - std::log(std::tan(Lat) + 1.0 / std::cos(Lat)) / 3.14159265358979323846) * 0.5 * N));
		};
		const int X0 = TileX(Header.MinLonE7), X1 = TileX(Header.MaxLonE7), Y0 = TileY(Header.MaxLatE7), Y1 = TileY(Header.MinLatE7);
		std::vector<std::pair<int, int>> Wanted;
		{
			std::vector<std::vector<bool>> Has(size_t(X1 - X0 + 3), std::vector<bool>(size_t(Y1 - Y0 + 3), false));
			for (int X = X0; X <= X1; ++X)
			{
				for (int Y = Y0; Y <= Y1; ++Y)
				{
					bool bAny = false;
					for (uint32_t DX = 0; DX < 4 && !bAny; ++DX)
					{
						for (uint32_t DY = 0; DY < 4 && !bAny; ++DY)
						{
							bAny = std::binary_search(Ids.begin(), Ids.end(), ZxyToTileId(14, uint32_t(X) * 4 + DX, uint32_t(Y) * 4 + DY));
						}
					}
					if (bAny)
					{
						for (int RX = -1; RX <= 1; ++RX)
						{
							for (int RY = -1; RY <= 1; ++RY)
							{
								Has[size_t(X - X0 + 1 + RX)][size_t(Y - Y0 + 1 + RY)] = true;
							}
						}
					}
				}
			}
			for (int X = X0 - 1; X <= X1 + 1; ++X)
			{
				for (int Y = Y0 - 1; Y <= Y1 + 1; ++Y)
				{
					if (Has[size_t(X - X0 + 1)][size_t(Y - Y0 + 1)])
					{
						Wanted.emplace_back(X, Y);
					}
				}
			}
		}

		std::filesystem::create_directories(CacheDir);
		std::vector<std::pair<int, int>> Missing;
		for (const auto& [X, Y] : Wanted)
		{
			if (!std::filesystem::exists(CacheDir + "/" + std::to_string(Zoom) + "_" + std::to_string(X) + "_" + std::to_string(Y) + ".png"))
			{
				Missing.emplace_back(X, Y);
			}
		}
		std::printf("terrain: %zu tiles cover the region's map data (of %d in its box), %zu already cached, %zu to fetch, %d at a time\n",
			Wanted.size(), (X1 - X0 + 1) * (Y1 - Y0 + 1), Wanted.size() - Missing.size(), Missing.size(), Jobs);
		std::fflush(stdout);

		std::atomic<size_t> Next{ 0 }, Done{ 0 }, Failed{ 0 };
		std::vector<std::thread> Workers;
		for (int j = 0; j < std::max(1, Jobs); ++j)
		{
			Workers.emplace_back([&]()
			{
				for (size_t i = Next++; i < Missing.size(); i = Next++)
				{
					const auto [X, Y] = Missing[i];
					const std::string Path = CacheDir + "/" + std::to_string(Zoom) + "_" + std::to_string(X) + "_" + std::to_string(Y) + ".png";
					const std::string Url = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/" + std::to_string(Zoom) + "/" + std::to_string(X) + "/" + std::to_string(Y) + ".png";
					if (!Download(Url, Path))
					{
						++Failed;
					}
					const size_t Count = ++Done;
					if (Count % 100 == 0 || Count == Missing.size())
					{
						const double S = Elapsed();
						std::printf("  %zu of %zu (%zu failed), %.0fs, about %.0f min to go\n", Count, Missing.size(), size_t(Failed), S,
							S / Count * (Missing.size() - Count) / 60.0);
						std::fflush(stdout);
					}
				}
			});
		}
		for (std::thread& T : Workers)
		{
			T.join();
		}
		std::printf("terrain: %zu fetched, %zu failed (run again for those), %.0fs\n", Missing.size() - size_t(Failed), size_t(Failed), Elapsed());
		return Failed > 0 ? 4 : 0;
	}
}
