// Geodrive tile tool - AGPL-3.0 (see LICENSE)
//
//   geotile selftest
//   geotile pack  <osm2.json> <out.pmtiles> [level] [precision]
//         Pack a cached Overpass response into tiles. zstd level (default 19); precision in 1e-7 degrees (default 1 = exact)
//   geotile check <osm2.json> <in.pmtiles>            Prove nothing the game uses was lost
//   geotile bench <osm2.json> <in.pmtiles>            Sizes and timings against JSON and OSM PBF
//   geotile pbf   <osm2.json> <out.osm.pbf>           Same area as a standard OSM PBF (all tags), e.g. for Planetiler

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <array>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "json.hpp"
#include "zstd.h"

#include "OsmJson.h"
#include "OsmPbf.h"
#include "OsmPbfRead.h"
#include "Lidar.h"
#include "GodotExport.h"
#include "Sections.h"
#include "Copernicus.h"
#include "BritishGrid.h"
#include "HeightTiles.h"
#include "PMTiles.h"
#include "RoadBake.h"
#include "TileArchive.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

using namespace geotile;

namespace
{
	double Seconds(const std::function<void()>& Fn)
	{
		const auto Start = std::chrono::steady_clock::now();
		Fn();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	}

	/** Fastest of a few runs, so a background hiccup doesn't count */
	double BestSeconds(int Runs, const std::function<void()>& Fn)
	{
		double Best = 1e30;
		for (int I = 0; I < Runs; ++I)
		{
			Best = std::min(Best, Seconds(Fn));
		}
		return Best;
	}

	std::vector<uint8_t> Zstd(const void* Data, size_t Size, int Level)
	{
		std::vector<uint8_t> Out(ZSTD_compressBound(Size));
		const size_t N = ZSTD_compress(Out.data(), Out.size(), Data, Size, Level);
		if (ZSTD_isError(N))
		{
			throw std::runtime_error(ZSTD_getErrorName(N));
		}
		Out.resize(N);
		return Out;
	}

	std::vector<uint8_t> Unzstd(const uint8_t* Data, size_t Size)
	{
		const unsigned long long N = ZSTD_getFrameContentSize(Data, Size);
		if (N == ZSTD_CONTENTSIZE_ERROR || N == ZSTD_CONTENTSIZE_UNKNOWN)
		{
			throw std::runtime_error("bad zstd frame");
		}
		std::vector<uint8_t> Out(static_cast<size_t>(N));
		const size_t Got = ZSTD_decompress(Out.data(), Out.size(), Data, Size);
		if (ZSTD_isError(Got) || Got != N)
		{
			throw std::runtime_error("zstd decompress failed");
		}
		return Out;
	}

	std::string Mb(size_t Bytes)
	{
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%.2f MB", Bytes / 1e6);
		return Buf;
	}

	/** Core reports errors as false + message; the tool just stops */
	void Must(bool bOk, const std::string& Error)
	{
		if (!bOk)
		{
			throw std::runtime_error(Error);
		}
	}

	struct LoadedArchive
	{
		std::vector<uint8_t> File;
		PmHeader Header;
		std::vector<PmEntry> Entries;
	};

	LoadedArchive LoadArchive(const std::string& Path)
	{
		LoadedArchive A;
		A.File = ReadFile(Path);
		std::string Error;
		Must(ReadPmTiles(A.File.data(), A.File.size(), A.Header, A.Entries, Error), Error);
		return A;
	}

	DecodedTile DecodeOne(const LoadedArchive& A, const PmEntry& E)
	{
		DecodedTile T;
		std::string Error;
		Must(ReadArchiveTile(A.File.data(), A.Header, E, T, Error), Error);
		return T;
	}

	std::vector<DecodedTile> DecodeAll(const LoadedArchive& A)
	{
		std::vector<DecodedTile> Tiles;
		Tiles.reserve(A.Entries.size());
		for (const PmEntry& E : A.Entries)
		{
			Tiles.push_back(DecodeOne(A, E));
		}
		return Tiles;
	}

	OsmSet Join(const std::vector<DecodedTile>& Tiles)
	{
		OsmSet Set;
		std::string Error;
		Must(Assemble(Tiles, Set, Error), Error);
		return Set;
	}

	/** The tiles of an area, uncompressed, in tile-id order */
	std::map<uint64_t, std::vector<uint8_t>> EncodeAll(const OsmSet& Set, int64_t Quantum, PackStats& Stats)
	{
		const std::map<TileXY, std::vector<WayPiece>> Split = SplitIntoTiles(Set, Stats);
		const std::unordered_map<int64_t, bool> KeepId = NodesNeedingIds(Split);
		Stats.GlobalIdNodes = KeepId.size();
		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		for (const auto& [Xy, Pieces] : Split)
		{
			Tiles[ZxyToTileId(TileZoom, Xy.X, Xy.Y)] = EncodeTile(Pieces, Set, Quantum, KeepId);
		}
		return Tiles;
	}

	int SelfTest()
	{
		// Values from the PMTiles spec's own tests
		struct { int Z; uint32_t X, Y; uint64_t Id; } Cases[] = {
			{ 0, 0, 0, 0 }, { 1, 0, 0, 1 }, { 1, 0, 1, 2 }, { 1, 1, 1, 3 }, { 1, 1, 0, 4 }, { 2, 0, 0, 5 },
		};
		int Bad = 0;
		for (const auto& C : Cases)
		{
			const uint64_t Id = ZxyToTileId(C.Z, C.X, C.Y);
			if (Id != C.Id)
			{
				std::printf("FAIL tile id %d/%u/%u = %llu, want %llu\n", C.Z, C.X, C.Y, (unsigned long long)Id, (unsigned long long)C.Id);
				++Bad;
			}
		}
		std::printf(Bad ? "selftest FAILED\n" : "selftest ok\n");
		return Bad ? 1 : 0;
	}

	int PackSet(const OsmSet& Set, double LoadS, const std::string& OutPath, int Level, int64_t Quantum);

	int Pack(const std::string& JsonPath, const std::string& OutPath, int Level, int64_t Quantum)
	{
		OsmSet Set;
		const double LoadS = Seconds([&] { Set = LoadOverpassJson(JsonPath, false); });
		return PackSet(Set, LoadS, OutPath, Level, Quantum);
	}

	/** A whole country (or any .osm.pbf extract) into one archive: only what the game builds from */
	/** Several .osm.pbf files read into one set (county extracts overlap at their borders: each way and node once) */
	OsmSet ReadMany(const std::vector<std::string>& Paths, const WayFilter& Keep = GameWays)
	{
		OsmSet Set;
		std::unordered_set<int64_t> Ways;
		for (const std::string& PbfPath : Paths)
		{
			PbfReadStats Read;
			OsmSet One;
			const double LoadS = Seconds([&] { One = ReadOsmPbf(PbfPath, Read, Keep); });
			std::printf("read %s: %llu blocks, %llu of %llu ways kept, %llu nodes kept (of %llu), %llu missing; %.1fs\n", PbfPath.c_str(),
			(unsigned long long)Read.Blocks, (unsigned long long)Read.WaysKept, (unsigned long long)Read.WaysSeen, (unsigned long long)Read.NodesKept,
			(unsigned long long)Read.NodesSeen, (unsigned long long)Read.MissingNodes, LoadS);
			std::fflush(stdout);
			Set.Nodes.insert(One.Nodes.begin(), One.Nodes.end());
			for (Way& W : One.Ways)
			{
				if (Ways.insert(W.Id).second)
				{
					Set.Ways.push_back(std::move(W));
				}
			}
		}
		return Set;
	}

	int PackPbf(const std::vector<std::string>& PbfPaths, const std::string& OutPath, int Level, int64_t Quantum)
	{
		OsmSet Set;
		const double LoadS = Seconds([&] { Set = ReadMany(PbfPaths); });
		return PackSet(Set, LoadS, OutPath, Level, Quantum);
	}

	/** Runs Work(i) for i in [0, Count) on all cores */
	template <typename F>
	void ParallelFor(size_t Count, F Work, const ProgressSpan* Span = nullptr)
	{
		std::atomic<size_t> NextIndex{ 0 };
		std::atomic<size_t> Finished{ 0 };
		std::mutex ErrorLock;
		std::string Error;
		std::vector<std::thread> Threads;
		for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
		{
			Threads.emplace_back([&]()
			{
				for (size_t i = NextIndex++; i < Count; i = NextIndex++)
				{
					try
					{
						Work(i);
					}
					catch (const std::exception& E)
					{
						std::lock_guard<std::mutex> Guard(ErrorLock);
						Error = E.what();
					}
					ReportChunk(Span, ++Finished, Count);
				}
			});
		}
		for (std::thread& T : Threads)
		{
			T.join();
		}
		Must(Error.empty(), Error);
	}

	/**
	 *  A big country (England) into one archive, a band of tile columns at a time, so the ways and tiles of only one
	 *  band are in memory. Bands hold about the same number of points. The tiles come out the same as in one go: each
	 *  piece of way sits in the tile of its first point, so a band's tiles only need the ways with a point in the band,
	 *  and the nodes used twice anywhere (which keep their ids) are known for the whole country before the first band.
	 */

	/** The most memory this run has had committed (Windows; 0 elsewhere) */
	double PeakMemoryGB()
	{
#ifdef _WIN32
		PROCESS_MEMORY_COUNTERS Counters{};
		if (GetProcessMemoryInfo(GetCurrentProcess(), &Counters, sizeof(Counters)))
		{
			return Counters.PeakPagefileUsage / 1073741824.0;
		}
#endif
		return 0.0;
	}
	/** The north-west corner of a tile (1e-7 degrees) */
	LatLon TileCorner(uint32_t X, uint32_t Y, int Zoom)
	{
		const double N = double(1u << Zoom);
		LatLon P;
		P.Lon = int32_t(std::llround((X / N * 360.0 - 180.0) * 1e7));
		P.Lat = int32_t(std::llround(std::atan(std::sinh(3.14159265358979323846 * (1.0 - 2.0 * Y / N))) * 180.0 / 3.14159265358979323846 * 1e7));
		return P;
	}

	/** Only an area of a country: the z14 tiles around a point (a player's PC packs what it will drive, not all of it) */
	struct PackArea
	{
		bool bSet = false;
		TileXY Min, Max;	// the tiles, inclusive
		LatLon BoxMin, BoxMax;	// their outer edges
		bool Contains(const TileXY& T) const { return !bSet || (T.X >= Min.X && T.X <= Max.X && T.Y >= Min.Y && T.Y <= Max.Y); }
	};

	PackArea AreaAround(double Lat, double Lon, double Km)
	{
		const double HalfLat = Km / 111.32, HalfLon = Km / (111.32 * std::max(std::cos(Lat * 3.14159265358979323846 / 180.0), 0.05));
		LatLon SouthWest, NorthEast;
		SouthWest.Lat = int32_t(std::llround((Lat - HalfLat) * 1e7));
		SouthWest.Lon = int32_t(std::llround((Lon - HalfLon) * 1e7));
		NorthEast.Lat = int32_t(std::llround((Lat + HalfLat) * 1e7));
		NorthEast.Lon = int32_t(std::llround((Lon + HalfLon) * 1e7));
		PackArea A;
		A.bSet = true;
		const TileXY SW = TileOf(SouthWest, TileZoom), NE = TileOf(NorthEast, TileZoom);	// y grows southwards
		A.Min = { std::min(SW.X, NE.X), std::min(SW.Y, NE.Y) };
		A.Max = { std::max(SW.X, NE.X), std::max(SW.Y, NE.Y) };
		const LatLon NW = TileCorner(A.Min.X, A.Min.Y, TileZoom), SE = TileCorner(A.Max.X + 1, A.Max.Y + 1, TileZoom);
		A.BoxMin.Lat = SE.Lat;
		A.BoxMin.Lon = NW.Lon;
		A.BoxMax.Lat = NW.Lat;
		A.BoxMax.Lon = SE.Lon;
		return A;
	}

	/** Measured on England (peak 8.9 GB in 5 bands, 5.9 GB in 12): a band costs about this much per point it holds (ways, tags, pieces, tile maps) */
	constexpr double BandBytesPerNode = 250.0;

	int PackPbfBands(const std::string& PbfPath, const std::string& OutPath, int Level, int64_t Quantum, int Bands,
		const PackArea& Area = PackArea(), double MemoryGB = 0.0)
	{
		const auto Start = std::chrono::steady_clock::now();
		auto Elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(); };

		ReportProgress(0.0, "", true);	// (starts again from nothing)
		PbfReadStats Read;
		const PbfBands Pbf(PbfPath, Read, GameWays, Area.bSet ? &Area.BoxMin : nullptr, Area.bSet ? &Area.BoxMax : nullptr);
		std::printf("read %s: %llu blocks, %llu of %llu ways kept, %llu nodes kept (of %llu), %llu missing; %.1fs\n", PbfPath.c_str(),
			(unsigned long long)Read.Blocks, (unsigned long long)Read.WaysKept, (unsigned long long)Read.WaysSeen, (unsigned long long)Read.NodesKept,
			(unsigned long long)Read.NodesSeen, (unsigned long long)Read.MissingNodes, Elapsed());
		std::fflush(stdout);

