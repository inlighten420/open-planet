// Geodrive for Godot - AGPL-3.0 (see LICENSE)

#include "geodrive_world.h"
#include "BritishGrid.h"

#include "TileArchive.h"
#include "GeoGround/HeightRaster.h"
#include "GeoGround/Horizon.h"
#include "GeoGround/Terrain.h"
#include "GeoPlaces/Areas.h"
#include "GeoRoads/Projection.h"
#include "GeoRoads/RoadSurface.h"
#include "GeoWorld/Piece.h"
#include "GeoTraffic/Traffic.h"
#include "GeoTraffic/RobotDriver.h"
#include "GeoMesh/Plants.h"
#include "GeoPeople/Body.h"
#include "GeoMesh/Aircraft.h"
#include "geodrive_car.h"
#include "process_memory.h"
#include "http_fetch.h"
#include "landcover.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/tls_options.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace godot;

namespace
{
	// where reports are written: the Saved folder beside the project in the editor (shared with the Unreal game); in a
	// packaged game the player's own folder
	String ReportsFolder()
	{
		if (OS::get_singleton()->has_feature("template"))
		{
			return OS::get_singleton()->get_user_data_dir().path_join("RoadReports");
		}
		return ProjectSettings::get_singleton()->globalize_path("res://").path_join("../Saved/RoadReports").simplify_path();
	}
}

namespace
{
	using georoads::Vec2;
	using geoworld::Cell;

	// --- the streaming distances and budgets (m, the Unreal game's) ---
	// (how far the detailed ground, the 1 km chunks, and the roads and buildings reach: from the detail distance, see
	// GeodriveWorld::_process; at the usual 7 km, as the Unreal game has them)
	constexpr double DistantRadius = 30000.0;		// distant blocks out to here
	constexpr double ReplaceRadius = 2000.0;		// a square cut off at a piece's edge is redone from a whole one only beyond this
	constexpr double NextAhead = 4000.0;			// the next piece starts when less than this is built ahead...
	constexpr double NextLead = 3000.0;				// ...centred this far ahead
	constexpr double StartFillRadius = 1500.0;		// a new area is ready once nothing this near is pending (the rest streams in while driving)
	constexpr int MaxChunkJobs = 8;
	constexpr int MaxTileJobs = 4;
	constexpr double BudgetMs = 4.0;				// main-thread time a frame for putting things in (more while loading)
	constexpr double LoadingBudgetMs = 30.0;

	// collision: 62.5 m tiles, bodies within this of the focus (Godot builds a concave shape's search tree on the main
	// thread, ~1.6 us a triangle, so a few a frame)
	constexpr double CollisionTileSize = 62.5;
	constexpr float CollisionRadius = 600.0f;
	constexpr float CollisionKeepRadius = 900.0f;
	constexpr float SpawnSolidRadius = 300.0f;
	constexpr float LinesDrawnTo = 600.0f;	// painted lines aren't drawn further than this (m)
	// the 3D roads aren't drawn further than this (m): the painted roads (the ground's road map) are fully in by then,
	// and the ground past PlainFrom has no road corridors to sit them in
	constexpr float RoadsDrawnTo = 3500.0f;
	constexpr double PlainFrom = 4500.0;	// m: coarse ground from here on is plain (no corridors: ~800 triangles a km, not ~30,000)

	const char* HeightTileUrl = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%d/%d/%d.png";

	/** Worker threads run below normal priority, so the world built while driving leaves the game its share of the CPU */
	void LowerPriority()
	{
#ifdef _WIN32
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	}

	double Seconds()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	/** A file mapped into memory, read only */
	struct MappedFile
	{
		const uint8_t* Data = nullptr;
		size_t Size = 0;

		explicit MappedFile(const std::filesystem::path& Path)
		{
#ifdef _WIN32
			File = CreateFileW(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			LARGE_INTEGER Length;
			if (File == INVALID_HANDLE_VALUE || !GetFileSizeEx(File, &Length) || Length.QuadPart == 0)
			{
				return;
			}
			Mapping = CreateFileMappingW(File, nullptr, PAGE_READONLY, 0, 0, nullptr);
			if (Mapping)
			{
				Data = static_cast<const uint8_t*>(MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0));
				Size = Data ? size_t(Length.QuadPart) : 0;
			}
#else
			Descriptor = open(Path.c_str(), O_RDONLY);
			struct stat Info;
			if (Descriptor < 0 || fstat(Descriptor, &Info) != 0 || Info.st_size == 0)
			{
				return;
			}
			void* View = mmap(nullptr, size_t(Info.st_size), PROT_READ, MAP_PRIVATE, Descriptor, 0);
			if (View != MAP_FAILED)
			{
				Data = static_cast<const uint8_t*>(View);
				Size = size_t(Info.st_size);
			}
#endif
		}

		~MappedFile()
		{
#ifdef _WIN32
			if (Data)
			{
				UnmapViewOfFile(Data);
			}
			if (Mapping)
			{
				CloseHandle(Mapping);
			}
			if (File != INVALID_HANDLE_VALUE)
			{
				CloseHandle(File);
			}
#else
			if (Data)
			{
				munmap(const_cast<uint8_t*>(Data), Size);
			}
			if (Descriptor >= 0)
			{
				close(Descriptor);
			}
#endif
		}

		MappedFile(const MappedFile&) = delete;
		MappedFile& operator=(const MappedFile&) = delete;

	private:
#ifdef _WIN32
		HANDLE File = INVALID_HANDLE_VALUE;
		HANDLE Mapping = nullptr;
#else
		int Descriptor = -1;
#endif
	};

	std::vector<uint8_t> ReadAll(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		return std::vector<uint8_t>((std::istreambuf_iterator<char>(In)), std::istreambuf_iterator<char>());
	}

	/** World cm (x north, y east, z up) to Godot metres (x east, y up, z south) */
	Vector3 ToGodot(const Vec2& P, double Height) { return Vector3(float(P.Y / 100.0), float(Height / 100.0), float(-P.X / 100.0)); }
	Vec2 ToWorld(const Vector3& P) { return Vec2(-P.z * 100.0, P.x * 100.0); }

	/** A Core mesh as one surface of a Godot mesh. The axes swap changes handedness, so each triangle's corners go the
	 *  other way round to stay front-facing. */
	// (bLook: the texture coordinates and the corners' colours as they are, for a shader: the buildings' look)
	void AddSurface(const Ref<ArrayMesh>& Out, const geoground::MeshData& Mesh, bool bColours, const Ref<Material>& With, bool bLook = false)
	{
		if (Mesh.Indices.empty())
		{
			return;
		}
		PackedVector3Array Positions, Normals;
		PackedColorArray Colours;
		PackedInt32Array Indices;
		const int64_t N = int64_t(Mesh.Positions.size());
		Positions.resize(N);
		Normals.resize(N);
		Vector3* P = Positions.ptrw();
		Vector3* Nm = Normals.ptrw();
		for (int64_t i = 0; i < N; ++i)
		{
			const geoground::Vec3f& V = Mesh.Positions[size_t(i)];
			const geoground::Vec3f& W = Mesh.Normals[size_t(i)];
			P[i] = Vector3(V.Y / 100.0f, V.Z / 100.0f, -V.X / 100.0f);
			Nm[i] = Vector3(W.Y, W.Z, -W.X);
		}
		PackedVector2Array UVs;
		if (bLook && Mesh.UVs.size() == Mesh.Positions.size() && Mesh.Colors.size() == Mesh.Positions.size())
		{
			UVs.resize(N);
			Colours.resize(N);
			Vector2* T = UVs.ptrw();
			Color* C = Colours.ptrw();
			for (int64_t i = 0; i < N; ++i)
			{
				const geoground::Vec2f& U = Mesh.UVs[size_t(i)];
				const geoground::Vec4f& V = Mesh.Colors[size_t(i)];
				T[i] = Vector2(U.X, U.Y);
				C[i] = Color(V.X, V.Y, V.Z, V.W);
			}
		}
		else 		if (bColours && Mesh.Colors.size() == Mesh.Positions.size())
		{
			// the game squares its ground colours for its own pipeline: undone here
			Colours.resize(N);
			Color* C = Colours.ptrw();
			for (int64_t i = 0; i < N; ++i)
			{
				const geoground::Vec4f& V = Mesh.Colors[size_t(i)];
				C[i] = Color(std::sqrt(V.X), std::sqrt(V.Y), std::sqrt(V.Z), std::clamp(V.W, 0.0f, 1.0f));	// (alpha: 1 - how covered by trees and shrubs)
			}
		}
		Indices.resize(int64_t(Mesh.Indices.size()));
		int32_t* I = Indices.ptrw();
		for (size_t t = 0; t + 2 < Mesh.Indices.size(); t += 3)
		{
			I[t] = Mesh.Indices[t];
			I[t + 1] = Mesh.Indices[t + 2];
			I[t + 2] = Mesh.Indices[t + 1];
		}
		Array Arrays;
		Arrays.resize(Mesh::ARRAY_MAX);
		Arrays[Mesh::ARRAY_VERTEX] = Positions;
		Arrays[Mesh::ARRAY_NORMAL] = Normals;
		if (!Colours.is_empty())
		{
			Arrays[Mesh::ARRAY_COLOR] = Colours;
		}
		if (!UVs.is_empty())
		{
			Arrays[Mesh::ARRAY_TEX_UV] = UVs;
		}
		Arrays[Mesh::ARRAY_INDEX] = Indices;
		Out->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, Arrays, Array(), Dictionary(), Mesh::ARRAY_FLAG_COMPRESS_ATTRIBUTES);	// (16-bit corners in its box: half the GPU memory)
		if (With.is_valid())
		{
			Out->surface_set_material(Out->get_surface_count() - 1, With);
		}
	}

	/** A plain material (colour given in linear light, as the game's are) */
	Ref<StandardMaterial3D> Paint(float R, float G, float B, float Roughness, bool bVertexColours = false)
	{
		Ref<StandardMaterial3D> Out;
		Out.instantiate();
		Out->set_albedo(bVertexColours ? Color(1, 1, 1) : Color(R, G, B).linear_to_srgb());
		Out->set_roughness(Roughness);
		Out->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, bVertexColours);
		return Out;
	}

	/** Collision faces (Godot axes) in 62.5 m buckets */
	using FaceBuckets = std::map<std::pair<int32_t, int32_t>, PackedVector3Array>;
	void AddFaces(const geoground::MeshData& Mesh, FaceBuckets& Buckets)
	{
		const double Size = CollisionTileSize * 100.0;
		for (size_t t = 0; t + 2 < Mesh.Indices.size(); t += 3)
		{
			const geoground::Vec3f& A = Mesh.Positions[size_t(Mesh.Indices[t])];
			const geoground::Vec3f& B = Mesh.Positions[size_t(Mesh.Indices[t + 1])];
			const geoground::Vec3f& C = Mesh.Positions[size_t(Mesh.Indices[t + 2])];
			const std::pair<int32_t, int32_t> Key(int32_t(std::floor((double(A.X) + B.X + C.X) / (3.0 * Size))), int32_t(std::floor((double(A.Y) + B.Y + C.Y) / (3.0 * Size))));
			PackedVector3Array& Faces = Buckets[Key];
			for (const geoground::Vec3f* V : { &A, &C, &B })
			{
				Faces.push_back(Vector3(V->Y / 100.0f, V->Z / 100.0f, -V->X / 100.0f));
			}
		}
	}

	/** A log line of detail (for finding faults, many a piece): shown only with verbose on */
	bool IsDetail(const std::string& Line)
	{
		for (const char* Prefix : { "[Heights]   ", "[Terrain]", "[Follow]", "[GroundCheck]", "[Gaps]", "[Seams]", "[Markings]" })
		{
			if (Line.rfind(Prefix, 0) == 0)
			{
				return true;
			}
		}
		return false;
	}

	/** A worker's hold on a piece, let go as the worker finishes, on its own thread: a job's copy can be a piece's last
	 *  hold (the piece let go meanwhile), and freeing a piece takes ~80 ms, a stutter if the main thread does it when it
	 *  takes the job's result */
	struct ClientPiece;
	struct LetGoHere
	{
		std::shared_ptr<ClientPiece>& Piece;
		~LetGoHere() { Piece.reset(); }
	};

	/** A static body that collides with the faces (both sides, so a sweep finds them whichever way they face) */
	StaticBody3D* MakeCollision(const PackedVector3Array& Faces)
	{
		Ref<ConcavePolygonShape3D> Shape;
		Shape.instantiate();
		Shape->set_backface_collision_enabled(true);
		Shape->set_faces(Faces);
		CollisionShape3D* ShapeNode = memnew(CollisionShape3D);
		ShapeNode->set_shape(Shape);
		StaticBody3D* Body = memnew(StaticBody3D);
		Body->add_child(ShapeNode);
		return Body;
	}

	/** Roads a car may be put on: not footpaths, steps, cycle paths, bridleways, tracks, platforms... */
	bool Drivable(const std::string& Highway)
	{
		for (const char* Not : { "footway", "path", "cycleway", "steps", "pedestrian", "bridleway", "corridor", "platform", "track", "proposed", "construction", "elevator", "via_ferrata", "busway" })
		{
			if (Highway == Not)
			{
				return false;
			}
		}
		return true;
	}

	/** UK rules where the UK is */
	bool InUK(double Lat, double Lon) { return Lat >= 49.8 && Lat <= 61.0 && Lon >= -8.7 && Lon <= 1.9; }

	/** Fetches a height tile over HTTPS (blocking; on a worker thread). Empty if it couldn't. */
	std::vector<uint8_t> Download(const std::string& Url)
	{
		return HttpGet(Url).Body;
	}

	/** The road segments of a piece by 200 m square, for nearest_road */
	struct RoadGrid
	{
		static constexpr double Cell = 20000.0;	// cm
		std::map<std::pair<int32_t, int32_t>, std::vector<std::pair<int32_t, int32_t>>> Cells;

		void Build(const georoads::RoadGraph& Roads)
		{
			for (int32_t EdgeIndex = 0; EdgeIndex < int32_t(Roads.Edges.size()); ++EdgeIndex)
			{
				const std::vector<Vec2>& Points = Roads.Edges[EdgeIndex].Points;
				for (int32_t i = 0; i + 1 < int32_t(Points.size()); ++i)
				{
					const Vec2 Lo(std::min(Points[i].X, Points[i + 1].X), std::min(Points[i].Y, Points[i + 1].Y));
					const Vec2 Hi(std::max(Points[i].X, Points[i + 1].X), std::max(Points[i].Y, Points[i + 1].Y));
					for (int32_t X = int32_t(std::floor(Lo.X / Cell)); X <= int32_t(std::floor(Hi.X / Cell)); ++X)
					{
						for (int32_t Y = int32_t(std::floor(Lo.Y / Cell)); Y <= int32_t(std::floor(Hi.Y / Cell)); ++Y)
						{
							Cells[{ X, Y }].push_back({ EdgeIndex, i });
						}
					}
				}
			}
		}
	};

	/** A road tile's meshes, packed (Godot's zstd): a piece keeps its tiles like this until they stream in */
	struct PackedTile
	{
		PackedByteArray Bytes;
		int64_t Size = 0;	// unpacked
	};

	/** The seven meshes of a tile as bytes: per mesh its counts, then positions, normals, indices, and (where they have
	    them: the buildings) texture coordinates and colours */
	PackedTile Pack(const geoworld::TileMeshes& Tile)
	{
		const geoground::MeshData* Meshes[7] = { &Tile.Roads[0], &Tile.Roads[1], &Tile.Roads[2], &Tile.Skirts, &Tile.Markings[0], &Tile.Markings[1], &Tile.Buildings };
		std::vector<uint8_t> Raw;
		auto Put = [&Raw](const void* Data, size_t Bytes)
		{
			const uint8_t* From = static_cast<const uint8_t*>(Data);
			Raw.insert(Raw.end(), From, From + Bytes);
		};
		for (const geoground::MeshData* M : Meshes)
		{
			// (texture coordinates and colours only where every corner has them: the buildings' look)
			const bool bLook = M == &Tile.Buildings;
			const bool bUVs = bLook && !M->UVs.empty() && M->UVs.size() == M->Positions.size();
			const bool bColours = bLook && !M->Colors.empty() && M->Colors.size() == M->Positions.size();
			const bool bTags = bLook && !M->Tags.empty() && M->Tags.size() * 3 == M->Indices.size();	// (whose each triangle is)
			const uint32_t Counts[3] = { uint32_t(M->Positions.size()), uint32_t(M->Indices.size()), uint32_t(bUVs ? 1 : 0) | uint32_t(bColours ? 2 : 0) | uint32_t(bTags ? 4 : 0) };
			Put(Counts, sizeof(Counts));
			Put(M->Positions.data(), M->Positions.size() * sizeof(geoground::Vec3f));
			Put(M->Normals.data(), M->Normals.size() * sizeof(geoground::Vec3f));
			Put(M->Indices.data(), M->Indices.size() * sizeof(int32_t));
			if (bUVs)
			{
				Put(M->UVs.data(), M->UVs.size() * sizeof(geoground::Vec2f));
			}
			if (bColours)
			{
				// a byte a channel (the buildings' look needs no more)
				for (const geoground::Vec4f& C : M->Colors)
				{
					const uint8_t Bytes[4] = { uint8_t(std::clamp(C.X, 0.0f, 1.0f) * 255.0f + 0.5f), uint8_t(std::clamp(C.Y, 0.0f, 1.0f) * 255.0f + 0.5f),
						uint8_t(std::clamp(C.Z, 0.0f, 1.0f) * 255.0f + 0.5f), uint8_t(std::clamp(C.W, 0.0f, 1.0f) * 255.0f + 0.5f) };
					Put(Bytes, 4);
				}
			}
			if (bTags)
			{
				Put(M->Tags.data(), M->Tags.size() * sizeof(int32_t));
			}
		}
		PackedByteArray Bytes;
		Bytes.resize(int64_t(Raw.size()));
		std::copy(Raw.begin(), Raw.end(), Bytes.ptrw());
		PackedTile Out;
		Out.Size = int64_t(Raw.size());
		Out.Bytes = Bytes.compress(FileAccess::COMPRESSION_ZSTD);
		return Out;
	}

	geoworld::TileMeshes Unpack(const PackedTile& Packed)
	{
		geoworld::TileMeshes Out;
		const PackedByteArray Raw = Packed.Bytes.decompress(Packed.Size, FileAccess::COMPRESSION_ZSTD);
		if (Raw.size() != Packed.Size)
		{
			return Out;
		}
		geoground::MeshData* Meshes[7] = { &Out.Roads[0], &Out.Roads[1], &Out.Roads[2], &Out.Skirts, &Out.Markings[0], &Out.Markings[1], &Out.Buildings };
		const uint8_t* At = Raw.ptr();
		auto Get = [&At](void* Data, size_t Bytes)
		{
			std::memcpy(Data, At, Bytes);
			At += Bytes;
		};
		for (geoground::MeshData* M : Meshes)
		{
			uint32_t Counts[3];
			Get(Counts, sizeof(Counts));
			M->Positions.resize(Counts[0]);
			M->Normals.resize(Counts[0]);
			M->Indices.resize(Counts[1]);
			Get(M->Positions.data(), Counts[0] * sizeof(geoground::Vec3f));
			Get(M->Normals.data(), Counts[0] * sizeof(geoground::Vec3f));
			Get(M->Indices.data(), Counts[1] * sizeof(int32_t));
			if (Counts[2] & 1)
			{
				M->UVs.resize(Counts[0]);
				Get(M->UVs.data(), Counts[0] * sizeof(geoground::Vec2f));
			}
			if (Counts[2] & 2)
			{
				M->Colors.resize(Counts[0]);
				for (geoground::Vec4f& C : M->Colors)
				{
					uint8_t Bytes[4];
					Get(Bytes, 4);
					C = geoground::Vec4f{ Bytes[0] / 255.0f, Bytes[1] / 255.0f, Bytes[2] / 255.0f, Bytes[3] / 255.0f };
				}
			}
			if (Counts[2] & 4)
			{
				M->Tags.resize(Counts[1] / 3);
				Get(M->Tags.data(), M->Tags.size() * sizeof(int32_t));
			}
		}
		return Out;
	}

	/** A built piece as the client keeps it: Core's piece (its tiles moved out and packed) and its roads by square */
	struct ClientPiece
	{
		std::shared_ptr<geoworld::Piece> Core;
		std::map<Cell, PackedTile> Tiles;
		int64_t PackedBytes = 0, UnpackedBytes = 0;
		RoadGrid Grid;
	};
	using PiecePtr = std::shared_ptr<ClientPiece>;

	/** A piece of ground or road that collides, ~60 m across: a body only near the focus. Its faces are measured from its
	 *  piece's point (Anchor, drive cm), where the body goes. */
	struct CollisionTile
	{
		Vec2 Centre;	// drive cm
		Vec2 Anchor;
		PackedVector3Array Faces;
		StaticBody3D* Body = nullptr;
		bool bRoads = false;	// a road tile's (roads and buildings), not the ground's
	};

	void FreeTiles(std::vector<CollisionTile>& Tiles)
	{
		for (CollisionTile& Tile : Tiles)
		{
			if (Tile.Body)
			{
				Tile.Body->queue_free();
				Tile.Body = nullptr;
			}
		}
	}

	void MakeTiles(FaceBuckets& Buckets, const Vec2& Anchor, std::vector<CollisionTile>& Out)
	{
		for (auto& Entry : Buckets)
		{
			CollisionTile& Tile = Out.emplace_back();
			Tile.Centre = Anchor + Vec2((Entry.first.first + 0.5) * CollisionTileSize * 100.0, (Entry.first.second + 0.5) * CollisionTileSize * 100.0);
			Tile.Anchor = Anchor;
			Tile.Faces = std::move(Entry.second);
		}
	}

	/** What a worker hands over for a ground chunk or a road tile: the Godot mesh and the collision, ready to put in (both
	 *  measured from Anchor, its piece's point) */
	struct Made
	{
		Ref<ArrayMesh> Mesh;
		Ref<ArrayMesh> Lines, Buildings;	// road tiles: the painted lines (only drawn near) and the buildings (the only part casting shadows)
		std::vector<CollisionTile> Collision;
		Vec2 Anchor;
		int64_t Triangles = 0;
	};

	/** The materials, made once on the main thread */
	struct Materials
	{
		Ref<Material> Ground;	// (plain colours, or the game's textured ground: set_ground_material)
		Ref<Material> Asphalt, Paving, Gravel;	// (plain colours, or the game's textured roads: set_road_materials)
		Ref<Material> Buildings;	// (plain, or the game's buildings shader: set_building_material)
		Ref<StandardMaterial3D> Skirts, White, Yellow, Water;
	};
}

struct GeodriveWorld::State
{
	std::string CacheDir;

	// the M map's roads, sorted once per piece (not every frame): the main roads, and the named ones longest first
	struct MapRoad
	{
		int32_t Edge = 0;
		Vec2 Mid;
		double Half = 0.0;	// cm from the middle to the further end
		double Length = 0.0;
		bool bTrunk = false;
	};
	struct MapLabel
	{
		int32_t Edge = 0;
		Vec2 Mid;
		double Half = 0.0;
		double Length = 0.0;
		String Name;
		mutable Vector2 Size;	// the name's size on screen (measured the first time it's wanted)
		mutable bool bMeasured = false;
	};
	mutable std::weak_ptr<ClientPiece> MapPiece;	// (the piece they were sorted for)
	mutable int MapFontSize = 0;
	mutable std::vector<MapRoad> MapRoads;
	mutable std::vector<MapLabel> MapLabels;
	double PieceKm = 0.0;
	double BuildingsDrawnTo = 0.0;	// m; 0 = as far as the tiles go
	bool bFollowLand = true;	// the road style (for the next piece built)
	bool bKerbs = true;
	bool bScanRoads = false;	// scan every road of each piece built (debug)
	Materials Paints;

	// --- the drive ---
	uint32_t Generation = 0;	// a new area starts a new one; anything made for an older one is dropped
	georoads::Projection Projection;
	bool bUK = false;
	georoads::CountryRules Country;	// the newest piece's country's road rules (traffic and the robot keep to its side)
	std::map<std::string, georoads::CountryRules> CountryByRegion;	// (each region's, once read)
	double StartLat = 0.0, StartLon = 0.0;
	std::vector<PiecePtr> Pieces;	// oldest first
	int32_t NextNumber = 1;
	std::string Error;

