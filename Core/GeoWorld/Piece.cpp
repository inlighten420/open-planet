// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Piece.h"

#include "TileArchive.h"
#include "GeoMesh/RoadMesh.h"
#include "GeoRoads/RoadNetwork.h"
#include "GeoRoads/RoadSeams.h"
#include "GeoRoads/RoadSurface.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

namespace geoworld
{
	namespace PieceImpl
	{
		using georoads::Vec2;

		constexpr double Margin = 100000.0;	// cm: the rim of a piece not trusted to be whole (roads cut at its edge)

		std::string Format(const char* Pattern, ...)
		{
			char Buffer[512];
			va_list Args;
			va_start(Args, Pattern);
			std::vsnprintf(Buffer, sizeof(Buffer), Pattern, Args);
			va_end(Args);
			return Buffer;
		}

		bool StartsWith(const std::string& Text, const char* Prefix)
		{
			return Text.rfind(Prefix, 0) == 0;
		}
	}

	bool Piece::HasWhole(const Cell& C) const
	{
		const double Size = geoground::Terrain::ChunkSize;
		return Usable.bValid && C.first * Size >= Usable.Min.X && (C.first + 1) * Size <= Usable.Max.X
			&& C.second * Size >= Usable.Min.Y && (C.second + 1) * Size <= Usable.Max.Y;
	}

	void SplitIntoTiles(const geoground::MeshData& Mesh, double Size, std::map<Cell, geoground::MeshData>& Out)
	{
		// each triangle's square, the triangles sorted by it, then each square's corners renumbered through one table
		const size_t NumTriangles = Mesh.Indices.size() / 3;
		std::vector<std::pair<Cell, uint32_t>> Order(NumTriangles);
		for (size_t t = 0; t < NumTriangles; ++t)
		{
			const geoground::Vec3f& A = Mesh.Positions[size_t(Mesh.Indices[t * 3])];
			const geoground::Vec3f& B = Mesh.Positions[size_t(Mesh.Indices[t * 3 + 1])];
			const geoground::Vec3f& C = Mesh.Positions[size_t(Mesh.Indices[t * 3 + 2])];
			Order[t] = { Cell(int32_t(std::floor((double(A.X) + B.X + C.X) / (3.0 * Size))), int32_t(std::floor((double(A.Y) + B.Y + C.Y) / (3.0 * Size)))), uint32_t(t) };
		}
		std::sort(Order.begin(), Order.end());
		const bool bNormals = Mesh.Normals.size() == Mesh.Positions.size();
		const bool bUVs = Mesh.UVs.size() == Mesh.Positions.size();
		const bool bColours = Mesh.Colors.size() == Mesh.Positions.size();
		const bool bTags = Mesh.Tags.size() == NumTriangles;	// (whose each triangle is: the buildings')
		std::vector<int32_t> Remap(Mesh.Positions.size(), -1);
		std::vector<int32_t> Touched;
		for (size_t First = 0; First < Order.size();)
		{
			size_t Last = First;
			while (Last < Order.size() && Order[Last].first == Order[First].first)
			{
				++Last;
			}
			geoground::MeshData& Tile = Out[Order[First].first];
			Tile.Indices.reserve(Tile.Indices.size() + (Last - First) * 3);
			for (size_t i = First; i < Last; ++i)
			{
				const size_t t = Order[i].second;
				if (bTags)
				{
					Tile.Tags.push_back(Mesh.Tags[t]);
				}
				for (size_t k = 0; k < 3; ++k)
				{
					const int32_t V = Mesh.Indices[t * 3 + k];
					if (Remap[size_t(V)] < 0)
					{
						Remap[size_t(V)] = int32_t(Tile.Positions.size());
						Touched.push_back(V);
						Tile.Positions.push_back(Mesh.Positions[size_t(V)]);
						if (bNormals)
						{
							Tile.Normals.push_back(Mesh.Normals[size_t(V)]);
						}
						if (bUVs)
						{
							Tile.UVs.push_back(Mesh.UVs[size_t(V)]);
						}
						if (bColours)
						{
							Tile.Colors.push_back(Mesh.Colors[size_t(V)]);
						}
					}
					Tile.Indices.push_back(Remap[size_t(V)]);
				}
			}
			for (const int32_t V : Touched)
			{
				Remap[size_t(V)] = -1;
			}
			Touched.clear();
			First = Last;
		}
	}