		// points per tile column, the archive's bounds, and the band edges
		PmHeader H;
		H.TileCompression = PmCompression::None; // each tile is its own zstd frame inside our format
		H.MinZoom = H.MaxZoom = H.CenterZoom = TileZoom;
		H.MinLatE7 = H.MinLonE7 = INT32_MAX;
		H.MaxLatE7 = H.MaxLonE7 = INT32_MIN;
		std::map<uint32_t, uint64_t> PerColumn;
		uint64_t InArea = 0;
		for (size_t i = 0; i < Pbf.NodeIds.size(); ++i)
		{
			if (!Pbf.Found[i])
			{
				continue;
			}
			const LatLon& P = Pbf.Positions[i];
			const TileXY T = TileOf(P, TileZoom);
			if (!Area.Contains(T))
			{
				continue;	// the far end of a way leaving the area
			}
			H.MinLatE7 = std::min(H.MinLatE7, P.Lat);
			H.MaxLatE7 = std::max(H.MaxLatE7, P.Lat);
			H.MinLonE7 = std::min(H.MinLonE7, P.Lon);
			H.MaxLonE7 = std::max(H.MaxLonE7, P.Lon);
			++PerColumn[T.X];
			++InArea;
		}
		if (Area.bSet)
		{
			// the archive says it covers its whole area (the game uses it for any box inside), not just where points are
			H.MinLatE7 = Area.BoxMin.Lat;
			H.MinLonE7 = Area.BoxMin.Lon;
			H.MaxLatE7 = Area.BoxMax.Lat;
			H.MaxLonE7 = Area.BoxMax.Lon;
		}
		H.CenterLatE7 = int32_t((int64_t(H.MinLatE7) + H.MaxLatE7) / 2);
		H.CenterLonE7 = int32_t((int64_t(H.MinLonE7) + H.MaxLonE7) / 2);
		if (Bands <= 0 && MemoryGB > 0.0)
		{
			// what is left of the budget once the node table (ids, positions), the finished tiles and the archive written
			// from them are in memory (England: 3.5 GB of the 5.9 GB it took in 12 bands)
			const double Fixed = Pbf.NodeIds.size() * 17.0 + Pbf.Shared.size() * 8.0 + InArea * 10.0 + 512e6;
			const double Budget = MemoryGB * 1073741824.0;
			if (Budget - Fixed < 512e6)
			{
				std::printf("memory %.1f GB is too little for this much map: about %.1f GB is needed before the first band. "
					"Pack a smaller area with --around=lat,lon,km\n", MemoryGB, (Fixed + 512e6) / 1073741824.0);
				return 3;
			}
			Bands = int(std::ceil(InArea * BandBytesPerNode / (Budget - Fixed)));
			std::printf("memory %.1f GB: node table and tiles %.1f GB, %d bands of about %.1f GB\n", MemoryGB, Fixed / 1073741824.0, Bands,
				(Budget - Fixed) / 1073741824.0);
		}
		if (Bands <= 0)
		{
			Bands = int(std::max<uint64_t>(1, (InArea + 24'999'999) / 25'000'000));	// a band's ways, pieces and nodes in a few GB
		}
		Bands = std::max(Bands, 1);
		std::vector<uint32_t> FirstColumns;	// each band's first tile column
		uint64_t Counted = 0;
		for (const auto& [X, Count] : PerColumn)
		{
			if (FirstColumns.empty() || Counted >= InArea * FirstColumns.size() / uint64_t(Bands))
			{
				FirstColumns.push_back(X);
			}
			Counted += Count;
		}

		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		PackStats Total;
		size_t RawBytes = 0;
		for (size_t Band = 0; Band < FirstColumns.size(); ++Band)
		{
			const uint32_t First = FirstColumns[Band];
			const uint32_t Last = Band + 1 < FirstColumns.size() ? FirstColumns[Band + 1] - 1 : UINT32_MAX;
			auto InBand = [&](uint32_t X) { return X >= First && X <= Last; };
			// (timed on a country: reading the file is the first 6% of the job, the bands 6% to 95% (cutting each band's ways
			// out about a fifth of a band, then a stretch with nothing to count, then its tiles packed), saving the rest)
			const double BandSize = 0.89 / double(FirstColumns.size());
			const double BandFrom = 0.06 + BandSize * double(Band);
			const ProgressSpan ReadSpan{ BandFrom, BandFrom + BandSize * 0.2, "cutting the map into tiles" };
			const ProgressSpan PackSpan{ BandFrom + BandSize * 0.4, BandFrom + BandSize, "packing the tiles" };
			const OsmSet Set = Pbf.ReadBand([&](const LatLon& P) { const TileXY T = TileOf(P, TileZoom); return InBand(T.X) && Area.Contains(T); }, &ReadSpan);

			PackStats Stats;
			const std::map<TileXY, std::vector<WayPiece>> Split = SplitIntoTiles(Set, Stats);
			std::unordered_map<int64_t, bool> KeepId = NodesNeedingIds(Split);	// the cuts, and twice in this band
			for (const auto& [Id, P] : Set.Nodes)
			{
				if (std::binary_search(Pbf.Shared.begin(), Pbf.Shared.end(), Id))
				{
					KeepId[Id] = true;	// twice anywhere
				}
			}

			std::vector<const std::pair<const TileXY, std::vector<WayPiece>>*> Mine;
			for (const auto& Entry : Split)
			{
				if (InBand(Entry.first.X) && Area.Contains(Entry.first))
				{
					Mine.push_back(&Entry);
					Total.Pieces += Entry.second.size();
				}
			}
			std::vector<std::vector<uint8_t>> Packed(Mine.size());
			std::vector<size_t> Raw(Mine.size());
			ParallelFor(Mine.size(), [&](size_t i)
			{
				const std::vector<uint8_t> Bytes = EncodeTile(Mine[i]->second, Set, Quantum, KeepId);
				Raw[i] = Bytes.size();
				Packed[i] = Zstd(Bytes.data(), Bytes.size(), Level);
			}, &PackSpan);
			for (size_t i = 0; i < Mine.size(); ++i)
			{
				RawBytes += Raw[i];
				Tiles[ZxyToTileId(TileZoom, Mine[i]->first.X, Mine[i]->first.Y)] = std::move(Packed[i]);
			}
			Total.MissingNodeRefs += Stats.MissingNodeRefs;
			std::printf("band %zu of %zu (tile columns %u to %u): %zu ways, %zu nodes, %zu tiles; %.1fs\n", Band + 1, FirstColumns.size(), First,
				Band + 1 < FirstColumns.size() ? Last : PerColumn.rbegin()->first, Set.Ways.size(), Set.Nodes.size(), Mine.size(), Elapsed());
			std::fflush(stdout);
		}

		ReportProgress(0.96, "saving the map");
		const std::string Meta = nlohmann::json{
			{ "format", "geodrive-osm" }, { "format_version", 0 }, { "tile_compression", "zstd" },
			{ "attribution", "(c) OpenStreetMap contributors, ODbL" },
		}.dump();
		std::vector<uint8_t> Archive;
		std::string Error;
		Must(WritePmTiles(Tiles, H, Meta, Archive, Error), Error);
		Tiles.clear();
		WriteFile(OutPath, Archive);
		ReportProgress(1.0, "the map is packed");