	// the piece being built (one at a time)
	std::future<PiecePtr> PieceJob;
	bool bPieceJob = false;
	bool bNextPiece = false;
	uint32_t PieceGeneration = 0;
	double LastPieceTime = -1e9;
	double LastEdgeCheck = 0.0;
	Vec2 LastNextFocus, LastNextHeading;	// where the last next piece was asked for from (cm, the drive's frame), and which way
	bool bLastNext = false;
	std::string PieceError;

	// ready: the first piece in, and nothing near the start pending for a moment
	bool bStarted = false;
	bool bReady = false;
	double QuietSince = -1.0;
	double StartedAt = 0.0;
	Vec2 Spawn, SpawnHeading = Vec2(1.0, 0.0);
	float SpawnHeight = 0.0f;
	std::string SpawnRoad;

	// files, mapped once
	std::vector<std::unique_ptr<MappedFile>> Regions;
	std::vector<std::string> RegionNames;
	std::vector<std::unique_ptr<MappedFile>> HeightFiles;
	std::vector<geoground::Archive> HeightArchives;
	bool bFilesMapped = false;
	std::set<std::string> MappedPaths;	// (every file mapped: a refresh maps only new ones)

	// what's shown
	struct Chunk
	{
		MeshInstance3D* Node = nullptr;
		Vec2 Anchor;	// its piece's point: the mesh is measured from it
		bool bNear = false;
		bool bPlain = false;	// (coarse, without the road corridors: far off)
		std::vector<CollisionTile> Tiles;
		std::weak_ptr<ClientPiece> Source;
		bool bComplete = false;
		bool bBuilding = false;
		bool bBuildingNear = false;
		bool bBuildingPlain = false;
		bool bBuildingComplete = false;
		std::weak_ptr<ClientPiece> BuildingFrom;
		std::future<Made> Job;
	};
	struct Tile
	{
		Node3D* Node = nullptr;	// roads, lines and buildings
		Vec2 Anchor;
		bool bLoaded = false;
		std::vector<CollisionTile> Tiles;
		std::weak_ptr<ClientPiece> Source;
		bool bComplete = false;
		bool bBuilding = false;
		bool bBuildingComplete = false;
		std::weak_ptr<ClientPiece> BuildingFrom;
		std::future<Made> Job;
		bool bRedo = false;	// (built again: a building of the map's taken out of it)
	};
	struct Block
	{
		MeshInstance3D* Node = nullptr;
		bool bDetail = false;	// split into chunks near the focus
		Vec2 Anchor;
		// its mesh, made on a worker (one took up to 95 ms on the main thread: a stutter as each piece's came in)
		std::future<Ref<ArrayMesh>> Job;
		bool bBuilding = false;
		Vec2 JobAnchor;
	};
	std::map<Cell, Chunk> Chunks;
	std::map<Cell, Tile> Tiles;
	std::map<Cell, Block> Blocks;
	// the map's buildings taken out (their map ids): left out of every tile (each job takes the set as it is)
	std::shared_ptr<const std::unordered_set<int64_t>> Removed = std::make_shared<const std::unordered_set<int64_t>>();
	// the plants cut down (PlantKey: its channel and fine square), left out wherever they'd be placed
	std::shared_ptr<const std::unordered_set<uint64_t>> RemovedPlants = std::make_shared<const std::unordered_set<uint64_t>>();
	MeshInstance3D* WaterNode = nullptr;
	std::weak_ptr<ClientPiece> WaterFrom;
	Vec2 WaterAnchor;
	int ChunkJobs = 0, TileJobs = 0;

	// jobs from an older area, finished off quietly
	std::vector<std::future<Made>> Leftovers;
	std::vector<std::future<PiecePtr>> LeftoverPieces;

	// where Godot's 0 is (drive cm): moved along as the car drives, so positions in Godot stay small and exact
	Vec2 RenderOrigin;
	Vector3 Render(const Vec2& P, double Height) const { return ToGodot(P - RenderOrigin, Height); }
	Vec2 Drive(const Vector3& P) const { return ToWorld(P) + RenderOrigin; }
	Vector3 At(const Vec2& Anchor) const { return Render(Anchor, 0.0); }

	Vector3 Focus;	// Godot's
	Vector3 Velocity;
	double StreamTimer = 0.0;

	// the plants' maps round the camera, for the plants' shaders (vegetation.gd, canopy.gdshader), from the pieces'
	// vegetation maps: the near one 512 x 512 cells of 10 m (5 km, every plant and the near blanket), the far one 512 x
	// 512 of 40 m (20 km, the far blanket: each cell the average of its 4 x 4 near ones). Each's corner is on its grid
	// (1 or 4 km) with the camera near the middle, made again when the camera reaches the next square or a piece comes
	// or goes. Columns run east, rows south from its north edge (as Godot's x and z)
	static constexpr int32_t PlantCells = 512;
	struct PlantWindow
	{
		double Cell = 1000.0;	// cm
		double Grid = 100000.0;	// cm: its corner snaps to this
		Ref<ImageTexture> Texture;	// RGBA8: trees, shrubs, grass, land class (as VegetationMap's)
		Ref<ImageTexture> Heights;	// RG: the ground's height (m) at each cell's middle, for standing the plants on it, and the room (m) to the nearest road edge or wall
		Vec2 Min;	// drive cm: the south-west corner
		size_t Pieces = size_t(-1);
		// (the same, kept for the plants' placing on a worker: place_plants)
		std::shared_ptr<const std::vector<uint8_t>> CpuTexels;
		std::shared_ptr<const std::vector<float>> CpuHeights;
		// the next one, made on a worker (the game never waits for it), and what it's for
		struct Made
		{
			Vec2 Min;
			size_t Pieces = 0;
			std::vector<uint8_t> Texels;
			std::vector<float> Heights;
			double Ms = 0.0;
		};
		std::future<Made> Job;
	};
	PlantWindow NearPlants{ 1000.0, 100000.0 };
	// the plants placed (place_plants: which grow round the camera, ring by ring, on a worker)
	struct PlacedTile { int32_t X = 0, Z = 0; std::vector<float> Buffer; int32_t Count = 0; float MinY = 1e9f, MaxY = -1e9f; };
	struct Placed { double AnchorX = 0.0, AnchorZ = 0.0; std::vector<int32_t> Rings; std::vector<std::vector<PlacedTile>> Tiles; double Ms = 0.0; };
	std::future<Placed> PlantJob;
	// the roads painted into the ground far off (for flying, and long views): 2048 x 2048 cells of 25 m (51 km) round
	// the camera, made on a worker when it's moved 5 km or the pieces change: every road in the map's tiles there as
	// the offset to it and its width (see UpdateRoadMap), and where it's built up (the pieces' land cover)
	static constexpr int32_t RoadCells = 2048;
	static constexpr double RoadCell = 2500.0;	// cm
	struct RoadWindow
	{
		Ref<ImageTexture> Texture;
		Vec2 Min;	// drive cm: the south-west corner
		size_t Pieces = size_t(-1);
		struct Made { Vec2 Min; size_t Pieces = 0; Ref<Image> Picture; double Ms = 0.0; int TilesRead = 0; };
		std::future<Made> Job;
	} RoadMap;
	// the road lines read from the map's tiles (drive cm), kept for the next window: only its road job reads or writes it
	struct RoadTileCache
	{
		struct Road { float HalfWidth = 2.8f; std::vector<Vec2> Points; };
		std::unordered_map<uint64_t, std::vector<Road>> Lines;
		std::deque<uint64_t> Order;
	};
	std::shared_ptr<RoadTileCache> RoadTiles;
	// the airports' runways, taxiways and aprons (set_plant_clearings), cut out of each piece's plants as it's built
	std::shared_ptr<const std::vector<geoworld::PlantClearing>> PlantClearings;
	PlantWindow FarPlants{ 4000.0, 400000.0 };

	// traffic (Core's), on the roads of the piece the player is in: the cars are GeodriveCars under the world node
	std::unique_ptr<geotraffic::TrafficHost> TrafficCars;	// (before Traffic: it outlives it)
	std::unique_ptr<geotraffic::Traffic> Traffic;
	PiecePtr TrafficPiece;
	std::string TrafficReport;	// Saved/RoadReports/traffic_<time>.csv, once something is felt

	// the robot test driver (Core's), on the roads of the piece the car was in when it started
	std::unique_ptr<geotraffic::RobotDriver> Robot;

	// the far horizon: plain ground out to the view distance beyond the built pieces, made on a worker round the focus
	double ViewDistance = 30000.0;	// m
	double DetailDistance = 7000.0;	// m: how far roads, buildings and the detailed ground are built
	std::function<void(geoground::HeightRaster&)> HorizonFill;
	MeshInstance3D* HorizonNode = nullptr;
	Vec2 HorizonCentre;
	double HorizonRadius = 0.0;
	std::string HorizonPieces;	// which pieces it was cut round
	bool bHorizonJob = false;
	uint32_t HorizonGeneration = 0;
	Vec2 HorizonJobCentre;
	double HorizonJobRadius = 0.0;
	std::string HorizonJobPieces;
	// the horizon's plant map, made with it: where the woods are out to the view distance (the near and far maps only
	// reach 10 km), for the far blanket beyond them. Cells of 200 m (more, for a view past ~55 km), 640 across, from
	// the satellite's coarse land cover (and the pieces' own maps where they are); its heights the ground's there
	static constexpr int32_t HorizonPlantCells = 640;
	static double HorizonPlantCell(double Radius) { return std::max(20000.0, std::ceil((2.0 * Radius + 2500000.0) / HorizonPlantCells / 1000.0) * 1000.0); }
	Ref<ImageTexture> HorizonPlants, HorizonPlantHeights;	// (as a PlantWindow's)
	Vec2 HorizonPlantMin;
	double HorizonPlantCellSize = 20000.0;
	struct HorizonMade
	{
		Ref<ArrayMesh> Mesh;	// (the Godot mesh made on the worker too: ~17 ms on the main thread)
		Ref<ImageTexture> Plants, PlantHeights;
		Vec2 PlantMin;
		double PlantCell = 0.0;
	};
	std::future<HorizonMade> HorizonJob;
	static void MakeHorizonPlants(const geoground::HeightRaster& Raster, const georoads::Projection& Projection, const geoground::HorizonOptions& Options,
		const std::vector<PiecePtr>& Pieces, const std::string& CacheDir, HorizonMade& Out, State& Self);
	PiecePtr RobotPiece;

	std::mutex LogLock;
	std::vector<std::string> LogLines;
	std::string LoadStage;	// what the first piece is on (for the loading screen), under LogLock
	uint32_t LoadStageGeneration = 0;

	void Log(const std::string& Line)
	{
		std::lock_guard<std::mutex> Guard(LogLock);
		LogLines.push_back(Line);
	}