	void BoxAround(double Lat, double Lon, double Km, double& South, double& West, double& North, double& East)
	{
		const double HalfLat = Km * 0.5 / 111.32;
		const double HalfLon = Km * 0.5 / (111.32 * std::cos(Lat * 3.14159265358979323846 / 180.0));
		South = Lat - HalfLat;
		North = Lat + HalfLat;
		West = Lon - HalfLon;
		East = Lon + HalfLon;
	}

	double PieceSizeKm(double TotalRamGB)
	{
		return TotalRamGB < 12.0 ? 8.0 : TotalRamGB < 24.0 ? 12.0 : 16.0;
	}

	std::shared_ptr<Piece> BuildPiece(const PieceRequest& R, std::string& Error)
	{
		using namespace PieceImpl;
		const auto Start = std::chrono::steady_clock::now();
		auto Elapsed = [&Start] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(); };
		std::string Steps;
		double Last = 0.0;
		auto Lap = [&](const char* Name)
		{
			const double Now = Elapsed();
			Steps += Format("%s%s %.2fs", Steps.empty() ? "" : ", ", Name, Now - Last);
			Last = Now;
		};
		auto Stage = [&R](const char* What)
		{
			if (R.Stage)
			{
				R.Stage(What);
			}
		};
		auto Log = [&R](const std::string& Line)
		{
			if (R.Log)
			{
				R.Log(Line);
			}
		};
		const georoads::ParallelForFn ParallelFor = R.ParallelFor ? R.ParallelFor : georoads::ParallelForFn(georoads::ThreadsParallelFor);

		auto P = std::make_shared<Piece>();
		P->Number = R.Number;
		P->South = R.South;
		P->West = R.West;
		P->North = R.North;
		P->East = R.East;

		// its own point: the middle of its box, on the 4 km block grid (so its squares are the drive's)
		const double Size = geoground::Terrain::ChunkSize;
		const double BlockSize = Size * geoground::Terrain::ChunksPerBlock;
		{
			const Vec2 Centre = R.Projection.ToWorld((R.South + R.North) * 0.5, (R.West + R.East) * 0.5);
			P->Origin = Vec2(std::round(Centre.X / BlockSize) * BlockSize, std::round(Centre.Y / BlockSize) * BlockSize);
			P->OriginX = int32_t(std::llround(P->Origin.X / Size));
			P->OriginY = int32_t(std::llround(P->Origin.Y / Size));
		}
		const georoads::Projection Local = R.Projection.Shifted(P->Origin);