		std::printf("ways %llu, pieces %zu, nodes %llu, used twice %zu, missing node refs (counted per band) %zu\n", (unsigned long long)Read.WaysKept,
			Total.Pieces, (unsigned long long)Read.NodesKept, Pbf.Shared.size(), Total.MissingNodeRefs);
		std::printf("precision %lld x 1e-7 deg, raw tiles %s -> zstd %d -> archive %s; %.1fs\n", (long long)Quantum, Mb(RawBytes).c_str(), Level,
			Mb(Archive.size()).c_str(), Elapsed());
		std::printf("peak memory %.2f GB\n", PeakMemoryGB());
		return 0;
	}

	/** Douglas-Peucker on lat/lon (1e-7 degrees): keeps the points needed to stay within Tolerance of the line */
	void Simplify(const std::vector<LatLon>& In, size_t FirstIndex, size_t LastIndex, double Tolerance, std::vector<bool>& Keep)
	{
		// a to-do list of spans instead of recursion: a long road would run out of stack
		std::vector<std::pair<size_t, size_t>> Spans = { { FirstIndex, LastIndex } };
		while (!Spans.empty())
		{
			const auto [First, Last] = Spans.back();
			Spans.pop_back();
			if (Last <= First + 1)
			{
				continue;
			}
			const double Ax = In[First].Lon, Ay = In[First].Lat, Bx = In[Last].Lon, By = In[Last].Lat;
			const double Dx = Bx - Ax, Dy = By - Ay, Len2 = Dx * Dx + Dy * Dy;
			double Worst = -1.0;
			size_t WorstIndex = First;
			for (size_t I = First + 1; I < Last; ++I)
			{
				const double Px = In[I].Lon - Ax, Py = In[I].Lat - Ay;
				const double T = Len2 > 0.0 ? std::clamp((Px * Dx + Py * Dy) / Len2, 0.0, 1.0) : 0.0;
				const double Ex = Px - T * Dx, Ey = Py - T * Dy;
				const double D = Ex * Ex + Ey * Ey;
				if (D > Worst)
				{
					Worst = D;
					WorstIndex = I;
				}
			}
			if (Worst > Tolerance * Tolerance)
			{
				Keep[WorstIndex] = true;
				Spans.emplace_back(First, WorstIndex);
				Spans.emplace_back(WorstIndex, Last);
			}
		}
	}

	/**
	 *  A world index for the in-game map, from a country .osm.pbf: every named city, town, village, suburb and hamlet
	 *  (with population when tagged), and the main roads (motorway to secondary) simplified to about 30 m for drawing.
	 */
	/**
	 *  Every airport of the extracts, for flying: its runways (centrelines, width, surface, their ends' numbers), taxiways
	 *  and aprons, from OSM's aeroway=runway / taxiway / apron, each given to the nearest airport (aeroway=aerodrome, an
	 *  outline or a point, with its name and codes) within 5 km; runways with none near make an airstrip of their own.
	 *  Runways under 300 m are left out. Positions to 5 decimals (about 1 m).
	 */
	int Airports(const std::vector<std::string>& PbfPaths, const std::string& OutPath)
	{
		struct Field
		{
			std::string Name, Icao, Iata;
			double Lat = 0.0, Lon = 0.0;
			nlohmann::json Runways = nlohmann::json::array(), Taxiways = nlohmann::json::array(), Aprons = nlohmann::json::array();
		};
		std::vector<Field> Fields;
		for (const std::string& PbfPath : PbfPaths)
		{
			for (const Place& P : ReadPlaces(PbfPath, true))
			{
				Field F;
				F.Name = P.Name;
				F.Icao = P.Icao;
				F.Iata = P.Iata;
				F.Lat = P.Position.Lat / 1e7;
				F.Lon = P.Position.Lon / 1e7;
				Fields.push_back(std::move(F));
			}
		}
		static const char* Keys[] = { "aeroway", "name", "ref", "width", "surface", "icao", "iata" };
		const OsmSet Aero = ReadMany(PbfPaths, [](std::vector<Tag>& Tags)
		{
			bool bWanted = false;
			for (const Tag& T : Tags)
			{
				bWanted = bWanted || (T.first == "aeroway" && (T.second == "runway" || T.second == "taxiway" || T.second == "apron" || T.second == "aerodrome"));
			}
			if (bWanted)
			{
				Tags.erase(std::remove_if(Tags.begin(), Tags.end(), [](const Tag& T)
				{
					return std::none_of(std::begin(Keys), std::end(Keys), [&](const char* K) { return T.first == K; });
				}), Tags.end());
			}
			return bWanted;
		});
		auto TagOfWay = [](const Way& W, const char* Key) -> std::string
		{
			for (const Tag& T : W.Tags)
			{
				if (T.first == Key)
				{
					return T.second;
				}
			}
			return std::string();
		};
		auto Points = [&](const Way& W)
		{
			std::vector<std::pair<double, double>> Out;
			for (const int64_t Id : W.Nodes)
			{
				const auto It = Aero.Nodes.find(Id);
				if (It != Aero.Nodes.end())
				{
					Out.emplace_back(It->second.Lat / 1e7, It->second.Lon / 1e7);
				}
			}
			return Out;
		};
		auto Metres = [](double LatA, double LonA, double LatB, double LonB)
		{
			const double Y = (LatB - LatA) * 111320.0, X = (LonB - LonA) * 111320.0 * std::cos(LatA * 3.14159265 / 180.0);
			return std::sqrt(X * X + Y * Y);
		};
		auto Round5 = [](double D) { return std::round(D * 1e5) / 1e5; };
		auto Line = [&](const std::vector<std::pair<double, double>>& P)
		{
			nlohmann::json Out = nlohmann::json::array();
			for (const auto& [Lat, Lon] : P)
			{
				Out.push_back({ Round5(Lat), Round5(Lon) });
			}
			return Out;
		};
		// airports drawn as outlines: their middle
		for (const Way& W : Aero.Ways)
		{
			if (TagOfWay(W, "aeroway") != "aerodrome")
			{
				continue;
			}
			const auto P = Points(W);
			if (P.size() < 3)
			{
				continue;
			}
			Field F;
			F.Name = TagOfWay(W, "name");
			F.Icao = TagOfWay(W, "icao");
			F.Iata = TagOfWay(W, "iata");
			for (const auto& [Lat, Lon] : P)
			{
				F.Lat += Lat / double(P.size());
				F.Lon += Lon / double(P.size());
			}
			Fields.push_back(std::move(F));
		}
		auto Nearest = [&](double Lat, double Lon, double Within) -> int
		{
			int Best = -1;
			double BestD = Within;
			for (size_t i = 0; i < Fields.size(); ++i)
			{
				const double D = Metres(Lat, Lon, Fields[i].Lat, Fields[i].Lon);
				if (D < BestD)
				{
					BestD = D;
					Best = int(i);
				}
			}
			return Best;
		};
		// runways first (they make airstrips of their own where no airport is near), then taxiways and aprons
		size_t RunwayCount = 0;
		std::vector<bool> HasRunway(Fields.size(), false);
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			for (const Way& W : Aero.Ways)
			{
				const std::string Kind = TagOfWay(W, "aeroway");
				if ((Pass == 0) != (Kind == "runway") || Kind == "aerodrome")
				{
					continue;
				}
				const auto P = Points(W);
				if (P.size() < 2)
				{
					continue;
				}
				double Length = 0.0;
				for (size_t i = 0; i + 1 < P.size(); ++i)
				{
					Length += Metres(P[i].first, P[i].second, P[i + 1].first, P[i + 1].second);
				}
				const double MidLat = (P.front().first + P.back().first) * 0.5, MidLon = (P.front().second + P.back().second) * 0.5;
				int At = Nearest(MidLat, MidLon, Kind == "runway" ? 5000.0 : 3000.0);
				if (Kind == "runway")
				{
					const bool bArea = W.Nodes.size() > 3 && W.Nodes.front() == W.Nodes.back();
					if (Length < 300.0 || bArea)
					{
						continue;	// (short strips, and runways drawn as outlines rather than centrelines)
					}
					if (At < 0)
					{
						Field F;
						F.Lat = MidLat;
						F.Lon = MidLon;
						Fields.push_back(std::move(F));
						HasRunway.push_back(false);
						At = int(Fields.size()) - 1;
					}
					const std::string Width = TagOfWay(W, "width");
					Fields[At].Runways.push_back({ { "ref", TagOfWay(W, "ref") }, { "width", Width.empty() ? 0.0 : std::atof(Width.c_str()) },
						{ "surface", TagOfWay(W, "surface") }, { "line", Line(P) } });
					HasRunway[At] = true;
					++RunwayCount;
				}
				else if (At >= 0 && HasRunway[At])
				{
					if (Kind == "taxiway")
					{
						const std::string Width = TagOfWay(W, "width");
						Fields[At].Taxiways.push_back({ { "width", Width.empty() ? 0.0 : std::atof(Width.c_str()) }, { "line", Line(P) } });
					}
					else
					{
						Fields[At].Aprons.push_back(Line(P));
					}
				}
			}
		}
		nlohmann::json Out;
		Out["format"] = "geodrive-airports";
		Out["version"] = 1;
		nlohmann::json List = nlohmann::json::array();
		size_t Named = 0;
		for (size_t i = 0; i < Fields.size(); ++i)
		{
			if (!HasRunway[i])
			{
				continue;
			}
			const Field& F = Fields[i];
			Named += F.Name.empty() ? 0 : 1;
			List.push_back({ { "name", F.Name }, { "icao", F.Icao }, { "iata", F.Iata }, { "lat", Round5(F.Lat) }, { "lon", Round5(F.Lon) },
				{ "runways", F.Runways }, { "taxiways", F.Taxiways }, { "aprons", F.Aprons } });
		}
		Out["airports"] = std::move(List);
		const std::string Text = Out.dump();
		WriteFile(OutPath, std::vector<uint8_t>(Text.begin(), Text.end()));
		std::printf("airports: %zu with runways (%zu named), %zu runways -> %s\n", Out["airports"].size(), Named, RunwayCount, Mb(Text.size()).c_str());
		return 0;
	}

	/**
	 *  The places a game suits, for the game's own games (Godot/game_gen.gd): named game and nature reserves, protected
	 *  areas and national parks (as outlines), parks and golf courses over 300 m across, stadiums and sports centres; and
	 *  the race tracks (highway=raceway, or a motor, karting or motocross track) with their shape, to race round (the
	 *  longest piece of each by its name, over 600 m). Each {id, name, kind (reserve, park, stadium, track), lat, lon,
	 *  half (m: half its size, 250 to 2000), course [[lat, lon]...] for a track}. A golf course with its holes mapped is
	 *  kind golf: its holes' pars here, and in a file of its own (<out>.golf/<id>.json, read when it's played) its holes
	 *  {ref, par, line [[lat, lon]...]: tee to pin} in order, its greens and bunkers (outlines).
	 *  Ways only (the reader has no relations: the biggest reserves come from OpenStreetMap's Overpass in the game).
	 */
	int Games(const std::vector<std::string>& PbfPaths, const std::string& OutPath)
	{
		static const char* Keys[] = { "leisure", "boundary", "highway", "sport", "name", "golf", "ref", "par", "landuse" };
		auto Kind = [](const std::vector<Tag>& Tags) -> std::string
		{
			std::string Leisure, Boundary, Highway, Sport, Golf, Landuse;
			for (const Tag& T : Tags)
			{
				if (T.first == "leisure") Leisure = T.second;
				else if (T.first == "boundary") Boundary = T.second;
				else if (T.first == "highway") Highway = T.second;
				else if (T.first == "sport") Sport = T.second;
				else if (T.first == "golf") Golf = T.second;
				else if (T.first == "landuse") Landuse = T.second;
			}
			// (a motor sport ground, or a raceway's land: not a game of its own, the name of the circuit in it)
			const bool bMotor = Sport.find("motor") != std::string::npos || Sport.find("karting") != std::string::npos;
			if (Landuse == "raceway" || (bMotor && (Leisure == "sports_centre" || Leisure == "stadium" || Leisure == "track") && Highway.empty()
				&& Leisure != "track"))
			{
				return "motor_area";
			}
			if (Golf == "hole" || Golf == "green" || Golf == "bunker")
			{
				return "golf_" + Golf;	// (a course's parts: given to their course below)
			}
			if (Leisure == "golf_course")
			{
				return "golf";
			}
			if (Highway == "raceway" || (Leisure == "track" && (Sport.find("motor") != std::string::npos || Sport.find("karting") != std::string::npos)))
			{
				return "track";
			}
			if (Leisure == "nature_reserve" || Boundary == "protected_area" || Boundary == "national_park")
			{
				return "reserve";
			}
			if (Leisure == "park")
			{
				return "park";
			}
			if (Leisure == "stadium" || Leisure == "sports_centre")
			{
				return "stadium";
			}
			return std::string();
		};
		const OsmSet Set = ReadMany(PbfPaths, [&](std::vector<Tag>& Tags)
		{
			if (Kind(Tags).empty())
			{
				return false;
			}
			Tags.erase(std::remove_if(Tags.begin(), Tags.end(), [](const Tag& T)
			{
				return std::none_of(std::begin(Keys), std::end(Keys), [&](const char* K) { return T.first == K; });
			}), Tags.end());
			return true;
		});
		auto Metres = [](double LatA, double LonA, double LatB, double LonB)
		{
			const double Y = (LatB - LatA) * 110574.0, X = (LonB - LonA) * 111320.0 * std::cos(LatA * 3.14159265 / 180.0);
			return std::sqrt(X * X + Y * Y);
		};
		auto Round5 = [](double D) { return std::round(D * 1e5) / 1e5; };
		struct Found
		{
			nlohmann::json Json;
			double Length = 0.0;
		};
		std::map<std::string, Found> ByName;	// (one of each name: a track's longest piece, a place's biggest)
		size_t Tracks = 0;
		struct Part
		{
			std::string Kind, Ref;
			int Par = 0;
			nlohmann::json Line;
			double Lat = 0.0, Lon = 0.0;	// (its middle)
		};
		std::vector<Part> Parts;	// golf holes, greens and bunkers
		struct TrackPiece
		{
			int64_t Id;
			std::vector<int64_t> Nodes;
			std::string Name;
			std::vector<std::pair<double, double>> Points;
			double Length;
		};
		std::vector<TrackPiece> TrackPieces;
		struct MotorArea { std::string Name; double S, W, N, E; };
		std::vector<MotorArea> MotorAreas;
		std::map<std::string, std::array<double, 4>> Boxes;	// a golf course's box (by its name): s, w, n, e
		for (const Way& W : Set.Ways)
		{
			const std::string K = Kind(W.Tags);
			std::string Name, Ref, Par;
			for (const Tag& T : W.Tags)
			{
				if (T.first == "name") Name = T.second;
				else if (T.first == "ref") Ref = T.second;
				else if (T.first == "par") Par = T.second;
			}
			if (K == "motor_area")
			{
				if (!Name.empty())
				{
					double South = 1e9, West = 1e9, North = -1e9, East = -1e9;
					for (const int64_t Id : W.Nodes)
					{
						const auto It = Set.Nodes.find(Id);
						if (It != Set.Nodes.end())
						{
							const double Lat = It->second.Lat / 1e7, Lon = It->second.Lon / 1e7;
							South = std::min(South, Lat), North = std::max(North, Lat), West = std::min(West, Lon), East = std::max(East, Lon);
						}
					}
					if (North > South)
					{
						MotorAreas.push_back({ Name, South, West, North, East });
					}
				}
				continue;
			}
			if (K.rfind("golf_", 0) == 0)
			{
				Part G;
				G.Kind = K.substr(5);
				G.Ref = Ref;
				G.Par = std::atoi(Par.c_str());
				G.Line = nlohmann::json::array();
				size_t Count = 0;
				for (const int64_t Id : W.Nodes)
				{
					const auto It = Set.Nodes.find(Id);
					if (It != Set.Nodes.end())
					{
						const double Lat = It->second.Lat / 1e7, Lon = It->second.Lon / 1e7;
						G.Line.push_back({ std::round(Lat * 1e6) / 1e6, std::round(Lon * 1e6) / 1e6 });	// (to 10 cm: a green's edge)
						G.Lat += Lat, G.Lon += Lon, ++Count;
					}
				}
				if (Count >= 2)
				{
					G.Lat /= double(Count), G.Lon /= double(Count);
					Parts.push_back(std::move(G));
				}
				continue;
			}
			if (Name.empty() && K != "track")
			{
				continue;
			}
			std::vector<std::pair<double, double>> P;
			double MinLat = 1e9, MaxLat = -1e9, MinLon = 1e9, MaxLon = -1e9, Length = 0.0;
			for (const int64_t Id : W.Nodes)
			{
				const auto It = Set.Nodes.find(Id);
				if (It == Set.Nodes.end())
				{
					continue;
				}
				const double Lat = It->second.Lat / 1e7, Lon = It->second.Lon / 1e7;
				if (!P.empty())
				{
					Length += Metres(P.back().first, P.back().second, Lat, Lon);
				}
				P.emplace_back(Lat, Lon);
				MinLat = std::min(MinLat, Lat), MaxLat = std::max(MaxLat, Lat), MinLon = std::min(MinLon, Lon), MaxLon = std::max(MaxLon, Lon);
			}
			if (P.size() < 2)
			{
				continue;
			}
			const double Half = Metres(MinLat, MinLon, MaxLat, MaxLon) * 0.5;
			nlohmann::json J = { { "id", "w" + std::to_string(W.Id) }, { "kind", K } };
			double Size = Half;
			if (K == "track")
			{
				// (a circuit's often mapped in pieces, its corners named on their own: joined up below; not its pit lanes)
				std::string Lower = Name;
				std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char Ch) { return char(std::tolower(Ch)); });
				if (Lower.find("pit") == std::string::npos && Lower.find("boxengasse") == std::string::npos && Lower.find("box lane") == std::string::npos)
				{
					TrackPieces.push_back({ W.Id, W.Nodes, Name, P, Length });
				}
				continue;
			}
			else
			{
				if (P.size() < 3 || (K == "park" && Half < 150.0) || (K == "stadium" && Half < 60.0))
				{
					continue;	// (a patch of grass, a hall or a court: not a game's)
				}
				J["lat"] = Round5((MinLat + MaxLat) * 0.5);
				J["lon"] = Round5((MinLon + MaxLon) * 0.5);
				J["half"] = std::round(std::clamp(Half, 250.0, 2000.0));
				if (K == "golf")
				{
					Boxes[Name] = { MinLat, MinLon, MaxLat, MaxLon };
				}
			}
			J["name"] = Name;
			Found& F = ByName[Name];
			if (F.Json.is_null() || Size > F.Length)
			{
				F.Json = std::move(J);
				F.Length = Size;
			}
		}
		// the race tracks: their pieces joined end to end (sharing an end's node) into whole laps or runs; each named for
		// the name most of its length has (a circuit's own over its corners'), kept if over 600 m
		{
			std::multimap<int64_t, size_t> ByEnd;
			for (size_t i = 0; i < TrackPieces.size(); ++i)
			{
				ByEnd.insert({ TrackPieces[i].Nodes.front(), i });
				ByEnd.insert({ TrackPieces[i].Nodes.back(), i });
			}
			std::vector<bool> Used(TrackPieces.size(), false);
			for (size_t First = 0; First < TrackPieces.size(); ++First)
			{
				if (Used[First])
				{
					continue;
				}
				Used[First] = true;
				std::vector<int64_t> Nodes = TrackPieces[First].Nodes;
				std::vector<std::pair<double, double>> Line = TrackPieces[First].Points;
				std::map<std::string, double> NameLength = { { TrackPieces[First].Name, TrackPieces[First].Length } };
				double Total = TrackPieces[First].Length;
				// (on from either end while a piece not yet used meets it there)
				for (int End = 0; End < 2; ++End)
				{
					std::string Along = TrackPieces[First].Name;	// (at a fork, the way the same name goes: the Nordschleife, not
																	// the Grand Prix circuit it meets)
					for (;;)
					{
						const int64_t At = End == 0 ? Nodes.back() : Nodes.front();
						if (Nodes.size() > 2 && Nodes.front() == Nodes.back())
						{
							break;	// (a lap: round to the start)
						}
						size_t Next = SIZE_MAX;
						const auto Range = ByEnd.equal_range(At);
						for (auto It = Range.first; It != Range.second; ++It)
						{
							if (!Used[It->second] && (Next == SIZE_MAX || (TrackPieces[It->second].Name == Along && TrackPieces[Next].Name != Along)))
							{
								Next = It->second;
							}
						}
						if (Next == SIZE_MAX)
						{
							break;
						}
						Used[Next] = true;
						const TrackPiece& T = TrackPieces[Next];
						std::vector<int64_t> N = T.Nodes;
						std::vector<std::pair<double, double>> L = T.Points;
						const bool bFlip = End == 0 ? N.front() != At : N.back() != At;
						if (bFlip)
						{
							std::reverse(N.begin(), N.end());
							std::reverse(L.begin(), L.end());
						}
						if (End == 0)
						{
							Nodes.insert(Nodes.end(), N.begin() + 1, N.end());
							Line.insert(Line.end(), L.begin() + (L.empty() ? 0 : 1), L.end());
						}
						else
						{
							Nodes.insert(Nodes.begin(), N.begin(), N.end() - 1);
							Line.insert(Line.begin(), L.begin(), L.end() - (L.empty() ? 0 : 1));
						}
						NameLength[T.Name] += T.Length;
						Total += T.Length;
						if (!T.Name.empty())
						{
							Along = T.Name;
						}
					}
				}
				if (Total < 600.0 || Line.size() < 2)
				{
					continue;
				}
				// its name: a circuit's, raceway's or track's if it has one, else the name most of it has
				std::string Name;
				double Best = -1.0;
				for (const auto& [N, L] : NameLength)
				{
					// (a whole circuit's name, in English or not: not one of its corners')
					std::string Lower = N;
					std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char Ch) { return char(std::tolower(Ch)); });
					bool bTrackName = false;
					for (const char* Word : { "circuit", "raceway", "track", "speedway", "racetrack", "schleife", "autodromo", "autódromo",
						"circuito", "motodrom", "rennbahn" })	// (not "strecke", "ring": corners have them, the Steilstrecke)
					{
						bTrackName = bTrackName || Lower.find(Word) != std::string::npos;
					}
					const double Score = N.empty() ? 0.0 : L + (bTrackName ? 1e6 : 0.0);
					if (Score > Best)
					{
						Best = Score;
						Name = N;
					}
				}
				const bool bOwnName = Best >= 1e6;
				if (!bOwnName)
				{
					const auto& Mid = Line[Line.size() / 2];
					double Smallest = 1e18;
					for (const MotorArea& A : MotorAreas)
					{
						const double Pad = 0.003;	// (~300 m: a circuit's edge runs along its ground's)
						if (Mid.first >= A.S - Pad && Mid.first <= A.N + Pad && Mid.second >= A.W - Pad && Mid.second <= A.E + Pad
							&& (A.N - A.S) * (A.E - A.W) < Smallest)
						{
							Smallest = (A.N - A.S) * (A.E - A.W);
							Name = A.Name;
						}
					}
				}
				// (a bit of one, or an unnamed something: not a race of its own)
				if ((!bOwnName && !Name.empty() && Total < 1500.0) || (Name.empty() && Total < 2000.0))
				{
					continue;
				}
				if (Name.empty())
				{
					Name = "Race track";
				}
				if (!bOwnName && ByName.count(Name))
				{
					char Km[32];
					std::snprintf(Km, sizeof(Km), ", %.1f km", Total / 1000.0);
					Name += Km;	// (two circuits in one ground: the Nürburgring's Grand Prix track and its Nordschleife)
				}
				if (Total > 8000.0)
				{
					// (a long one: what its pieces are called, to see it's named right)
					std::vector<std::pair<double, std::string>> Parts;
					for (const auto& [N, L] : NameLength)
					{
						Parts.push_back({ L, N });
					}
					std::sort(Parts.rbegin(), Parts.rend());
					std::string Said;
					for (size_t i = 0; i < std::min<size_t>(Parts.size(), 8); ++i)
					{
						Said += (i ? ", " : "") + (Parts[i].second.empty() ? std::string("(unnamed)") : Parts[i].second) + " " + std::to_string(int(Parts[i].first)) + " m";
					}
					std::printf("  a %.1f km track, %s: %s\n", Total / 1000.0, Name.c_str(), Said.c_str());
				}
				nlohmann::json Course = nlohmann::json::array();
				for (const auto& [Lat, Lon] : Line)
				{
					Course.push_back({ Round5(Lat), Round5(Lon) });
				}
				const auto& Middle = Line[Line.size() / 2];
				nlohmann::json J = { { "id", "w" + std::to_string(TrackPieces[First].Id) }, { "kind", "track" }, { "name", Name },
					{ "lat", Round5(Middle.first) }, { "lon", Round5(Middle.second) }, { "half", 600.0 }, { "course", std::move(Course) } };
				Found& F = ByName[Name];
				if (F.Json.is_null() || Total > F.Length)
				{
					F.Json = std::move(J);
					F.Length = Total;
				}
			}
		}

		// a golf course's holes (in order), greens and bunkers: those inside its box (and a little round it); a course
		// with no holes mapped is a park
		size_t Courses = 0, Holes = 0;
		for (auto& [Name, F] : ByName)
		{
			if (F.Json["kind"] != "golf")
			{
				continue;
			}
			const auto& B = Boxes[Name];
			const double Pad = 0.001;	// (~100 m)
			nlohmann::json HoleList = nlohmann::json::array(), Greens = nlohmann::json::array(), Bunkers = nlohmann::json::array();
			std::vector<const Part*> Mine;
			for (const Part& G : Parts)
			{
				if (G.Lat >= B[0] - Pad && G.Lat <= B[2] + Pad && G.Lon >= B[1] - Pad && G.Lon <= B[3] + Pad)
				{
					Mine.push_back(&G);
				}
			}
			std::stable_sort(Mine.begin(), Mine.end(), [](const Part* A, const Part* C) { return std::atoi(A->Ref.c_str()) < std::atoi(C->Ref.c_str()); });
			for (const Part* G : Mine)
			{
				if (G->Kind == "hole")
				{
					HoleList.push_back({ { "ref", G->Ref }, { "par", G->Par }, { "line", G->Line } });
				}
				else
				{
					(G->Kind == "green" ? Greens : Bunkers).push_back(G->Line);
				}
			}
			if (HoleList.empty())
			{
				F.Json["kind"] = "park";
				continue;
			}
			++Courses;
			Holes += HoleList.size();
			F.Json["holes"] = std::move(HoleList);
			F.Json["greens"] = std::move(Greens);
			F.Json["bunkers"] = std::move(Bunkers);
		}
		// (each course's holes, greens and bunkers in a file of its own: the list stays small)
		const std::string Stem = OutPath.size() > 11 && OutPath.compare(OutPath.size() - 11, 11, ".games.json") == 0 ? OutPath.substr(0, OutPath.size() - 11) : OutPath;
		const std::filesystem::path GolfDir = std::filesystem::u8path(Stem + ".golf");
		std::filesystem::create_directories(GolfDir);
		for (auto& [Name, F] : ByName)
		{
			if (F.Json["kind"] != "golf")
			{
				continue;
			}
			nlohmann::json Course = { { "format", "geodrive-golf" }, { "version", 1 }, { "name", Name }, { "holes", F.Json["holes"] },
				{ "greens", F.Json["greens"] }, { "bunkers", F.Json["bunkers"] } };
			nlohmann::json Pars = nlohmann::json::array(), Fairways = nlohmann::json::array();
			for (const auto& H : F.Json["holes"])
			{
				Pars.push_back(H["par"]);
				nlohmann::json Line = nlohmann::json::array();	// (tee to pin, flat: lat, lon, lat, lon...: no trees on it)
				for (const auto& Point : H["line"])
				{
					Line.push_back(Round5(double(Point[0])));
					Line.push_back(Round5(double(Point[1])));
				}
				Fairways.push_back(std::move(Line));
			}
			F.Json["fairways"] = std::move(Fairways);
			F.Json.erase("holes");
			F.Json.erase("greens");
			F.Json.erase("bunkers");
			F.Json["pars"] = std::move(Pars);
			const std::string Text = Course.dump();
			WriteFile((GolfDir / (std::string(F.Json["id"]) + ".json")).u8string(), std::vector<uint8_t>(Text.begin(), Text.end()));
		}
		std::printf("golf: %zu courses with their holes mapped (%zu holes) -> %s\n", Courses, Holes, std::filesystem::path(GolfDir).u8string().c_str());
		nlohmann::json List = nlohmann::json::array();
		for (auto& [Name, F] : ByName)
		{
			Tracks += F.Json["kind"] == "track" ? 1 : 0;
			List.push_back(std::move(F.Json));
		}
		nlohmann::json Out;
		Out["format"] = "geodrive-games";
		Out["version"] = 1;
		Out["places"] = std::move(List);
		const std::string Text = Out.dump();
		WriteFile(OutPath, std::vector<uint8_t>(Text.begin(), Text.end()));
		std::printf("game places: %zu (%zu race tracks) -> %s\n", Out["places"].size(), Tracks, Mb(Text.size()).c_str());
		return 0;
	}

	int World(const std::vector<std::string>& PbfPaths, const std::string& OutPath)
	{
		std::vector<Place> Places;
		const double PlacesS = Seconds([&]
		{
			for (const std::string& PbfPath : PbfPaths)
			{
				std::vector<Place> Some = ReadPlaces(PbfPath);
				Places.insert(Places.end(), Some.begin(), Some.end());
			}
		});
		std::printf("places read: %zu\n", Places.size());
		std::fflush(stdout);

		PbfReadStats Stats;
		OsmSet Roads;
		const double RoadsS = Seconds([&]
		{
			Roads = ReadMany(PbfPaths, [](std::vector<Tag>& Tags)
			{
				for (const Tag& T : Tags)
				{
					if (T.first == "highway" && (T.second == "motorway" || T.second == "trunk" || T.second == "primary" || T.second == "secondary"))
					{
						const std::string Highway = T.second;
						Tags.assign(1, Tag("highway", Highway));
						return true;
					}
				}
				return false;
			});
		});

		nlohmann::json Out;
		Out["format"] = "geodrive-world";
		Out["version"] = 1;
		int32_t MinLat = INT32_MAX, MinLon = INT32_MAX, MaxLat = INT32_MIN, MaxLon = INT32_MIN;
		auto Deg = [](int32_t E7) { return std::round(E7 / 100.0) / 1e5; };	// 5 decimals, about 1 m

		nlohmann::json PlaceList = nlohmann::json::array();
		for (const Place& P : Places)
		{
			PlaceList.push_back({ P.Name, P.Kind, Deg(P.Position.Lat), Deg(P.Position.Lon), P.Population });
		}
		Out["places"] = std::move(PlaceList);

		nlohmann::json RoadList = nlohmann::json::array();
		size_t PointsIn = 0, PointsOut = 0;
		for (const Way& W : Roads.Ways)
		{
			std::vector<LatLon> Line;
			for (const int64_t Id : W.Nodes)
			{
				const auto It = Roads.Nodes.find(Id);
				if (It != Roads.Nodes.end())
				{
					Line.push_back(It->second);
					MinLat = std::min(MinLat, It->second.Lat);
					MaxLat = std::max(MaxLat, It->second.Lat);
					MinLon = std::min(MinLon, It->second.Lon);
					MaxLon = std::max(MaxLon, It->second.Lon);
				}
			}
			if (Line.size() < 2)
			{
				continue;
			}
			std::vector<bool> Keep(Line.size(), false);
			Keep.front() = Keep.back() = true;
			Simplify(Line, 0, Line.size() - 1, 3000.0, Keep);	// 3000 x 1e-7 degrees: about 30 m
			const std::string& Highway = W.Tags[0].second;
			const int Class = Highway == "motorway" ? 0 : Highway == "trunk" ? 1 : Highway == "primary" ? 2 : 3;
			nlohmann::json Points = nlohmann::json::array();
			for (size_t I = 0; I < Line.size(); ++I)
			{
				if (Keep[I])
				{
					Points.push_back(Deg(Line[I].Lat));
					Points.push_back(Deg(Line[I].Lon));
				}
			}
			PointsIn += Line.size();
			PointsOut += Points.size() / 2;
			RoadList.push_back({ { "c", Class }, { "p", std::move(Points) } });
		}
		Out["roads"] = std::move(RoadList);
		Out["bounds"] = { Deg(MinLat), Deg(MinLon), Deg(MaxLat), Deg(MaxLon) };

		const std::string Text = Out.dump();
		WriteFile(OutPath, std::vector<uint8_t>(Text.begin(), Text.end()));
		std::printf("world: %zu places (%.1fs), %zu main roads, %zu of %zu points kept (%.1fs) -> %s\n", Places.size(), PlacesS,
			Roads.Ways.size(), PointsOut, PointsIn, RoadsS, Mb(Text.size()).c_str());
		return 0;
	}

	int PackSet(const OsmSet& Set, double LoadS, const std::string& OutPath, int Level, int64_t Quantum)
	{

		PackStats Stats;
		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		const double EncodeS = Seconds([&] { Tiles = EncodeAll(Set, Quantum, Stats); });

		size_t RawBytes = 0;
		const double ZipS = Seconds([&]
		{
			for (auto& [Id, Bytes] : Tiles)
			{
				RawBytes += Bytes.size();
				Bytes = Zstd(Bytes.data(), Bytes.size(), Level);
			}
		});

		PmHeader H;
		H.TileCompression = PmCompression::None; // each tile is its own zstd frame inside our format
		H.MinZoom = H.MaxZoom = H.CenterZoom = TileZoom;
		H.MinLatE7 = H.MinLonE7 = INT32_MAX;
		H.MaxLatE7 = H.MaxLonE7 = INT32_MIN;
		for (const auto& [Id, P] : Set.Nodes)
		{
			H.MinLatE7 = std::min(H.MinLatE7, P.Lat);
			H.MaxLatE7 = std::max(H.MaxLatE7, P.Lat);
			H.MinLonE7 = std::min(H.MinLonE7, P.Lon);
			H.MaxLonE7 = std::max(H.MaxLonE7, P.Lon);
		}
		H.CenterLatE7 = int32_t((int64_t(H.MinLatE7) + H.MaxLatE7) / 2);
		H.CenterLonE7 = int32_t((int64_t(H.MinLonE7) + H.MaxLonE7) / 2);

		const std::string Meta = nlohmann::json{
			{ "format", "geodrive-osm" }, { "format_version", 0 }, { "tile_compression", "zstd" },
			{ "attribution", "(c) OpenStreetMap contributors, ODbL" },
		}.dump();
		std::vector<uint8_t> Archive;
		std::string Error;
		Must(WritePmTiles(Tiles, H, Meta, Archive, Error), Error);
		WriteFile(OutPath, Archive);

		std::printf("ways %zu, pieces %zu, tiles %zu, nodes kept with ids %zu of %zu, missing node refs %zu\n",
			Stats.Ways, Stats.Pieces, Tiles.size(), Stats.GlobalIdNodes, Set.Nodes.size(), Stats.MissingNodeRefs);
		std::printf("precision %lld x 1e-7 deg, raw tiles %s -> zstd %d -> archive %s\n", (long long)Quantum, Mb(RawBytes).c_str(), Level,
			Mb(Archive.size()).c_str());
		std::printf("time: read json %.2fs, encode %.2fs, zstd %.2fs\n", LoadS, EncodeS, ZipS);
		return 0;
	}

	/** Packs the roads a game baked (roads_<box>.roadbin, one uncompressed road tile) into a road archive, then reads
	 *  it back and checks every height came through */
	int PackRoads(const std::string& BinPath, const std::string& OutPath, int Level)
	{
		const std::vector<uint8_t> Bin = ReadFile(BinPath);
		RoadBake Bake;
		Must(DecodeRoads(Bin.data(), Bin.size(), Bake), "the baked roads file is broken");

		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		size_t RawBytes = 0, Heights = 0;
		PmHeader H;
		H.TileCompression = PmCompression::None; // each tile is its own zstd frame inside our format
		H.MinZoom = H.MaxZoom = H.CenterZoom = TileZoom;
		H.MinLatE7 = H.MinLonE7 = INT32_MAX;
		H.MaxLatE7 = H.MaxLonE7 = INT32_MIN;
		for (const BakedRoad& Road : Bake.Roads)
		{
			Heights += Road.Heights.size();
			H.MinLatE7 = std::min(H.MinLatE7, Road.Start.Lat);
			H.MaxLatE7 = std::max(H.MaxLatE7, Road.Start.Lat);
			H.MinLonE7 = std::min(H.MinLonE7, Road.Start.Lon);
			H.MaxLonE7 = std::max(H.MaxLonE7, Road.Start.Lon);
		}
		H.CenterLatE7 = int32_t((int64_t(H.MinLatE7) + H.MaxLatE7) / 2);
		H.CenterLonE7 = int32_t((int64_t(H.MinLonE7) + H.MaxLonE7) / 2);
		for (const auto& [Tile, Raw] : SplitRoadsIntoTiles(Bake))
		{
			RawBytes += Raw.size();
			Tiles[ZxyToTileId(TileZoom, Tile.X, Tile.Y)] = Zstd(Raw.data(), Raw.size(), Level);
		}

		const std::string Meta = nlohmann::json{
			{ "format", "geodrive-roads" }, { "format_version", RoadBakeFormat }, { "tile_compression", "zstd" },
			{ "rules", Bake.Rules }, { "style", Bake.Style },
			{ "attribution", "(c) OpenStreetMap contributors, ODbL" },
		}.dump();
		std::vector<uint8_t> Archive;
		std::string Error;
		Must(WritePmTiles(Tiles, H, Meta, Archive, Error), Error);

		// read it back as a game would
		RoadBake Back;
		Must(ReadRoadArchive(Archive.data(), Archive.size(), Back, Error), Error);
		std::map<std::pair<int64_t, uint32_t>, const BakedRoad*> ByKey;
		for (const BakedRoad& Road : Back.Roads)
		{
			ByKey[{ Road.WayId, Road.Part }] = &Road;
		}
		size_t Bad = 0;
		for (const BakedRoad& Road : Bake.Roads)
		{
			const auto It = ByKey.find({ Road.WayId, Road.Part });
			Bad += It == ByKey.end() || It->second->Heights != Road.Heights || !(It->second->Start == Road.Start) ? 1 : 0;
		}
		Must(Bad == 0 && Back.Roads.size() == Bake.Roads.size() && Back.CanopyCm == Bake.CanopyCm, "roads didn't read back the same");
		WriteFile(OutPath, Archive);

		std::printf("roads %zu, heights %zu, tiles %zu, rules %u, style %u, canopy %d cm\n", Bake.Roads.size(), Heights, Tiles.size(),
			Bake.Rules, Bake.Style, Bake.CanopyCm);
		std::printf("baked %s -> raw tiles %s -> zstd %d -> archive %s, read back ok\n", Mb(Bin.size()).c_str(), Mb(RawBytes).c_str(), Level,
			Mb(Archive.size()).c_str());
		return 0;
	}

	int Check(const std::string& JsonPath, const std::string& ArchivePath)
	{
		const OsmSet Original = LoadOverpassJson(JsonPath, false);
		const std::vector<DecodedTile> Tiles = DecodeAll(LoadArchive(ArchivePath));
		const OsmSet Decoded = Join(Tiles);

		// With reduced precision a point may move up to half a step on each axis, and no further
		int64_t Quantum = 1;
		for (const DecodedTile& T : Tiles)
		{
			Quantum = std::max(Quantum, T.Quantum);
		}
		const int64_t Tolerance = Quantum / 2;
		int64_t WorstMove = 0;

		std::unordered_map<int64_t, const Way*> DecodedWays;
		for (const Way& W : Decoded.Ways)
		{
			DecodedWays[W.Id] = &W;
		}

		// Node identity must match exactly: two references share a node in the tiles exactly when they did in OSM
		std::unordered_map<int64_t, int64_t> DecodedToOriginal, OriginalToDecoded;
		size_t Problems = 0, Ways = 0, Refs = 0;
		auto Problem = [&](const std::string& What)
		{
			if (++Problems <= 20)
			{
				std::printf("  %s\n", What.c_str());
			}
		};

		for (const Way& O : Original.Ways)
		{
			std::vector<int64_t> Nodes;
			for (int64_t Id : O.Nodes)
			{
				if (Original.Nodes.count(Id))
				{
					Nodes.push_back(Id);
				}
			}
			if (Nodes.empty())
			{
				continue;
			}
			++Ways;
			auto It = DecodedWays.find(O.Id);
			if (It == DecodedWays.end())
			{
				Problem("way " + std::to_string(O.Id) + " missing");
				continue;
			}
			const Way& D = *It->second;
			if (D.Tags != O.Tags)
			{
				Problem("way " + std::to_string(O.Id) + " tags differ");
			}
			if (D.Nodes.size() != Nodes.size())
			{
				Problem("way " + std::to_string(O.Id) + " node count differs");
				continue;
			}
			for (size_t I = 0; I < Nodes.size(); ++I)
			{
				++Refs;
				const int64_t OId = Nodes[I];
				const int64_t DId = D.Nodes[I];
				const LatLon& OP = Original.Nodes.at(OId);
				const LatLon& DP = Decoded.Nodes.at(DId);
				const int64_t Move = std::max(std::llabs(int64_t(OP.Lat) - DP.Lat), std::llabs(int64_t(OP.Lon) - DP.Lon));
				WorstMove = std::max(WorstMove, Move);
				if (Move > Tolerance)
				{
					Problem("way " + std::to_string(O.Id) + " node " + std::to_string(I) + " moved");
				}
				const auto [A, bNewA] = DecodedToOriginal.emplace(DId, OId);
				const auto [B, bNewB] = OriginalToDecoded.emplace(OId, DId);
				if (A->second != OId || B->second != DId)
				{
					Problem("node " + std::to_string(OId) + " lost its sharing (junction or bridge link)");
				}
			}
		}
		if (Decoded.Ways.size() != Ways)
		{
			Problem("way count " + std::to_string(Decoded.Ways.size()) + " vs " + std::to_string(Ways));
		}

		std::printf("checked %zu ways, %zu node references: %zu differences (precision %lld x 1e-7 deg, worst point move %lld)\n",
			Ways, Refs, Problems, (long long)Quantum, (long long)WorstMove);
		return Problems ? 1 : 0;
	}

	// --- a whole region in one go ---

	std::string Lower(std::string Text)
	{
		std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return char(std::tolower(C)); });
		return Text;
	}

	/**
	 *  A country (or a state, a province: any of Geofabrik's extracts, by its id: "south-africa", "germany", "england",
	 *  "bayern", "california") made ready to play, into the game's data folder: the OpenStreetMap extract fetched from
	 *  Geofabrik (resumed if it stops), packed as the region's map, its world map index and its airports; its Copernicus
	 *  30 m heights; and a note of what it is (its country's code: the road rules go by it). Every step is skipped if
	 *  its result is already there, so a stopped run carries on.
	 */
	// Whether a GeoJSON polygon (its rings: the outer, then holes) holds a point
	bool PolygonHolds(const nlohmann::json& Rings, double Lat, double Lon)
	{
		bool bInside = false;
		for (size_t r = 0; r < Rings.size(); ++r)
		{
			const nlohmann::json& Ring = Rings[r];
			bool bIn = false;
			for (size_t i = 0, j = Ring.size() - 1; i < Ring.size(); j = i++)
			{
				const double XI = Ring[i][0], YI = Ring[i][1], XJ = Ring[j][0], YJ = Ring[j][1];
				if ((YI > Lat) != (YJ > Lat) && Lon < (XJ - XI) * (Lat - YI) / (YJ - YI) + XI)
				{
					bIn = !bIn;
				}
			}
			if (r == 0)
			{
				bInside = bIn;
			}
			else if (bIn)
			{
				return false;	// (in a hole)
			}
		}
		return bInside;
	}

	/** The smallest of Geofabrik's extracts whose outline holds a spot (its id), or "" */
	std::string ExtractAt(const std::filesystem::path& Downloads, double Lat, double Lon)
	{
		namespace fs = std::filesystem;
		const fs::path Index = Downloads / "geofabrik-index-outlines.json";
		std::error_code Ignore;
		if (!fs::exists(Index) || fs::file_time_type::clock::now() - fs::last_write_time(Index, Ignore) > std::chrono::hours(24 * 7))
		{
			const std::string Command = "curl.exe -s -f -L --retry 5 -o \"" + Index.u8string() + "\" \"https://download.geofabrik.de/index-v1.json\"";
			Must(std::system(Command.c_str()) == 0, "couldn't fetch Geofabrik's index (no internet?)");
		}
		const std::vector<uint8_t> Bytes = ReadFile(Index.u8string());
		const nlohmann::json List = nlohmann::json::parse(Bytes.begin(), Bytes.end());
		std::string Best;
		double Smallest = 1e30;
		for (const nlohmann::json& Feature : List["features"])
		{
			const nlohmann::json& G = Feature["geometry"];
			if (G.is_null())
			{
				continue;
			}
			const std::string Type = G.value("type", "");
			std::vector<const nlohmann::json*> Polygons;
			if (Type == "Polygon")
			{
				Polygons.push_back(&G["coordinates"]);
			}
			else if (Type == "MultiPolygon")
			{
				for (const nlohmann::json& P : G["coordinates"])
				{
					Polygons.push_back(&P);
				}
			}
			for (const nlohmann::json* P : Polygons)
			{
				if (!PolygonHolds(*P, Lat, Lon))
				{
					continue;
				}
				// (its size: the box round its outline)
				double MinX = 1e9, MinY = 1e9, MaxX = -1e9, MaxY = -1e9;
				for (const nlohmann::json& Point : (*P)[0])
				{
					MinX = std::min(MinX, double(Point[0]));
					MaxX = std::max(MaxX, double(Point[0]));
					MinY = std::min(MinY, double(Point[1]));
					MaxY = std::max(MaxY, double(Point[1]));
				}
				const double Area = (MaxX - MinX) * (MaxY - MinY);
				if (Area < Smallest)
				{
					Smallest = Area;
					Best = Feature["properties"]["id"];
				}
				break;
			}
		}
		return Best;
	}

	/** A name for a small area's files: its words, then where it is (hundredths of a degree, m for minus): cape_town_m3392_1842 */
	std::string AreaStem(const std::string& Name, double Lat, double Lon)
	{
		std::string Words;
		for (const char C : Lower(Name))
		{
			if (std::isalnum(static_cast<unsigned char>(C)))
			{
				Words += C;
			}
			else if (!Words.empty() && Words.back() != '_')
			{
				Words += '_';
			}
		}
		while (!Words.empty() && Words.back() == '_')
		{
			Words.pop_back();
		}
		auto Hundredths = [](double Degrees) { const long V = std::lround(Degrees * 100.0); return std::string(V < 0 ? "m" : "") + std::to_string(std::labs(V)); };
		return "local_" + (Words.empty() ? std::string("area") : Words.substr(0, 40)) + "_" + Hundredths(Lat) + "_" + Hundredths(Lon);
	}

	/**
	 *  A small area (a town: Km round a spot) straight from OpenStreetMap's Overpass service, as a .osm.pbf of what the game builds
	 *  from (roads, buildings, water, woods, leisure and protected areas, airports, place names): a few MB for a town, where
	 *  a whole country's extract is hundreds. Tries the main server, then another.
	 */
	bool FetchAreaPbf(double Lat, double Lon, double Km, const std::filesystem::path& Pbf)
	{
		namespace fs = std::filesystem;
		const double DLat = Km / 111.32;
		const double DLon = Km / (111.32 * std::max(0.1, std::cos(Lat * 3.14159265358979 / 180.0)));
		char Box[160];
		std::snprintf(Box, sizeof(Box), "(%.6f,%.6f,%.6f,%.6f)", Lat - DLat, Lon - DLon, Lat + DLat, Lon + DLon);
		const std::string B = Box;
		const std::string Query =
			"[out:json][timeout:300][maxsize:2147483648];(" +
			std::string("way[\"highway\"~\"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|service|road|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link|track|raceway)$\"]") + B + ";" +
			"way[\"building\"]" + B + ";way[\"natural\"~\"^(water|wood|beach|sand)$\"]" + B + ";way[\"landuse\"]" + B + ";" +
			"way[\"leisure\"]" + B + ";way[\"boundary\"~\"protected_area|national_park\"]" + B + ";way[\"aeroway\"]" + B + ";" +
			"node[\"place\"]" + B + ";node[\"aeroway\"=\"aerodrome\"]" + B + ";);(._;>;);out body;";
		const fs::path QueryFile = fs::u8path(Pbf.u8string() + ".query");
		const fs::path JsonFile = fs::u8path(Pbf.u8string() + ".json");
		WriteFile(QueryFile.u8string(), std::vector<uint8_t>(Query.begin(), Query.end()));
		// (public servers: each is sometimes too busy; one that answers in a minute or two wins)
		const char* Servers[] = { "https://overpass.private.coffee/api/interpreter", "https://overpass-api.de/api/interpreter", "https://overpass.kumi.systems/api/interpreter" };
		bool bGot = false;
		// (they answer one try in two or three with an error, some busy, some not: round and round them, a few times)
		for (int Round = 0; Round < 4 && !bGot; ++Round)
		{
			for (const char* Server : Servers)
			{
				const std::string Command = std::string("curl.exe -s -f -L --max-time 400 -A \"Open-Planet/0.2 (https://github.com/inlighten420/open-planet)\" -o \"") + JsonFile.u8string() +
					"\" --data-urlencode \"data@" + QueryFile.u8string() + "\" \"" + Server + "\"";
				// (curl runs, and meanwhile how much has come is said every two seconds: the server sends nothing until it has made
				// the whole answer, then it flows, and there is no total to show a percent of)
				std::atomic<bool> bFinished{ false };
				std::thread Watcher([&]()
				{
					uint64_t Last = UINT64_MAX;
					for (int Tick = 0; !bFinished; ++Tick)
					{
						std::error_code SizeError;
						const uint64_t Bytes = fs::exists(JsonFile) ? uint64_t(fs::file_size(JsonFile, SizeError)) : 0;
						if (!SizeError && (Bytes != Last || Tick % 5 == 0))
						{
							Last = Bytes;
							std::printf("downloaded %.1f MB\n", Bytes / 1048576.0);
							std::fflush(stdout);
						}
						for (int Wait = 0; Wait < 20 && !bFinished; ++Wait)
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
						}
					}
				});
				const int CurlResult = std::system(Command.c_str());
				bFinished = true;
				Watcher.join();
				if (CurlResult == 0 && fs::exists(JsonFile) && fs::file_size(JsonFile) > 1000)
				{
					bGot = true;
					break;
				}
				std::printf("try %d: %s didn't answer\n", Round + 1, Server);
				std::fflush(stdout);
			}
			if (!bGot)
			{
				std::this_thread::sleep_for(std::chrono::seconds(3));
			}
		}
		std::error_code Ignore;
		fs::remove(QueryFile, Ignore);
		if (!bGot)
		{
			fs::remove(JsonFile, Ignore);
			return false;
		}
		std::printf("the area's map is here: %s; making it a map file\n", Mb(fs::file_size(JsonFile)).c_str());
		std::fflush(stdout);
		WriteFile(Pbf.u8string(), WriteOsmPbf(LoadOverpassJson(JsonFile.u8string(), true)));
		fs::remove(JsonFile, Ignore);
		return true;
	}

	int Region(std::string Wanted, const std::string& DataDir, double MemoryGB, bool bHeights, double AtLat, double AtLon, bool bKeepDownload, double LocalKm = 0.0, const std::string& LocalName = "")
	{
		namespace fs = std::filesystem;
		const auto Start = std::chrono::steady_clock::now();
		auto Say = [&](const std::string& Text)
		{
			std::printf("[%5.0f s] %s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(), Text.c_str());
			std::fflush(stdout);
		};
		const fs::path Data = fs::u8path(DataDir);
		const fs::path Downloads = Data / "downloads", Regions = Data / "regions", Heights = Data / "heights";
		fs::create_directories(Downloads);
		fs::create_directories(Regions);
		fs::create_directories(Heights);
		if (Wanted.empty())
		{
			// (a spot: the smallest extract that covers it, a quicker download than its whole country)
			Wanted = ExtractAt(Downloads, AtLat, AtLon);
			if (Wanted.empty())
			{
				std::printf("no extract covers %.5f, %.5f (out at sea?)\n", AtLat, AtLon);
				return 1;
			}
			Say("the extract that covers " + std::to_string(AtLat) + ", " + std::to_string(AtLon) + ": " + Wanted);
		}
		auto Fetch = [](const std::string& Url, const fs::path& To, bool bResume)
		{
			const std::string Command = std::string("curl.exe -f -L --retry 5 --retry-delay 5 ") + (bResume ? "-C - " : "") + "-# -o \"" + To.u8string() + "\" \"" + Url + "\"";
			return std::system(Command.c_str()) == 0;
		};

		// 1. which extract: Geofabrik's index (every extract's id, name, parent, country code and download address)
		const fs::path Index = Downloads / "geofabrik-index.json";
		std::error_code Ignore;
		const bool bOld = !fs::exists(Index) || fs::file_time_type::clock::now() - fs::last_write_time(Index, Ignore) > std::chrono::hours(24 * 7);
		if (bOld)
		{
			Say("fetching Geofabrik's list of extracts");
			Must(Fetch("https://download.geofabrik.de/index-v1-nogeom.json", Index, false), "couldn't fetch Geofabrik's index (no internet?)");
		}
		const std::vector<uint8_t> IndexBytes = ReadFile(Index.u8string());
		const nlohmann::json List = nlohmann::json::parse(IndexBytes.begin(), IndexBytes.end());
		const nlohmann::json* Found = nullptr;
		std::vector<std::string> Close;
		const std::string Key = Lower(Wanted);
		for (const nlohmann::json& Feature : List["features"])
		{
			const nlohmann::json& P = Feature["properties"];
			const std::string Id = P.value("id", ""), Name = P.value("name", "");
			if (Lower(Id) == Key || Lower(Name) == Key)
			{
				Found = &Feature;
				break;
			}
			if (Lower(Id).find(Key) != std::string::npos || Lower(Name).find(Key) != std::string::npos)
			{
				Close.push_back(Id + " (" + Name + ")");
			}
		}
		if (!Found)
		{
			std::printf("no extract called \"%s\"%s\n", Wanted.c_str(), Close.empty() ? " (the ids are Geofabrik's: see download.geofabrik.de)" : "; did you mean:");
			for (size_t i = 0; i < Close.size() && i < 20; ++i)
			{
				std::printf("  %s\n", Close[i].c_str());
			}
			return 1;
		}
		const nlohmann::json& P = (*Found)["properties"];
		// (a small area: named for where it is, with its country's rules from the extract that covers it)
		const std::string Id = LocalKm > 0.0 ? "local/" + AreaStem(LocalName, AtLat, AtLon).substr(6) : std::string(P["id"]);
		const std::string Name = LocalKm > 0.0 ? LocalName : P.value("name", Id);
		std::string Country, Subdivision;
		if (P.contains("iso3166-2") && !P["iso3166-2"].empty())
		{
			Subdivision = P["iso3166-2"][0];	// (a state or province: its own limits, US-CA)
		}
		if (P.contains("iso3166-1:alpha2") && !P["iso3166-1:alpha2"].empty())
		{
			Country = P["iso3166-1:alpha2"][0];
		}
		else if (P.contains("iso3166-2") && !P["iso3166-2"].empty())
		{
			Country = std::string(P["iso3166-2"][0]).substr(0, 2);	// (a state's: its country's code first)
		}
		// (a part of a state, of a country, without codes of its own: its parents', up the tree)
		for (std::string Parent = P.value("parent", ""); (Country.empty() || Subdivision.empty()) && !Parent.empty();)
		{
			std::string Next;
			for (const nlohmann::json& Feature : List["features"])
			{
				const nlohmann::json& Q = Feature["properties"];
				if (Q.value("id", "") != Parent)
				{
					continue;
				}
				if (Subdivision.empty() && Q.contains("iso3166-2") && !Q["iso3166-2"].empty())
				{
					Subdivision = Q["iso3166-2"][0];
					Country = Country.empty() ? Subdivision.substr(0, 2) : Country;
				}
				if (Country.empty() && Q.contains("iso3166-1:alpha2") && !Q["iso3166-1:alpha2"].empty())
				{
					Country = Q["iso3166-1:alpha2"][0];
				}
				Next = Q.value("parent", "");
				break;
			}
			Parent = Next;
		}
		std::string Stem = Id;
		std::replace(Stem.begin(), Stem.end(), '-', '_');
		std::replace(Stem.begin(), Stem.end(), '/', '_');	// (us/california: us_california)
		Say(Name + " (" + Id + (Country.empty() ? "" : ", country " + Country) + "): into " + Data.u8string());

		// 2. the OpenStreetMap extract
		const fs::path Pbf = Downloads / (Stem + "-latest.osm.pbf");
		const fs::path Done = Downloads / (Stem + "-latest.osm.pbf.done");
		const fs::path Archive = Regions / (Stem + ".pmtiles");
		const fs::path WorldMap = Regions / (Stem + ".world.json");
		const fs::path Fields = Regions / (Stem + ".airports.json");
		const bool bPacked = fs::exists(Archive) && fs::exists(WorldMap) && fs::exists(Fields);
		if (!bPacked && !fs::exists(Done))
		{
			if (LocalKm > 0.0)
			{
				Say("downloading just the area round it (" + std::to_string(int(std::lround(LocalKm))) + " km, from OpenStreetMap)");
				Must(FetchAreaPbf(AtLat, AtLon, LocalKm, Pbf), "the download failed (is the internet on? OpenStreetMap's servers are sometimes busy): try again in a while");
			}
			else
			{
				Say("downloading " + std::string(P["urls"]["pbf"]));
				Must(Fetch(P["urls"]["pbf"], Pbf, true), "the download failed: run the same again to carry on");
			}
			WriteFile(Done.u8string(), { 'o', 'k' });
		}
		if (fs::exists(Pbf))
		{
			Say("the extract: " + Mb(fs::file_size(Pbf)));
		}

		// 3. the region: its map, its world map, its airports (all made already: the extract isn't needed)
		if (!fs::exists(Archive))
		{
			Say("packing the map (a few minutes a country)");
			const fs::path Part = Regions / (Stem + ".pmtiles.part");
			Must(PackPbfBands(Pbf.u8string(), Part.u8string(), 19, 1, 0, PackArea(), MemoryGB) == 0, "packing failed");
			fs::rename(Part, Archive);
		}
		if (!fs::exists(WorldMap))
		{
			Say("the world map's places and main roads");
			Must(World({ Pbf.u8string() }, WorldMap.u8string()) == 0, "the world map failed");
			if (LocalKm > 0.0)
			{
				// (the area's place names aren't in what Overpass gave: its own name, where it was asked for, so the world map and the
				// go-to lists have it)
				try
				{
					const std::vector<uint8_t> Bytes = ReadFile(WorldMap.u8string());
					nlohmann::json WorldJson = nlohmann::json::parse(Bytes.begin(), Bytes.end());
					if (WorldJson["places"].empty())
					{
						WorldJson["places"].push_back({ Name, "town", std::round(AtLat * 1e5) / 1e5, std::round(AtLon * 1e5) / 1e5, 0 });
						const std::string Text = WorldJson.dump();
						WriteFile(WorldMap.u8string(), std::vector<uint8_t>(Text.begin(), Text.end()));
					}
				}
				catch (const std::exception& E) { std::printf("warning: couldn't add the area's name to its map (%s)\n", E.what()); }
			}
		}
		if (!fs::exists(Fields))
		{
			Say("the airports");
			Must(Airports({ Pbf.u8string() }, Fields.u8string()) == 0, "the airports failed");
		}
		const fs::path GamePlaces = Regions / (Stem + ".games.json");
		if (!fs::exists(GamePlaces) && fs::exists(Pbf))
		{
			Say("the places for games");
			Must(Games({ Pbf.u8string() }, GamePlaces.u8string()) == 0, "the places for games failed");
		}
		// (what it is: the game's road rules go by its country)
		nlohmann::json About;
		About["id"] = Id;
		About["name"] = Name;
		About["country"] = Country;
		About["subdivision"] = Subdivision;
		About["source"] = LocalKm > 0.0 ? std::string("OpenStreetMap (Overpass), ") + std::to_string(int(std::lround(LocalKm))) + " km round " + Name : std::string(P["urls"]["pbf"]);
		About["packed"] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		const std::string AboutText = About.dump(1, '\t');
		WriteFile((Regions / (Stem + ".region.json")).u8string(), std::vector<uint8_t>(AboutText.begin(), AboutText.end()));

		// 4. its heights (Copernicus 30 m, from AWS Open Data: about 2 GB for South Africa)
		const fs::path Base = Heights / ("cop_" + Stem + ".pmtiles");
		if (bHeights && !fs::exists(Base))
		{
			Say("the heights (Copernicus 30 m: a while for a big country)");
			// (the map is made and works without them: a failure here is said, not fatal, and the same again carries on)
			int HeightsResult = 1;
			try { HeightsResult = PackCopernicusRegion(Base.u8string(), Archive.u8string(), 8); }
			catch (const std::exception& E) { std::printf("warning: the heights failed (%s)\n", E.what()); }
			if (HeightsResult == 0)
			{
				fs::remove_all(fs::u8path(Base.u8string() + ".download"), Ignore);	// (the 1-degree tiles it was made from)
			}
			else
			{
				std::printf("warning: no heights for now (the map works without them): run the same again to carry on\n");
			}
		}
		// (and the rougher heights the game blends them with and reaches far with (SRTM, AWS Terrain Tiles), fetched now
		// rather than as you first drive there)
		if (bHeights)
		{
			Say("the far terrain (SRTM tiles)");
			int TerrainResult = 1;
			try { TerrainResult = PrefetchTerrain(Archive.u8string(), (Data / "terrain").u8string(), 16); }
			catch (const std::exception& E) { std::printf("warning: the far terrain failed (%s)\n", E.what()); }
			if (TerrainResult != 0)
			{
				std::printf("warning: not all the far terrain for now (it's fetched as you drive there)\n");
			}
		}
		// (the extract, packed, isn't needed again: a few times the map's size; --keep-download keeps it, to pack again)
		if (!bKeepDownload && fs::exists(Pbf))
		{
			Say("the extract's packed: its " + Mb(fs::file_size(Pbf)) + " download let go (--keep-download keeps it)");
			fs::remove(Pbf, Ignore);
			fs::remove(Done, Ignore);
		}
		std::printf("READY %s\n", Stem.c_str());	// (for the game: what's ready, by its file's name)
		Say(Name + " is ready: " + Mb(fs::file_size(Archive)) + " of map" + (bHeights && fs::exists(Base) ? ", " + Mb(fs::file_size(Base)) + " of heights" : "")
			+ ". Start the game and go there (Go somewhere: type a place, or the world map)");
		return 0;
	}

	int Bench(const std::string& JsonPath, const std::string& ArchivePath)
	{
		std::printf("| Version | Size | Notes |\n|---|---|---|\n");

		const std::vector<uint8_t> JsonRaw = ReadFile(JsonPath);
		nlohmann::json Root;
		const double ParseS = BestSeconds(3, [&] { Root = nlohmann::json::parse(JsonRaw.begin(), JsonRaw.end()); });
		std::printf("| JSON as cached | %s | parse %.2fs (nlohmann; the game's parser is timed separately) |\n", Mb(JsonRaw.size()).c_str(), ParseS);

		const std::string Minified = Root.dump();
		std::vector<uint8_t> Z3, Z19;
		const double Z3S = Seconds([&] { Z3 = Zstd(Minified.data(), Minified.size(), 3); });
		const double Z19S = Seconds([&] { Z19 = Zstd(Minified.data(), Minified.size(), 19); });
		const double UnzipS = BestSeconds(3, [&] { Unzstd(Z19.data(), Z19.size()); });
		std::printf("| JSON minified | %s | |\n", Mb(Minified.size()).c_str());
		std::printf("| JSON minified + zstd 3 | %s | zip %.2fs |\n", Mb(Z3.size()).c_str(), Z3S);
		std::printf("| JSON minified + zstd 19 | %s | zip %.1fs, unzip %.3fs, still needs the full JSON parse; one file, no tiles |\n",
			Mb(Z19.size()).c_str(), Z19S, UnzipS);

		// Same baseline with only the tags the game uses, to separate "fewer tags" from "better packing"
		for (nlohmann::json& E : Root["elements"])
		{
			if (E.contains("tags"))
			{
				nlohmann::json Kept = nlohmann::json::object();
				for (auto It = E["tags"].begin(); It != E["tags"].end(); ++It)
				{
					if (KeptKeyIndex(It.key()) >= 0)
					{
						Kept[It.key()] = It.value();
					}
				}
				E["tags"] = Kept;
			}
		}
		const std::string Filtered = Root.dump();
		const std::vector<uint8_t> FZ19 = Zstd(Filtered.data(), Filtered.size(), 19);
		std::printf("| JSON, game tags only, minified + zstd 19 | %s | |\n", Mb(FZ19.size()).c_str());

		// OSM's own compact format, from exactly the same data
		std::printf("| OSM PBF, all tags | %s | zlib 9, the standard OSM download format |\n", Mb(WriteOsmPbf(LoadOverpassJson(JsonPath, true)).size()).c_str());
		std::printf("| OSM PBF, game tags only | %s | |\n", Mb(WriteOsmPbf(LoadOverpassJson(JsonPath, false)).size()).c_str());

		// Ours
		const LoadedArchive A = LoadArchive(ArchivePath);
		size_t Largest = 0;
		const PmEntry* LargestEntry = nullptr;
		for (const PmEntry& E : A.Entries)
		{
			if (E.Length > Largest)
			{
				Largest = E.Length;
				LargestEntry = &E;
			}
		}
		const double DecodeAllS = BestSeconds(5, [&] { DecodeAll(A); });
		const double AssembleS = BestSeconds(3, [&] { Join(DecodeAll(A)); }) - DecodeAllS;
		const double DecodeLargestS = BestSeconds(50, [&] { DecodeOne(A, *LargestEntry); });
		const size_t Average = A.Header.DataLength / std::max<size_t>(1, A.Entries.size());
		std::printf("| **Ours (PMTiles, zstd per tile)** | **%s** | %zu tiles, average %.1f KB, largest %.1f KB; decode all %.3fs (+ join %.3fs), largest tile %.2f ms |\n",
			Mb(A.File.size()).c_str(), A.Entries.size(), Average / 1e3, Largest / 1e3, DecodeAllS, AssembleS, DecodeLargestS * 1e3);
		return 0;
	}
}