	/** Pieces newest first holding the square, preferring one that has it whole */
	PiecePtr ChoosePiece(const Cell& C, bool& bWhole) const
	{
		for (auto It = Pieces.rbegin(); It != Pieces.rend(); ++It)
		{
			if ((*It)->Core->HasWhole(C))
			{
				bWhole = true;
				return *It;
			}
		}
		bWhole = false;
		for (auto It = Pieces.rbegin(); It != Pieces.rend(); ++It)
		{
			if ((*It)->Core->Has(C))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The newest piece whose box holds a point (world cm) */
	PiecePtr PieceAt(const Vec2& P) const
	{
		for (auto It = Pieces.rbegin(); It != Pieces.rend(); ++It)
		{
			if ((*It)->Core->Ground->Bounds.IsInside(P - (*It)->Core->Origin))
			{
				return *It;
			}
		}
		return Pieces.empty() ? nullptr : Pieces.back();
	}

	/** The data folder's region and height files, mapped into memory (Again: any new since: a region just made) */
	void MapFiles(bool bAgain = false)
	{
		if (bFilesMapped && !bAgain)
		{
			return;
		}
		bFilesMapped = true;
		const std::filesystem::path Cache = std::filesystem::u8path(CacheDir);
		std::error_code Ignore;
		for (const char* Folder : { "regions", "heights" })
		{
			if (!std::filesystem::is_directory(Cache / Folder, Ignore))
			{
				continue;
			}
			for (const auto& Entry : std::filesystem::directory_iterator(Cache / Folder, Ignore))
			{
				if (Entry.path().extension() != ".pmtiles" || !MappedPaths.insert(Entry.path().u8string()).second)
				{
					continue;	// (not a map, or mapped already: still in use, never mapped twice)
				}
				auto File = std::make_unique<MappedFile>(Entry.path());
				if (!File->Data)
				{
					continue;
				}
				if (std::string(Folder) == "regions")
				{
					RegionNames.push_back(Entry.path().filename().u8string());
					Regions.push_back(std::move(File));
				}
				else
				{
					HeightArchives.push_back({ File->Data, File->Size, Entry.path().filename().u8string() });
					HeightFiles.push_back(std::move(File));
				}
			}
		}
	}

	/** The country archive that holds a box, or null */
	const MappedFile* RegionFor(double South, double West, double North, double East, std::string* Name = nullptr) const
	{
		// the one whose bounds hold the box's middle (where you are: a box may reach over a border, or past a small
		// country's edges); of several, the smallest (a state's before its country's)
		auto E7 = [](double Degrees) { return int32_t(std::llround(Degrees * 1e7)); };
		const int32_t Lat = E7((South + North) * 0.5), Lon = E7((West + East) * 0.5);
		int Best = -1;
		double Smallest = 0.0;
		for (size_t i = 0; i < Regions.size(); ++i)
		{
			int32_t MinLat = 0, MinLon = 0, MaxLat = 0, MaxLon = 0;
			if (geotile::ArchiveBounds(Regions[i]->Data, Regions[i]->Size, MinLat, MinLon, MaxLat, MaxLon)
				&& Lat >= MinLat && Lat <= MaxLat && Lon >= MinLon && Lon <= MaxLon)
			{
				const double Area = double(MaxLat - MinLat) * double(MaxLon - MinLon);
				if (Best < 0 || Area < Smallest)
				{
					Best = int(i);
					Smallest = Area;
				}
			}
		}
		if (Best < 0)
		{
			return nullptr;
		}
		if (Name)
		{
			*Name = RegionNames[size_t(Best)];
		}
		return Regions[size_t(Best)].get();
	}

	double SizeKm() const
	{
		if (PieceKm > 0.0)
		{
			return std::clamp(PieceKm, 4.0, 30.0);
		}
		const Dictionary Memory = OS::get_singleton()->get_memory_info();
		const double TotalGB = double(int64_t(Memory.get("physical", int64_t(16) << 30))) / double(int64_t(1) << 30);
		return geoworld::PieceSizeKm(TotalGB);
	}

	/**
	 *  A region's country's road rules: its country from its note (<name>.region.json beside it, as geotile region
	 *  writes it; without one, the UK's box or South Africa), the built-in rules for that country, and the country's
	 *  own file over them (the game's rules/<code>.json; a rules/<code>.json in the data folder over that, for a
	 *  player's own changes)
	 */
	georoads::CountryRules RulesForRegion(const std::string& RegionFile, double Lat, double Lon)
	{
		const auto Known = CountryByRegion.find(RegionFile);
		if (Known != CountryByRegion.end())
		{
			return Known->second;
		}
		const std::string Stem = RegionFile.substr(0, RegionFile.rfind('.'));
		String Code = InUK(Lat, Lon) ? "GB" : "ZA", Part;
		const String NotePath = String::utf8(CacheDir.c_str()).path_join("regions").path_join(String::utf8(Stem.c_str()) + ".region.json");
		const Variant Note = FileAccess::file_exists(NotePath) ? JSON::parse_string(FileAccess::get_file_as_string(NotePath)) : Variant();
		if (Note.get_type() == Variant::DICTIONARY && !String(Dictionary(Note).get("country", "")).is_empty())
		{
			Code = String(Dictionary(Note).get("country", "")).to_upper();
			Part = String(Dictionary(Note).get("subdivision", "")).to_upper();	// (a state's own limits: US-CA)
		}
		georoads::CountryRules Rules = georoads::BuiltInRules(Code.utf8().get_data());
		const String Own = String::utf8(CacheDir.c_str()).path_join("rules");
		std::vector<String> Files = { String("res://rules/") + Code + ".json" };
		if (!Part.is_empty())
		{
			Files.push_back(String("res://rules/") + Part + ".json");
		}
		Files.push_back(Own.path_join(Code + ".json"));
		if (!Part.is_empty())
		{
			Files.push_back(Own.path_join(Part + ".json"));
		}
		for (const String& Path : Files)
		{
			const Variant Parsed = FileAccess::file_exists(Path) ? JSON::parse_string(FileAccess::get_file_as_string(Path)) : Variant();
			if (Parsed.get_type() != Variant::DICTIONARY)
			{
				continue;
			}
			const Dictionary D = Parsed;
			Rules.Name = String(D.get("name", String::utf8(Rules.Name.c_str()))).utf8().get_data();
			if (D.has("drive"))
			{
				Rules.bDriveLeft = String(D["drive"]) == "left";
			}
			Rules.bMph = bool(D.get("mph", Rules.bMph));
			const String Style = D.get("style", "");
			Rules.Style = Style == "uk" ? georoads::RoadStyle::UK : Style == "south_africa" ? georoads::RoadStyle::SouthAfrica : Style == "europe" ? georoads::RoadStyle::Europe : Rules.Style;
			const Dictionary Speeds = D.get("speed_kmh", Dictionary());
			for (int64_t k = 0; k < Speeds.keys().size(); ++k)
			{
				const String Class = Speeds.keys()[k];
				Rules.SpeedKmh[Class.utf8().get_data()] = float(double(Speeds[Class]));
			}
			Rules.UrbanKmh = float(double(D.get("urban_kmh", double(Rules.UrbanKmh))));
			const Dictionary Urban = D.get("urban_speed_kmh", Dictionary());
			for (int64_t k = 0; k < Urban.keys().size(); ++k)
			{
				const String Class = Urban.keys()[k];
				Rules.UrbanSpeedKmh[Class.utf8().get_data()] = float(double(Urban[Class]));
			}
			const Dictionary Sections = D.get("sections", Dictionary());
			for (int64_t k = 0; k < Sections.keys().size(); ++k)
			{
				const String Class = Sections.keys()[k];
				const Array Values = Sections[Class];
				if (Values.size() >= 3)
				{
					Rules.Sections[Class.utf8().get_data()] = { float(double(Values[0])), float(double(Values[1])), float(double(Values[2])), Values.size() >= 4 ? float(double(Values[3])) : -1.0f };
				}
			}
		}
		Log("[Country] " + RegionFile + ": " + Rules.Name + " (" + Rules.Code + (Part.is_empty() ? std::string() : std::string(", ") + Part.utf8().get_data()) + "), keeping " + (Rules.bDriveLeft ? "left" : "right") + (Rules.bMph ? ", mph" : "")
			+ (Rules.SpeedKmh.empty() ? "" : ", " + std::to_string(Rules.SpeedKmh.size()) + " road limits") + (Rules.Sections.empty() ? "" : ", " + std::to_string(Rules.Sections.size()) + " cross-sections"));
		CountryByRegion[RegionFile] = Rules;
		return Rules;
	}

	/** Builds a piece on a worker: the Core pipeline, with the height tiles read from the cache or downloaded */
	void LaunchPiece(double South, double West, double North, double East, bool bStart, const Vec2& StartNear)
	{
		std::string RegionName;
		const MappedFile* Region = RegionFor(South, West, North, East, &RegionName);
		if (Region)
		{
			Country = RulesForRegion(RegionName, (South + North) * 0.5, (West + East) * 0.5);
			bUK = Country.IsUK();
		}
		if (!Region)
		{
			// a box poking out of the country file: try a little smaller, then give up
			if (bStart)
			{
				Error = "no map data here (no region file in Cache/regions covers it)";
				Log(Error);
			}
			return;
		}
		geoworld::PieceRequest Request;
		Request.Number = NextNumber++;
		Request.South = South;
		Request.West = West;
		Request.North = North;
		Request.East = East;
		Request.Projection = Projection;
		Request.bUK = bUK;
		Request.Country = Country;
		Request.Region = Region->Data;
		Request.RegionSize = Region->Size;
		Request.HeightArchives = HeightArchives;
		Request.bStart = bStart;
		Request.StartNear = StartNear;
		Request.StartRadius = StartFillRadius * 100.0;
		Request.MaxWork = 25000.0;	// ~a city centre's: Cape Town's 12 km, London's ~6 km
		Request.MinKm = 5.0;
		Request.bFollowLand = bFollowLand;
		Request.bKerbs = bKerbs;
		Request.bScanRoads = bScanRoads;
		Request.ParallelFor = georoads::ThreadsParallelFor;
		State* Self = this;
		Request.Log = [Self](const std::string& Line) { Self->Log(Line); };
		if (bStart)
		{
			const uint32_t Gen = Generation;
			Request.Stage = [Self, Gen](const char* What)
			{
				std::lock_guard<std::mutex> Guard(Self->LogLock);
				Self->LoadStage = What;
				Self->LoadStageGeneration = Gen;
			};
		}
		if (!bStart && !Pieces.empty())
		{
			// next to the newest piece: keep the road heights already loaded from it
			Request.Previous = Pieces.back()->Core;
			const geoworld::Piece& Previous = *Pieces.back()->Core;
			for (int32_t X = Previous.MinX; X <= Previous.MaxX; ++X)
			{
				for (int32_t Y = Previous.MinY; Y <= Previous.MaxY; ++Y)
				{
					if (Previous.HasWhole({ X, Y }))
					{
						Request.PinnedCells.insert({ X, Y });
					}
				}
			}
			for (const auto& Entry : Tiles)
			{
				if (Entry.second.bLoaded && Entry.second.bComplete)
				{
					Request.PinnedCells.insert(Entry.first);
				}
			}
		}
		const std::filesystem::path TerrainDir = std::filesystem::u8path(CacheDir) / "terrain";
		Request.FillTiles = [TerrainDir, Self](geoground::HeightRaster& Raster)
		{
			std::error_code Ignore;
			std::filesystem::create_directories(TerrainDir, Ignore);
			int32_t Fetched = 0, Missing = 0;
			for (int32_t Index = 0; Index < Raster.TilesX * Raster.TilesY; ++Index)
			{
				const int X = Raster.MinTileX + Index % Raster.TilesX, Y = Raster.MinTileY + Index / Raster.TilesX;
				const std::filesystem::path File = TerrainDir / (std::to_string(Raster.Zoom) + "_" + std::to_string(X) + "_" + std::to_string(Y) + ".png");
				std::vector<uint8_t> Png = ReadAll(File);
				if (Png.empty())
				{
					char Url[256];
					std::snprintf(Url, sizeof(Url), HeightTileUrl, int(Raster.Zoom), X, Y);
					Png = Download(Url);
					if (!Png.empty())
					{
						std::ofstream(File, std::ios::binary).write(reinterpret_cast<const char*>(Png.data()), std::streamsize(Png.size()));
						++Fetched;
					}
				}
				PackedByteArray Bytes;
				Bytes.resize(int64_t(Png.size()));
				std::copy(Png.begin(), Png.end(), Bytes.ptrw());
				Ref<Image> Tile;
				Tile.instantiate();
				if (Png.empty() || Tile->load_png_from_buffer(Bytes) != OK || Tile->get_width() != 256 || Tile->get_height() != 256)
				{
					++Missing;
					continue;
				}
				Tile->convert(Image::FORMAT_RGB8);
				const PackedByteArray Pixels = Tile->get_data();
				Raster.SetTile(Index, Pixels.ptr(), 3);
			}
			if (Fetched > 0 || Missing > 0)
			{
				Self->Log("[Heights] " + std::to_string(Fetched) + " height tiles downloaded, " + std::to_string(Missing) + " missing (flat there)");
			}
		};
		HorizonFill = Request.FillTiles;	// (the far horizon reads its coarse tiles the same way)
		const std::string LandCoverDir = CacheDir;
		Request.PlantClearings = PlantClearings;
		Request.FillLandCover = [LandCoverDir, Self](geoground::LandCover& Cover)
		{
			// where plants grow, from the satellite (ESA WorldCover): only the parts this piece needs, kept for next time
			std::string Log;
			FetchWorldCover(Cover, LandCoverDir, Log);
			if (!Log.empty())
			{
				Self->Log(Log);
			}
		};
		Log("[Piece] building piece " + std::to_string(Request.Number) + " (" + RegionName + ")" + (bStart ? "" : ", next to the last"));
		bPieceJob = true;
		bNextPiece = !bStart;
		bLastNext = bLastNext && !bStart;	// (a new area: a new frame)
		PieceGeneration = Generation;
		LastPieceTime = Seconds();
		PieceJob = std::async(std::launch::async, [Request, Self]() -> PiecePtr
		{
			LowerPriority();
			std::string BuildError;
			auto Built = geoworld::BuildPiece(Request, BuildError);
			if (!Built)
			{
				Self->Log("[Piece] couldn't build it: " + BuildError);
				return nullptr;
			}
			auto Out = std::make_shared<ClientPiece>();
			Out->Core = Built;
			Out->Grid.Build(*Built->Roads);
			// the road tiles packed (they stream in a few at a time; packed they take a fraction of the memory)
			std::vector<std::pair<Cell, const geoworld::TileMeshes*>> List;
			for (const auto& Entry : Built->Tiles)
			{
				List.push_back({ Entry.first, &Entry.second });
			}
			std::vector<PackedTile> Packed(List.size());
			georoads::ThreadsParallelFor(int32_t(List.size()), [&](int32_t i) { Packed[size_t(i)] = Pack(*List[size_t(i)].second); });
			for (size_t i = 0; i < List.size(); ++i)
			{
				Out->PackedBytes += Packed[i].Bytes.size();
				Out->UnpackedBytes += Packed[i].Size;
				Out->Tiles[List[i].first] = std::move(Packed[i]);
			}
			Built->Tiles.clear();
			Self->Log("[Piece] " + std::to_string(Built->Number) + "'s road tiles packed: " + std::to_string(Out->UnpackedBytes >> 20) + " MB to " + std::to_string(Out->PackedBytes >> 20) + " MB");
			return Out;
		});
	}

	/** Lets everything shown go (a new area) */
	void Clear()
	{
		for (auto& Entry : Chunks)
		{
			if (Entry.second.Node)
			{
				Entry.second.Node->queue_free();
			}
			FreeTiles(Entry.second.Tiles);
			if (Entry.second.Job.valid())
			{
				Leftovers.push_back(std::move(Entry.second.Job));
			}
		}
		for (auto& Entry : Tiles)
		{
			if (Entry.second.Node)
			{
				Entry.second.Node->queue_free();
			}
			FreeTiles(Entry.second.Tiles);
			if (Entry.second.Job.valid())
			{
				Leftovers.push_back(std::move(Entry.second.Job));
			}
		}
		for (auto& Entry : Blocks)
		{
			if (Entry.second.Node)
			{
				Entry.second.Node->queue_free();
			}
		}
		if (WaterNode)
		{
			WaterNode->queue_free();
			WaterNode = nullptr;
		}
		if (HorizonNode)
		{
			HorizonNode->queue_free();
			HorizonNode = nullptr;
			HorizonRadius = 0.0;
		}
		Chunks.clear();
		Tiles.clear();
		Blocks.clear();
		ChunkJobs = TileJobs = 0;
		if (bPieceJob)
		{
			LeftoverPieces.push_back(std::move(PieceJob));
			bPieceJob = false;
		}
		Traffic.reset();
		TrafficPiece = nullptr;
		// the pieces' memory goes on a worker (freeing a city's worth on the main thread would hitch)
		std::vector<PiecePtr> Old = std::move(Pieces);
		Pieces.clear();
		RoadTiles.reset();
		std::thread([Old = std::move(Old)]() mutable { Old.clear(); }).detach();
	}
};

namespace
{
	struct GodotTrafficCars : public geotraffic::TrafficHost
	{
		GeodriveWorld* Owner = nullptr;
		GeodriveWorld::State* S = nullptr;
		Vec2 Origin;	// the roads' frame: their piece's point
		std::map<int32_t, ObjectID> Cars;
		int32_t NextId = 1;

		GeodriveCar* Find(int32_t Id) const
		{
			const auto Found = Cars.find(Id);
			return Found == Cars.end() ? nullptr : Object::cast_to<GeodriveCar>(ObjectDB::get_instance(Found->second));
		}

		int32_t SpawnCar(const Vec2& Position, double Height, const Vec2& Heading, int32_t Body, int32_t Paint, float Speed, bool bParked) override;
		void RemoveCar(int32_t Id) override
		{
			if (GeodriveCar* Car = Find(Id))
			{
				Car->queue_free();
			}
			Cars.erase(Id);
		}
		bool GetCar(int32_t Id, geotraffic::CarPose& Out) const override;
		void DriveCar(int32_t Id, float SteerDegrees, float Throttle, float Brake) override
		{
			if (GeodriveCar* Car = Find(Id))
			{
				Car->set_input(SteerDegrees / std::max(Car->get_max_steer_degrees(), 1.0), Throttle, Brake, 0.0);
			}
		}
	};
}

GeodriveWorld::GeodriveWorld()
	: World(std::make_unique<State>())
{
	// Core's thread pool: all but two cores (the game and Godot's own threads keep those), below normal priority
	static std::once_flag PoolSet;
	std::call_once(PoolSet, []
	{
		const int32_t Cores = int32_t(std::max(1u, std::thread::hardware_concurrency()));
		georoads::ConfigureThreadsParallelFor(std::max(1, Cores - 2), LowerPriority);
	});
	Materials& M = World->Paints;
	M.Ground = Paint(1, 1, 1, 1.0f, true);
	M.Asphalt = Paint(0.025f, 0.025f, 0.028f, 0.9f);
	M.Paving = Paint(0.13f, 0.11f, 0.09f, 0.9f);
	M.Gravel = Paint(0.25f, 0.17f, 0.09f, 0.95f);
	M.Skirts = Paint(0.03f, 0.03f, 0.03f, 0.95f);
	M.White = Paint(0.8f, 0.8f, 0.78f, 0.6f);
	M.Yellow = Paint(0.8f, 0.55f, 0.05f, 0.6f);
	M.Buildings = Paint(0.55f, 0.52f, 0.48f, 0.9f);
	M.Water = Paint(0.02f, 0.06f, 0.09f, 0.15f);
}

GeodriveWorld::~GeodriveWorld()
{
	// jobs still running finish before the pieces they read go
	State& S = *World;
	for (auto& Entry : S.Chunks)
	{
		if (Entry.second.Job.valid())
		{
			Entry.second.Job.wait();
		}
	}
	for (auto& Entry : S.Tiles)
	{
		if (Entry.second.Job.valid())
		{
			Entry.second.Job.wait();
		}
	}
	for (auto& Job : S.Leftovers)
	{
		Job.wait();
	}
	for (auto& Job : S.LeftoverPieces)
	{
		Job.wait();
	}
	if (S.bPieceJob)
	{
		S.PieceJob.wait();
	}
}

void GeodriveWorld::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("set_cache_dir", "dir"), &GeodriveWorld::set_cache_dir);
	ClassDB::bind_method(D_METHOD("get_cache_dir"), &GeodriveWorld::get_cache_dir);
	ClassDB::bind_method(D_METHOD("set_piece_km", "km"), &GeodriveWorld::set_piece_km);
	ClassDB::bind_method(D_METHOD("get_piece_km"), &GeodriveWorld::get_piece_km);
	ClassDB::bind_method(D_METHOD("set_building_distance", "metres"), &GeodriveWorld::set_building_distance);
	ClassDB::bind_method(D_METHOD("get_building_distance"), &GeodriveWorld::get_building_distance);
	ClassDB::bind_method(D_METHOD("start_area", "lat", "lon"), &GeodriveWorld::start_area);
	ClassDB::bind_method(D_METHOD("is_ready"), &GeodriveWorld::is_ready);
	ClassDB::bind_method(D_METHOD("is_loading"), &GeodriveWorld::is_loading);
	ClassDB::bind_method(D_METHOD("get_error"), &GeodriveWorld::get_error);
	ClassDB::bind_method(D_METHOD("get_spawn"), &GeodriveWorld::get_spawn);
	ClassDB::bind_method(D_METHOD("set_focus", "position", "velocity"), &GeodriveWorld::set_focus);
	ClassDB::bind_method(D_METHOD("is_solid_at", "position"), &GeodriveWorld::is_solid_at);
	ClassDB::bind_method(D_METHOD("get_status"), &GeodriveWorld::get_status);
	ClassDB::bind_method(D_METHOD("get_loading_status"), &GeodriveWorld::get_loading_status);
	ClassDB::bind_method(D_METHOD("road_lines", "position", "radius"), &GeodriveWorld::road_lines);
	ClassDB::bind_method(D_METHOD("get_attribution"), &GeodriveWorld::get_attribution);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("is_uk", "lat", "lon"), &GeodriveWorld::is_uk);
	ClassDB::bind_method(D_METHOD("get_country"), &GeodriveWorld::get_country);
	ClassDB::bind_method(D_METHOD("has_map_at", "lat", "lon"), &GeodriveWorld::has_map_at);
	ClassDB::bind_method(D_METHOD("refresh_maps"), &GeodriveWorld::refresh_maps);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("get_game_memory"), &GeodriveWorld::get_game_memory);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("get_game_cpu"), &GeodriveWorld::get_game_cpu);
	ClassDB::bind_method(D_METHOD("get_built_boxes"), &GeodriveWorld::get_built_boxes);
	ClassDB::bind_method(D_METHOD("draw_map", "canvas_item", "camera", "font", "font_size"), &GeodriveWorld::draw_map);
	ClassDB::bind_method(D_METHOD("update_traffic", "delta", "position", "forward", "velocity", "wanted"), &GeodriveWorld::update_traffic);
	ClassDB::bind_method(D_METHOD("get_traffic_status"), &GeodriveWorld::get_traffic_status);
	ClassDB::bind_method(D_METHOD("get_traffic_debug"), &GeodriveWorld::get_traffic_debug);
	ClassDB::bind_method(D_METHOD("get_traffic_count"), &GeodriveWorld::get_traffic_count);
	ClassDB::bind_method(D_METHOD("clear_traffic"), &GeodriveWorld::clear_traffic);
	ClassDB::bind_method(D_METHOD("get_start"), &GeodriveWorld::get_start);
	ClassDB::bind_method(D_METHOD("nearest_road", "position", "max_distance", "drivable_only"), &GeodriveWorld::nearest_road, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("to_lat_lon", "position"), &GeodriveWorld::to_lat_lon);
	ClassDB::bind_method(D_METHOD("from_lat_lon", "lat", "lon"), &GeodriveWorld::from_lat_lon);
	ClassDB::bind_method(D_METHOD("contains_lat_lon", "lat", "lon"), &GeodriveWorld::contains_lat_lon);
	ClassDB::bind_method(D_METHOD("building_at", "position"), &GeodriveWorld::building_at);
	ClassDB::bind_method(D_METHOD("remove_building", "id"), &GeodriveWorld::remove_building);
	ClassDB::bind_method(D_METHOD("set_removed_buildings", "ids"), &GeodriveWorld::set_removed_buildings);
	ClassDB::bind_method(D_METHOD("remove_plant", "channel", "fx", "fz"), &GeodriveWorld::remove_plant);
	ClassDB::bind_method(D_METHOD("set_removed_plants", "plants"), &GeodriveWorld::set_removed_plants);
	ClassDB::bind_method(D_METHOD("get_removed_plant_count"), &GeodriveWorld::get_removed_plant_count);
	ClassDB::bind_method(D_METHOD("get_removed_buildings"), &GeodriveWorld::get_removed_buildings);
	ClassDB::bind_method(D_METHOD("ground_height", "position"), &GeodriveWorld::ground_height);
	ClassDB::bind_method(D_METHOD("collision_status", "position"), &GeodriveWorld::collision_status);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("areas"), &GeodriveWorld::areas);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "cache_dir"), "set_cache_dir", "get_cache_dir");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "piece_km"), "set_piece_km", "get_piece_km");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "building_distance"), "set_building_distance", "get_building_distance");
	ClassDB::bind_method(D_METHOD("set_verbose", "on"), &GeodriveWorld::set_verbose);
	ClassDB::bind_method(D_METHOD("set_ground_material", "material"), &GeodriveWorld::set_ground_material);
	ClassDB::bind_method(D_METHOD("set_road_materials", "asphalt", "paving", "gravel"), &GeodriveWorld::set_road_materials);
	ClassDB::bind_method(D_METHOD("set_building_material", "material"), &GeodriveWorld::set_building_material);
	ClassDB::bind_method(D_METHOD("get_plant_texture", "far"), &GeodriveWorld::get_plant_texture, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_plant_origin", "far"), &GeodriveWorld::get_plant_origin, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_plant_size", "far"), &GeodriveWorld::get_plant_size, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_plant_heights", "far"), &GeodriveWorld::get_plant_heights, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("place_plants", "rings", "eye", "cape"), &GeodriveWorld::place_plants);
	ClassDB::bind_method(D_METHOD("take_plants"), &GeodriveWorld::take_plants);
	ClassDB::bind_method(D_METHOD("get_horizon_plants", "heights"), &GeodriveWorld::get_horizon_plants, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_horizon_plant_origin"), &GeodriveWorld::get_horizon_plant_origin);
	ClassDB::bind_method(D_METHOD("get_horizon_plant_size"), &GeodriveWorld::get_horizon_plant_size);
	ClassDB::bind_method(D_METHOD("get_render_offset"), &GeodriveWorld::get_render_offset);
	ClassDB::bind_method(D_METHOD("get_road_texture"), &GeodriveWorld::get_road_texture);
	ClassDB::bind_method(D_METHOD("get_road_origin"), &GeodriveWorld::get_road_origin);
	ClassDB::bind_method(D_METHOD("get_road_size"), &GeodriveWorld::get_road_size);
	ClassDB::bind_method(D_METHOD("set_plant_clearings", "clearings"), &GeodriveWorld::set_plant_clearings);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("make_plant_mesh", "kind", "seed", "detail"), &GeodriveWorld::make_plant_mesh);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("body_measures", "height_cm"), &GeodriveWorld::body_measures);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("make_aircraft_mesh", "kind", "gear"), &GeodriveWorld::make_aircraft_mesh);
	ClassDB::bind_static_method("GeodriveWorld", D_METHOD("walk", "height_cm", "forward", "right", "run", "sprint", "jump", "yaw", "on_ground", "velocity", "delta"), &GeodriveWorld::walk);
	ClassDB::bind_method(D_METHOD("get_verbose"), &GeodriveWorld::get_verbose);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "verbose"), "set_verbose", "get_verbose");
	ClassDB::bind_method(D_METHOD("set_detail_distance", "metres"), &GeodriveWorld::set_detail_distance);
	ClassDB::bind_method(D_METHOD("get_detail_distance"), &GeodriveWorld::get_detail_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail_distance"), "set_detail_distance", "get_detail_distance");
	ClassDB::bind_method(D_METHOD("set_view_distance", "metres"), &GeodriveWorld::set_view_distance);
	ClassDB::bind_method(D_METHOD("get_view_distance"), &GeodriveWorld::get_view_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_distance"), "set_view_distance", "get_view_distance");
	ClassDB::bind_method(D_METHOD("set_follow_land", "on"), &GeodriveWorld::set_follow_land);
	ClassDB::bind_method(D_METHOD("get_follow_land"), &GeodriveWorld::get_follow_land);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_land"), "set_follow_land", "get_follow_land");
	ClassDB::bind_method(D_METHOD("set_kerbs", "on"), &GeodriveWorld::set_kerbs);
	ClassDB::bind_method(D_METHOD("get_kerbs"), &GeodriveWorld::get_kerbs);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "kerbs"), "set_kerbs", "get_kerbs");
	ClassDB::bind_method(D_METHOD("set_scan_roads", "on"), &GeodriveWorld::set_scan_roads);
	ClassDB::bind_method(D_METHOD("get_scan_roads"), &GeodriveWorld::get_scan_roads);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "scan_roads"), "set_scan_roads", "get_scan_roads");
	ADD_SIGNAL(MethodInfo("origin_shifted", PropertyInfo(Variant::VECTOR3, "offset")));
	ADD_SIGNAL(MethodInfo("robot_place_car", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::VECTOR3, "facing")));
	ADD_SIGNAL(MethodInfo("robot_screenshot", PropertyInfo(Variant::INT, "problem")));
	ClassDB::bind_method(D_METHOD("start_robot", "car", "mode", "town_routes", "shard", "shards"), &GeodriveWorld::start_robot, DEFVAL(0), DEFVAL(1));
	ClassDB::bind_method(D_METHOD("update_robot", "delta", "car"), &GeodriveWorld::update_robot);
	ClassDB::bind_method(D_METHOD("stop_robot"), &GeodriveWorld::stop_robot);
	ClassDB::bind_method(D_METHOD("is_robot_driving"), &GeodriveWorld::is_robot_driving);
	ClassDB::bind_method(D_METHOD("get_robot_status"), &GeodriveWorld::get_robot_status);
}

void GeodriveWorld::set_cache_dir(const String& Dir) { World->CacheDir = Dir.utf8().get_data(); }
String GeodriveWorld::get_cache_dir() const { return String::utf8(World->CacheDir.c_str()); }
void GeodriveWorld::set_piece_km(double Km) { World->PieceKm = Km; }
double GeodriveWorld::get_piece_km() const { return World->PieceKm; }
double GeodriveWorld::get_building_distance() const { return World->BuildingsDrawnTo; }
void GeodriveWorld::set_follow_land(bool bOn) { World->bFollowLand = bOn; }
bool GeodriveWorld::get_follow_land() const { return World->bFollowLand; }
void GeodriveWorld::set_kerbs(bool bOn) { World->bKerbs = bOn; }
bool GeodriveWorld::get_kerbs() const { return World->bKerbs; }
void GeodriveWorld::set_view_distance(double Metres) { World->ViewDistance = std::clamp(Metres, 1000.0, 100000.0); }
double GeodriveWorld::get_view_distance() const { return World->ViewDistance; }
void GeodriveWorld::set_detail_distance(double Metres) { World->DetailDistance = std::clamp(Metres, 2000.0, 15000.0); }
double GeodriveWorld::get_detail_distance() const { return World->DetailDistance; }
void GeodriveWorld::set_scan_roads(bool bOn) { World->bScanRoads = bOn; }
bool GeodriveWorld::get_scan_roads() const { return World->bScanRoads; }

void GeodriveWorld::set_building_distance(double Metres)
{
	State& S = *World;
	S.BuildingsDrawnTo = std::max(0.0, Metres);
	for (auto& Entry : S.Tiles)
	{
		if (MeshInstance3D* Buildings = Entry.second.Node ? Object::cast_to<MeshInstance3D>(Entry.second.Node->get_node_or_null(NodePath("Buildings"))) : nullptr)
		{
			const float To = float(S.BuildingsDrawnTo);
			Buildings->set_visibility_range_end(To);
			Buildings->set_visibility_range_end_margin(To * 0.1f);
			Buildings->set_visibility_range_fade_mode(To > 0.0f ? GeometryInstance3D::VISIBILITY_RANGE_FADE_SELF : GeometryInstance3D::VISIBILITY_RANGE_FADE_DISABLED);
		}
	}
}
bool GeodriveWorld::is_ready() const { return World->bReady; }
bool GeodriveWorld::is_loading() const { return World->bStarted && !World->bReady && World->Error.empty(); }
String GeodriveWorld::get_error() const { return String::utf8(World->Error.c_str()); }

void GeodriveWorld::set_focus(const Vector3& Position, const Vector3& Velocity)
{
	// (while a new area loads it streams round the start)
	if (!World->bReady)
	{
		return;
	}
	World->Focus = Position;
	World->Velocity = Velocity;
}

void GeodriveWorld::start_area(double Lat, double Lon)
{
	State& S = *World;
	S.MapFiles();
	S.Clear();
	++S.Generation;
	S.Error.clear();
	S.bStarted = true;
	S.bReady = false;
	S.QuietSince = -1.0;
	S.StartedAt = Seconds();
	S.StartLat = Lat;
	S.StartLon = Lon;
	S.Projection = georoads::Projection(Lat, Lon);
	S.RenderOrigin = Vec2();
	S.bUK = InUK(Lat, Lon);
	S.Focus = Vector3();
	double South, West, North, East;
	geoworld::BoxAround(Lat, Lon, S.SizeKm(), South, West, North, East);
	S.LaunchPiece(South, West, North, East, true, Vec2(0.0, 0.0));
}

Dictionary GeodriveWorld::get_spawn() const
{
	const State& S = *World;
	Dictionary Out;
	Out["position"] = S.Render(S.Spawn, S.SpawnHeight);
	Out["heading"] = Vector3(float(S.SpawnHeading.Y), 0.0f, float(-S.SpawnHeading.X));
	Out["road"] = String::utf8(S.SpawnRoad.c_str());
	return Out;
}