		Stage("Reading the map");
		// --- the map data in the box; a dense city gets a smaller box (as much to build as a city centre's, so it comes in
		// as fast: London has three times Cape Town's roads and seven times its buildings in the same 12 km) ---
		geotile::OsmSet Set;
		auto E7 = [](double Degrees) { return int32_t(std::llround(Degrees * 1e7)); };
		double Work = 0.0, Km = 0.0;
		auto Read = [&]
		{
			const double ReadStart = Elapsed();
			Set = geotile::OsmSet();
			if (!R.Region || !geotile::ReadArchiveArea(R.Region, R.RegionSize, E7(P->South), E7(P->West), E7(P->North), E7(P->East), Set, Error))
			{
				if (Error.empty())
				{
					Error = "no map data";
				}
				return false;
			}
			int32_t Roads = 0, Buildings = 0;
			for (const geotile::Way& W : Set.Ways)
			{
				for (const geotile::Tag& T : W.Tags)
				{
					Roads += T.first == "highway" ? 1 : 0;
					Buildings += T.first == "building" ? 1 : 0;
				}
			}
			Work = Roads + Buildings / 6.0;
			Km = (P->North - P->South) * 111.32;
			Log(Format("[Piece] %d: %d roads and %d buildings in %.1f km (read in %.2f s)", R.Number, Roads, Buildings, Km, Elapsed() - ReadStart));
			return true;
		};
		auto Scale = [&](double By)
		{
			const double MidLat = (P->South + P->North) * 0.5, MidLon = (P->West + P->East) * 0.5;
			const double HalfLat = (P->North - P->South) * 0.5 * By, HalfLon = (P->East - P->West) * 0.5 * By;
			P->South = MidLat - HalfLat;
			P->North = MidLat + HalfLat;
			P->West = MidLon - HalfLon;
			P->East = MidLon + HalfLon;
		};
		// a dense place's box is found from its middle out: the smallest box first (quick to read), then grown (or shrunk) to
		// as much as a piece may hold, each read's density saying by how much. Reading the whole box first, then smaller, a
		// city centre's piece read three times what it used (London: 2.2 s for the 24 km box, 0.8 for 7 km, 0.2 for the
		// 5 km it was built from)
		const double FullKm = (P->North - P->South) * 111.32;
		if (R.MaxWork > 0.0 && FullKm > R.MinKm + 0.1)
		{
			Scale(R.MinKm / FullKm);
		}
		for (int32_t Attempt = 0; ; ++Attempt)
		{
			if (!Read())
			{
				return nullptr;
			}
			if (R.MaxWork <= 0.0 || Attempt >= 4)
			{
				break;
			}
			const double By = std::sqrt(R.MaxWork / std::max(Work, 1.0));
			if (Work > R.MaxWork && Km > R.MinKm + 0.1)
			{
				Scale(std::max(By * 0.97, R.MinKm / Km));
				Log(Format("[Piece] %d: dense here, built %.1f km across instead", R.Number, (P->North - P->South) * 111.32));
			}
			else if (Work < R.MaxWork * 0.9 && Km < FullKm - 0.1)
			{
				Scale(std::min(By, FullKm / Km));
				Log(Format("[Piece] %d: room for more, %.1f km across", R.Number, (P->North - P->South) * 111.32));
			}
			else
			{
				break;
			}
		}
		georoads::OsmData RoadWays, LandWays;
		for (const geotile::Way& W : Set.Ways)
		{
			const bool bRoad = std::any_of(W.Tags.begin(), W.Tags.end(), [](const geotile::Tag& T) { return T.first == "highway"; });
			const bool bLand = std::any_of(W.Tags.begin(), W.Tags.end(), [](const geotile::Tag& T) { return T.first == "building" || T.first == "natural" || T.first == "landuse"; });
			for (georoads::OsmData* Data : { bRoad ? &RoadWays : nullptr, bLand ? &LandWays : nullptr })
			{
				if (!Data)
				{
					continue;
				}
				georoads::OsmWay& Out = Data->Ways.emplace_back();
				Out.Id = W.Id;
				Out.Nodes = W.Nodes;
				Out.Tags.assign(W.Tags.begin(), W.Tags.end());
				for (const int64_t Node : W.Nodes)
				{
					if (const auto Found = Set.Nodes.find(Node); Found != Set.Nodes.end())
					{
						Data->Nodes.emplace(Node, Local.ToWorld(Found->second.Lat / 1e7, Found->second.Lon / 1e7));
					}
				}
			}
		}
		Set = geotile::OsmSet();
		Lap("map");
		Stage("Heights");

		// --- the heights: the host's SRTM tiles, then Copernicus and LiDAR where there are any ---
		P->Raster = std::make_unique<geoground::HeightRaster>();
		P->Raster->SetBox(P->South - 0.01, P->West - 0.01, P->North + 0.01, P->East + 0.01, 12);
		if (R.FillTiles)
		{
			R.FillTiles(*P->Raster);
		}
		P->bUsedCopernicus = geoground::ApplyBase(*P->Raster, R.HeightArchives) > 0;
		std::string FineLog;
		P->bUsedLidar = geoground::LoadFine(*P->Raster, R.HeightArchives, P->South, P->West, P->North, P->East, FineLog) > 0;
		P->FineGrid = P->bUsedLidar ? int(P->Raster->FineGridKind) : 0;
		if (!FineLog.empty())
		{
			Log(FineLog);
		}
		Lap("heights");
		Stage("Land cover");