namespace
{
	/** The tile a PMTiles tile id stands for (the spec's Hilbert curve, backwards) */
	void TileIdToZxy(uint64_t Id, int& Z, uint32_t& X, uint32_t& Y)
	{
		uint64_t Acc = 0;
		Z = 0;
		while (Z < 31 && Acc + (uint64_t(1) << (2 * Z)) <= Id)
		{
			Acc += uint64_t(1) << (2 * Z);
			++Z;
		}
		const uint64_t N = uint64_t(1) << Z;
		uint64_t T = Id - Acc, Tx = 0, Ty = 0;
		for (uint64_t S = 1; S < N; S *= 2)
		{
			const uint64_t Rx = 1 & (T / 2), Ry = 1 & (T ^ Rx);
			if (Ry == 0)
			{
				if (Rx == 1)
				{
					Tx = S - 1 - Tx;
					Ty = S - 1 - Ty;
				}
				std::swap(Tx, Ty);
			}
			Tx += S * Rx;
			Ty += S * Ry;
			T /= 4;
		}
		X = uint32_t(Tx);
		Y = uint32_t(Ty);
	}

	/**
	 *  An area cut out of an archive this PC already has (a country's map, or its Copernicus heights), into an archive of
	 *  its own: only the tiles over the area are copied, nothing is downloaded or packed again. Only the archive's
	 *  directories are read whole (a country's heights run to gigabytes); then the tiles over the area, one by one.
	 *  Map archives are web tiles at z14; height archives (bHeights) are 120 arc-second squares (Copernicus.cpp)
	 */
	int CropArchive(const std::string& InPath, const std::string& OutPath, double Lat, double Lon, double Km, bool bHeights)
	{
		using namespace geotile;
		std::ifstream In(std::filesystem::u8path(InPath), std::ios::binary);
		Must(bool(In), "can't open " + InPath);
		In.seekg(0, std::ios::end);
		const uint64_t FileSize = uint64_t(In.tellg());
		std::vector<uint8_t> Head(127);
		In.seekg(0);
		In.read(reinterpret_cast<char*>(Head.data()), Head.size());
		Must(bool(In) && std::string(reinterpret_cast<const char*>(Head.data()), 7) == "PMTiles", InPath + " isn't a PMTiles archive");
		uint64_t DataOffset = 0;
		for (int i = 7; i >= 0; --i)
		{
			DataOffset = (DataOffset << 8) | Head[56 + i];
		}
		Must(DataOffset >= 127 && DataOffset < FileSize, "broken archive: " + InPath);
		// (everything before the tile data: the header, its directories and metadata)
		std::vector<uint8_t> Front(static_cast<size_t>(DataOffset));
		In.seekg(0);
		In.read(reinterpret_cast<char*>(Front.data()), Front.size());
		PmHeader Header;
		std::vector<PmEntry> Entries;
		std::string Error;
		Must(ReadPmTiles(Front.data(), size_t(FileSize), Header, Entries, Error), InPath + ": " + Error);
		Must(Header.RootOffset + Header.RootLength <= DataOffset && Header.MetadataOffset + Header.MetadataLength <= DataOffset
			&& (Header.LeafLength == 0 || Header.LeafOffset + Header.LeafLength <= DataOffset), "directories after the tiles aren't supported: " + InPath);
		const std::string Metadata(reinterpret_cast<const char*>(Front.data() + Header.MetadataOffset), size_t(Header.MetadataLength));

		const double HalfLat = Km * 0.5 / 111.32, HalfLon = HalfLat / std::max(0.05, std::cos(Lat * 3.14159265358979 / 180.0));
		const double South = Lat - HalfLat, North = Lat + HalfLat, West = Lon - HalfLon, East = Lon + HalfLon;
		uint32_t X0 = 0, X1 = 0, Y0 = 0, Y1 = 0;
		int Zoom = 14;
		if (bHeights)
		{
			X0 = uint32_t(std::floor((West + 180.0) * 30.0)); X1 = uint32_t(std::floor((East + 180.0) * 30.0));
			Y0 = uint32_t(std::floor((South + 90.0) * 30.0)); Y1 = uint32_t(std::floor((North + 90.0) * 30.0));
		}
		else
		{
			LatLon SouthWest, NorthEast;
			SouthWest.Lat = int32_t(std::lround(South * 1e7)); SouthWest.Lon = int32_t(std::lround(West * 1e7));	// (LatLon: 1e-7 degrees)
			NorthEast.Lat = int32_t(std::lround(North * 1e7)); NorthEast.Lon = int32_t(std::lround(East * 1e7));
			const TileXY A = TileOf(SouthWest, TileZoom), B = TileOf(NorthEast, TileZoom);
			X0 = std::min(A.X, B.X); X1 = std::max(A.X, B.X);
			Y0 = std::min(A.Y, B.Y); Y1 = std::max(A.Y, B.Y);
			Zoom = TileZoom;
		}

		std::map<uint64_t, std::vector<uint8_t>> Tiles;
		uint64_t Bytes = 0;
		for (const PmEntry& E : Entries)
		{
			for (uint32_t Run = 0; Run < std::max<uint32_t>(1, E.RunLength); ++Run)
			{
				int Z = 0;
				uint32_t X = 0, Y = 0;
				TileIdToZxy(E.TileId + Run, Z, X, Y);
				if (Z != Zoom || X < X0 || X > X1 || Y < Y0 || Y > Y1)
				{
					continue;
				}
				std::vector<uint8_t> Tile(E.Length);
				In.seekg(std::streamoff(DataOffset + E.Offset));
				In.read(reinterpret_cast<char*>(Tile.data()), Tile.size());
				Must(bool(In), "can't read a tile of " + InPath);
				Bytes += Tile.size();
				Tiles[E.TileId + Run] = std::move(Tile);
			}
		}
		if (Tiles.empty())
		{
			int MinZ = 99, MaxZ = -1;
			uint32_t MinX = UINT32_MAX, MaxX = 0, MinY = UINT32_MAX, MaxY = 0;
			for (const PmEntry& E : Entries)
			{
				int Z = 0;
				uint32_t X = 0, Y = 0;
				TileIdToZxy(E.TileId, Z, X, Y);
				MinZ = std::min(MinZ, Z); MaxZ = std::max(MaxZ, Z);
				if (Z == Zoom) { MinX = std::min(MinX, X); MaxX = std::max(MaxX, X); MinY = std::min(MinY, Y); MaxY = std::max(MaxY, Y); }
			}
			char Why[400];
			std::snprintf(Why, sizeof(Why), "nothing of %s lies in that area (wanted z%d x %u-%u y %u-%u; it has z%d-%d, at z%d x %u-%u y %u-%u)", InPath.c_str(),
				Zoom, X0, X1, Y0, Y1, MinZ, MaxZ, Zoom, MinX, MaxX, MinY, MaxY);
			Must(false, Why);
		}
		Header.MinLatE7 = int32_t(South * 1e7); Header.MaxLatE7 = int32_t(North * 1e7);
		Header.MinLonE7 = int32_t(West * 1e7); Header.MaxLonE7 = int32_t(East * 1e7);
		Header.CenterLatE7 = int32_t(Lat * 1e7); Header.CenterLonE7 = int32_t(Lon * 1e7);
		std::vector<uint8_t> Out;
		Must(WritePmTiles(Tiles, Header, Metadata, Out, Error), Error);
		std::ofstream File(std::filesystem::u8path(OutPath), std::ios::binary);
		File.write(reinterpret_cast<const char*>(Out.data()), Out.size());
		Must(bool(File), "can't write " + OutPath);
		std::printf("%s: %zu of %zu tiles (%.1f MB) cut out of %s, %.0f km round %.5f, %.5f\n", OutPath.c_str(), Tiles.size(), Entries.size(),
			Out.size() / 1048576.0, InPath.c_str(), Km, Lat, Lon);
		return 0;
	}
}