void GeodriveWorld::_process(double Delta)
{

	State& S = *World;
	// how far the world is built round the focus, from the detail distance (the Unreal game's at the usual 7 km): roads and
	// buildings to it, the 1 km ground chunks a km further, the 10 m ground within 2.5 km (always), an old piece kept while it
	// has squares that near
	double TileLoadRadius = S.DetailDistance;
	const double TileUnloadRadius = S.DetailDistance + 2000.0;
	double DetailRadius = S.DetailDistance + 1000.0;
	const double DetailKeepRadius = S.DetailDistance + 3000.0;
	double NearRadius = 1200.0, NearKeepRadius = 1400.0;	// (a square by its middle; less high up: see Over. Past ~1.2 km a full square's ~250,000 triangles are under a few pixels each: the coarse ones, their road corridors coarser, are as good to the eye. Kept fine till 1.4 km, not 2: driving, up to 9 had been, ~2 ms of the frame for 4)
	const double KeepNeededRadius = S.DetailDistance;
	const double FrameStart = Seconds();
	// (once the area is in: while loading it may take more on purpose)
	struct SlowCheck
	{
		double Start;
		const bool& bReady;
		bool bVerbose;
		~SlowCheck()
		{
			const double Ms = (Seconds() - Start) * 1000.0;
			if (Ms > 12.0 && bReady && bVerbose)
			{
				UtilityFunctions::print(vformat("Geodrive: the world took %.1f ms of a frame", Ms));
			}
		}
	} Check{ FrameStart, S.bReady, bVerbose };
	{
		// the log, a few lines a frame (the road solver writes hundreds at once, and printing them all in one frame hitched)
		std::lock_guard<std::mutex> Guard(S.LogLock);
		const size_t Lines = std::min<size_t>(S.LogLines.size(), 6);
		for (size_t i = 0; i < Lines; ++i)
		{
			if (!bVerbose && IsDetail(S.LogLines[i]))
			{
				continue;
			}
			UtilityFunctions::print(String::utf8(("Geodrive: " + S.LogLines[i]).c_str()));
		}
		S.LogLines.erase(S.LogLines.begin(), S.LogLines.begin() + std::ptrdiff_t(Lines));
	}

	// leftovers from an older area: dropped once done
	auto Done = [](auto& Future) { return Future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; };
	S.Leftovers.erase(std::remove_if(S.Leftovers.begin(), S.Leftovers.end(), [&](auto& F) { return Done(F); }), S.Leftovers.end());
	S.LeftoverPieces.erase(std::remove_if(S.LeftoverPieces.begin(), S.LeftoverPieces.end(), [&](auto& F) { return Done(F); }), S.LeftoverPieces.end());

	// --- a piece finished ---
	if (S.bPieceJob && Done(S.PieceJob))
	{
		PiecePtr Piece = S.PieceJob.get();
		S.bPieceJob = false;
		if (!Piece)
		{
			if (!S.bNextPiece)
			{
				S.Error = "couldn't build the area (see the log)";
			}
		}
		else
		{
			S.Pieces.push_back(Piece);
			if (Piece->Core->bScanned)
			{
				// every fault the road scan found, to compare runs or put on a map (as the Unreal game writes them)
				const String Folder = ReportsFolder();
				std::filesystem::create_directories(std::filesystem::u8path(Folder.utf8().get_data()));
				const String Stamp = Time::get_singleton()->get_datetime_string_from_system().replace(":", "").replace("-", "").replace("T", "_");
				const String Path = Folder.path_join(vformat("scan_%s_piece%d.csv", Stamp, Piece->Core->Number));
				std::ofstream(std::filesystem::u8path(Path.utf8().get_data()), std::ios::binary) << geoworld::RoadScanCsv(*Piece->Core->Roads, *Piece->Core->Ground, Piece->Core->Scan);
				S.Log("[RoadScan] every fault written to " + std::string(Path.utf8().get_data()));
			}
			if (!S.bNextPiece)
			{
				S.Spawn = Piece->Core->Spawn;
				S.SpawnHeading = Piece->Core->SpawnHeading;
				S.SpawnHeight = Piece->Core->SpawnHeight;
				S.SpawnRoad = Piece->Core->SpawnRoad;
				S.RenderOrigin = Vec2(std::round(S.Spawn.X / 100000.0) * 100000.0, std::round(S.Spawn.Y / 100000.0) * 100000.0);
				S.Focus = S.Render(S.Spawn, S.SpawnHeight);
				UtilityFunctions::print(vformat("Geodrive: area ready to stream in %.1f s: %s; start on %s", Seconds() - S.StartedAt,
					String::utf8(Piece->Core->Summary.c_str()), String::utf8(S.SpawnRoad.c_str())));
			}
			// the newest piece's water
			if (S.WaterNode)
			{
				S.WaterNode->queue_free();
			}
			Ref<ArrayMesh> Water;
			Water.instantiate();
			AddSurface(Water, Piece->Core->Water, false, S.Paints.Water);
			S.WaterNode = memnew(MeshInstance3D);
			S.WaterNode->set_name("Water");
			S.WaterNode->set_layer_mask(1u << 16);
			S.WaterNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			S.WaterNode->set_mesh(Water);
			S.WaterAnchor = Piece->Core->Origin;
			S.WaterNode->set_position(S.At(S.WaterAnchor));
			add_child(S.WaterNode);
		}
	}
	if (S.Pieces.empty())
	{
		return;
	}

	const bool bLoading = !S.bReady;
	const double Budget = (bLoading ? LoadingBudgetMs : BudgetMs) / 1000.0;
	const Vec2 Focus = S.Drive(S.Focus);
	// how high the focus is over the ground (flying): less fine ground is *loaded* from 300 m up, none from 1,500 (from up
	// there it's no finer than the 1 km chunks look), and no new collision past ~900 m (nothing to touch); what's loaded
	// stays until it's as far behind as when driving (so climbing never takes anything away)
	double Over = 0.0;
	{
		const PiecePtr Under = S.PieceAt(Focus);
		if (Under && Under->Core && Under->Core->Ground)
		{
			Over = S.Focus.y - Under->Core->Ground->BareHeight(Focus - Under->Core->Origin) / 100.0;
		}
	}
	const double HighUp = std::clamp((Over - 300.0) / 1200.0, 0.0, 1.0);
	NearRadius *= 1.0 - HighUp;
	// flying high, the level of detail a flight sim has: no new 3D roads and buildings (the painted roads and towns show
	// them) and no 1 km ground chunks (the far ground's blocks do), from 300 m up and none from 500; what's loaded stays
	// till it's as far behind as ever, and it all fills in again coming down. (At speed, the ring they load in at was a
	// churn of tiles popping in 7 km ahead, and the ground behind it fell behind: patches missing)
	const double Flying = std::clamp((Over - 300.0) / 200.0, 0.0, 1.0);
	TileLoadRadius *= 1.0 - Flying;
	DetailRadius *= 1.0 - Flying;
	const float CollisionReach = std::max(0.0f, CollisionRadius - std::max(0.0f, float(Over) - 300.0f));
	auto Distance = [&Focus](const Cell& C)
	{
		const double Size = geoground::Terrain::ChunkSize;
		return (Vec2((C.first + 0.5) * Size, (C.second + 0.5) * Size) - Focus).Size() / 100.0;
	};
	auto OverBudget = [&] { return Seconds() - FrameStart > Budget; };

	// --- finished chunks and tiles go in, nearest first within the budget (at least one a frame) ---
	{
		struct ReadyItem { double Distance; Cell Key; bool bTile; };
		std::vector<ReadyItem> Ready;
		for (auto& Entry : S.Chunks)
		{
			if (Entry.second.bBuilding && Done(Entry.second.Job))
			{
				Ready.push_back({ Distance(Entry.first), Entry.first, false });
			}
		}
		for (auto& Entry : S.Tiles)
		{
			if (Entry.second.bBuilding && Done(Entry.second.Job))
			{
				Ready.push_back({ Distance(Entry.first), Entry.first, true });
			}
		}
		std::sort(Ready.begin(), Ready.end(), [](const ReadyItem& A, const ReadyItem& B) { return A.Distance < B.Distance; });
		bool bFirst = true;
		for (const ReadyItem& Item : Ready)
		{
			if (!bFirst && OverBudget())
			{
				break;
			}
			bFirst = false;
			if (!Item.bTile)
			{
				State::Chunk& C = S.Chunks[Item.Key];
				Made Result = C.Job.get();
				C.bBuilding = false;
				--S.ChunkJobs;
				if (!C.Node)
				{
					C.Node = memnew(MeshInstance3D);
					C.Node->set_name(vformat("Ground_%d_%d", Item.Key.first, Item.Key.second));
					C.Node->set_material_override(S.Paints.Ground);
					C.Node->set_layer_mask(1u << 10);	// (the render layers: what's what, for the graphics card's sums and settings)
					C.Node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);	// (the ground needn't shadow itself: the shadow pass would draw it all again)
					add_child(C.Node);
				}
				C.Node->set_mesh(Result.Mesh);
				C.Anchor = Result.Anchor;
				C.Node->set_position(S.At(C.Anchor));
				C.bNear = C.bBuildingNear;
				C.bPlain = C.bBuildingPlain;
				C.Source = C.BuildingFrom;
				C.bComplete = C.bBuildingComplete;
				FreeTiles(C.Tiles);
				C.Tiles = std::move(Result.Collision);
			}
			else
			{
				State::Tile& T = S.Tiles[Item.Key];
				Made Result = T.Job.get();
				T.bBuilding = false;
				--S.TileJobs;
				if (T.Node)
				{
					T.Node->queue_free();
					T.Node = nullptr;
				}
				if ((Result.Mesh.is_valid() && Result.Mesh->get_surface_count() > 0) || (Result.Buildings.is_valid() && Result.Buildings->get_surface_count() > 0))
				{
					T.Node = memnew(Node3D);
					T.Node->set_name(vformat("Roads_%d_%d", Item.Key.first, Item.Key.second));
					T.Node->set_position(S.At(Result.Anchor));
					auto Part = [&T](const Ref<ArrayMesh>& Mesh, bool bShadows, float DrawnTo, const char* Name)
					{
						if (Mesh.is_null() || Mesh->get_surface_count() == 0)
						{
							return;
						}
						MeshInstance3D* Node = memnew(MeshInstance3D);
						Node->set_mesh(Mesh);
						Node->set_name(Name);
						Node->set_layer_mask(1u << (Name[0] == 'R' ? 11 : Name[0] == 'L' ? 12 : 13));	// (roads, their lines, buildings)
						Node->set_cast_shadows_setting(bShadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
						if (DrawnTo > 0.0f)
						{
							Node->set_visibility_range_end(DrawnTo);
							Node->set_visibility_range_end_margin(DrawnTo * 0.1f);
							Node->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_SELF);
						}
						T.Node->add_child(Node);
					};
					Part(Result.Mesh, false, RoadsDrawnTo, "Roads");
					Part(Result.Lines, false, LinesDrawnTo, "Lines");
					Part(Result.Buildings, true, float(S.BuildingsDrawnTo), "Buildings");
					add_child(T.Node);
				}
				T.Anchor = Result.Anchor;
				T.bLoaded = true;
				T.Source = T.BuildingFrom;
				T.bComplete = T.bBuildingComplete;
				FreeTiles(T.Tiles);
				T.Tiles = std::move(Result.Collision);
			}
		}
	}

	// --- collision near the focus: bodies for the nearest tiles, a few a frame; far ones let go ---
	{
		std::vector<std::pair<float, CollisionTile*>> Missing;
		auto Look = [&](std::vector<CollisionTile>& List)
		{
			for (CollisionTile& Tile : List)
			{
				const float D = float((Tile.Centre - Focus).Size() / 100.0);
				if (!Tile.Body && D < CollisionReach)
				{
					Missing.push_back({ D, &Tile });
				}
				else if (Tile.Body && D > CollisionKeepRadius)
				{
					Tile.Body->queue_free();
					Tile.Body = nullptr;
				}
			}
		};
		for (auto& Entry : S.Chunks)
		{
			Look(Entry.second.Tiles);
		}
		for (auto& Entry : S.Tiles)
		{
			for (CollisionTile& T : Entry.second.Tiles)
			{
				T.bRoads = true;
			}
			Look(Entry.second.Tiles);
		}
		std::sort(Missing.begin(), Missing.end(), [](const auto& A, const auto& B) { return A.first < B.first; });
		bool bFirst = true;
		for (const auto& Want : Missing)
		{
			// while loading, everything near the start goes in now
			if (!bFirst && OverBudget() && !(bLoading && Want.first < SpawnSolidRadius))
			{
				break;
			}
			bFirst = false;
			Want.second->Body = MakeCollision(Want.second->Faces);
			static int64_t BodyNumber = 0;	// (names must be unique, or Godot makes them @StaticBody3D@...)
			Want.second->Body->set_name(String(Want.second->bRoads ? "RoadsAndBuildings" : "Ground") + String::num_int64(++BodyNumber));	// (what a car hit, for the knock log)
			Want.second->Body->set_position(S.At(Want.second->Anchor));
			add_child(Want.second->Body);
		}
	}

	// --- what's wanted round the focus (ten times a second) ---
	S.StreamTimer -= Delta;
	if (S.StreamTimer <= 0.0)
	{
		S.StreamTimer = 0.1;
		UpdateRoadMap();
		if (!UpdatePlantMap(false))
		{
			UpdatePlantMap(true);	// (not both in one tick)
		}
		const double Size = geoground::Terrain::ChunkSize;
		const double BlockSize = Size * geoground::Terrain::ChunksPerBlock;

		// distant blocks: from the newest piece that has each; split into chunks near the focus
		int32_t BlockJobs = 0;
		for (const auto& Entry : S.Blocks)
		{
			BlockJobs += Entry.second.bBuilding ? 1 : 0;
		}
		std::map<Cell, PiecePtr> BlockSource;
		for (const PiecePtr& P : S.Pieces)
		{
			for (const auto& Entry : P->Core->DistantBlocks)
			{
				BlockSource[Entry.first] = P;
			}
		}
		std::set<Cell> DetailBlocks;
		for (const auto& Entry : BlockSource)
		{
			const Cell& Key = Entry.first;
			const double D = (Vec2((Key.first + 0.5) * BlockSize, (Key.second + 0.5) * BlockSize) - Focus).Size() / 100.0;
			State::Block& B = S.Blocks[Key];
			B.bDetail = B.bDetail ? D < DetailKeepRadius : D < DetailRadius;
			if (B.bDetail)
			{
				DetailBlocks.insert(Key);
			}
			if (D > DistantRadius)
			{
				if (B.Node)
				{
					B.Node->queue_free();
					B.Node = nullptr;
				}
				continue;
			}
			if (!B.Node && !B.bBuilding && bLoading)
			{
				// (loading: at once, behind the loading screen, so the far land is all there when it lifts)
				Ref<ArrayMesh> Mesh;
				Mesh.instantiate();
				AddSurface(Mesh, Entry.second->Core->DistantBlocks.at(Key), true, Ref<Material>());
				B.Node = memnew(MeshInstance3D);
				B.Node->set_name(vformat("Distant_%d_%d", Key.first, Key.second));
				B.Node->set_material_override(S.Paints.Ground);
				B.Node->set_layer_mask(1u << 14);
				B.Node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				B.Node->set_mesh(Mesh);
				B.Anchor = Entry.second->Core->Origin;
				B.Node->set_position(S.At(B.Anchor));
				add_child(B.Node);
			}
			if (!B.Node && !B.bBuilding && BlockJobs < 4)
			{
				PiecePtr From = Entry.second;	// (kept alive while the worker reads it)
				B.Job = std::async(std::launch::async, [From, Key]() mutable
				{
					LowerPriority();
					Ref<ArrayMesh> Mesh;
					Mesh.instantiate();
					AddSurface(Mesh, From->Core->DistantBlocks.at(Key), true, Ref<Material>());
					From.reset();	// (on this thread, if it's the piece's last hold: see the plant maps' job)
					return Mesh;
				});
				B.bBuilding = true;
				B.JobAnchor = From->Core->Origin;
				++BlockJobs;
			}
			if (!B.Node && B.bBuilding && B.Job.wait_for(std::chrono::seconds(0)) == std::future_status::ready && !OverBudget())
			{
				const Ref<ArrayMesh> Mesh = B.Job.get();
				B.bBuilding = false;
				--BlockJobs;
				B.Node = memnew(MeshInstance3D);
				B.Node->set_name(vformat("Distant_%d_%d", Key.first, Key.second));
				B.Node->set_material_override(S.Paints.Ground);
				B.Node->set_layer_mask(1u << 14);
				B.Node->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				B.Node->set_mesh(Mesh);
				B.Anchor = B.JobAnchor;
				B.Node->set_position(S.At(B.Anchor));
				add_child(B.Node);
			}
		}

		// chunks: inside detail blocks; near or far by distance; gone outside them
		struct Want { double Distance; Cell Key; bool bNear; bool bPlain; };
		std::vector<Want> Wanted;
		std::set<Cell> Keep;
		for (const Cell& BlockKey : DetailBlocks)
		{
			for (int32_t dx = 0; dx < geoground::Terrain::ChunksPerBlock; ++dx)
			{
				for (int32_t dy = 0; dy < geoground::Terrain::ChunksPerBlock; ++dy)
				{
					const Cell Key(BlockKey.first * geoground::Terrain::ChunksPerBlock + dx, BlockKey.second * geoground::Terrain::ChunksPerBlock + dy);
					bool bWhole = false;
					if (!S.ChoosePiece(Key, bWhole))
					{
						continue;
					}
					Keep.insert(Key);
					const double D = Distance(Key);
					const auto Found = S.Chunks.find(Key);
					const bool bWasNear = Found != S.Chunks.end() && Found->second.Node && Found->second.bNear;
					const bool bWasPlain = Found != S.Chunks.end() && Found->second.Node && Found->second.bPlain;
					const bool bNear = bWasNear ? D < NearKeepRadius : D < NearRadius;
					// (plain far off: a kilometre's give each way, so a square on the line isn't built again and again)
					Wanted.push_back({ D, Key, bNear, !bNear && (bWasPlain ? D > PlainFrom - 1000.0 : D > PlainFrom) });
				}
			}
		}
		for (auto It = S.Chunks.begin(); It != S.Chunks.end();)
		{
			if (!Keep.count(It->first) && !It->second.bBuilding)
			{
				if (It->second.Node)
				{
					It->second.Node->queue_free();
				}
				FreeTiles(It->second.Tiles);
				It = S.Chunks.erase(It);
			}
			else
			{
				++It;
			}
		}
		std::sort(Wanted.begin(), Wanted.end(), [](const Want& A, const Want& B) { return A.Distance < B.Distance; });
		for (const Want& W : Wanted)
		{
			if (S.ChunkJobs >= MaxChunkJobs)
			{
				break;
			}
			State::Chunk& C = S.Chunks[W.Key];
			if (C.bBuilding)
			{
				continue;
			}
			bool bWhole = false;
			PiecePtr From = S.ChoosePiece(W.Key, bWhole);
			// built if missing or at the wrong detail, or cut off at a piece's edge when a whole one has it (only away from the car)
			const bool bReplace = C.Node && !C.bComplete && bWhole && W.Distance > ReplaceRadius;
			if (C.Node && C.bNear == W.bNear && C.bPlain == W.bPlain && !bReplace)
			{
				continue;
			}
			C.bBuilding = true;
			C.bBuildingNear = W.bNear;
			C.bBuildingPlain = W.bPlain;
			C.bBuildingComplete = bWhole;
			C.BuildingFrom = From;
			++S.ChunkJobs;
			const Cell Key = W.Key;
			const bool bNear = W.bNear;
			const bool bPlain = W.bPlain;
			// the ground round the start is already built: handed over (moved, not copied), then the piece hasn't it any more
			auto Prebuilt = std::make_shared<geoground::MeshData>();
			bool bPrebuilt = false;
			if (bNear)
			{
				const auto Start = From->Core->StartChunks.find(Key);
				if (Start != From->Core->StartChunks.end())
				{
					*Prebuilt = std::move(Start->second);
					From->Core->StartChunks.erase(Start);
					bPrebuilt = true;
				}
			}
			C.Job = std::async(std::launch::async, [From, Key, bNear, bPlain, Prebuilt, bPrebuilt]() mutable -> Made
			{
				LetGoHere Release{ From };
				LowerPriority();
				Made Out;
				Out.Anchor = From->Core->Origin;
				geoground::MeshData Mesh;
				if (bPrebuilt)
				{
					Mesh = std::move(*Prebuilt);
				}
				else
				{
					From->Core->Ground->BuildChunk(From->Core->LocalX(Key.first), From->Core->LocalY(Key.second), bNear, Mesh, bPlain);
				}
				Out.Triangles = int64_t(Mesh.Indices.size() / 3);
				Out.Mesh.instantiate();
				AddSurface(Out.Mesh, Mesh, true, Ref<Material>());
				if (bNear)
				{
					FaceBuckets Buckets;
					AddFaces(Mesh, Buckets);
					MakeTiles(Buckets, Out.Anchor, Out.Collision);
				}
				return Out;
			});
		}

		// a block shows until all its chunks are in; then it goes (no hole while they come)
		for (auto& Entry : S.Blocks)
		{
			State::Block& B = Entry.second;
			if (!B.Node)
			{
				continue;
			}
			bool bCovered = B.bDetail;
			for (int32_t dx = 0; dx < geoground::Terrain::ChunksPerBlock && bCovered; ++dx)
			{
				for (int32_t dy = 0; dy < geoground::Terrain::ChunksPerBlock && bCovered; ++dy)
				{
					const Cell Key(Entry.first.first * geoground::Terrain::ChunksPerBlock + dx, Entry.first.second * geoground::Terrain::ChunksPerBlock + dy);
					bool bWhole = false;
					if (!S.ChoosePiece(Key, bWhole))
					{
						continue;
					}
					const auto Found = S.Chunks.find(Key);
					bCovered = Found != S.Chunks.end() && Found->second.Node;
				}
			}
			B.Node->set_visible(!bCovered);
		}

		// road and building tiles near the focus
		std::vector<std::pair<double, Cell>> WantedTiles;
		std::set<Cell> Squares;
		for (const PiecePtr& P : S.Pieces)
		{
			const int32_t Reach = int32_t(std::ceil(TileLoadRadius * 100.0 / Size));
			const int32_t CX = int32_t(std::floor(Focus.X / Size)), CY = int32_t(std::floor(Focus.Y / Size));
			for (int32_t X = std::max(P->Core->MinX, CX - Reach); X <= std::min(P->Core->MaxX, CX + Reach); ++X)
			{
				for (int32_t Y = std::max(P->Core->MinY, CY - Reach); Y <= std::min(P->Core->MaxY, CY + Reach); ++Y)
				{
					Squares.insert({ X, Y });
				}
			}
		}
		for (const Cell& Key : Squares)
		{
			const double D = Distance(Key);
			if (D < TileLoadRadius)
			{
				WantedTiles.push_back({ D, Key });
			}
		}
		for (auto It = S.Tiles.begin(); It != S.Tiles.end();)
		{
			if (Distance(It->first) > TileUnloadRadius && !It->second.bBuilding)
			{
				if (It->second.Node)
				{
					It->second.Node->queue_free();
				}
				FreeTiles(It->second.Tiles);
				It = S.Tiles.erase(It);
			}
			else
			{
				++It;
			}
		}
		std::sort(WantedTiles.begin(), WantedTiles.end());
		for (const auto& W : WantedTiles)
		{
			if (S.TileJobs >= MaxTileJobs)
			{
				break;
			}
			State::Tile& T = S.Tiles[W.second];
			if (T.bBuilding)
			{
				continue;
			}
			bool bWhole = false;
			PiecePtr From = S.ChoosePiece(W.second, bWhole);
			if (!From)
			{
				continue;
			}
			const bool bReplace = (T.bLoaded && !T.bComplete && bWhole && W.first > ReplaceRadius) || T.bRedo;
			if (T.bLoaded && !bReplace)
			{
				continue;
			}
			T.bRedo = false;
			T.bBuilding = true;
			T.bBuildingComplete = bWhole;
			T.BuildingFrom = From;
			++S.TileJobs;
			const Cell Key = W.second;
			const Materials Paints = S.Paints;
			const auto Removed = S.Removed;
			T.Job = std::async(std::launch::async, [From, Key, Paints, Removed]() mutable -> Made
			{
				LetGoHere Release{ From };
				LowerPriority();
				Made Out;
				Out.Anchor = From->Core->Origin;
				Out.Mesh.instantiate();
				const auto Found = From->Tiles.find(Key);
				if (Found == From->Tiles.end())
				{
					return Out;
				}
				geoworld::TileMeshes M = Unpack(Found->second);
				// (the map's buildings taken out: their triangles left out, drawn and solid)
				if (!Removed->empty() && M.Buildings.Tags.size() * 3 == M.Buildings.Indices.size())
				{
					const auto& Outlines = From->Core->Outlines;
					std::vector<int32_t> Kept;
					std::vector<int32_t> KeptTags;
					for (size_t t = 0; t < M.Buildings.Tags.size(); ++t)
					{
						const int32_t Tag = M.Buildings.Tags[t];
						if (Tag >= 0 && size_t(Tag) < Outlines.size() && Removed->count(Outlines[size_t(Tag)].Id))
						{
							continue;
						}
						Kept.insert(Kept.end(), M.Buildings.Indices.begin() + std::ptrdiff_t(t * 3), M.Buildings.Indices.begin() + std::ptrdiff_t(t * 3 + 3));
						KeptTags.push_back(Tag);
					}
					M.Buildings.Indices = std::move(Kept);
					M.Buildings.Tags = std::move(KeptTags);
				}
				AddSurface(Out.Mesh, M.Roads[0], false, Paints.Asphalt);
				AddSurface(Out.Mesh, M.Roads[1], false, Paints.Paving);
				AddSurface(Out.Mesh, M.Roads[2], false, Paints.Gravel);
				AddSurface(Out.Mesh, M.Skirts, false, Paints.Skirts);
				Out.Lines.instantiate();
				AddSurface(Out.Lines, M.Markings[0], false, Paints.White);
				AddSurface(Out.Lines, M.Markings[1], false, Paints.Yellow);
				Out.Buildings.instantiate();
				AddSurface(Out.Buildings, M.Buildings, false, Paints.Buildings, true);
				FaceBuckets Buckets;
				for (const geoground::MeshData* Solid : { &M.Roads[0], &M.Roads[1], &M.Roads[2], &M.Buildings })
				{
					AddFaces(*Solid, Buckets);
					Out.Triangles += int64_t(Solid->Indices.size() / 3);
				}
				MakeTiles(Buckets, Out.Anchor, Out.Collision);
				return Out;
			});
		}

		// --- Godot's 0 follows the car (when it's 2 km away), so positions in Godot stay small and exact; the game moves
		// the car and camera by the same (origin_shifted) ---
		if (S.bReady && (Focus - S.RenderOrigin).Size() > 200000.0)
		{
			const Vec2 NewOrigin(std::round(Focus.X / 100000.0) * 100000.0, std::round(Focus.Y / 100000.0) * 100000.0);
			const Vector3 Shift = ToGodot(S.RenderOrigin - NewOrigin, 0.0);
			S.RenderOrigin = NewOrigin;
			auto Move = [&S](std::vector<CollisionTile>& List)
			{
				for (CollisionTile& Tile : List)
				{
					if (Tile.Body)
					{
						Tile.Body->set_position(S.At(Tile.Anchor));
					}
				}
			};
			for (auto& Entry : S.Chunks)
			{
				if (Entry.second.Node)
				{
					Entry.second.Node->set_position(S.At(Entry.second.Anchor));
				}
				Move(Entry.second.Tiles);
			}
			for (auto& Entry : S.Tiles)
			{
				if (Entry.second.Node)
				{
					Entry.second.Node->set_position(S.At(Entry.second.Anchor));
				}
				Move(Entry.second.Tiles);
			}
			for (auto& Entry : S.Blocks)
			{
				if (Entry.second.Node)
				{
					Entry.second.Node->set_position(S.At(Entry.second.Anchor));
				}
			}
			if (S.WaterNode)
			{
				S.WaterNode->set_position(S.At(S.WaterAnchor));
			}
			if (S.HorizonNode)
			{
				S.HorizonNode->set_position(S.At(S.HorizonCentre));
			}
			if (S.TrafficCars)
			{
				for (const auto& Entry : static_cast<GodotTrafficCars&>(*S.TrafficCars).Cars)
				{
					if (GeodriveCar* Car = Object::cast_to<GeodriveCar>(ObjectDB::get_instance(Entry.second)))
					{
						Car->set_global_position(Car->get_global_position() + Shift);
					}
				}
			}
			S.Focus += Shift;
			emit_signal("origin_shifted", Shift);
			S.Log("[World] Godot's 0 moved " + std::to_string(int(Shift.length())) + " m along with the car");
		}

		// --- the next piece, when less than 4 km is built ahead (as the Unreal game does it), or 40 s at speed ---
		const double Now = Seconds();
		if (S.bReady && !S.bPieceJob && Now - S.LastEdgeCheck > 1.0 && Now - S.LastPieceTime > 10.0)
		{
			S.LastEdgeCheck = Now;
			Vec2 Heading = ToWorld(S.Velocity) * 0.01;
			const double Speed = Heading.Size();
			Heading = Speed > 3.0 ? Heading * (1.0 / Speed) : Vec2();
			if (Speed > 3.0)
			{
				double Ahead = 0.0;
				auto Inside = [&S](const Vec2& P)
				{
					for (const PiecePtr& Piece : S.Pieces)
					{
						if (Piece->Core->Usable.IsInside(P))
						{
							return true;
						}
					}
					return false;
				};
				// (at speed, 40 s ahead: an aircraft would outrun 4 km before the piece is built)
				const double Wanted = std::max(NextAhead, Speed * 40.0) * 100.0;
				while (Ahead < Wanted + 1.0 && Inside(Focus + Heading * (Ahead + 25000.0)))
				{
					Ahead += 25000.0;
				}
				if (Ahead <= Wanted)
				{
					// (flying: centred past what's built, overlapping it a little; driving, as before)
					const Vec2 Centre = Focus + Heading * (Speed > 60.0 ? std::max(NextLead * 100.0, Ahead + S.SizeKm() * 100000.0 * 0.35) : NextLead * 100.0);
					// (not again from where the last was asked for, the same way: it would be the same piece. In a dense
					// city a piece is cut to ~5 km, too small to give 4 km ahead, and standing there it was built again
					// every 10 s. Once 500 m on, or turned round, it is)
					const bool bSame = S.bLastNext && (Focus - S.LastNextFocus).Size() < 50000.0
						&& Heading.X * S.LastNextHeading.X + Heading.Y * S.LastNextHeading.Y > 0.5;
					if (!bSame)
					{
						S.LastNextFocus = Focus;
						S.LastNextHeading = Heading;
						S.bLastNext = true;
						double Lat, Lon;
						S.Projection.ToLatLon(Centre, Lat, Lon);
						double South, West, North, East;
						geoworld::BoxAround(Lat, Lon, S.SizeKm(), South, West, North, East);
						S.LaunchPiece(South, West, North, East, false, Vec2());
					}
				}
			}
		}

		// --- old pieces go: two are kept, an older one only while it has squares near that nothing else has ---
		while (S.Pieces.size() > 2)
		{
			const PiecePtr Oldest = S.Pieces.front();
			bool bNeeded = false;
			const int32_t Reach = int32_t(std::ceil(KeepNeededRadius * 100.0 / Size));
			const int32_t CX = int32_t(std::floor(Focus.X / Size)), CY = int32_t(std::floor(Focus.Y / Size));
			for (int32_t X = CX - Reach; X <= CX + Reach && !bNeeded; ++X)
			{
				for (int32_t Y = CY - Reach; Y <= CY + Reach && !bNeeded; ++Y)
				{
					const Cell Key(X, Y);
					if (!Oldest->Core->Has(Key) || Distance(Key) > KeepNeededRadius)
					{
						continue;
					}
					bool bOthers = false;
					for (size_t i = 1; i < S.Pieces.size(); ++i)
					{
						bOthers = bOthers || S.Pieces[i]->Core->Has(Key);
					}
					const auto Chunk = S.Chunks.find(Key);
					const auto Tile = S.Tiles.find(Key);
					const bool bLoaded = Chunk != S.Chunks.end() && Chunk->second.Node && Tile != S.Tiles.end() && Tile->second.bLoaded;
					bNeeded = !bOthers && !bLoaded;
				}
			}
			if (bNeeded)
			{
				break;
			}
			S.Pieces.erase(S.Pieces.begin());
			S.Log("[Piece] piece " + std::to_string(Oldest->Core->Number) + " let go");
			std::thread([Old = Oldest]() mutable { Old.reset(); }).detach();
		}

		// --- the far horizon: plain ground out to the view distance, beyond the pieces; made again once the focus has
		// moved a fair way, the view distance changed, or the built pieces changed (it's cut round them) ---
		{
			std::string PieceList;
			for (const PiecePtr& P : S.Pieces)
			{
				PieceList += std::to_string(P->Core->Number) + ",";
			}
			const Vec2 Here = S.Drive(S.Focus);
			const double Radius = S.ViewDistance * 100.0;
			if (S.bHorizonJob && S.HorizonJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				S.bHorizonJob = false;
				State::HorizonMade Made = S.HorizonJob.get();
				const Ref<ArrayMesh>& Mesh = Made.Mesh;
				if (Mesh.is_valid() && S.HorizonGeneration == S.Generation)
				{
					S.HorizonPlants = Made.Plants;
					S.HorizonPlantHeights = Made.PlantHeights;
					S.HorizonPlantMin = Made.PlantMin;
					S.HorizonPlantCellSize = Made.PlantCell;
					if (S.HorizonNode)
					{
						S.HorizonNode->queue_free();
						S.HorizonNode = nullptr;
					}
					S.HorizonNode = memnew(MeshInstance3D);
					S.HorizonNode->set_name("Horizon");
					S.HorizonNode->set_layer_mask(1u << 15);
					S.HorizonNode->set_material_override(S.Paints.Ground);
					S.HorizonNode->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
					S.HorizonNode->set_mesh(Mesh);
					S.HorizonCentre = S.HorizonJobCentre;
					S.HorizonRadius = S.HorizonJobRadius;
					S.HorizonPieces = S.HorizonJobPieces;
					S.HorizonNode->set_position(S.At(S.HorizonCentre));
					add_child(S.HorizonNode);
				}
			}
			// (only worth it beyond what the pieces show: a few km)
			const bool bWanted = S.bReady && S.HorizonFill && Radius > 800000.0;
			if (!bWanted && S.HorizonNode)
			{
				S.HorizonNode->queue_free();
				S.HorizonNode = nullptr;
				S.HorizonRadius = 0.0;
			}
			const bool bStale = !S.HorizonNode || std::abs(S.HorizonRadius - Radius) > 1.0 || S.HorizonPieces != PieceList
				|| (Here - S.HorizonCentre).Size() > std::max(500000.0, Radius * 0.2);
			// (not waiting for a piece to finish building any more: flying, they're built back to back, and the horizon
			// never had its turn, the land ending in a straight edge with the sea beyond)
			if (bWanted && bStale && !S.bHorizonJob)
			{
				// a coarse raster (Terrarium zoom 10, ~120 m a pixel) over the whole circle, the pieces' boxes left out
				std::vector<geoground::Box2> Holes;
				for (const PiecePtr& P : S.Pieces)
				{
					const geoground::Box2& B = P->Core->Ground->Bounds;
					Holes.push_back(geoground::Box2(B.Min + P->Core->Origin + Vec2(50000.0, 50000.0), B.Max + P->Core->Origin - Vec2(50000.0, 50000.0)));
				}
				double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
				S.Projection.ToLatLon(Here - Vec2(Radius * 1.05, Radius * 1.05), South, West);
				S.Projection.ToLatLon(Here + Vec2(Radius * 1.05, Radius * 1.05), North, East);
				geoground::HorizonOptions Options;
				Options.Centre = Here;
				Options.Radius = Radius;
				Options.Spacing = std::max(20000.0, Radius / 160.0);	// ~320 points across, however far
				Options.Holes = std::move(Holes);
				S.bHorizonJob = true;
				S.HorizonGeneration = S.Generation;
				S.HorizonJobCentre = Here;
				S.HorizonJobRadius = Radius;
				S.HorizonJobPieces = PieceList;
				const georoads::Projection Projection = S.Projection;
				const std::function<void(geoground::HeightRaster&)> Fill = S.HorizonFill;
				State* Self = &S;
				std::vector<PiecePtr> PlantPieces = S.Pieces;
				const std::string CoverDir = S.CacheDir;
				S.HorizonJob = std::async(std::launch::async, [=]() mutable -> State::HorizonMade
				{
					LowerPriority();
					geoground::HeightRaster Raster;
					Raster.SetBox(South, West, North, East, 10);
					Fill(Raster);
					geoground::MeshData Built;
					geoground::BuildHorizon(Raster, Projection, Options, Built);
					Self->Log("[Horizon] " + std::to_string(int(Options.Radius / 100000.0)) + " km round, " + std::to_string(Built.Indices.size() / 3) + " triangles");
					State::HorizonMade Out;
					Out.Mesh.instantiate();
					AddSurface(Out.Mesh, Built, true, Ref<Material>());
					State::MakeHorizonPlants(Raster, Projection, Options, PlantPieces, CoverDir, Out, *Self);
					PlantPieces.clear();	// (let go here: see the plant maps)
					return Out;
				});
			}
		}

		// --- a new area is ready once nothing near the start is waiting (or after 30 s) ---
		if (!S.bReady)
		{
			bool bPending = false;
			for (const Want& W : Wanted)
			{
				if (W.Distance < StartFillRadius)
				{
					const auto Found = S.Chunks.find(W.Key);
					bPending = bPending || Found == S.Chunks.end() || !Found->second.Node || Found->second.bBuilding;
				}
			}
			for (const auto& W : WantedTiles)
			{
				if (W.first < StartFillRadius)
				{
					const auto Found = S.Tiles.find(W.second);
					bPending = bPending || Found == S.Tiles.end() || !Found->second.bLoaded;
				}
			}
			if (!is_solid_at(S.Focus))
			{
				bPending = true;
			}
			if (bPending)
			{
				S.QuietSince = -1.0;
			}
			else if (S.QuietSince < 0.0)
			{
				S.QuietSince = Now;
			}
			if ((S.QuietSince >= 0.0 && Now - S.QuietSince > 0.35) || Now - S.StartedAt > 30.0)
			{
				S.bReady = true;
				UtilityFunctions::print(vformat("Geodrive: area in, %.1f s after it was asked for", Now - S.StartedAt));
			}
		}
	}
}