		// --- the ground builder, the road network and its heights (joined to the piece before at the seam) ---
		P->Ground = std::make_unique<geoground::Terrain>();
		geoground::Terrain& Ground = *P->Ground;
		Ground.Projection = Local;
		Ground.Dem = P->Raster.get();
		Ground.Bounds = geoground::Box2();
		Ground.Bounds += Local.ToWorld(P->South, P->West);
		Ground.Bounds += Local.ToWorld(P->North, P->East);
		Ground.Options.bFollowLand = R.bFollowLand;
		Ground.Options.bKerbs = R.bKerbs;
		Ground.Options.Heights.bUK = R.bUK;
		Ground.Options.Heights.Log = R.Log;
		Ground.Options.Heights.ParallelFor = ParallelFor;
		if (R.Previous && R.Previous->Ground)
		{
			Ground.ForcedCanopy = R.Previous->Ground->GetCanopyHeight();
		}
		Ground.BuildMasks(LandWays);
		Lap("land cover");
		Stage("The road network");
		georoads::NetworkOptions NetworkOptions;
		NetworkOptions.bUK = R.bUK;
		NetworkOptions.Country = R.Country;
		NetworkOptions.Log = R.Log;
		georoads::RoadGraph Graph;
		georoads::BuildRoadGraph(RoadWays, Graph, NetworkOptions);
		RoadWays = georoads::OsmData();
		Lap("roads");
		Stage("Road heights");
		Ground.ComputeRoadProfiles(Graph);
		if (R.Previous && R.Previous->Roads && !R.PinnedCells.empty())
		{
			const georoads::SeamStats Seam = georoads::JoinSeams(Graph, *R.Previous->Roads, R.PinnedCells, Size, 40000.0, ParallelFor,
				P->Origin, P->Origin - R.Previous->Origin);
			Log(Format("[Seams] piece %d: %d road points kept the loaded heights, %d eased (worst %.2f m, biggest difference %.2f m)",
				R.Number, Seam.Pinned, Seam.Faded, Seam.WorstFaded / 100.0, Seam.WorstDelta / 100.0));
		}
		P->Roads = std::make_shared<const georoads::RoadGraph>(std::move(Graph));
		Ground.BuildRoadIndex(P->Roads);
		Lap("road heights");
		Stage("Road surfaces and lines");

		P->MinX = int32_t(std::floor(Ground.Bounds.Min.X / Size)) + P->OriginX;
		P->MaxX = int32_t(std::floor(Ground.Bounds.Max.X / Size)) + P->OriginX;
		P->MinY = int32_t(std::floor(Ground.Bounds.Min.Y / Size)) + P->OriginY;
		P->MaxY = int32_t(std::floor(Ground.Bounds.Max.Y / Size)) + P->OriginY;
		P->Usable = geoground::Box2(Ground.Bounds.Min + P->Origin + Vec2(Margin, Margin), Ground.Bounds.Max + P->Origin - Vec2(Margin, Margin));
		const Cell Shift(P->OriginX, P->OriginY);
		auto Drive = [&Shift](const Cell& C) { return Cell(C.first + Shift.first, C.second + Shift.second); };