int main(int Argc, char** Argv)
{
	try
	{
		const std::string Cmd = Argc > 1 ? Argv[1] : "";
		if (Cmd == "selftest")
		{
			return SelfTest();
		}
		if (Cmd == "pack" && Argc >= 4)
		{
			return Pack(Argv[2], Argv[3], Argc >= 5 ? std::stoi(Argv[4]) : 19, Argc >= 6 ? std::stoll(Argv[5]) : 1);
		}
		if (Cmd == "packpbf" && Argc >= 4)
		{
			// band by band (bands: 0 = enough to fit in memory); packmany with one file reads it in one go.
			// --around=lat,lon,km packs only that area; --memory=GB picks the bands to stay within that much RAM
			std::vector<std::string> Args;
			PackArea Area;
			double MemoryGB = 0.0;
			for (int i = 2; i < Argc; ++i)
			{
				const std::string A = Argv[i];
				if (A.rfind("--around=", 0) == 0)
				{
					double Lat = 0.0, Lon = 0.0, Km = 0.0;
					Must(std::sscanf(A.c_str() + 9, "%lf,%lf,%lf", &Lat, &Lon, &Km) == 3 && Km > 0.0, "--around=lat,lon,km");
					Area = AreaAround(Lat, Lon, Km);
				}
				else if (A.rfind("--memory=", 0) == 0)
				{
					MemoryGB = std::stod(A.substr(9));
				}
				else
				{
					Args.push_back(A);
				}
			}
			Must(Args.size() >= 2, "geotile packpbf <in.osm.pbf> <out.pmtiles> [level] [precision] [bands] [--around=lat,lon,km] [--memory=GB]");
			return PackPbfBands(Args[0], Args[1], Args.size() >= 3 ? std::stoi(Args[2]) : 19, Args.size() >= 4 ? std::stoll(Args[3]) : 1,
				Args.size() >= 5 ? std::stoi(Args[4]) : 0, Area, MemoryGB);
		}
		if (Cmd == "height" && Argc == 5)
		{
			// one height out of a heights archive, as the game reads it: geotile height <heights.pmtiles> lat lon
			const std::vector<uint8_t> File = ReadFile(Argv[2]);
			PmHeader Header;
			std::vector<PmEntry> Entries;
			std::string Error;
			Must(ReadPmTiles(File.data(), File.size(), Header, Entries, Error), Error);
			double E = 0.0, N = 0.0;
			uint64_t Id = 0;
			if (Header.MinZoom == 14)
			{
				// a lat/lon grid (Copernicus): arc-seconds from -180 / -90, 120 arc-second squares
				E = (std::stod(Argv[4]) + 180.0) * 3600.0;
				N = (std::stod(Argv[3]) + 90.0) * 3600.0;
				Id = ZxyToTileId(14, uint32_t(std::floor(E / 120.0)), uint32_t(std::floor(N / 120.0)));
			}
			else
			{
				ToBritishGrid(std::stod(Argv[3]), std::stod(Argv[4]), E, N);
				Id = HeightTileId(int(std::floor(E / 1000.0)), int(std::floor(N / 1000.0)));
			}
			const auto It = std::find_if(Entries.begin(), Entries.end(), [Id](const PmEntry& Entry) { return Entry.TileId == Id; });
			Must(It != Entries.end(), "no heights there");
			std::vector<uint8_t> Raw;
			Must(UnzipArchiveTile(File.data(), Header, *It, Raw, Error), Error);
			HeightTile Tile;
			Must(DecodeHeightTile(Raw.data(), Raw.size(), Tile), "broken height tile");
			const int X = int((E - Tile.SouthWestE) / Tile.CellMetres), Y = Tile.Size - 1 - int((N - Tile.SouthWestN) / Tile.CellMetres);
			std::printf("grid E %.1f N %.1f: %.2f m\n", E, N, Tile.Heights[size_t(Y) * Tile.Size + X]);
			return 0;
		}
		if (Cmd == "sections" && Argc >= 6)
		{
			// real road cross-sections: geotile sections <region.pmtiles> <heights-1m.pmtiles> --around=lat,lon,km <out.csv>
			double Lat = 0.0, Lon = 0.0, Km = 0.0;
			Must(std::sscanf(Argv[4], "--around=%lf,%lf,%lf", &Lat, &Lon, &Km) == 3 && Km > 0.0, "--around=lat,lon,km");
			return RoadSections(Argv[2], Argv[3], Lat, Lon, Km, Argv[5]);
		}
		if (Cmd == "copernicus" && Argc >= 4)
		{
			// Copernicus 30 m base heights for an area: geotile copernicus <out.pmtiles> --around=lat,lon,km
			// (or --region=<region.pmtiles>: every 1-degree tile with the region's map data in it)
			const std::string A = Argv[3];
			if (A.rfind("--region=", 0) == 0)
			{
				return PackCopernicusRegion(Argv[2], A.substr(9), 8);
			}
			double Lat = 0.0, Lon = 0.0, Km = 0.0;
			Must(std::sscanf(Argv[3], "--around=%lf,%lf,%lf", &Lat, &Lon, &Km) == 3 && Km > 0.0, "--around=lat,lon,km or --region=<region.pmtiles>");
			return PackCopernicus(Argv[2], Lat, Lon, Km, 8);
		}
		if (Cmd == "godot" && Argc >= 6)
		{
			// an area's roads at their real heights for the Godot client: geotile godot <region.pmtiles> <Cache/terrain> --box=s,w,n,e <out.glb> [--uk]
			double S = 0.0, W = 0.0, N = 0.0, E = 0.0;
			Must(std::sscanf(Argv[4], "--box=%lf,%lf,%lf,%lf", &S, &W, &N, &E) == 4 && N > S && E > W, "--box=south,west,north,east");
			return ExportGodot(Argv[2], Argv[3], S, W, N, E, Argv[5], Argc >= 7 && std::string(Argv[6]) == "--uk");
		}
		if (Cmd == "crop" && Argc >= 5)
		{
			// an area cut out of an archive this PC has: geotile crop <in.pmtiles> <out.pmtiles> --around=lat,lon,km [--heights]
			double Lat = 0.0, Lon = 0.0, Km = 0.0;
			Must(std::sscanf(Argv[4], "--around=%lf,%lf,%lf", &Lat, &Lon, &Km) == 3 && Km > 0.0, "--around=lat,lon,km");
			const bool bHeights = (Argc >= 6 && std::string(Argv[5]) == "--heights") || std::filesystem::u8path(Argv[2]).filename().u8string().rfind("cop_", 0) == 0;
			return CropArchive(Argv[2], Argv[3], Lat, Lon, Km, bHeights);
		}
		if (Cmd == "terrain" && Argc >= 4)
		{
			// the SRTM heights for a whole region ahead of time: geotile terrain <region.pmtiles> <Cache/terrain> [--jobs=16]
			const int Jobs = Argc >= 5 && std::string(Argv[4]).rfind("--jobs=", 0) == 0 ? std::stoi(std::string(Argv[4]).substr(7)) : 16;
			return PrefetchTerrain(Argv[2], Argv[3], Jobs);
		}
		if (Cmd == "dgm" && Argc >= 4)
		{
			// geotile dgm <out.pmtiles> <folder of 1 km GeoTIFFs and .tfw, UTM 32> [--cell=2] [--credit=...]
			double Cell = 2.0;
			std::string Credit = "Hoehendaten: (c) GeoBasis-DE / LVermGeoRP, dl-de/by-2-0, www.lvermgeo.rlp.de";
			for (int i = 4; i < Argc; ++i)
			{
				const std::string A = Argv[i];
				if (A.rfind("--cell=", 0) == 0) { Cell = std::stod(A.substr(7)); }
				else if (A.rfind("--credit=", 0) == 0) { Credit = A.substr(9); }
			}
			return PackDgm(Argv[2], Argv[3], Cell, Credit);
		}
		if (Cmd == "lidar" && Argc >= 4)
		{
			// England's LiDAR heights for an area: geotile lidar <out.pmtiles> --around=lat,lon,km [--cell=4] [--jobs=8]
			double Lat = 0.0, Lon = 0.0, Km = 0.0, Cell = 4.0;
			int Jobs = 8;
			for (int i = 3; i < Argc; ++i)
			{
				const std::string A = Argv[i];
				if (A.rfind("--around=", 0) == 0) { Must(std::sscanf(A.c_str() + 9, "%lf,%lf,%lf", &Lat, &Lon, &Km) == 3 && Km > 0.0, "--around=lat,lon,km"); }
				else if (A.rfind("--cell=", 0) == 0) { Cell = std::stod(A.substr(7)); }
				else if (A.rfind("--jobs=", 0) == 0) { Jobs = std::stoi(A.substr(7)); }
			}
			Must(Km > 0.0, "geotile lidar <out.pmtiles> --around=lat,lon,km [--cell=4] [--jobs=8]");
			return PackLidar(Argv[2], Lat, Lon, Km, Cell, Jobs);
		}
		if (Cmd == "region" && Argc >= 3)
		{
			// geotile region <geofabrik id> [data folder] [--memory=GB] [--no-heights]: a country ready to play
			// (or --at=lat,lon for the name: the smallest extract that covers that spot)
			std::string DataDir, Name = Argv[2];
			double MemoryGB = 0.0, AtLat = 0.0, AtLon = 0.0, LocalKm = 0.0;
			std::string LocalName;
			bool bHeights = true, bKeep = false;
			if (Name.rfind("--at=", 0) == 0)
			{
				Must(std::sscanf(Name.c_str() + 5, "%lf,%lf", &AtLat, &AtLon) == 2, "--at=lat,lon");
				Name.clear();
			}
			for (int i = 3; i < Argc; ++i)
			{
				const std::string A = Argv[i];
				if (A.rfind("--memory=", 0) == 0) { MemoryGB = std::stod(A.substr(9)); }
				else if (A == "--no-heights") { bHeights = false; }
				else if (A == "--keep-download") { bKeep = true; }
				else if (A.rfind("--km=", 0) == 0) { LocalKm = std::stod(A.substr(5)); }
				else if (A.rfind("--name=", 0) == 0) { LocalName = A.substr(7); }
				else { DataDir = A; }
			}
			if (DataDir.empty())
			{
				// (beside a packaged game: its data folder; else this PC's cache, as the editor's game reads)
				char Exe[MAX_PATH] = {};
				GetModuleFileNameA(nullptr, Exe, MAX_PATH);
				const std::filesystem::path Beside = std::filesystem::path(Exe).parent_path() / "data";
				const char* Local = std::getenv("LOCALAPPDATA");
				DataDir = std::filesystem::exists(Beside / "regions") ? Beside.u8string() : std::string(Local ? Local : ".") + "\\Geodrive\\Cache";
			}
			Must(LocalKm <= 0.0 || (Name.empty() && !LocalName.empty()), "--km=N needs --at=lat,lon and --name=<the place's name>");
			return Region(Name, DataDir, MemoryGB, bHeights, AtLat, AtLon, bKeep, LocalKm, LocalName);
		}
		if (Cmd == "world" && Argc == 4)
		{
			return World({ Argv[2] }, Argv[3]);
		}
		if (Cmd == "games" && Argc >= 4)
		{
			// geotile games <out.games.json> <in.osm.pbf>...
			return Games(std::vector<std::string>(Argv + 3, Argv + Argc), Argv[2]);
		}
		if (Cmd == "airports" && Argc >= 4)
		{
			// geotile airports <out.airports.json> <in.osm.pbf>...
			return Airports(std::vector<std::string>(Argv + 3, Argv + Argc), Argv[2]);
		}
		if ((Cmd == "packmany" || Cmd == "worldmany") && Argc >= 4)
		{
			// several extracts (neighbouring counties) into one archive or world index: geotile packmany <out> <in.pbf>...
			const std::vector<std::string> Inputs(Argv + 3, Argv + Argc);
			return Cmd == "packmany" ? PackPbf(Inputs, Argv[2], 19, 1) : World(Inputs, Argv[2]);
		}
		if (Cmd == "area" && Argc == 7)
		{
			// an area out of a big archive, as the game reads it: geotile area <archive> south west north east
			const std::vector<uint8_t> File = ReadFile(Argv[2]);
			auto E7 = [](const char* V) { return int32_t(std::llround(std::stod(V) * 1e7)); };
			OsmSet Set;
			std::string Error;
			bool bOk = false;
			const double S = Seconds([&] { bOk = ReadArchiveArea(File.data(), File.size(), E7(Argv[3]), E7(Argv[4]), E7(Argv[5]), E7(Argv[6]), Set, Error); });
			Must(bOk, Error);
			std::printf("area: %zu ways, %zu nodes in %.2fs (archive %s)\n", Set.Ways.size(), Set.Nodes.size(), S, Mb(File.size()).c_str());
			return 0;
		}
		if (Cmd == "roads" && Argc >= 4)
		{
			return PackRoads(Argv[2], Argv[3], Argc >= 5 ? std::stoi(Argv[4]) : 19);
		}
		if (Cmd == "check" && Argc == 4)
		{
			return Check(Argv[2], Argv[3]);
		}
		if (Cmd == "bench" && Argc == 4)
		{
			return Bench(Argv[2], Argv[3]);
		}
		if (Cmd == "pbf" && Argc == 4)
		{
			WriteFile(Argv[3], WriteOsmPbf(LoadOverpassJson(Argv[2], true)));
			return 0;
		}
		std::printf("usage:\n  geotile region <name | --at=lat,lon> [data folder] [--memory=GB] [--no-heights] [--keep-download]   a country (any Geofabrik extract) ready to play\n  geotile selftest\n  geotile pack <osm2.json> <out.pmtiles> [level] [precision]\n"
			"  geotile packpbf <country.osm.pbf> <out.pmtiles> [level] [precision] [bands] [--around=lat,lon,km] [--memory=GB]\n"
			"  geotile world <country.osm.pbf> <out.world.json>\n"
			"  geotile airports <out.airports.json> <in.osm.pbf>...   runways, taxiways and aprons for flying\n"
			"  geotile games <out.games.json> <in.osm.pbf>...         the places a game suits: reserves, parks, stadiums, race tracks\n"
			"  geotile lidar <out.pmtiles> --around=lat,lon,km [--cell=4] [--jobs=8]   (England's LiDAR heights)\n"
			"  geotile dgm <out.pmtiles> <folder of 1 km GeoTIFFs, UTM 32> [--cell=2]   (Germany's 1 m heights: a track pack's)\n"
			"  geotile terrain <region.pmtiles> <Cache/terrain> [--jobs=16]   (a region's SRTM heights ahead of time)\n"
			"  geotile crop <in.pmtiles> <out.pmtiles> --around=lat,lon,km [--heights]   (an area cut out of a map or heights this PC has)\n"
			"  geotile sections <region.pmtiles> <heights-1m.pmtiles> --around=lat,lon,km <out.csv>   (real road cross-sections)\n"
			"  geotile packmany <out.pmtiles> <in.osm.pbf>...   (several extracts as one region)\n"
			"  geotile worldmany <out.world.json> <in.osm.pbf>...\n"
			"  geotile roads <roads.roadbin> <roads.pmtiles> [level]\n"
			"  geotile check <osm2.json> <in.pmtiles>\n  geotile bench <osm2.json> <in.pmtiles>\n  geotile pbf <osm2.json> <out.osm.pbf>\n");
		return 2;
	}
	catch (const std::exception& E)
	{
		std::fprintf(stderr, "error: %s\n", E.what());
		return 1;
	}
}