void GeodriveWorld::State::MakeHorizonPlants(const geoground::HeightRaster& Raster, const georoads::Projection& Projection, const geoground::HorizonOptions& Options,
	const std::vector<PiecePtr>& Pieces, const std::string& CacheDir, State::HorizonMade& Out, State& Self)
{
	// the horizon's plant map (on its worker): the woods out to the view distance, for the far blanket. The trees from
	// the pieces' own maps where they are, the satellite's coarse land cover beyond; the ground's height as the ground
	// drawn there (the pieces' where they are, the horizon's own triangles, sunk as it is, beyond), so the blanket sits
	// on it rather than floating or buried
	const auto Start = std::chrono::steady_clock::now();
	const int32_t N = State::HorizonPlantCells;
	const double Cell = State::HorizonPlantCell(Options.Radius), Span = Cell * N;
	const Vec2 Min(std::floor((Options.Centre.X - Span * 0.5) / Cell) * Cell, std::floor((Options.Centre.Y - Span * 0.5) / Cell) * Cell);
	Out.PlantMin = Min;
	Out.PlantCell = Cell;
	geoground::LandCover Cover;
	Projection.ToLatLon(Min, Cover.South, Cover.West);
	Projection.ToLatLon(Min + Vec2(Span, Span), Cover.North, Cover.East);
	std::string CoverLog;
	FetchWorldCover(Cover, CacheDir, CoverLog, 4);
	// the horizon's grid heights (cm), as BuildHorizon has them
	const double S = Options.Spacing;
	const int32_t Half = int32_t(std::ceil(Options.Radius / S)), Side = Half * 2 + 1;
	std::vector<float> Grid(size_t(Side) * size_t(Side));
	for (int32_t X = 0; X < Side; ++X)
	{
		for (int32_t Y = 0; Y < Side; ++Y)
		{
			double Lat = 0.0, Lon = 0.0;
			Projection.ToLatLon(Options.Centre + Vec2((X - Half) * S, (Y - Half) * S), Lat, Lon);
			Grid[size_t(X) * size_t(Side) + size_t(Y)] = Raster.Sample(Lat, Lon) * 100.0f;
		}
	}
	auto HorizonHeight = [&](const Vec2& P)
	{
		const double FX = (P.X - Options.Centre.X) / S + Half, FY = (P.Y - Options.Centre.Y) / S + Half;
		const int32_t X = std::clamp(int32_t(std::floor(FX)), 0, Side - 2), Y = std::clamp(int32_t(std::floor(FY)), 0, Side - 2);
		const float TX = float(std::clamp(FX - X, 0.0, 1.0)), TY = float(std::clamp(FY - Y, 0.0, 1.0));
		auto G = [&](int32_t A, int32_t B) { return Grid[size_t(A) * size_t(Side) + size_t(B)]; };
		const float A = G(X, Y), B = G(X + 1, Y), C = G(X + 1, Y + 1), D = G(X, Y + 1);
		// (its two triangles, A D C and A C B, as the mesh has them)
		const float H = TX >= TY ? A + TX * (B - A) + TY * (C - B) : A + TY * (D - A) + TX * (C - D);
		return H - Options.Sink;
	};
	// how thick the trees are on each land class (as Core's vegetation map has them)
	uint8_t Trees[256] = {};
	Trees[geoground::LandTrees] = 230;
	Trees[geoground::LandMangrove] = 210;
	Trees[geoground::LandBuilt] = 28;
	Trees[geoground::LandShrubs] = 14;
	Trees[geoground::LandWetland] = 12;
	Trees[geoground::LandGrass] = 6;
	std::vector<uint8_t> Texels(size_t(N) * N * 4, 0);
	std::vector<float> Heights(size_t(N) * N * 2, 0.0f);
	const double Reach2 = Options.Radius * Options.Radius;
	const double Fine = geoground::VegetationMap::Cell;
	auto Band = [&](int32_t FirstRow, int32_t EndRow)
	{
		for (int32_t Row = FirstRow; Row < EndRow; ++Row)
		{
			for (int32_t Col = 0; Col < N; ++Col)
			{
				const Vec2 P(Min.X + Span - (Row + 0.5) * Cell, Min.Y + (Col + 0.5) * Cell);	// (rows run south, as the plant maps)
				const size_t At = size_t(Row) * N + size_t(Col);
				Heights[At * 2 + 1] = 40.0f;	// (room: the roads' and buildings' isn't kept this coarse)
				const ClientPiece* Piece = nullptr;
				for (auto It = Pieces.rbegin(); It != Pieces.rend() && !Piece; ++It)
				{
					if ((*It)->Core->Ground->Bounds.IsInside(P - (*It)->Core->Origin))
					{
						Piece = It->get();
					}
				}
				uint32_t Sum = 0, Count = 0;
				if (Piece)
				{
					Heights[At * 2] = Piece->Core->Ground->BareHeight(P - Piece->Core->Origin) / 100.0f;
					const geoground::VegetationMap* Map = Piece->Core->Vegetation.get();
					for (int32_t i = 0; Map && Map->IsValid() && i < 16; ++i)
					{
						const Vec2 Local = P + Vec2((i / 4 - 1.5) * Cell / 4.0, (i % 4 - 1.5) * Cell / 4.0) - Piece->Core->Origin - Map->Min;
						const int32_t X = int32_t(std::floor(Local.X / Fine)), Y = int32_t(std::floor(Local.Y / Fine));
						if (X >= 0 && Y >= 0 && X < Map->Width && Y < Map->Height)
						{
							Sum += Map->Texels[(size_t(X) * size_t(Map->Height) + size_t(Y)) * 4];
							++Count;
						}
					}
				}
				else
				{
					Heights[At * 2] = HorizonHeight(P) / 100.0f;
					if ((P - Options.Centre).SizeSquared() > Reach2)
					{
						continue;	// (past the horizon's edge: no ground under it)
					}
					for (int32_t i = 0; Cover.IsValid() && i < 9; ++i)
					{
						double Lat = 0.0, Lon = 0.0;
						Projection.ToLatLon(P + Vec2((i / 3 - 1) * Cell / 3.0, (i % 3 - 1) * Cell / 3.0), Lat, Lon);
						Sum += Trees[Cover.At(Lat, Lon)];
						++Count;
					}
				}
				Texels[At * 4] = Count > 0 ? uint8_t(Sum / Count) : 0;
			}
		}
	};
	const int32_t Bands = std::clamp(int32_t(std::thread::hardware_concurrency()) / 2, 1, 8);	// (leaving the game cores)
	std::vector<std::future<void>> Running;
	for (int32_t i = 1; i < Bands; ++i)
	{
		Running.push_back(std::async(std::launch::async, Band, N * i / Bands, N * (i + 1) / Bands));
	}
	Band(0, N / Bands);
	for (std::future<void>& Each : Running)
	{
		Each.wait();
	}
	PackedByteArray Bytes;
	Bytes.resize(int64_t(Texels.size()));
	std::memcpy(Bytes.ptrw(), Texels.data(), Texels.size());
	PackedByteArray HeightBytes;
	HeightBytes.resize(int64_t(Heights.size() * sizeof(float)));
	std::memcpy(HeightBytes.ptrw(), Heights.data(), Heights.size() * sizeof(float));
	// (the textures made here too, off the main thread, new each time: see the plant maps)
	Out.Plants = ImageTexture::create_from_image(Image::create_from_data(N, N, false, Image::FORMAT_RGBA8, Bytes));
	Out.PlantHeights = ImageTexture::create_from_image(Image::create_from_data(N, N, false, Image::FORMAT_RGF, HeightBytes));
	char Line[300];
	std::snprintf(Line, sizeof(Line), "[Plants] the horizon's plant map (%.0f m cells, %.0f km) made in %.0f ms (on a worker); %s", Cell / 100.0, Span / 100000.0,
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count(), CoverLog.c_str());
	Self.Log(Line);
}

Ref<Texture2D> GeodriveWorld::get_horizon_plants(bool bHeights) const
{
	return bHeights ? World->HorizonPlantHeights : World->HorizonPlants;
}

Vector3 GeodriveWorld::get_horizon_plant_origin() const
{
	const State& S = *World;
	return S.Render(S.HorizonPlantMin + Vec2(S.HorizonPlantCellSize * State::HorizonPlantCells, 0.0), 0.0);
}

double GeodriveWorld::get_horizon_plant_size() const
{
	return World->HorizonPlantCellSize * State::HorizonPlantCells / 100.0;
}

bool GeodriveWorld::is_solid_at(const Vector3& Position) const
{
	const State& S = *World;
	if (S.Pieces.empty())
	{
		return false;
	}
	const Vec2 P = S.Drive(Position);
	const double Size = geoground::Terrain::ChunkSize;
	auto Solid = [&P](const std::vector<CollisionTile>& List)
	{
		for (const CollisionTile& Tile : List)
		{
			if (!Tile.Body && (Tile.Centre - P).Size() < 15000.0)
			{
				return false;
			}
		}
		return true;
	};
	const int32_t CX = int32_t(std::floor(P.X / Size)), CY = int32_t(std::floor(P.Y / Size));
	for (int32_t X = CX - 1; X <= CX + 1; ++X)
	{
		for (int32_t Y = CY - 1; Y <= CY + 1; ++Y)
		{
			// (only the squares within 300 m: the ground round it. All nine, the far corners ~1.5 km off are past where
			// the ground's fine (1.2 km), never near, and it was never solid: every load waited out its 30 s)
			const double OutX = std::max({ X * Size - P.X, 0.0, P.X - (X + 1) * Size });
			const double OutY = std::max({ Y * Size - P.Y, 0.0, P.Y - (Y + 1) * Size });
			bool bWhole = false;
			if (OutX * OutX + OutY * OutY > 30000.0 * 30000.0 || !S.ChoosePiece({ X, Y }, bWhole))
			{
				continue;
			}
			const auto Chunk = S.Chunks.find({ X, Y });
			if (Chunk == S.Chunks.end() || !Chunk->second.Node || !Chunk->second.bNear || !Solid(Chunk->second.Tiles))
			{
				return false;
			}
			const auto Tile = S.Tiles.find({ X, Y });
			if (Tile == S.Tiles.end() || !Tile->second.bLoaded || !Solid(Tile->second.Tiles))
			{
				return false;
			}
		}
	}
	return true;
}

String GeodriveWorld::get_status() const
{
	const State& S = *World;
	int32_t Near = 0, Far = 0, Bodies = 0, Loaded = 0, Shown = 0;
	for (const auto& Entry : S.Chunks)
	{
		Near += Entry.second.Node && Entry.second.bNear ? 1 : 0;
		Far += Entry.second.Node && !Entry.second.bNear ? 1 : 0;
		for (const CollisionTile& Tile : Entry.second.Tiles)
		{
			Bodies += Tile.Body ? 1 : 0;
		}
	}
	for (const auto& Entry : S.Tiles)
	{
		Loaded += Entry.second.bLoaded ? 1 : 0;
		for (const CollisionTile& Tile : Entry.second.Tiles)
		{
			Bodies += Tile.Body ? 1 : 0;
		}
	}
	for (const auto& Entry : S.Blocks)
	{
		Shown += Entry.second.Node && Entry.second.Node->is_visible() ? 1 : 0;
	}
	std::string Pieces;
	for (const PiecePtr& P : S.Pieces)
	{
		Pieces += (Pieces.empty() ? "" : ",") + std::to_string(P->Core->Number);
	}
	return vformat("pieces %s%s; ground %d detailed, %d coarse km, %d distant blocks; %d road tiles; %d solid tiles; jobs %d+%d",
		String::utf8(Pieces.c_str()), S.bPieceJob ? String(" (building the next)") : String(), Near, Far, Shown, Loaded, Bodies, S.ChunkJobs, S.TileJobs);
}