		// --- roads, lines and buildings, cut into 1 km tiles ---
		geomesh::MeshOptions MeshOptions;
		MeshOptions.bUK = R.bUK;
		MeshOptions.bDriveLeft = R.Country.bDriveLeft;
		MeshOptions.bWhiteEdges = R.Country.Style == georoads::RoadStyle::Europe;
		MeshOptions.ParallelFor = ParallelFor;
		MeshOptions.Log = R.Log;
		{
			geomesh::RoadMeshes RoadMeshes;
			geomesh::BuildRoads(*P->Roads, RoadMeshes, MeshOptions);
			if (R.bScanRoads)
			{
				ScanRoads(*P->Roads, { &RoadMeshes.Roads[0], &RoadMeshes.Roads[1], &RoadMeshes.Roads[2] }, Ground, ParallelFor, R.bUK, P->Scan);
				P->bScanned = true;
				for (const std::string& Line : RoadScanSummary(*P->Roads, Ground, P->Scan))
				{
					Log(Line);
				}
			}
			P->NumJunctions = RoadMeshes.NumJunctions;
			// the seven meshes cut at once, then filed into the tiles
			const geoground::MeshData* Sources[6] = { &RoadMeshes.Roads[0], &RoadMeshes.Roads[1], &RoadMeshes.Roads[2],
				&RoadMeshes.Skirts, &RoadMeshes.Markings[0], &RoadMeshes.Markings[1] };
			std::map<Cell, geoground::MeshData> Split[6];
			ParallelFor(6, [&](int32_t i) { SplitIntoTiles(*Sources[i], Size, Split[i]); });
			for (int32_t i = 0; i < 6; ++i)
			{
				for (auto& Entry : Split[i])
				{
					TileMeshes& Tile = P->Tiles[Drive(Entry.first)];
					geoground::MeshData& Into = i < 3 ? Tile.Roads[i] : i == 3 ? Tile.Skirts : Tile.Markings[i - 4];
					Into = std::move(Entry.second);
				}
			}
		}
		Lap("road meshes");
		Stage("Buildings");
		{
			geomesh::BuildingMeshes Buildings;
			geomesh::BuildBuildings(LandWays, [&Ground](const Vec2& Point) { return Ground.GroundHeight(Point); }, Buildings, P->Roads.get(), MeshOptions);
			P->NumBuildings = Buildings.NumBuildings;
			P->Outlines = std::move(Buildings.Outlines);
			std::map<Cell, geoground::MeshData> Split;
			SplitIntoTiles(Buildings.Buildings, Size, Split);
			for (auto& Entry : Split)
			{
				P->Tiles[Drive(Entry.first)].Buildings = std::move(Entry.second);
			}
		}
		Lap("buildings");
		Stage("Plants");
		{
			// where plants grow: the satellite's land cover (if the host has it), the map's forests, roads and buildings
			// cleared; the ground built from here on carries it in its colour
			geoground::LandCover Cover;
			Cover.South = P->South;
			Cover.West = P->West;
			Cover.North = P->North;
			Cover.East = P->East;
			if (R.FillLandCover)
			{
				R.FillLandCover(Cover);
			}
			P->Vegetation = std::make_unique<geoground::VegetationMap>();
			geoground::VegetationOptions PlantOptions;
			PlantOptions.ParallelFor = ParallelFor;
			PlantOptions.Log = R.Log;
			geoground::BuildVegetation(Cover, Ground, P->Roads.get(), LandWays, *P->Vegetation, PlantOptions);
			// the airports' runways, taxiways and aprons (those in the box)
			if (R.PlantClearings)
			{
				int64_t Cleared = 0;
				int32_t Count = 0;
				const double Margin = 0.01;	// degrees (~1 km: a runway strip's reach)
				std::vector<Vec2> Line;
				for (const PlantClearing& Clear : *R.PlantClearings)
				{
					bool bInside = false;
					for (const auto& Point : Clear.Line)
					{
						bInside = bInside || (Point.first > P->South - Margin && Point.first < P->North + Margin && Point.second > P->West - Margin && Point.second < P->East + Margin);
					}
					if (!bInside)
					{
						continue;
					}
					Line.clear();
					for (const auto& Point : Clear.Line)
					{
						Line.push_back(Local.ToWorld(Point.first, Point.second));
					}
					Cleared += geoground::ClearPlants(*P->Vegetation, Line, Clear.Reach, Clear.bArea);
					++Count;
				}
				if (Count > 0 && R.Log)
				{
					R.Log("[Vegetation] cleared " + std::to_string(Cleared) + " cells for " + std::to_string(Count) + " runways, taxiways and aprons");
				}
			}
			Ground.Vegetation = P->Vegetation.get();
			P->bUsedWorldCover = P->Vegetation->bFromSatellite;
		}
		LandWays = georoads::OsmData();
		Lap("plants");
		Stage("Water and the distant ground");

