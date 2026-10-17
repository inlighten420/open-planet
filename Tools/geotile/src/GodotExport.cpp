// Geodrive tile tool - GPL (see Core/README.md)

#include "GodotExport.h"
#include "OsmJson.h"
#include "TileArchive.h"
#include "GeoRoads/Projection.h"
#include "GeoRoads/RoadNetwork.h"
#include "GeoRoads/RoadSurface.h"
#include "GeoGround/HeightRaster.h"
#include "GeoGround/Terrain.h"
#include "GeoMesh/RoadMesh.h"

#include "miniz.h"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace geotile
{
	namespace
	{
		using georoads::Vec2;

		/**
		 *  The 8-bit RGB / RGBA PNGs the Terrarium height tiles are: chunks, the zlib stream inflated, each row
		 *  un-filtered (none, sub, up, average, Paeth). Out: RGB per pixel.
		 */
		bool DecodePng(const std::vector<uint8_t>& File, int& OutWidth, int& OutHeight, std::vector<uint8_t>& OutRgb)
		{
			static const uint8_t Signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
			if (File.size() < 8 || std::memcmp(File.data(), Signature, 8) != 0)
			{
				return false;
			}
			auto U32 = [&File](size_t At) { return (uint32_t(File[At]) << 24) | (uint32_t(File[At + 1]) << 16) | (uint32_t(File[At + 2]) << 8) | File[At + 3]; };
			int Width = 0, Height = 0, Channels = 0;
			std::vector<uint8_t> Compressed;
			for (size_t At = 8; At + 12 <= File.size();)
			{
				const uint32_t Length = U32(At);
				const std::string Type(reinterpret_cast<const char*>(&File[At + 4]), 4);
				if (At + 12 + Length > File.size())
				{
					return false;
				}
				const uint8_t* Data = &File[At + 8];
				if (Type == "IHDR")
				{
					Width = int(U32(At + 8));
					Height = int(U32(At + 12));
					const int Depth = Data[8], Colour = Data[9], Interlace = Data[12];
					if (Depth != 8 || Interlace != 0 || (Colour != 2 && Colour != 6))
					{
						return false;	// not the kind height tiles are
					}
					Channels = Colour == 2 ? 3 : 4;
				}
				else if (Type == "IDAT")
				{
					Compressed.insert(Compressed.end(), Data, Data + Length);
				}
				else if (Type == "IEND")
				{
					break;
				}
				At += 12 + Length;
			}
			if (Width <= 0 || Height <= 0 || Channels == 0)
			{
				return false;
			}
			const size_t Stride = size_t(Width) * Channels;
			std::vector<uint8_t> Raw((Stride + 1) * Height);
			mz_ulong RawSize = mz_ulong(Raw.size());
			if (mz_uncompress(Raw.data(), &RawSize, Compressed.data(), mz_ulong(Compressed.size())) != MZ_OK || RawSize != Raw.size())
			{
				return false;
			}
			std::vector<uint8_t> Pixels(Stride * Height);
			for (int Y = 0; Y < Height; ++Y)
			{
				const uint8_t Filter = Raw[Y * (Stride + 1)];
				const uint8_t* In = &Raw[Y * (Stride + 1) + 1];
				uint8_t* Row = &Pixels[Y * Stride];
				const uint8_t* Up = Y > 0 ? &Pixels[(Y - 1) * Stride] : nullptr;
				for (size_t i = 0; i < Stride; ++i)
				{
					const int A = i >= size_t(Channels) ? Row[i - Channels] : 0;
					const int B = Up ? Up[i] : 0;
					const int C = Up && i >= size_t(Channels) ? Up[i - Channels] : 0;
					int Predicted = 0;
					switch (Filter)
					{
					case 0: Predicted = 0; break;
					case 1: Predicted = A; break;
					case 2: Predicted = B; break;
					case 3: Predicted = (A + B) / 2; break;
					case 4:
					{
						const int P = A + B - C, PA = std::abs(P - A), PB = std::abs(P - B), PC = std::abs(P - C);
						Predicted = PA <= PB && PA <= PC ? A : PB <= PC ? B : C;
						break;
					}
					default: return false;
					}
					Row[i] = uint8_t(In[i] + Predicted);
				}
			}
			OutRgb.resize(size_t(Width) * Height * 3);
			for (size_t P = 0; P < size_t(Width) * Height; ++P)
			{
				std::memcpy(&OutRgb[P * 3], &Pixels[P * Channels], 3);
			}
			OutWidth = Width;
			OutHeight = Height;
			return true;
		}


		/**
		 *  The elevation for a box, as the game builds it (Core/GeoGround/HeightRaster): the Terrarium tiles from the
		 *  game's terrain cache (missing ones fetched as the game fetches them), Copernicus and LiDAR from its heights folder
		 */
		void LoadHeights(const std::string& TerrainDir, const std::string& HeightsDir, double South, double West, double North, double East,
			geoground::HeightRaster& Raster)
		{
			Raster.SetBox(South, West, North, East, 12);
			for (int32_t Index = 0; Index < Raster.TilesX * Raster.TilesY; ++Index)
			{
				const int X = Raster.MinTileX + Index % Raster.TilesX, Y = Raster.MinTileY + Index / Raster.TilesX;
				const std::string Path = TerrainDir + "/12_" + std::to_string(X) + "_" + std::to_string(Y) + ".png";
				if (!std::filesystem::exists(Path))
				{
					const std::string Url = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/12/" + std::to_string(X) + "/" + std::to_string(Y) + ".png";
					const std::string Command = "curl.exe -s -f -L --retry 3 -m 120 -o \"" + Path + "\" \"" + Url + "\"";
					if (std::system(Command.c_str()) != 0)
					{
						throw std::runtime_error("can't fetch terrain tile " + Url);
					}
				}
				int W = 0, H = 0;
				std::vector<uint8_t> Rgb;
				if (!DecodePng(ReadFile(Path), W, H, Rgb) || W != geoground::HeightRaster::TileSize || H != geoground::HeightRaster::TileSize)
				{
					throw std::runtime_error("can't read terrain tile " + Path);
				}
				Raster.SetTile(Index, Rgb.data(), 3);
			}

			// better base heights and fine heights where the game has them
			std::vector<std::vector<uint8_t>> Files;
			std::vector<geoground::Archive> Archives;
			if (std::filesystem::is_directory(HeightsDir))
			{
				for (const auto& Entry : std::filesystem::directory_iterator(HeightsDir))
				{
					if (Entry.path().extension() == ".pmtiles" && Entry.file_size() < (size_t(1) << 30))
					{
						Files.push_back(ReadFile(Entry.path().string()));
						Archives.push_back({ Files.back().data(), Files.back().size(), Entry.path().filename().string() });
					}
				}
			}
			// (the data pointers stay valid: Files isn't touched again until they're used)
			for (size_t i = 0; i < Archives.size(); ++i)
			{
				Archives[i].Data = Files[i].data();
			}
			const int64_t Replaced = geoground::ApplyBase(Raster, Archives);
			std::string Log;
			geoground::LoadFine(Raster, Archives, South, West, North, East, Log);
			std::printf("godot: heights %d x %d tiles%s%s%s\n", Raster.TilesX, Raster.TilesY, Replaced > 0 ? ", Copernicus base" : "",
				Log.empty() ? "" : ", ", Log.c_str());
		}

		/** One mesh piece in Godot's axes (x east, y up, z south, metres), corners shared where they're the same */
		struct Piece
		{
			std::string Name;
			float Colour[3] = { 1.0f, 1.0f, 1.0f };
			float Roughness = 0.9f;
			std::vector<float> Positions;	// xyz
			std::vector<float> Normals;
			std::vector<float> Colours;		// rgba, only if the piece has vertex colours
			std::vector<uint32_t> Indices;
			std::unordered_map<std::string, uint32_t> Shared;

			/** A corner from the world (cm, x north, y east, z up) */
			uint32_t Add(const geoground::Vec3f& P, const geoground::Vec3f& N, const geoground::Vec4f* C)
			{
				const float Corner[10] = { P.Y / 100.0f, P.Z / 100.0f, -P.X / 100.0f, N.Y, N.Z, -N.X, C ? C->X : 0.0f, C ? C->Y : 0.0f, C ? C->Z : 0.0f, C ? C->W : 0.0f };
				const std::string Key(reinterpret_cast<const char*>(Corner), sizeof(Corner));
				const auto Found = Shared.find(Key);
				if (Found != Shared.end())
				{
					return Found->second;
				}
				Positions.insert(Positions.end(), Corner, Corner + 3);
				Normals.insert(Normals.end(), Corner + 3, Corner + 6);
				if (C)
				{
					Colours.insert(Colours.end(), Corner + 6, Corner + 10);
				}
				const uint32_t Index = uint32_t(Positions.size() / 3 - 1);
				Shared.emplace(Key, Index);
				return Index;
			}

			/** A triangle facing the way its normals point (counter-clockwise from the front, as glTF wants) */
			void Triangle(uint32_t A, uint32_t B, uint32_t C)
			{
				const float* PA = &Positions[A * 3], * PB = &Positions[B * 3], * PC = &Positions[C * 3];
				const float U[3] = { PB[0] - PA[0], PB[1] - PA[1], PB[2] - PA[2] }, V[3] = { PC[0] - PA[0], PC[1] - PA[1], PC[2] - PA[2] };
				const float Face[3] = { U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0] };
				const float* N = &Normals[A * 3];
				const float Facing = Face[0] * N[0] + Face[1] * N[1] + Face[2] * N[2];
				Indices.insert(Indices.end(), Facing >= 0.0f ? std::initializer_list<uint32_t>{ A, B, C } : std::initializer_list<uint32_t>{ A, C, B });
			}

			/** A whole Core mesh (vertex colours: the ground's, which the game squares for its pipeline, undone) */
			void AddMesh(const geoground::MeshData& Mesh, bool bColours)
			{
				for (size_t i = 0; i + 2 < Mesh.Indices.size(); i += 3)
				{
					uint32_t Corner[3];
					for (int k = 0; k < 3; ++k)
					{
						const int32_t V = Mesh.Indices[i + k];
						geoground::Vec4f Tint;
						if (bColours && size_t(V) < Mesh.Colors.size())
						{
							const geoground::Vec4f& C = Mesh.Colors[V];
							Tint = { std::sqrt(C.X), std::sqrt(C.Y), std::sqrt(C.Z), 1.0f };
						}
						Corner[k] = Add(Mesh.Positions[V], Mesh.Normals[V], bColours ? &Tint : nullptr);
					}
					Triangle(Corner[0], Corner[1], Corner[2]);
				}
			}
		};

		/** A glTF 2.0 binary: one node, one mesh with a primitive and a plain (two-sided) material per piece */
		void WriteGlb(const std::string& Path, const std::vector<Piece>& Pieces)
		{
			std::vector<uint8_t> Bin;
			auto Append = [&Bin](const void* Data, size_t Bytes)
			{
				const size_t At = Bin.size();
				Bin.resize(At + Bytes);
				std::memcpy(Bin.data() + At, Data, Bytes);
				while (Bin.size() % 4) { Bin.push_back(0); }
				return At;
			};
			nlohmann::json Views = nlohmann::json::array(), Accessors = nlohmann::json::array(), Primitives = nlohmann::json::array(),
				Materials = nlohmann::json::array();
			for (const Piece& P : Pieces)
			{
				if (P.Indices.empty())
				{
					continue;
				}
				float Min[3] = { 1e30f, 1e30f, 1e30f }, Max[3] = { -1e30f, -1e30f, -1e30f };
				for (size_t i = 0; i < P.Positions.size(); ++i)
				{
					Min[i % 3] = std::min(Min[i % 3], P.Positions[i]);
					Max[i % 3] = std::max(Max[i % 3], P.Positions[i]);
				}
				const size_t Vertices = P.Positions.size() / 3;
				auto View = [&](const void* Data, size_t Bytes, int Target)
				{
					Views.push_back({ { "buffer", 0 }, { "byteOffset", Append(Data, Bytes) }, { "byteLength", Bytes }, { "target", Target } });
					return Views.size() - 1;
				};
				nlohmann::json Attributes;
				Accessors.push_back({ { "bufferView", View(P.Positions.data(), P.Positions.size() * 4, 34962) }, { "componentType", 5126 }, { "count", Vertices }, { "type", "VEC3" },
					{ "min", { Min[0], Min[1], Min[2] } }, { "max", { Max[0], Max[1], Max[2] } } });
				Attributes["POSITION"] = Accessors.size() - 1;
				Accessors.push_back({ { "bufferView", View(P.Normals.data(), P.Normals.size() * 4, 34962) }, { "componentType", 5126 }, { "count", Vertices }, { "type", "VEC3" } });
				Attributes["NORMAL"] = Accessors.size() - 1;
				if (!P.Colours.empty())
				{
					Accessors.push_back({ { "bufferView", View(P.Colours.data(), P.Colours.size() * 4, 34962) }, { "componentType", 5126 }, { "count", Vertices }, { "type", "VEC4" } });
					Attributes["COLOR_0"] = Accessors.size() - 1;
				}
				Accessors.push_back({ { "bufferView", View(P.Indices.data(), P.Indices.size() * 4, 34963) }, { "componentType", 5125 }, { "count", P.Indices.size() }, { "type", "SCALAR" } });
				Primitives.push_back({ { "attributes", Attributes }, { "indices", Accessors.size() - 1 }, { "material", Materials.size() } });
				Materials.push_back({ { "name", P.Name }, { "doubleSided", true }, { "pbrMetallicRoughness", { { "baseColorFactor", { P.Colour[0], P.Colour[1], P.Colour[2], 1.0 } },
					{ "metallicFactor", 0.0 }, { "roughnessFactor", P.Roughness } } } });
			}
			const nlohmann::json Gltf = {
				{ "asset", { { "version", "2.0" }, { "generator", "Geodrive Core (geotile godot)" } } },
				{ "scene", 0 }, { "scenes", { { { "nodes", { 0 } } } } },
				{ "nodes", { { { "name", "Area" }, { "mesh", 0 } } } },
				{ "meshes", { { { "name", "Area" }, { "primitives", Primitives } } } },
				{ "materials", Materials }, { "accessors", Accessors }, { "bufferViews", Views },
				{ "buffers", { { { "byteLength", Bin.size() } } } },
			};
			std::string Json = Gltf.dump();
			while (Json.size() % 4) { Json.push_back(' '); }
			std::vector<uint8_t> Out;
			auto U32 = [&Out](uint32_t V) { for (int i = 0; i < 4; ++i) { Out.push_back(uint8_t(V >> (8 * i))); } };
			U32(0x46546C67);	// "glTF"
			U32(2);
			U32(uint32_t(12 + 8 + Json.size() + 8 + Bin.size()));
			U32(uint32_t(Json.size()));
			U32(0x4E4F534A);	// "JSON"
			Out.insert(Out.end(), Json.begin(), Json.end());
			U32(uint32_t(Bin.size()));
			U32(0x004E4942);	// "BIN"
			Out.insert(Out.end(), Bin.begin(), Bin.end());
			WriteFile(Path, Out);
		}
	}

	int ExportGodot(const std::string& RegionPath, const std::string& TerrainDir, double South, double West, double North, double East,
		const std::string& OutPath, bool bUK)
	{
		const auto Start = std::chrono::steady_clock::now();
		auto Elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count(); };
		auto Log = [](const std::string& Line) { std::printf("  %s\n", Line.c_str()); };

		// --- the map data in the box (Core/GeoTiles), projected around its middle: the roads, and what the ground reads
		// (buildings for "in town", water, forest, beaches) ---
		const std::vector<uint8_t> Region = ReadFile(RegionPath);
		OsmSet Set;
		std::string Error;
		auto E7 = [](double Degrees) { return int32_t(std::llround(Degrees * 1e7)); };
		if (!ReadArchiveArea(Region.data(), Region.size(), E7(South), E7(West), E7(North), E7(East), Set, Error))
		{
			throw std::runtime_error(Error);
		}
		const georoads::Projection Projection((South + North) * 0.5, (West + East) * 0.5);
		georoads::OsmData Roads, Land;
		for (const Way& W : Set.Ways)
		{
			const bool bRoad = std::any_of(W.Tags.begin(), W.Tags.end(), [](const Tag& T) { return T.first == "highway"; });
			const bool bLand = std::any_of(W.Tags.begin(), W.Tags.end(), [](const Tag& T) { return T.first == "building" || T.first == "natural" || T.first == "landuse"; });
			for (georoads::OsmData* Data : { bRoad ? &Roads : nullptr, bLand ? &Land : nullptr })
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
						Data->Nodes.emplace(Node, Projection.ToWorld(Found->second.Lat / 1e7, Found->second.Lon / 1e7));
					}
				}
			}
		}
		std::printf("godot: %zu road ways, %zu land ways in the box, %.1fs\n", Roads.Ways.size(), Land.Ways.size(), Elapsed());

		// --- the road network (Core/GeoRoads) ---
		georoads::NetworkOptions NetworkOptions;
		NetworkOptions.bUK = bUK;
		NetworkOptions.Log = Log;
		georoads::RoadGraph Graph;
		georoads::BuildRoadGraph(Roads, Graph, NetworkOptions);
		std::printf("godot: %zu roads, %zu junctions and ends, %.1fs\n", Graph.Edges.size(), Graph.Nodes.size(), Elapsed());

		// --- the ground, as the game builds it (Core/GeoGround): heights, land cover, the roads' heights solved, the
		// road index the corridors are shaped from ---
		geoground::HeightRaster Raster;
		const std::string HeightsDir = (std::filesystem::path(TerrainDir).parent_path() / "heights").string();
		LoadHeights(TerrainDir, HeightsDir, South - 0.01, West - 0.01, North + 0.01, East + 0.01, Raster);
		geoground::Terrain Ground;
		Ground.Projection = Projection;
		Ground.Dem = &Raster;
		Ground.Bounds += Projection.ToWorld(South, West);
		Ground.Bounds += Projection.ToWorld(North, East);
		Ground.Options.Heights.bUK = bUK;
		Ground.Options.Heights.Log = Log;
		Ground.BuildMasks(Land);
		Ground.ComputeRoadProfiles(Graph);
		const auto Solved = std::make_shared<const georoads::RoadGraph>(Graph);
		Ground.BuildRoadIndex(Solved);
		const georoads::RoadSurface Surface(Graph);
		std::printf("godot: heights solved, road index built, %.1fs\n", Elapsed());

		// --- where to start: on a main road near the middle, facing along it ---
		double Best = 1e300;
		Vec2 Spawn, Heading(1.0, 0.0);
		int32_t SpawnEdge = -1;
		for (int32_t EdgeIndex = 0; EdgeIndex < int32_t(Graph.Edges.size()); ++EdgeIndex)
		{
			const georoads::RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (Edge.bTunnel || Edge.bBridge)
			{
				continue;
			}
			const std::string& C = Edge.Highway;
			const double Penalty = (georoads::StartsWith(C, "trunk") || georoads::StartsWith(C, "motorway") || georoads::StartsWith(C, "primary")) ? 0.0
				: georoads::StartsWith(C, "secondary") ? 50000.0 : georoads::StartsWith(C, "tertiary") ? 100000.0 : 200000.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				const Vec2 Middle = (Edge.Points[i] + Edge.Points[i + 1]) * 0.5;
				const double Score = Middle.Size() + Penalty;
				if (Score < Best)
				{
					Best = Score;
					Spawn = Middle;
					Heading = (Edge.Points[i + 1] - Edge.Points[i]).Normal();
					SpawnEdge = EdgeIndex;
				}
			}
		}

		// --- meshes, as the game builds them (Core/GeoMesh): roads with their junctions, skirts and lines; buildings ---
		std::vector<Piece> Pieces(9);
		Pieces[0].Name = "Asphalt";	Pieces[0].Colour[0] = 0.025f; Pieces[0].Colour[1] = 0.025f; Pieces[0].Colour[2] = 0.028f;
		Pieces[1].Name = "Paving";	Pieces[1].Colour[0] = 0.13f; Pieces[1].Colour[1] = 0.11f; Pieces[1].Colour[2] = 0.09f;
		Pieces[2].Name = "Gravel";	Pieces[2].Colour[0] = 0.25f; Pieces[2].Colour[1] = 0.17f; Pieces[2].Colour[2] = 0.09f;
		Pieces[3].Name = "Ground";	Pieces[3].Roughness = 1.0f;	// (its colour is per corner)
		Pieces[4].Name = "Water";	Pieces[4].Colour[0] = 0.02f; Pieces[4].Colour[1] = 0.06f; Pieces[4].Colour[2] = 0.09f; Pieces[4].Roughness = 0.15f;
		Pieces[5].Name = "Skirts";	Pieces[5].Colour[0] = 0.03f; Pieces[5].Colour[1] = 0.03f; Pieces[5].Colour[2] = 0.03f;
		Pieces[6].Name = "WhiteLines";	Pieces[6].Colour[0] = 0.8f; Pieces[6].Colour[1] = 0.8f; Pieces[6].Colour[2] = 0.78f; Pieces[6].Roughness = 0.6f;
		Pieces[7].Name = "YellowLines";	Pieces[7].Colour[0] = 0.8f; Pieces[7].Colour[1] = 0.55f; Pieces[7].Colour[2] = 0.05f; Pieces[7].Roughness = 0.6f;
		Pieces[8].Name = "Buildings";	Pieces[8].Colour[0] = 0.55f; Pieces[8].Colour[1] = 0.52f; Pieces[8].Colour[2] = 0.48f;
		geomesh::MeshOptions MeshOptions;
		MeshOptions.bUK = bUK;
		MeshOptions.Log = Log;
		geomesh::RoadMeshes RoadMeshes;
		geomesh::BuildRoads(Graph, RoadMeshes, MeshOptions);
		size_t RoadTriangles = 0;
		for (int32_t Kind = 0; Kind < geomesh::NumSurfaces; ++Kind)
		{
			Pieces[Kind].AddMesh(RoadMeshes.Roads[Kind], false);
			RoadTriangles += RoadMeshes.Roads[Kind].Indices.size() / 3;
		}
		Pieces[5].AddMesh(RoadMeshes.Skirts, false);
		Pieces[6].AddMesh(RoadMeshes.Markings[0], false);
		Pieces[7].AddMesh(RoadMeshes.Markings[1], false);
		geomesh::BuildingMeshes Buildings;
		geomesh::BuildBuildings(Land, [&Ground](const Vec2& P) { return Ground.GroundHeight(P); }, Buildings, Solved.get(), MeshOptions);
		Pieces[8].AddMesh(Buildings.Buildings, false);
		std::printf("godot: roads (%zu triangles, %d junctions), lines, %d buildings, %.1fs\n", RoadTriangles, RoadMeshes.NumJunctions, Buildings.NumBuildings, Elapsed());

		// the ground chunks as the game streams them: the detailed ones (10 m, corridors checked against every road)
		// within 1 km of the start, the coarser ones (50 m) beyond; and the water
		const int32_t X0 = int32_t(std::floor(Ground.Bounds.Min.X / geoground::Terrain::ChunkSize)), X1 = int32_t(std::floor(Ground.Bounds.Max.X / geoground::Terrain::ChunkSize));
		const int32_t Y0 = int32_t(std::floor(Ground.Bounds.Min.Y / geoground::Terrain::ChunkSize)), Y1 = int32_t(std::floor(Ground.Bounds.Max.Y / geoground::Terrain::ChunkSize));
		int32_t Near = 0, Far = 0;
		for (int32_t X = X0; X <= X1; ++X)
		{
			for (int32_t Y = Y0; Y <= Y1; ++Y)
			{
				const Vec2 Centre((X + 0.5) * geoground::Terrain::ChunkSize, (Y + 0.5) * geoground::Terrain::ChunkSize);
				const bool bNear = georoads::Distance(Centre, Spawn) < 100000.0;
				geoground::MeshData Chunk;
				Ground.BuildChunk(X, Y, bNear, Chunk);
				Pieces[3].AddMesh(Chunk, true);
				(bNear ? Near : Far)++;
			}
		}
		geoground::MeshData Water;
		Ground.BuildWater(Water);
		Pieces[4].AddMesh(Water, false);
		std::printf("godot: ground %d detailed and %d coarse chunks (%zu triangles), %.1fs\n", Near, Far, Pieces[3].Indices.size() / 3, Elapsed());

		WriteGlb(OutPath, Pieces);

		const float SpawnHeight = SpawnEdge >= 0 ? Surface.Height(Spawn, SpawnEdge) : 0.0f;
		const nlohmann::json Info = {
			{ "box", { South, West, North, East } },
			{ "origin", { Projection.OriginLat, Projection.OriginLon } },
			{ "spawn", { Spawn.Y / 100.0, SpawnHeight / 100.0, -Spawn.X / 100.0 } },
			{ "heading", { Heading.Y, 0.0, -Heading.X } },
			{ "spawn_road", SpawnEdge >= 0 ? Graph.Edges[SpawnEdge].Name : std::string() },
			{ "roads", Graph.Edges.size() },
			{ "road_triangles", RoadTriangles },
			{ "ground_triangles", Pieces[3].Indices.size() / 3 },
		};
		const std::string InfoText = Info.dump(2);
		const std::string InfoPath = OutPath.substr(0, OutPath.find_last_of('.')) + ".json";
		WriteFile(InfoPath, std::vector<uint8_t>(InfoText.begin(), InfoText.end()));
		std::printf("godot: -> %s (%.0f MB; %zu road triangles, start on %s), %s, %.1fs\n", OutPath.c_str(), std::filesystem::file_size(OutPath) / 1e6, RoadTriangles,
			SpawnEdge >= 0 ? Graph.Edges[SpawnEdge].Name.c_str() : "?", InfoPath.c_str(), Elapsed());
		return 0;
	}
}