Dictionary GeodriveWorld::nearest_road(const Vector3& Position, double MaxDistance, bool bDrivableOnly) const
{
	const State& S = *World;
	Dictionary Out;
	Out["found"] = false;
	const Vec2 Drive = S.Drive(Position);
	// as the Unreal game finds it: the nearest by distance along the ground (not height), the search widening ring by
	// ring of 200 m squares until one is found. Every piece that could hold a nearer road is searched, the one the
	// point is in first: in busy places pieces are built smaller, and the nearest road can be in the next one (or the
	// point between two)
	double Best = MaxDistance * 100.0;
	PiecePtr BestPiece;
	int32_t BestEdge = -1;
	Vec2 BestPoint, BestDirection;
	double BestHeight = 0.0;
	auto Search = [&](const PiecePtr& Piece)
	{
		const Vec2 P = Drive - Piece->Core->Origin;	// the piece's roads are measured from its point
		const auto& Bounds = Piece->Core->Ground->Bounds;
		const double OutX = std::max({ Bounds.Min.X - P.X, 0.0, P.X - Bounds.Max.X });
		const double OutY = std::max({ Bounds.Min.Y - P.Y, 0.0, P.Y - Bounds.Max.Y });
		if (std::sqrt(OutX * OutX + OutY * OutY) >= Best)
		{
			return;	// (nothing in it can be nearer)
		}
		const georoads::RoadGraph& Roads = *Piece->Core->Roads;
		const int32_t MaxReach = int32_t(std::ceil(Best / RoadGrid::Cell));
		const int32_t CX = int32_t(std::floor(P.X / RoadGrid::Cell)), CY = int32_t(std::floor(P.Y / RoadGrid::Cell));
		bool bFoundHere = false;
		for (int32_t Ring = 0; Ring <= MaxReach; ++Ring)
		{
			// a road found nearer than this ring's inner edge can't be beaten further out
			if (bFoundHere && Best <= (Ring - 1) * RoadGrid::Cell)
			{
				break;
			}
			for (int32_t X = CX - Ring; X <= CX + Ring; ++X)
			{
			for (int32_t Y = CY - Ring; Y <= CY + Ring; ++Y)
			{
				if (std::max(std::abs(X - CX), std::abs(Y - CY)) != Ring)
				{
					continue;
				}
				const auto Found = Piece->Grid.Cells.find({ X, Y });
				if (Found == Piece->Grid.Cells.end())
				{
					continue;
				}
				for (const auto& [EdgeIndex, i] : Found->second)
				{
					const georoads::RoadEdge& Edge = Roads.Edges[EdgeIndex];
					if (bDrivableOnly && !Drivable(Edge.Highway))
					{
						continue;
					}
					const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
					const Vec2 AB = B - A;
					const double Length2 = AB.X * AB.X + AB.Y * AB.Y;
					const double T = Length2 > 1.0 ? std::clamp(((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / Length2, 0.0, 1.0) : 0.0;
					const Vec2 On = A + AB * T;
					const double H = Edge.Heights.size() == Edge.Points.size() ? Edge.Heights[i] + (Edge.Heights[i + 1] - Edge.Heights[i]) * T : 0.0;
					const double D = std::sqrt((P.X - On.X) * (P.X - On.X) + (P.Y - On.Y) * (P.Y - On.Y));
					if (D < Best)
					{
						Best = D;
						BestPiece = Piece;
						BestEdge = EdgeIndex;
						BestPoint = On;
						BestDirection = AB.Normal();
						BestHeight = H;
						bFoundHere = true;
					}
				}
			}
			}
		}
	};
	const PiecePtr Here = S.PieceAt(Drive);
	if (Here)
	{
		Search(Here);
	}
	for (const PiecePtr& Piece : S.Pieces)
	{
		if (Piece != Here)
		{
			Search(Piece);
		}
	}
	if (!BestPiece)
	{
		return Out;
	}
	const georoads::RoadEdge& Edge = BestPiece->Core->Roads->Edges[BestEdge];
	Out["found"] = true;
	Out["position"] = S.Render(BestPoint + BestPiece->Core->Origin, BestHeight);
	Out["direction"] = Vector3(float(BestDirection.Y), 0.0f, float(-BestDirection.X));
	Out["name"] = String::utf8(Edge.Name.c_str());
	Out["highway"] = String::utf8(Edge.Highway.c_str());
	Out["distance"] = Best / 100.0;
	return Out;
}

PackedFloat64Array GeodriveWorld::to_lat_lon(const Vector3& Position) const
{
	double Lat = 0.0, Lon = 0.0;
	World->Projection.ToLatLon(World->Drive(Position), Lat, Lon);
	PackedFloat64Array Out;
	Out.push_back(Lat);
	Out.push_back(Lon);
	return Out;
}

Vector3 GeodriveWorld::from_lat_lon(double Lat, double Lon) const
{
	const Vec2 P = World->Projection.ToWorld(Lat, Lon);
	const PiecePtr Piece = World->PieceAt(P);
	return World->Render(P, Piece ? Piece->Core->Ground->GroundHeight(P - Piece->Core->Origin) : 0.0);
}

bool GeodriveWorld::contains_lat_lon(double Lat, double Lon) const
{
	for (const PiecePtr& P : World->Pieces)
	{
		if (Lat >= P->Core->South && Lat <= P->Core->North && Lon >= P->Core->West && Lon <= P->Core->East)
		{
			return true;
		}
	}
	return false;
}

Dictionary GeodriveWorld::building_at(const Vector3& Position) const
{
	// the smallest of the map's buildings whose footprint holds the point (a part of a building inside a bigger one: that
	// part), newest piece first; not one already taken out
	const State& S = *World;
	Dictionary Out;
	Out["found"] = false;
	const Vec2 P = S.Drive(Position);
	auto Inside = [](const std::vector<Vec2>& Polygon, const Vec2& Q)
	{
		bool bIn = false;
		for (size_t i = 0, j = Polygon.size() - 1; i < Polygon.size(); j = i++)
		{
			const Vec2& A = Polygon[i];
			const Vec2& B = Polygon[j];
			if ((A.Y > Q.Y) != (B.Y > Q.Y) && Q.X < (B.X - A.X) * (Q.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bIn = !bIn;
			}
		}
		return bIn;
	};
	double Smallest = 0.0;
	for (auto It = S.Pieces.rbegin(); It != S.Pieces.rend(); ++It)
	{
		const PiecePtr& Piece = *It;
		const Vec2 L = P - Piece->Core->Origin;
		for (const geomesh::BuildingOutline& B : Piece->Core->Outlines)
		{
			if (L.X < B.Min.X || L.X > B.Max.X || L.Y < B.Min.Y || L.Y > B.Max.Y || S.Removed->count(B.Id) || !Inside(B.Footprint, L))
			{
				continue;
			}
			const double Area = (B.Max.X - B.Min.X) * (B.Max.Y - B.Min.Y);
			if (bool(Out["found"]) && Area >= Smallest)
			{
				continue;
			}
			Smallest = Area;
			Out["found"] = true;
			Out["id"] = B.Id;
			PackedVector3Array Outline;
			for (const Vec2& Corner : B.Footprint)
			{
				Outline.push_back(S.Render(Corner + Piece->Core->Origin, B.Top));
			}
			Out["outline"] = Outline;
			Out["height"] = (B.Top - B.Base) / 100.0;
		}
		if (bool(Out["found"]))
		{
			break;
		}
	}
	return Out;
}

void GeodriveWorld::footprints(const Vector3& A, const Vector3& B, std::vector<std::vector<Vector3>>& Out) const
{
	const State& S = *World;
	const Vec2 P = S.Drive(A), Q = S.Drive(B);
	const Vec2 Low(std::min(P.X, Q.X), std::min(P.Y, Q.Y)), High(std::max(P.X, Q.X), std::max(P.Y, Q.Y));
	std::unordered_set<int64_t> Seen;	// (a building in two pieces' overlap: once)
	for (const PiecePtr& Piece : S.Pieces)
	{
		const Vec2 Origin = Piece->Core->Origin;
		for (const geomesh::BuildingOutline& Building : Piece->Core->Outlines)
		{
			const Vec2 Min = Building.Min + Origin, Max = Building.Max + Origin;
			if (Max.X < Low.X || Min.X > High.X || Max.Y < Low.Y || Min.Y > High.Y || S.Removed->count(Building.Id) || !Seen.insert(Building.Id).second)
			{
				continue;
			}
			std::vector<Vector3> Footprint;
			Footprint.reserve(Building.Footprint.size());
			for (const Vec2& Corner : Building.Footprint)
			{
				Footprint.push_back(S.Render(Corner + Origin, 0.0));
			}
			Out.push_back(std::move(Footprint));
		}
	}
}

void GeodriveWorld::remove_building(int64_t Id)
{
	State& S = *World;
	if (S.Removed->count(Id))
	{
		return;
	}
	auto Next = std::make_shared<std::unordered_set<int64_t>>(*S.Removed);
	Next->insert(Id);
	S.Removed = Next;
	// the loaded tiles it's in built again without it
	const double Size = geoground::Terrain::ChunkSize;
	for (const PiecePtr& Piece : S.Pieces)
	{
		for (const geomesh::BuildingOutline& B : Piece->Core->Outlines)
		{
			if (B.Id != Id)
			{
				continue;
			}
			const Vec2 Min = B.Min + Piece->Core->Origin, Max = B.Max + Piece->Core->Origin;
			for (int32_t X = int32_t(std::floor(Min.X / Size)); X <= int32_t(std::floor(Max.X / Size)); ++X)
			{
				for (int32_t Y = int32_t(std::floor(Min.Y / Size)); Y <= int32_t(std::floor(Max.Y / Size)); ++Y)
				{
					const auto Found = S.Tiles.find({ X, Y });
					if (Found != S.Tiles.end() && Found->second.bLoaded)
					{
						Found->second.bRedo = true;
					}
				}
			}
		}
	}
}

void GeodriveWorld::set_removed_buildings(const PackedInt64Array& Ids)
{
	State& S = *World;
	auto Next = std::make_shared<std::unordered_set<int64_t>>();
	for (int64_t i = 0; i < Ids.size(); ++i)
	{
		Next->insert(Ids[i]);
	}
	if (*Next == *S.Removed)
	{
		return;
	}
	S.Removed = Next;
	for (auto& Entry : S.Tiles)
	{
		Entry.second.bRedo = Entry.second.bLoaded;	// (every loaded tile again: some may have them, or had)
	}
}

PackedInt64Array GeodriveWorld::get_removed_buildings() const
{
	PackedInt64Array Out;
	for (const int64_t Id : *World->Removed)
	{
		Out.push_back(Id);
	}
	return Out;
}

double GeodriveWorld::ground_height(const Vector3& Position) const
{
	const Vec2 P = World->Drive(Position);
	const PiecePtr Piece = World->PieceAt(P);
	return Piece ? Piece->Core->Ground->GroundHeight(P - Piece->Core->Origin) / 100.0 : 0.0;
}

String GeodriveWorld::collision_status(const Vector3& Position) const
{
	const State& S = *World;
	const Vec2 P = S.Drive(Position);
	const Cell Key(int32_t(std::floor(P.X / geoground::Terrain::ChunkSize)), int32_t(std::floor(P.Y / geoground::Terrain::ChunkSize)));
	const auto Found = S.Chunks.find(Key);
	if (Found == S.Chunks.end())
	{
		return vformat("chunk %d,%d not loaded", Key.first, Key.second);
	}
	const State::Chunk& C = Found->second;
	int32_t Bodies = 0;
	for (const CollisionTile& Tile : C.Tiles)
	{
		Bodies += Tile.Body ? 1 : 0;
	}
	return vformat("chunk %d,%d %s%s, %d collision tiles (%d with bodies)", Key.first, Key.second, C.bNear ? "detailed" : "coarse",
		C.bBuilding ? " (rebuilding)" : "", int64_t(C.Tiles.size()), Bodies);
}

Array GeodriveWorld::areas()
{
	Array Out;
	for (const geoplaces::Area& In : geoplaces::AllAreas())
	{
		Dictionary Area;
		Area["id"] = String::utf8(In.Id.c_str());
		Area["name"] = String::utf8(In.Name.c_str());
		Area["description"] = String::utf8(In.Description.c_str());
		Area["south"] = In.South;
		Area["west"] = In.West;
		Area["north"] = In.North;
		Area["east"] = In.East;
		Array Places;
		for (const geoplaces::Place& P : In.Places)
		{
			Dictionary Place;
			Place["name"] = String::utf8(P.Name.c_str());
			Place["lat"] = P.Lat;
			Place["lon"] = P.Lon;
			Places.push_back(Place);
		}
		Area["places"] = Places;
		Out.push_back(Area);
	}
	return Out;
}

String GeodriveWorld::get_loading_status() const
{
	const State& S = *World;
	if (!S.Error.empty())
	{
		return String::utf8(S.Error.c_str());
	}
	if (!S.bStarted)
	{
		return "";
	}
	if (S.bReady)
	{
		return "Ready";
	}
	if (S.Pieces.empty())
	{
		std::lock_guard<std::mutex> Guard(const_cast<State&>(S).LogLock);
		if (S.LoadStageGeneration == S.Generation && !S.LoadStage.empty())
		{
			return String::utf8(S.LoadStage.c_str());
		}
		return "Reading the map";
	}
	return "Putting in the ground and roads";
}

PackedFloat64Array GeodriveWorld::get_start() const
{
	PackedFloat64Array Out;
	Out.push_back(World->StartLat);
	Out.push_back(World->StartLon);
	return Out;
}

PackedVector3Array GeodriveWorld::road_lines(const Vector3& Position, double Radius) const
{
	const State& S = *World;
	PackedVector3Array Out;
	const Vec2 Drive = S.Drive(Position);
	const PiecePtr Piece = S.PieceAt(Drive);
	if (!Piece)
	{
		return Out;
	}
	const Vec2 P = Drive - Piece->Core->Origin;
	const georoads::RoadGraph& Roads = *Piece->Core->Roads;
	const int32_t Reach = int32_t(std::ceil(Radius * 100.0 / RoadGrid::Cell));
	const int32_t CX = int32_t(std::floor(P.X / RoadGrid::Cell)), CY = int32_t(std::floor(P.Y / RoadGrid::Cell));
	for (int32_t X = CX - Reach; X <= CX + Reach; ++X)
	{
		for (int32_t Y = CY - Reach; Y <= CY + Reach; ++Y)
		{
			const auto Found = Piece->Grid.Cells.find({ X, Y });
			if (Found == Piece->Grid.Cells.end())
			{
				continue;
			}
			for (const auto& [EdgeIndex, i] : Found->second)
			{
				const georoads::RoadEdge& Edge = Roads.Edges[EdgeIndex];
				const bool bHeights = Edge.Heights.size() == Edge.Points.size();
				// (30 cm up, so the line shows over the road)
				Out.push_back(S.Render(Edge.Points[i] + Piece->Core->Origin, (bHeights ? Edge.Heights[i] : 0.0) + 30.0));
				Out.push_back(S.Render(Edge.Points[i + 1] + Piece->Core->Origin, (bHeights ? Edge.Heights[i + 1] : 0.0) + 30.0));
			}
		}
	}
	return Out;
}

// --- traffic: Core's traffic drives GeodriveCars put in under the world node, in the roads' frame of the piece the
// player is in ---


struct GeodriveWorld::TrafficAccess
{
	static Vector3 Render(const State& S, const Vec2& P, double Height) { return S.Render(P, Height); }
	static Vec2 Drive(const State& S, const Vector3& P) { return S.Drive(P); }
};

int32_t GodotTrafficCars::SpawnCar(const Vec2& Position, double Height, const Vec2& Heading, int32_t Body, int32_t Paint, float Speed, bool bParked)
{
	GeodriveCar* Car = memnew(GeodriveCar);
	Car->set_name(vformat("Traffic%d", NextId));
	Car->set_body(Body);
	float R, G, B;
	geotraffic::PaintColour(Paint, R, G, B);
	Car->set_paint(Color(R, G, B));
	const Vector3 At = GeodriveWorld::TrafficAccess::Render(*S, Position + Origin, Height);
	const Vector3 Facing(float(Heading.Y), 0.0f, float(-Heading.X));
	Car->set_transform(Transform3D(Basis::looking_at(Facing, Vector3(0, 1, 0)), At));
	Owner->add_child(Car);
	Car->set_brake_reverses(false);
	Car->set_linear_velocity(Facing * Speed);
	if (bParked)
	{
		Car->set_input(0.0, 0.0, 1.0, 1.0);
	}
	const int32_t Id = NextId++;
	Cars[Id] = Car->get_instance_id();
	return Id;
}

bool GodotTrafficCars::GetCar(int32_t Id, geotraffic::CarPose& Out) const
{
	GeodriveCar* Car = Find(Id);
	if (!Car || !Car->is_inside_tree())
	{
		return false;
	}
	const Transform3D T = Car->get_global_transform();
	Out.Position = GeodriveWorld::TrafficAccess::Drive(*S, T.origin) - Origin;
	Out.Height = T.origin.y * 100.0;
	const Vector3 Forward = -T.basis.get_column(2);
	Out.Forward = Vec2(-Forward.z, Forward.x).Normal();
	Out.UpZ = T.basis.get_column(1).y;
	const Vector3 V = Car->get_linear_velocity();
	Out.Velocity = Vec2(-V.z * 100.0, V.x * 100.0);
	Out.State = Car->state();
	Out.Mass = float(Car->get_mass());
	Out.MaxSteerDegrees = float(Car->get_max_steer_degrees());
	return true;
}

void GeodriveWorld::update_traffic(double Delta, const Vector3& Position, const Vector3& Forward, const Vector3& Velocity, int Wanted)
{
	State& S = *World;
	if (S.Pieces.empty() || !S.bReady)
	{
		return;
	}
	const Vec2 Drive = S.Drive(Position);
	const PiecePtr Piece = S.PieceAt(Drive);
	if (!Piece || !Piece->Core->Roads)
	{
		return;
	}
	if (Wanted <= 0 && !S.Traffic)
	{
		return;
	}
	if (!S.Traffic)
	{
		auto Cars = std::make_unique<GodotTrafficCars>();
		Cars->Owner = this;
		Cars->S = &S;
		Cars->Origin = Piece->Core->Origin;
		S.TrafficCars = std::move(Cars);
		S.Traffic = std::make_unique<geotraffic::Traffic>(Piece->Core->Roads, *S.TrafficCars, uint32_t(Time::get_singleton()->get_ticks_usec()));
		S.TrafficPiece = Piece;
		S.Traffic->bDriveLeft = S.Country.bDriveLeft;
		State* Self = &S;
		S.Traffic->Log = [Self](const std::string& Line) { Self->Log(Line); };
		S.Traffic->OnProblem = [Self](const geotraffic::Problem& P)
		{
			// written to Saved/RoadReports/traffic_<time>.csv, as the Unreal game does
			const Vec2 At = P.Position + (Self->TrafficPiece ? Self->TrafficPiece->Core->Origin : Vec2());
			double Lat = 0.0, Lon = 0.0;
			Self->Projection.ToLatLon(At, Lat, Lon);
			if (Self->TrafficReport.empty())
			{
				const String Folder = ReportsFolder();
				std::filesystem::create_directories(std::filesystem::u8path(Folder.utf8().get_data()));
				const String Stamp = Time::get_singleton()->get_datetime_string_from_system().replace(":", "").replace("-", "").replace("T", "_");
				Self->TrafficReport = Folder.path_join("traffic_" + Stamp + ".csv").utf8().get_data();
				std::ofstream(std::filesystem::u8path(Self->TrafficReport)) << "type,lat,lon,road,class,speed_kmh,size\n";
			}
			std::string Road = P.Road;
			std::replace(Road.begin(), Road.end(), '"', '\'');
			char Line[512];
			std::snprintf(Line, sizeof(Line), "%s,%.6f,%.6f,\"%s\",%s,%.0f,%.2f\n", P.Type.c_str(), Lat, Lon, Road.c_str(), P.Highway.c_str(), P.SpeedKmh, P.Size);
			std::ofstream(std::filesystem::u8path(Self->TrafficReport), std::ios::app) << Line;
			std::snprintf(Line, sizeof(Line), "[Traffic] %s at %.5f,%.5f on %s, %.0f km/h", P.Type.c_str(), Lat, Lon, Road.empty() ? "an unnamed road" : Road.c_str(), P.SpeedKmh);
			Self->Log(Line);
		};
	}
	else if (Piece != S.TrafficPiece)
	{
		// the player is on the next piece: the traffic carries on with its roads
		const Vec2 Shift = S.TrafficPiece->Core->Origin - Piece->Core->Origin;
		S.Traffic->SetRoads(Piece->Core->Roads, Shift);
		static_cast<GodotTrafficCars&>(*S.TrafficCars).Origin = Piece->Core->Origin;
		S.TrafficPiece = Piece;
	}
	geotraffic::PlayerPose Player;
	Player.Position = Drive - Piece->Core->Origin;
	Player.Forward = Vec2(-Forward.z, Forward.x).Normal();
	Player.Velocity = Vec2(-Velocity.z * 100.0, Velocity.x * 100.0);
	S.Traffic->Tick(float(Delta), Player, std::clamp(Wanted, 0, 150));
}

String GeodriveWorld::get_traffic_status() const
{
	return World->Traffic ? String::utf8(World->Traffic->StatusText().c_str()) : String();
}

String GeodriveWorld::get_traffic_debug() const
{
	return World->Traffic ? String::utf8(World->Traffic->DebugLine().c_str()) : String();
}

int GeodriveWorld::get_traffic_count() const
{
	return World->Traffic ? World->Traffic->NumCars() : 0;
}

void GeodriveWorld::clear_traffic()
{
	World->Traffic.reset();
	World->TrafficPiece = nullptr;
}

// --- the robot test driver: Core's, driving the player's car on the roads of the piece it starts in ---

bool GeodriveWorld::start_robot(Node* CarNode, const String& Mode, int TownRoutes, int Shard, int Shards)
{
	State& S = *World;
	GeodriveCar* Car = Object::cast_to<GeodriveCar>(CarNode);
	stop_robot();
	if (!Car || S.Pieces.empty())
	{
		return false;
	}
	const PiecePtr Piece = S.PieceAt(S.Drive(Car->get_global_position()));
	if (!Piece || !Piece->Core->Roads)
	{
		return false;
	}
	State* Self = &S;
	GeodriveWorld* Owner = this;
	const Vec2 Origin = Piece->Core->Origin;
	S.RobotPiece = Piece;
	const bool bDriveLeft = S.Country.bDriveLeft;
	S.Robot = std::make_unique<geotraffic::RobotDriver>(Piece->Core->Roads, Piece->Core->Ground->Projection, [Self, Owner, Origin](const Vec2& At, float Yaw)
	{
		const double Radians = Yaw * 3.14159265358979 / 180.0;
		const Vector3 Facing(float(std::sin(Radians)), 0.0f, float(-std::cos(Radians)));	// (north +X, east +Y in the roads' frame)
		Owner->emit_signal("robot_place_car", Self->Render(At + Origin, 0.0), Facing);
	});
	S.Robot->bDriveLeft = bDriveLeft;
	S.Robot->Log = [Self](const std::string& Line) { Self->Log(Line); };
	S.Robot->Screenshot = [Owner](int32_t Problem) { Owner->emit_signal("robot_screenshot", Problem); };
	const String Stamp = Time::get_singleton()->get_datetime_string_from_system().replace(":", "").replace("-", "").replace("T", "_");
	S.Robot->Stamp = Stamp.utf8().get_data();
	S.Robot->Now = Time::get_singleton()->get_datetime_string_from_system(false, true).left(16).utf8().get_data();
	if (!S.Robot->Start(Mode.utf8().get_data(), TownRoutes, Shard, std::max(Shards, 1)))
	{
		S.Log("[Robot] no routes to drive here");
		S.Robot.reset();
		return false;
	}
	return true;
}

Dictionary GeodriveWorld::update_robot(double Delta, Node* CarNode)
{
	State& S = *World;
	Dictionary Out;
	Out["driving"] = false;
	GeodriveCar* Car = Object::cast_to<GeodriveCar>(CarNode);
	if (!S.Robot || !Car || !S.RobotPiece)
	{
		return Out;
	}
	const Transform3D T = Car->get_global_transform();
	geotraffic::RobotDriver::Car Pose;
	Pose.Position = S.Drive(T.origin) - S.RobotPiece->Core->Origin;
	Pose.Height = T.origin.y * 100.0;
	const Vector3 Forward = -T.basis.get_column(2);
	Pose.Forward = Vec2(-Forward.z, Forward.x).Normal();
	const Vector3 V = Car->get_linear_velocity();
	Pose.Velocity = Vec2(-V.z * 100.0, V.x * 100.0);
	Pose.State = Car->state();
	Pose.MaxSteerDegrees = float(Car->get_max_steer_degrees());
	Pose.Wheelbase = float(Car->get_wheelbase());
	float Steer = 0.0f, Throttle = 0.0f, Brake = 1.0f;
	const bool bDriving = S.Robot->Tick(float(Delta), Pose, Steer, Throttle, Brake);
	Out["driving"] = bDriving;
	Out["steer"] = Steer / std::max(Pose.MaxSteerDegrees, 1.0f);
	Out["throttle"] = Throttle;
	Out["brake"] = Brake;
	if (!bDriving)
	{
		stop_robot();
	}
	return Out;
}

void GeodriveWorld::stop_robot()
{
	State& S = *World;
	if (!S.Robot)
	{
		return;
	}
	std::string Name;
	const std::string Report = S.Robot->Finish(Name);
	if (!Report.empty())
	{
		const String Folder = ReportsFolder();
		std::filesystem::create_directories(std::filesystem::u8path(Folder.utf8().get_data()));
		std::ofstream(std::filesystem::u8path(Folder.path_join(String::utf8(Name.c_str())).utf8().get_data()), std::ios::binary) << Report;
	}
	S.Robot.reset();
	S.RobotPiece = nullptr;
}

bool GeodriveWorld::is_robot_driving() const { return World->Robot && World->Robot->IsDriving(); }

String GeodriveWorld::get_robot_status() const
{
	return World->Robot ? String::utf8(World->Robot->StatusText().c_str()) : String();
}

void GeodriveWorld::draw_map(const RID& Item, Node* CameraNode, const Ref<Font>& LabelFont, int FontSize) const
{
	// the M map, as the Unreal game draws it over the view from above: the main roads as lines (the 3D roads are only
	// built near the car; the map shows the whole piece), and street names along the longer ones, none on another.
	// Every frame: so the roads are sorted once per piece (main, named, longest first), points closer than a few
	// pixels are skipped before they're projected, and the names are capped
	const State& S = *World;
	Camera3D* Camera = Object::cast_to<Camera3D>(CameraNode);
	if (!Camera || S.Pieces.empty())
	{
		return;
	}
	const PiecePtr Piece = S.PieceAt(S.Drive(Camera->get_global_position()));
	if (!Piece || !Piece->Core->Roads)
	{
		return;
	}
	const georoads::RoadGraph& Graph = *Piece->Core->Roads;
	auto Starts = [](const std::string& Text, const char* P) { return Text.rfind(P, 0) == 0; };
	if (S.MapPiece.lock() != Piece || S.MapFontSize != FontSize)
	{
		S.MapPiece = Piece;
		S.MapFontSize = FontSize;
		S.MapRoads.clear();
		S.MapLabels.clear();
		for (int32_t EdgeIndex = 0; EdgeIndex < int32_t(Graph.Edges.size()); ++EdgeIndex)
		{
			const georoads::RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (Edge.Points.size() < 2)
			{
				continue;
			}
			const Vec2 Mid = Edge.Points[Edge.Points.size() / 2];
			const double Half = std::max(georoads::Distance(Mid, Edge.Points.front()), georoads::Distance(Mid, Edge.Points.back()));
			double Length = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				Length += georoads::Distance(Edge.Points[i], Edge.Points[i + 1]);
			}
			const bool bTrunk = Starts(Edge.Highway, "motorway") || Starts(Edge.Highway, "trunk");
			if (bTrunk || Starts(Edge.Highway, "primary") || Starts(Edge.Highway, "secondary") || Starts(Edge.Highway, "tertiary"))
			{
				S.MapRoads.push_back({ EdgeIndex, Mid, Half, Length, bTrunk });
			}
			if (!Edge.Name.empty() && LabelFont.is_valid())
			{
				S.MapLabels.push_back({ EdgeIndex, Mid, Half, Length, String::utf8(Edge.Name.c_str()), Vector2() });
			}
		}
		std::sort(S.MapLabels.begin(), S.MapLabels.end(), [](const State::MapLabel& A, const State::MapLabel& B) { return A.Length > B.Length; });
	}

	RenderingServer* RS = RenderingServer::get_singleton();
	const Rect2 Screen(Vector2(), Camera->get_viewport()->get_visible_rect().size);
	const Vec2 Origin = Piece->Core->Origin;
	// the camera's projection, worked out here as Camera3D::unproject_position and is_position_behind do (calling them
	// for every point of every road was most of the time)
	const Transform3D Eye = Camera->get_camera_transform();
	const Transform3D View = Eye.affine_inverse();
	const Projection Lens = Camera->get_camera_projection();
	const Vector3 Looking = -Eye.basis.get_column(2);
	const float Near = float(Camera->get_near());
	auto Project = [&](const georoads::RoadEdge& Edge, size_t i, bool& bInFront)
	{
		const Vector3 At = S.Render(Edge.Points[i] + Origin, i < Edge.Heights.size() ? Edge.Heights[i] : 0.0);
		bInFront = Looking.dot(At - Eye.origin) >= Near;
		Plane P(View.xform(At), 1.0f);
		P = Lens.xform4(P);
		P.normal /= P.d;
		return Vector2((P.normal.x * 0.5f + 0.5f) * Screen.size.x, (-P.normal.y * 0.5f + 0.5f) * Screen.size.y);
	};
	// only roads that can be in view: within about twice the height of the point under the camera
	const double Height = Camera->get_global_position().y * 100.0;	// cm above the drive's zero
	const Vec2 Below = S.Drive(Camera->get_global_position()) - Origin;
	const double Reach = std::max(Height, 5000.0) * 2.0;
	// a few pixels on the ground at this height (cm): points nearer together than that are one point on the map
	const double AboveGround = Height - Piece->Core->Ground->GroundHeight(Below);
	const double Pixel = std::max(AboveGround, 5000.0) * 2.0 * std::tan(Math::deg_to_rad(Camera->get_fov() * 0.5)) / std::max(Screen.size.y, 1.0f);
	const double Skip2 = (Pixel * 3.0) * (Pixel * 3.0);
	for (const State::MapRoad& Road : S.MapRoads)
	{
		if (georoads::Distance(Road.Mid, Below) >= Reach + Road.Half)
		{
			continue;
		}
		const georoads::RoadEdge& Edge = Graph.Edges[Road.Edge];
		PackedVector2Array Line;
		bool bAnyVisible = false;
		Vec2 Last;
		for (size_t i = 0; i < Edge.Points.size(); ++i)
		{
			const bool bEnd = i + 1 == Edge.Points.size();
			if (!Line.is_empty() && !bEnd)
			{
				const Vec2 D = Edge.Points[i] - Last;
				if (D.X * D.X + D.Y * D.Y < Skip2)
				{
					continue;	// (under 3 pixels from the last: merged with the next)
				}
			}
			bool bInFront = false;
			const Vector2 P = Project(Edge, i, bInFront);
			if (!bInFront)
			{
				Line.clear();
				continue;
			}
			bAnyVisible = bAnyVisible || Screen.has_point(P);
			Line.push_back(P);
			Last = Edge.Points[i];
		}
		if (bAnyVisible && Line.size() >= 2)
		{
			PackedColorArray Colour;
			Colour.push_back(Road.bTrunk ? Color(1.0f, 0.55f, 0.1f) : Color(0.95f, 0.95f, 0.85f));
			RS->canvas_item_add_polyline(Item, Line, Colour, Road.bTrunk ? 3.0f : 2.0f, true);
		}
	}
	if (LabelFont.is_null())
	{
		return;
	}
	// street names along the longer road pieces (longest first), skipping any that would overlap one already drawn
	constexpr size_t MaxNames = 60;
	std::vector<Rect2> Placed;
	std::set<std::string> NamesDrawn;
	const double MinLength = Height * 0.15;	// cm, as a share of the height
	for (const State::MapLabel& Label : S.MapLabels)
	{
		if (Placed.size() >= MaxNames)
		{
			break;
		}
		if (georoads::Distance(Label.Mid, Below) >= Reach + Label.Half)
		{
			continue;
		}
		const georoads::RoadEdge& Edge = Graph.Edges[Label.Edge];
		if (Label.Length < MinLength && NamesDrawn.count(Edge.Name))
		{
			continue;
		}
		bool bInFront = false;
		const Vector2 P = Project(Edge, Edge.Points.size() / 2, bInFront);
		if (!bInFront || !Screen.has_point(P))
		{
			continue;
		}
		if (!Label.bMeasured)
		{
			Label.Size = LabelFont->get_string_size(Label.Name, HORIZONTAL_ALIGNMENT_LEFT, -1, FontSize);
			Label.bMeasured = true;
		}
		const Rect2 Box(P - Label.Size * 0.5f - Vector2(6.0f, 3.0f), Label.Size + Vector2(12.0f, 6.0f));
		bool bOverlaps = false;
		for (const Rect2& Other : Placed)
		{
			if (Other.intersects(Box))
			{
				bOverlaps = true;
				break;
			}
		}
		if (bOverlaps)
		{
			continue;
		}
		Placed.push_back(Box);
		NamesDrawn.insert(Edge.Name);
		const Vector2 Baseline(Box.position.x + 6.0f, Box.position.y + 3.0f + float(LabelFont->get_ascent(FontSize)));
		LabelFont->draw_string(Item, Baseline + Vector2(1.0f, 1.0f), Label.Name, HORIZONTAL_ALIGNMENT_LEFT, -1, FontSize, Color(0, 0, 0));
		LabelFont->draw_string(Item, Baseline, Label.Name, HORIZONTAL_ALIGNMENT_LEFT, -1, FontSize, Color(1, 1, 1));
	}
}

String GeodriveWorld::get_attribution() const
{
	// what the data's licences ask for, as the Unreal game shows it: OpenStreetMap always, and the height data the
	// built pieces used (SRTM tiles always; Copernicus and the Environment Agency's LiDAR where they were used)
	bool bWorldCover = false;
	bool bCopernicus = false, bLidar = false, bRlp = false;
	for (const PiecePtr& P : World->Pieces)
	{
		bCopernicus = bCopernicus || P->Core->bUsedCopernicus;
		bLidar = bLidar || (P->Core->bUsedLidar && P->Core->FineGrid != int(geotile::FineGrid::Utm32));
		bRlp = bRlp || (P->Core->bUsedLidar && P->Core->FineGrid == int(geotile::FineGrid::Utm32));
		bWorldCover = bWorldCover || P->Core->bUsedWorldCover;
	}
	std::string Text = "Map data (c) OpenStreetMap contributors - openstreetmap.org/copyright.  "
		"Terrain: SRTM courtesy of the U.S. Geological Survey, ETOPO1 NOAA - Terrain Tiles via AWS Open Data";
	if (bCopernicus)
	{
		Text += ". Produced using Copernicus WorldDEM-30 (c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA";
	}
	if (bLidar)
	{
		Text += ". Contains Environment Agency information (c) Environment Agency and/or database right (OGL v3)";
	}
	if (bRlp)
	{
		Text += ". Heights (c) GeoBasis-DE / LVermGeoRP, dl-de/by-2-0, www.lvermgeo.rlp.de";
	}
	if (bWorldCover)
	{
		Text += ". Land cover (c) ESA WorldCover project 2021 / Contains modified Copernicus Sentinel data (2021) processed by ESA WorldCover consortium (CC BY 4.0)";
	}
	return String::utf8(Text.c_str());
}

bool GeodriveWorld::is_uk(double Lat, double Lon) { return InUK(Lat, Lon); }

bool GeodriveWorld::has_map_at(double Lat, double Lon)
{
	World->MapFiles();
	return World->RegionFor(Lat - 0.01, Lon - 0.01, Lat + 0.01, Lon + 0.01) != nullptr;
}

int GeodriveWorld::refresh_maps()
{
	const size_t Before = World->Regions.size() + World->HeightFiles.size();
	World->MapFiles(true);
	return int(World->Regions.size() + World->HeightFiles.size() - Before);
}

Dictionary GeodriveWorld::get_country() const
{
	const georoads::CountryRules& C = World->Country;
	Dictionary Out;
	Out["code"] = String::utf8(C.Code.c_str());
	Out["name"] = String::utf8(C.Name.c_str());
	Out["drive_left"] = C.bDriveLeft;
	Out["mph"] = C.bMph;
	return Out;
}
double GeodriveWorld::get_game_memory() { return ProcessMemoryBytes(); }
double GeodriveWorld::get_game_cpu() { return ProcessCpuPercent(); }

bool GeodriveWorld::UpdatePlantMap(bool bFar)
{
	State& S = *World;
	State::PlantWindow& W = bFar ? S.FarPlants : S.NearPlants;
	const int32_t N = State::PlantCells;
	// a finished one: into the textures (the shaders see it from the next frame)
	if (W.Job.valid())
	{
		if (W.Job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			return false;
		}
		State::PlantWindow::Made Made = W.Job.get();
		PackedByteArray Bytes;
		Bytes.resize(int64_t(Made.Texels.size()));
		std::memcpy(Bytes.ptrw(), Made.Texels.data(), Made.Texels.size());
		PackedByteArray HeightBytes;
		HeightBytes.resize(int64_t(Made.Heights.size() * sizeof(float)));
		std::memcpy(HeightBytes.ptrw(), Made.Heights.data(), Made.Heights.size() * sizeof(float));
		const Ref<Image> HeightPicture = Image::create_from_data(N, N, false, Image::FORMAT_RGF, HeightBytes);
		const Ref<Image> Picture = Image::create_from_data(N, N, false, Image::FORMAT_RGBA8, Bytes);
		// (new textures each time: overwriting ones the graphics card is drawing with made the game wait for it, ~85 ms)
		W.Heights = ImageTexture::create_from_image(HeightPicture);
		W.Texture = ImageTexture::create_from_image(Picture);
		W.CpuTexels = std::make_shared<const std::vector<uint8_t>>(std::move(Made.Texels));
		W.CpuHeights = std::make_shared<const std::vector<float>>(std::move(Made.Heights));
		W.Min = Made.Min;
		W.Pieces = Made.Pieces;
		char Line[128];
		std::snprintf(Line, sizeof(Line), "[Plants] the %s plant map (%d m cells, %.1f km) made in %.0f ms (on a worker)", bFar ? "far" : "near",
			int(W.Cell / 100.0), W.Cell * N / 100000.0, Made.Ms);
		S.Log(Line);
		return true;
	}
	if (!S.bReady || S.Pieces.empty())
	{
		return false;
	}
	// its corner on its grid with the camera near the middle: made again when that moves or the pieces change
	const double Cell = W.Cell;
	const double Span = Cell * N;
	const Vec2 Focus = S.Drive(S.Focus);
	const Vec2 Wanted(std::floor((Focus.X - Span * 0.5) / W.Grid + 0.5) * W.Grid, std::floor((Focus.Y - Span * 0.5) / W.Grid + 0.5) * W.Grid);
	if (W.Texture.is_valid() && Wanted.X == W.Min.X && Wanted.Y == W.Min.Y && W.Pieces == S.Pieces.size())
	{
		return false;
	}
	// made on a worker from its own list of the pieces (they stay alive while it has them; it only reads them)
	W.Job = std::async(std::launch::async, [Pieces = S.Pieces, Wanted, Cell, N, Span]() mutable
	{
		const auto Start = std::chrono::steady_clock::now();
		const double Fine = geoground::VegetationMap::Cell;
		const int32_t Sub = std::max(1, int32_t(Cell / Fine + 0.5));	// (the pieces' cells in each of its across)
		auto PieceAt = [&Pieces](const Vec2& P) -> const PiecePtr&
		{
			for (auto It = Pieces.rbegin(); It != Pieces.rend(); ++It)
			{
				if ((*It)->Core->Ground->Bounds.IsInside(P - (*It)->Core->Origin))
				{
					return *It;
				}
			}
			return Pieces.back();
		};
		State::PlantWindow::Made Out;
		Out.Min = Wanted;
		Out.Pieces = Pieces.size();
		Out.Texels.assign(size_t(N) * N * 4, 0);
		Out.Heights.assign(size_t(N) * N * 2, 0.0f);
		uint8_t* To = Out.Texels.data();
		float* Heights = Out.Heights.data();
		// (rows in bands across the cores)
		auto Band = [&](int32_t FirstRow, int32_t EndRow)
		{
			for (int32_t Row = FirstRow; Row < EndRow; ++Row)
			{
				const double North = Wanted.X + Span - (Row + 0.5) * Cell;	// (rows run south)
				for (int32_t Col = 0; Col < N; ++Col)
				{
					const Vec2 P(North, Wanted.Y + (Col + 0.5) * Cell);
					const PiecePtr& Piece = PieceAt(P);
					const size_t At = size_t(Row) * N + size_t(Col);
					float Room = 40.0f;	// m to the nearest road edge or wall (the map's Room; the far map leaves it out)
					const geoground::VegetationMap* Map = Piece && Piece->Core ? Piece->Core->Vegetation.get() : nullptr;
					if (Map && Map->IsValid())
					{
						// the average of the pieces' cells under it (one, for the near map), the class of the first
						uint32_t Sum[3] = { 0, 0, 0 };
						int32_t Count = 0;
						for (int32_t i = 0; i < Sub * Sub; ++i)
						{
							const Vec2 Q = P + Vec2((double(i / Sub) + 0.5 - Sub * 0.5) * Fine, (double(i % Sub) + 0.5 - Sub * 0.5) * Fine);
							const Vec2 Local = Q - Piece->Core->Origin - Map->Min;
							const int32_t X = int32_t(std::floor(Local.X / Fine)), Y = int32_t(std::floor(Local.Y / Fine));
							if (X < 0 || Y < 0 || X >= Map->Width || Y >= Map->Height)
							{
								continue;
							}
							const uint8_t* Texel = &Map->Texels[(size_t(X) * size_t(Map->Height) + size_t(Y)) * 4];
							if (Count++ == 0)
							{
								To[At * 4 + 3] = Texel[3];
								if (Sub == 1 && !Map->Room.empty())
								{
									const uint8_t R = Map->Room[size_t(X) * size_t(Map->Height) + size_t(Y)];
									Room = R == 255 ? 40.0f : (float(R) - geoground::VegetationMap::RoomZero) / geoground::VegetationMap::RoomScale;
								}
							}
							for (int32_t k = 0; k < 3; ++k)
							{
								Sum[k] += Texel[k];
							}
						}
						for (int32_t k = 0; Count > 0 && k < 3; ++k)
						{
							To[At * 4 + k] = uint8_t(Sum[k] / uint32_t(Count));
						}
					}
					// the ground: near a road the final one, after its cutting or embankment (the land as it was would
					// leave trees buried beside a road on a bank, or floating over a cutting); elsewhere they're the same,
					// and the bare height is far quicker to find
					const geoground::Terrain* Ground = Piece && Piece->Core ? Piece->Core->Ground.get() : nullptr;
					const Vec2 Local = P - (Piece && Piece->Core ? Piece->Core->Origin : Vec2());
					float Height = 0.0f;
					if (Ground && Room < 20.0f)
					{
						Height = Ground->GroundHeight(Local);
						// a road's cutting or bank (the ground moved over 1.5 m from the land as it was): nothing grows on
						// the earthworks (as no room)
						if (std::fabs(Height - Ground->BareHeight(Local)) > 150.0f)
						{
							Room = std::min(Room, 0.0f);
						}
					}
					else if (Ground)
					{
						Height = Ground->BareHeight(Local);
					}
					Heights[At * 2] = Height / 100.0f;
					Heights[At * 2 + 1] = Room;
				}
			}
		};
		const int32_t Bands = std::clamp(int32_t(std::thread::hardware_concurrency()) - 2, 1, 16);	// (leaving the game a couple of cores)
		std::vector<std::future<void>> Running;
		for (int32_t i = 1; i < Bands; ++i)
		{
			Running.push_back(std::async(std::launch::async, Band, N * i / Bands, N * (i + 1) / Bands));
		}
		Band(0, N / Bands);
		for (std::future<void>& Each : Running)
		{
			Each.wait();
		}
		Out.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		// (let go of the pieces here: if one was let go meanwhile, this holds the last of it, and freeing a piece takes
		// ~80 ms: on the main thread, when it took the result, that was a stutter with every new piece)
		Pieces.clear();
		return Out;
	});
	return false;
}

void GeodriveWorld::set_plant_clearings(const Array& Clearings)
{
	// each [reach m, whether it's an area, [lat, lon, lat, lon, ...]]: kept for the pieces built from now on (so the
	// trees are never there, rather than cleared off while playing)
	auto Out = std::make_shared<std::vector<geoworld::PlantClearing>>();
	for (int64_t i = 0; i < Clearings.size(); ++i)
	{
		const Array Each = Clearings[i];
		if (Each.size() < 3)
		{
			continue;
		}
		geoworld::PlantClearing Clear;
		Clear.Reach = double(Each[0]) * 100.0;
		Clear.bArea = bool(Each[1]);
		const PackedFloat64Array Points = Each[2];
		for (int64_t p = 0; p + 1 < Points.size(); p += 2)
		{
			Clear.Line.emplace_back(Points[p], Points[p + 1]);
		}
		if (!Clear.Line.empty())
		{
			Out->push_back(std::move(Clear));
		}
	}
	World->PlantClearings = std::move(Out);
}

void GeodriveWorld::UpdateRoadMap()
{
	State& S = *World;
	State::RoadWindow& W = S.RoadMap;
	const int32_t N = State::RoadCells;
	const double Cell = State::RoadCell, Span = Cell * N;
	// a finished one: into its texture (made, with its smaller versions, on the worker; a new texture each time: see the
	// plant maps)
	if (W.Job.valid())
	{
		if (W.Job.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		{
			return;
		}
		State::RoadWindow::Made Made = W.Job.get();
		if (Made.Picture.is_valid())
		{
			W.Texture = ImageTexture::create_from_image(Made.Picture);
		}
		W.Min = Made.Min;
		W.Pieces = Made.Pieces;
		char Line[160];
		std::snprintf(Line, sizeof(Line), "[Roads] the painted road map (%.0f km) made in %.0f ms (on a worker; %d map tiles read)", Span / 100000.0,
			Made.Ms, Made.TilesRead);
		S.Log(Line);
		return;
	}
	if (!S.bReady || S.Pieces.empty())
	{
		return;
	}
	const Vec2 Focus = S.Drive(S.Focus);
	const double Grid = 500000.0;	// cm: its corner on a 5 km grid
	const Vec2 Wanted(std::floor((Focus.X - Span * 0.5) / Grid + 0.5) * Grid, std::floor((Focus.Y - Span * 0.5) / Grid + 0.5) * Grid);
	if (W.Texture.is_valid() && Wanted.X == W.Min.X && Wanted.Y == W.Min.Y && W.Pieces == S.Pieces.size())
	{
		return;
	}
	// the country's map file there (it stays mapped), and the road lines read before (kept, for the next window)
	double FocusLat = 0.0, FocusLon = 0.0;
	S.Projection.ToLatLon(Focus, FocusLat, FocusLon);
	const MappedFile* Region = S.RegionFor(FocusLat - 0.01, FocusLon - 0.01, FocusLat + 0.01, FocusLon + 0.01);
	if (!S.RoadTiles)
	{
		S.RoadTiles = std::make_shared<State::RoadTileCache>();
	}
	W.Job = std::async(std::launch::async, [Pieces = S.Pieces, Cache = S.RoadTiles, Region, Projection = S.Projection, Wanted, N, Cell, Span]() mutable
	{
		LowerPriority();
		const auto Start = std::chrono::steady_clock::now();
		State::RoadWindow::Made Out;
		Out.Min = Wanted;
		Out.Pieces = Pieces.size();
		// each cell: the offset to the nearest road's centre (east, south; m) and that road's half width, so the shader
		// draws a road's edge exactly where it is at any distance (a straight road's offset changes evenly from cell to
		// cell, so blending between cells is exact: the way crisp text is drawn from a small map). RGBA8: R, G the offset
		// (0.5 m steps, +-63.5 m), B the half width (0.25 m steps), A built up
		const double Reach = 3.0;	// cells round a road the offset is kept
		std::vector<float> Nearest(size_t(N) * N, 1e9f);
		PackedByteArray Bytes;
		Bytes.resize(int64_t(N) * N * 4);
		uint8_t* T = Bytes.ptrw();
		for (int64_t i = 0; i < int64_t(N) * N; ++i)
		{
			T[i * 4 + 0] = 255;	// (far from any road)
			T[i * 4 + 1] = 255;
			T[i * 4 + 2] = 0;
			T[i * 4 + 3] = 0;
		}
		// (rows run south from the north edge, columns east: as the plant maps)
		auto Paint = [&](const Vec2& A, const Vec2& B, float HalfWidth)
		{
			const double AX = (A.Y - Wanted.Y) / Cell, AY = (Wanted.X + Span - A.X) / Cell;
			const double BX = (B.Y - Wanted.Y) / Cell, BY = (Wanted.X + Span - B.X) / Cell;
			const int32_t X0 = std::max(0, int32_t(std::floor(std::min(AX, BX) - Reach))), X1 = std::min(N - 1, int32_t(std::ceil(std::max(AX, BX) + Reach)));
			const int32_t Y0 = std::max(0, int32_t(std::floor(std::min(AY, BY) - Reach))), Y1 = std::min(N - 1, int32_t(std::ceil(std::max(AY, BY) + Reach)));
			if (X0 > X1 || Y0 > Y1)
			{
				return;
			}
			const double DX = BX - AX, DY = BY - AY;
			const double Length2 = std::max(DX * DX + DY * DY, 1e-9);
			const double Metres = Cell / 100.0;
			for (int32_t Y = Y0; Y <= Y1; ++Y)
			{
				for (int32_t X = X0; X <= X1; ++X)
				{
					const double PX = X + 0.5, PY = Y + 0.5;
					const double F = std::clamp(((PX - AX) * DX + (PY - AY) * DY) / Length2, 0.0, 1.0);
					const double OX = (AX + DX * F - PX) * Metres, OY = (AY + DY * F - PY) * Metres;	// m: to the road, east and south
					const float D2 = float(OX * OX + OY * OY);
					float& Best = Nearest[size_t(Y) * N + size_t(X)];
					if (D2 >= Best || D2 > float(Reach * Reach * Metres * Metres))
					{
						continue;
					}
					Best = D2;
					uint8_t* Texel = &T[(size_t(Y) * N + size_t(X)) * 4];
					Texel[0] = uint8_t(std::clamp(std::lround(OX * 2.0) + 128, 0L, 255L));
					Texel[1] = uint8_t(std::clamp(std::lround(OY * 2.0) + 128, 0L, 255L));
					Texel[2] = uint8_t(std::clamp(std::lround(HalfWidth * 4.0), 0L, 255L));
				}
			}
		};
		// every road in the window, from the map's tiles (those not read before), as wide as its kind
		if (Region)
		{
			double South, West, North, East;
			Projection.ToLatLon(Wanted, South, West);
			Projection.ToLatLon(Wanted + Vec2(Span, Span), North, East);
			auto E7 = [](double Degrees) { return int32_t(std::llround(Degrees * 1e7)); };
			std::string Error;
			geotile::ForEachRoadTile(Region->Data, Region->Size, E7(std::min(South, North)), E7(std::min(West, East)), E7(std::max(South, North)), E7(std::max(West, East)),
				[&](uint64_t Id) { return Cache->Lines.find(Id) == Cache->Lines.end(); },
				[&](uint64_t Id, const std::string& Highway, const std::vector<geotile::LatLon>& Line)
				{
					auto& Lines = Cache->Lines[Id];
					if (Line.empty())
					{
						Cache->Order.push_back(Id);
						++Out.TilesRead;
						return;
					}
					// (half widths, m: a carriageway's, as the map draws each of a dual one's on its own)
					static const std::map<std::string, float> Widths = { { "motorway", 5.5f }, { "trunk", 5.0f }, { "primary", 4.5f },
						{ "secondary", 4.0f }, { "tertiary", 3.5f }, { "motorway_link", 3.5f }, { "trunk_link", 3.5f }, { "primary_link", 3.2f },
						{ "secondary_link", 3.0f }, { "tertiary_link", 3.0f }, { "service", 2.3f }, { "track", 1.6f } };
					// (only what cars drive on: footpaths, trails, steps and the like aren't streets seen from above)
					static const std::set<std::string> NotRoads = { "footway", "path", "cycleway", "bridleway", "steps", "pedestrian",
						"corridor", "platform", "construction", "proposed", "abandoned", "elevator", "via_ferrata", "raceway", "bus_stop" };
					if (NotRoads.count(Highway))
					{
						return;
					}
					const auto Found = Widths.find(Highway);
					State::RoadTileCache::Road Road;
					Road.HalfWidth = Found != Widths.end() ? Found->second : 2.8f;	// (streets)
					for (const geotile::LatLon& P : Line)
					{
						Road.Points.push_back(Projection.ToWorld(P.Lat / 1e7, P.Lon / 1e7));
					}
					Lines.push_back(std::move(Road));
				}, Error);
			// (kept to ~3,000 tiles, about four windows' worth: the oldest go)
			while (Cache->Order.size() > 3000)
			{
				Cache->Lines.erase(Cache->Order.front());
				Cache->Order.pop_front();
			}
		}
		for (const auto& Entry : Cache->Lines)
		{
			for (const State::RoadTileCache::Road& Road : Entry.second)
			{
				for (size_t i = 0; i + 1 < Road.Points.size(); ++i)
				{
					Paint(Road.Points[i], Road.Points[i + 1], Road.HalfWidth);
				}
			}
		}
		// built up (the pieces' land cover): the towns' grey
		for (const PiecePtr& Piece : Pieces)
		{
			const geoground::VegetationMap* Map = Piece && Piece->Core ? Piece->Core->Vegetation.get() : nullptr;
			if (!Map || !Map->IsValid())
			{
				continue;
			}
			const Vec2 Origin = Piece->Core->Origin;
			for (int32_t X = 0; X < Map->Width; X += 2)
			{
				for (int32_t Y = 0; Y < Map->Height; Y += 2)
				{
					if (Map->Texels[(size_t(X) * size_t(Map->Height) + size_t(Y)) * 4 + 3] != geoground::LandBuilt)
					{
						continue;
					}
					const Vec2 P = Origin + Map->Min + Vec2((X + 1.0) * geoground::VegetationMap::Cell, (Y + 1.0) * geoground::VegetationMap::Cell);
					const int32_t Col = int32_t((P.Y - Wanted.Y) / Cell), Row = int32_t((Wanted.X + Span - P.X) / Cell);
					if (Col >= 0 && Row >= 0 && Col < N && Row < N)
					{
						T[(size_t(Row) * N + size_t(Col)) * 4 + 3] = 255;
					}
				}
			}
		}
		Pieces.clear();	// (let go here: see the plant maps)
		Out.Picture = Image::create_from_data(N, N, false, Image::FORMAT_RGBA8, Bytes);	// (no smaller versions: offsets don't average)
		Out.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		return Out;
	});
}

Ref<Texture2D> GeodriveWorld::get_road_texture() const
{
	return World->RoadMap.Texture;
}

Vector3 GeodriveWorld::get_road_origin() const
{
	const State& S = *World;
	return S.Render(S.RoadMap.Min + Vec2(State::RoadCell * State::RoadCells, 0.0), 0.0);
}

double GeodriveWorld::get_road_size() const
{
	return State::RoadCell * State::RoadCells / 100.0;
}

Ref<Texture2D> GeodriveWorld::get_plant_heights(bool bFar) const
{
	return (bFar ? World->FarPlants : World->NearPlants).Heights;
}

Vector3 GeodriveWorld::get_render_offset() const
{
	// Godot's x is east (the drive's y), -z north (its x); metres
	const State& S = *World;
	return Vector3(float(S.RenderOrigin.Y / 100.0), 0.0f, float(-S.RenderOrigin.X / 100.0));
}

Ref<ArrayMesh> GeodriveWorld::make_plant_mesh(int Kind, int Seed, int Detail)
{
	geoground::MeshData Data;
	geomesh::BuildPlant(geomesh::PlantKind(std::clamp(Kind, 0, int(geomesh::PlantKind::Count) - 1)), uint32_t(Seed), Detail, Data);
	Ref<ArrayMesh> Mesh;
	Mesh.instantiate();
	AddSurface(Mesh, Data, false, Ref<Material>(), true);
	return Mesh;
}

Ref<ArrayMesh> GeodriveWorld::make_aircraft_mesh(int Kind, bool bGear)
{
	geoground::MeshData Data;
	geomesh::BuildAircraft(geomesh::AircraftKind(std::clamp(Kind, 0, int(geomesh::AircraftKind::Count) - 1)), bGear, Data);
	Ref<ArrayMesh> Mesh;
	Mesh.instantiate();
	AddSurface(Mesh, Data, true, Ref<Material>());
	return Mesh;
}

Dictionary GeodriveWorld::body_measures(double HeightCm)
{
	const geopeople::BodyMeasures B = geopeople::MeasureBody(float(HeightCm));
	Dictionary Out;	// (metres and m/s)
	Out["height"] = B.Height / 100.0;
	Out["eye_standing"] = B.EyeStanding / 100.0;
	Out["eye_seated"] = B.EyeSeated / 100.0;
	Out["radius"] = B.Radius / 100.0;
	Out["step_length"] = B.StepLength / 100.0;
	Out["step_up"] = B.StepUp / 100.0;
	Out["walk_speed"] = B.WalkSpeed / 100.0;
	Out["run_speed"] = B.RunSpeed / 100.0;
	Out["sprint_speed"] = B.SprintSpeed / 100.0;
	Out["jump_speed"] = B.JumpSpeed / 100.0;
	return Out;
}

Vector3 GeodriveWorld::walk(double HeightCm, double Forward, double Right, bool bRun, bool bSprint, bool bJump, double Yaw, bool bOnGround, const Vector3& Velocity, double Delta)
{
	// Godot's x is east (Core's y), y up (z), z south (-x); its yaw turns from -z (north) towards -x (west), Core's from
	// north towards east
	const geopeople::BodyMeasures B = geopeople::MeasureBody(float(HeightCm));
	geopeople::WalkInput Input;
	Input.Forward = float(Forward);
	Input.Right = float(Right);
	Input.bRun = bRun;
	Input.bSprint = bSprint;
	Input.bJump = bJump;
	const geopeople::Vec3 Now{ float(-Velocity.z * 100.0), float(Velocity.x * 100.0), float(Velocity.y * 100.0) };
	const geopeople::Vec3 Next = geopeople::Walk(B, Input, float(-Yaw), bOnGround, Now, float(Delta));
	return Vector3(Next.Y / 100.0f, Next.Z / 100.0f, -Next.X / 100.0f);
}

Ref<Texture2D> GeodriveWorld::get_plant_texture(bool bFar) const
{
	return (bFar ? World->FarPlants : World->NearPlants).Texture;
}

namespace
{
	// a plant's own key: its channel (trees 0, bushes 1) and its fine square
	uint64_t PlantKey(int32_t Channel, int32_t FX, int32_t FZ)
	{
		return ((uint64_t(uint32_t(FX)) << 32) | uint64_t(uint32_t(FZ))) ^ (Channel != 0 ? 0x9E3779B97F4A7C15ull : 0ull);
	}
}

void GeodriveWorld::remove_plant(int Channel, int FX, int FZ)
{
	State& S = *World;
	auto Next = std::make_shared<std::unordered_set<uint64_t>>(*S.RemovedPlants);
	Next->insert(PlantKey(Channel, FX, FZ));
	S.RemovedPlants = Next;
}

void GeodriveWorld::set_removed_plants(const Array& Plants)
{
	auto Next = std::make_shared<std::unordered_set<uint64_t>>();
	for (int64_t i = 0; i < Plants.size(); ++i)
	{
		const Array P = Plants[i];
		if (P.size() == 3)
		{
			Next->insert(PlantKey(int32_t(P[0]), int32_t(P[1]), int32_t(P[2])));
		}
	}
	World->RemovedPlants = Next;
}

int GeodriveWorld::get_removed_plant_count() const { return int(World->RemovedPlants->size()); }

namespace
{
	// (the plant shader's own: vegetation.gdshader, in the same float steps)
	float Fract(float X) { return X - std::floor(X); }
	float Hash(float X, float Y)
	{
		float PX = Fract(X * 0.1031f), PY = Fract(Y * 0.1030f);
		const float D = PX * (PY + 33.33f) + PY * (PX + 33.33f);
		PX += D;
		PY += D;
		return Fract((PX + PY) * PX);
	}
	int32_t Choose(float Pick, float Land, bool bCape)
	{
		const bool bTown = Land > 45.0f && Land < 55.0f, bScrub = Land > 15.0f && Land < 25.0f;
		float Broad, Conifer;
		if (bCape)
		{
			Broad = bTown ? 0.7f : (bScrub ? 0.2f : 0.3f);
			Conifer = bTown ? 0.15f : (bScrub ? 0.5f : 0.4f);
		}
		else
		{
			Broad = bTown ? 0.85f : 0.72f;
			Conifer = 1.0f - Broad;
		}
		return Pick < Broad ? 0 : (Pick < Broad + Conifer ? 1 : 2);
	}
}

bool GeodriveWorld::place_plants(const Array& Rings, const Vector3& Eye, bool bCape)
{
	// which plants grow round the camera, ring by ring (as vegetation.gdshader chose them on the graphics card, square by
	// square: now only those that grow are sent, a few percent of the squares), on a worker; take_plants hands them over
	State& S = *World;
	const State::PlantWindow& W = S.NearPlants;
	if (S.PlantJob.valid() || !W.CpuTexels || !W.CpuHeights)
	{
		return false;
	}
	struct Ring { int32_t Index, Kind, Channel; float Cell, Fine, Inner, Outer, Grow, Tile; };
	std::vector<Ring> List;
	for (int64_t i = 0; i < Rings.size(); ++i)
	{
		const Dictionary D = Rings[i];
		List.push_back({ int32_t(D.get("index", 0)), int32_t(D.get("kind", 0)), int32_t(D.get("channel", 0)), float(double(D.get("cell", 7.0))),
			float(double(D.get("fine_cell", 7.0))), float(double(D.get("inner", 0.0))), float(double(D.get("outer", 100.0))), float(double(D.get("grow", 1.0))),
			float(double(D.get("tile", 60.0))) });
	}
	// world-fixed metres (Godot's, but for where its 0 is: x east, z south); the map's north-west corner in them
	const Vector3 Offset = get_render_offset();
	const double EyeX = Eye.x + Offset.x, EyeZ = Eye.z + Offset.z;
	const int32_t N = State::PlantCells;
	const double Size = W.Cell * N / 100.0;
	const double MapX = W.Min.Y / 100.0, MapZ = -(W.Min.X + W.Cell * N) / 100.0;
	const auto Texels = W.CpuTexels;
	const auto Heights = W.CpuHeights;
	const auto Gone = S.RemovedPlants;	// (cut down: as the set is now)
	S.PlantJob = std::async(std::launch::async, [List, EyeX, EyeZ, N, Size, MapX, MapZ, Texels, Heights, bCape, Gone]() -> State::Placed
	{
		LowerPriority();
		const auto Start = std::chrono::steady_clock::now();
		State::Placed Out;
		// (the instances measured from here, a 100 m point by the camera: small numbers, exact)
		Out.AnchorX = std::floor(EyeX / 100.0) * 100.0;
		Out.AnchorZ = std::floor(EyeZ / 100.0) * 100.0;
		// the maps, sampled as the graphics card's linear filter does (clamped at their edges)
		auto Sample = [N](const float* Grid, int32_t Stride, int32_t Channel, float U, float V)
		{
			const float FX = U * N - 0.5f, FY = V * N - 0.5f;
			const int32_t X0 = int32_t(std::floor(FX)), Y0 = int32_t(std::floor(FY));
			const float TX = FX - X0, TY = FY - Y0;
			auto At = [&](int32_t X, int32_t Y)
			{
				X = std::clamp(X, 0, N - 1);
				Y = std::clamp(Y, 0, N - 1);
				return Grid[(size_t(Y) * N + size_t(X)) * Stride + Channel];
			};
			return (At(X0, Y0) * (1.0f - TX) + At(X0 + 1, Y0) * TX) * (1.0f - TY) + (At(X0, Y0 + 1) * (1.0f - TX) + At(X0 + 1, Y0 + 1) * TX) * TY;
		};
		// (the texels as floats once: 0-1, as the shader reads an RGBA8 texture)
		std::vector<float> Map(Texels->size());
		for (size_t i = 0; i < Map.size(); ++i)
		{
			Map[i] = (*Texels)[i] / 255.0f;
		}
		const float* M = Map.data();
		const float* H = Heights->data();
		const float Texel = 1.0f / N;
		const float Across = float(Size) * Texel * 2.0f;
		for (const Ring& R : List)
		{
			std::map<std::pair<int32_t, int32_t>, State::PlacedTile> Tiles;
			const int32_t Grid = int32_t(std::ceil(R.Outer * 2.0f / R.Cell)) + 1;
			const float CX0 = std::floor(float(EyeX) / R.Cell) - float(Grid / 2), CZ0 = std::floor(float(EyeZ) / R.Cell) - float(Grid / 2);
			const float Ratio = std::max(std::floor(R.Cell / R.Fine + 0.5f), 1.0f);
			for (int32_t j = 0; j < Grid; ++j)
			{
				for (int32_t i = 0; i < Grid; ++i)
				{
					const float CX = CX0 + i, CZ = CZ0 + j;
					// (a square wholly outside the ring: nothing to look at)
					const double Rough = std::hypot((CX + 0.5) * R.Cell - EyeX, (CZ + 0.5) * R.Cell - EyeZ);
					if (Rough < R.Inner - 21.0f - R.Cell || Rough > R.Outer + R.Cell)
					{
						continue;
					}
					const float FX = CX * Ratio + std::floor(Hash(CX * 1.7f + 3.1f, CZ * 1.7f + 3.1f) * Ratio);
					const float FZ = CZ * Ratio + std::floor(Hash(CX * 2.3f + 7.7f, CZ * 2.3f + 7.7f) * Ratio);
					const double SX = (FX + 0.15f + 0.7f * Hash(FX, FZ)) * R.Fine, SZ = (FZ + 0.15f + 0.7f * Hash(FX + 17.31f, FZ + 17.31f)) * R.Fine;
					const float U = float((SX - MapX) / Size), V = float((SZ - MapZ) / Size);
					if (U <= 0.0f || U >= 1.0f || V <= 0.0f || V >= 1.0f)
					{
						continue;
					}
					const float D = float(std::hypot(SX - EyeX, SZ - EyeZ));
					const float Edge = Hash(FX + 5.13f, FZ + 5.13f);
					if (!(D > R.Inner - 20.0f * Edge && D < R.Outer - 20.0f * Edge))
					{
						continue;
					}
					const float Plant = R.Channel == 0 ? Hash(FX + 41.7f, FZ + 41.7f) : 0.0f;
					const int32_t Chosen = R.Channel == 0 ? Choose(Plant, Sample(M, 4, 3, U, V) * 255.0f, bCape) : 3;
					if (R.Kind >= 0 && Chosen != R.Kind)
					{
						continue;
					}
					float Density = Sample(M, 4, R.Channel == 0 ? 0 : 1, U, V);
					const float Grown = (0.7f + (1.3f - 0.7f) * Hash(FX + 23.9f, FZ + 23.9f)) * R.Grow;
					const float Room = Sample(H, 2, 1, U, V);
					const float Needs = std::max(5.0f, (Chosen == 3 ? 0.9f : (Chosen == 1 ? 3.3f : 2.2f)) * Grown);
					const float Slope = std::hypot(Sample(H, 2, 0, U + Texel, V) - Sample(H, 2, 0, U - Texel, V), Sample(H, 2, 0, U, V + Texel) - Sample(H, 2, 0, U, V - Texel)) / Across;
					auto Smooth = [](float A, float B, float X) { const float T = std::clamp((X - A) / (B - A), 0.0f, 1.0f); return T * T * (3.0f - 2.0f * T); };
					Density *= Chosen == 3 ? 1.0f - Smooth(0.8f, 1.1f, Slope) : 1.0f - Smooth(0.58f, 0.78f, Slope);
					if (!(Hash(FX + 9.71f, FZ + 9.71f) < Density && Room > Needs))
					{
						continue;
					}
					// (cut down)
					if (!Gone->empty() && Gone->count(PlantKey(R.Channel, int32_t(FX), int32_t(FZ))))
					{
						continue;
					}
					// it grows: turned and sized by its own numbers, on the ground; its tile (for culling) by where it is
					const float Turn = Hash(FX + 61.3f, FZ + 61.3f) * 6.2832f;
					const float C = std::cos(Turn) * Grown, Sn = std::sin(Turn) * Grown;
					const float Y = Sample(H, 2, 0, U, V);
					const float OX = float(SX - Out.AnchorX), OZ = float(SZ - Out.AnchorZ);
					State::PlacedTile& T = Tiles[{ int32_t(std::floor(SX / R.Tile)), int32_t(std::floor(SZ / R.Tile)) }];
					// (Godot's multimesh rows: the basis's rows and the origin; then its own numbers: kind, -, square)
					T.Buffer.insert(T.Buffer.end(), { C, 0.0f, -Sn, OX, 0.0f, Grown, 0.0f, Y, Sn, 0.0f, C, OZ, float(Chosen), 0.0f, FX, FZ });
					++T.Count;
					T.MinY = std::min(T.MinY, Y);
					T.MaxY = std::max(T.MaxY, Y);
				}
			}
			std::vector<State::PlacedTile> Ring;
			for (auto& Entry : Tiles)
			{
				Entry.second.X = Entry.first.first;
				Entry.second.Z = Entry.first.second;
				Ring.push_back(std::move(Entry.second));
			}
			Out.Rings.push_back(R.Index);
			Out.Tiles.push_back(std::move(Ring));
		}
		Out.Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		return Out;
	});
	return true;
}

Dictionary GeodriveWorld::take_plants()
{
	// a finished placing: {anchor (Godot), ms, rings: {index: [{tile, count, buffer, low, high}]}}
	State& S = *World;
	Dictionary Out;
	if (!S.PlantJob.valid() || S.PlantJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
	{
		return Out;
	}
	State::Placed P = S.PlantJob.get();
	const Vector3 Offset = get_render_offset();
	Out["anchor"] = Vector3(float(P.AnchorX - Offset.x), 0.0f, float(P.AnchorZ - Offset.z));
	Out["ms"] = P.Ms;
	Dictionary Rings;
	for (size_t r = 0; r < P.Rings.size(); ++r)
	{
		Array Tiles;
		for (State::PlacedTile& T : P.Tiles[r])
		{
			Dictionary D;
			D["tile"] = Vector2i(T.X, T.Z);
			D["count"] = T.Count;
			PackedFloat32Array Buffer;
			Buffer.resize(int64_t(T.Buffer.size()));
			std::memcpy(Buffer.ptrw(), T.Buffer.data(), T.Buffer.size() * sizeof(float));
			D["buffer"] = Buffer;
			D["low"] = T.MinY;
			D["high"] = T.MaxY;
			Tiles.push_back(D);
		}
		Rings[P.Rings[r]] = Tiles;
	}
	Out["rings"] = Rings;
	return Out;
}

Vector3 GeodriveWorld::get_plant_origin(bool bFar) const
{
	const State& S = *World;
	const State::PlantWindow& W = bFar ? S.FarPlants : S.NearPlants;
	return S.Render(W.Min + Vec2(W.Cell * State::PlantCells, 0.0), 0.0);
}

double GeodriveWorld::get_plant_size(bool bFar) const
{
	return (bFar ? World->FarPlants : World->NearPlants).Cell * State::PlantCells / 100.0;
}

void GeodriveWorld::set_ground_material(const Ref<Material>& Material)
{
	// the ground's look (the game's textured ground shader); the ground's vertex colours still say what's where
	if (Material.is_valid())
	{
		World->Paints.Ground = Material;
	}
}

void GeodriveWorld::set_building_material(const Ref<Material>& Material)
{
	// the buildings' look (the game's buildings shader: walls, windows, roofs, from their corners' colour and texture
	// coordinates), from the next tile built on
	if (Material.is_valid())
	{
		World->Paints.Buildings = Material;
	}
}

void GeodriveWorld::set_road_materials(const Ref<Material>& Asphalt, const Ref<Material>& Paving, const Ref<Material>& Gravel)
{
	// the road surfaces' look (the game's textured road shader), from the next road tile built on
	if (Asphalt.is_valid() && Paving.is_valid() && Gravel.is_valid())
	{
		World->Paints.Asphalt = Asphalt;
		World->Paints.Paving = Paving;
		World->Paints.Gravel = Gravel;
	}
}

Array GeodriveWorld::get_built_boxes() const
{
	Array Out;
	for (const PiecePtr& P : World->Pieces)
	{
		PackedFloat64Array Box;
		Box.push_back(P->Core->South);
		Box.push_back(P->Core->West);
		Box.push_back(P->Core->North);
		Box.push_back(P->Core->East);
		Out.push_back(Box);
	}
	return Out;
}