		// --- water, and the distant ground (4 km blocks over the whole piece) ---
		Ground.BuildWater(P->Water);
		{
			std::vector<Cell> Blocks;
			for (int32_t X = int32_t(std::floor(Ground.Bounds.Min.X / BlockSize)); X <= int32_t(std::floor(Ground.Bounds.Max.X / BlockSize)); ++X)
			{
				for (int32_t Y = int32_t(std::floor(Ground.Bounds.Min.Y / BlockSize)); Y <= int32_t(std::floor(Ground.Bounds.Max.Y / BlockSize)); ++Y)
				{
					Blocks.push_back({ X, Y });
				}
			}
			std::vector<geoground::MeshData> Built(Blocks.size());
			ParallelFor(int32_t(Blocks.size()), [&](int32_t i) { Ground.BuildDistantBlock(Blocks[size_t(i)].first, Blocks[size_t(i)].second, Built[size_t(i)]); });
			for (size_t i = 0; i < Blocks.size(); ++i)
			{
				const int32_t PerBlock = geoground::Terrain::ChunksPerBlock;
				P->DistantBlocks[Cell(Blocks[i].first + P->OriginX / PerBlock, Blocks[i].second + P->OriginY / PerBlock)] = std::move(Built[i]);
			}
		}
		Lap("water and distant ground");
		if (R.bStart)
		{
			Stage("The ground round the start");
		}

		// --- a new area: where to start (a main road near the point asked for), and the near ground round it ---
		if (R.bStart)
		{
			const Vec2 StartNear = R.StartNear - P->Origin;
			const georoads::RoadSurface Surface(*P->Roads);
			double Best = 1e300;
			int32_t SpawnEdge = -1;
			for (int32_t EdgeIndex = 0; EdgeIndex < int32_t(P->Roads->Edges.size()); ++EdgeIndex)
			{
				const georoads::RoadEdge& Edge = P->Roads->Edges[EdgeIndex];
				if (Edge.bTunnel || Edge.bBridge)
				{
					continue;
				}
				const std::string& C = Edge.Highway;
				const double Penalty = (StartsWith(C, "trunk") || StartsWith(C, "motorway") || StartsWith(C, "primary")) ? 0.0
					: StartsWith(C, "secondary") ? 50000.0 : StartsWith(C, "tertiary") ? 100000.0
					: (C == "residential" || C == "unclassified" || C == "living_street" || C == "service") ? 200000.0 : 1e9;
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					const Vec2 Middle = (Edge.Points[i] + Edge.Points[i + 1]) * 0.5;
					const double Score = (Middle - StartNear).Size() + Penalty;
					if (Score < Best)
					{
						Best = Score;
						P->Spawn = Middle;
						P->SpawnHeading = (Edge.Points[i + 1] - Edge.Points[i]).Normal();
						SpawnEdge = EdgeIndex;
					}
				}
			}
			if (SpawnEdge >= 0)
			{
				P->SpawnHeight = Surface.Height(P->Spawn, SpawnEdge);
				P->SpawnRoad = P->Roads->Edges[SpawnEdge].Name;
			}
			else
			{
				P->Spawn = StartNear;
				P->SpawnHeight = Ground.GroundHeight(P->Spawn);
			}
			std::vector<Cell> Near;
			for (int32_t X = P->MinX; X <= P->MaxX; ++X)
			{
				for (int32_t Y = P->MinY; Y <= P->MaxY; ++Y)
				{
					const Vec2 Centre((P->LocalX(X) + 0.5) * Size, (P->LocalY(Y) + 0.5) * Size);
					if ((Centre - P->Spawn).Size() < R.StartRadius)
					{
						Near.push_back({ X, Y });
					}
				}
			}
			// (one after another, each on every worker: its roads' corridors, the most of it, are built in parallel. Four
			// at once, each on one thread, the slowest (a dense square) held up the start)
			for (const Cell& C : Near)
			{
				Ground.BuildChunk(P->LocalX(C.first), P->LocalY(C.second), true, P->StartChunks[C]);
			}
			P->Spawn = P->Spawn + P->Origin;	// (the drive's, like everything the host is given)
			Lap("start ground");
		}

		P->Summary = Format("%d roads, %d junctions, %d buildings", int32_t(P->Roads->Edges.size()), P->NumJunctions, P->NumBuildings);
		Log(Format("[Piece] %d built in %.1f s (%s): %s", R.Number, Elapsed(), Steps.c_str(), P->Summary.c_str()));
		return P;
	}
}
