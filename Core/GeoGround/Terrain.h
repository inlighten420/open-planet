// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Everything known about the ground of one area: elevation, land cover from OSM, water levels, the roads' heights and
// the road corridors carved into the ground (verges, kerbs and pavements in town, cuttings and embankments, the ground
// kept clear under bridges and out of tunnel mouths). Built once, then read-only, so ground chunks can be built on any
// thread while the player drives. World units (cm, x north, y east), heights above sea level (cm).

#pragma once

#include "Mesh.h"
#include "HeightRaster.h"
#include "GeoRoads/Projection.h"
#include "GeoRoads/RoadNetwork.h"
#include "GeoRoads/RoadHeights.h"
#include "GeoRoads/RoadSurface.h"

#include <atomic>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace geoground
{
	struct VegetationMap;

	/** Solved road heights kept from an earlier build (a road bake): per OSM way piece, its heights and first point */
	struct RoadBake
	{
		float CanopyHeight = 0.0f;	// cm
		std::map<std::pair<int64_t, int32_t>, std::vector<float>> Heights;	// (way id, part) -> cm, one per road point
		std::map<std::pair<int64_t, int32_t>, std::pair<double, double>> Starts;	// (way id, part) -> first point, lat/lon
	};

	struct TerrainOptions
	{
		/** The road style: ground eased softly back to natural beside roads (land), or engineered cuttings and
		 *  embankments (rules). Also what the road heights are solved with. */
		bool bFollowLand = true;

		/** Town roads get a kerb and a raised pavement */
		bool bKerbs = true;

		/** How roads are solved (and the host's ParallelFor and log, used for the ground too) */
		georoads::HeightOptions Heights;
	};

	class Terrain
	{
	public:

		/** Ground chunk size and grid spacing per level of detail (cm) */
		static constexpr double ChunkSize = 100000.0;
		static constexpr double NearSpacing = 1000.0;
		static constexpr double FarSpacing = 5000.0;

		/** Distant ground: 4 km blocks at 250 m, shown beyond the detailed area around the car */
		static constexpr int32_t ChunksPerBlock = 4;
		static constexpr double DistantSpacing = 25000.0;

		georoads::Projection Projection;
		const HeightRaster* Dem = nullptr;	// the host keeps it alive
		TerrainOptions Options;

		/** Area covered (world cm) */
		Box2 Bounds;

		/** Builds land cover and water masks from OSM polygons (forest, beach, lakes) and counts buildings for "in town" */
		void BuildMasks(const georoads::OsmData& Data);

		/** Gives every road real heights: sampled, forest-corrected, then the whole network solved together. With a bake
		 *  that matches every road piece, its heights are used instead and nothing is solved. */
		void ComputeRoadProfiles(georoads::RoadGraph& Graph, const RoadBake* Bake = nullptr) const;

		/** How far the elevation data reads above the ground in forest (cm), measured by ComputeRoadProfiles */
		float GetCanopyHeight() const { return CanopyHeight; }

		/** Set (cm) for a piece of the world built next to one already loaded: its canopy is used instead of measuring */
		float ForcedCanopy = -1.0f;

		/** Indexes the road segments (with their final heights) so the ground can be shaped to them */
		void BuildRoadIndex(std::shared_ptr<const georoads::RoadGraph> Graph);

		/** Raw elevation (cm): in forest it reads the treetops */
		float DemHeight(const Vec2& Position) const;

		/** Elevation of the ground itself (cm): the raw elevation with the forest canopy taken off */
		float BareHeight(const Vec2& Position) const;

		/** Final ground height (cm): elevation with lakes below their surface, shaped to the roads the way they're built */
		float GroundHeight(const Vec2& Position) const;

		/** Whether a point is covered by a road corridor (the ground seen there is GroundHeight, at road detail) */
		bool InCorridor(const Vec2& Position) const;

		/** Whether a ground grid vertex sinks out of sight under a corridor (all its triangles are covered) */
		bool SinksUnderCorridor(const Vec2& Position, double Spacing = NearSpacing) const;

		/** Inside a lake or another mapped water area */
		bool IsWater(const Vec2& Position) const;

		/** What the map says covers the land here: 0 grass (or nothing mapped), 1 forest, 2 beach or sand */
		uint8_t MappedCover(const Vec2& Position) const { return CoverAt(Position); }

		/** The piece's vegetation map, once built (Vegetation.h): the ground's colour then carries how thick the trees
		 *  and shrubs are in its alpha (1 - cover: 1 bare, 0 closed forest), for the far-off canopy */
		const VegetationMap* Vegetation = nullptr;

		/** RAM held by the masks, road segments and their grids (bytes) */
		int64_t DataBytes() const;

		/** One ground chunk with the road corridors in it; Near = 10 m grid, otherwise 50 m */
		/** (bPlain: a coarse chunk without the road corridors, for far off where no 3D roads are drawn: its grid's
		 *  ~800 triangles, where the corridors made ~30,000) */
		void BuildChunk(int32_t ChunkX, int32_t ChunkY, bool bNear, MeshData& Out, bool bPlain = false) const;

		/** One distant block (ChunksPerBlock x ChunksPerBlock chunks), dropped so it never pokes through detail */
		void BuildDistantBlock(int32_t BlockX, int32_t BlockY, MeshData& Out) const;

		/** Flat water surfaces for lakes, plus the sea at 0 m */
		void BuildWater(MeshData& Out) const;

		int32_t NumForestCells = 0;
		int32_t NumWaterPolygons = 0;

		/** Ground triangles left out of the chunks built so far because some part of them stood above a road */
		mutable std::atomic<int32_t> GroundRemoved{ 0 };

	private:

		/** Ground triangles taken out because some part of them stood above a road, while building one chunk */
		struct GroundCheck
		{
			int32_t Removed = 0;
			Vec2 First;
			int32_t Kind = 0;	// what is being built: 0 grid, 1 strip, 2 fan
			int32_t ByKind[3] = { 0, 0, 0 };
			std::string Detail;
		};

		/** Roads to leave out of a check */
		struct Ignore
		{
			const int32_t* Data = nullptr;
			size_t Size = 0;
			bool Contains(int32_t Edge) const
			{
				for (size_t i = 0; i < Size; ++i)
				{
					if (Data[i] == Edge)
					{
						return true;
					}
				}
				return false;
			}
		};

		void BuildGrid(const Vec2& Min, int32_t Cells, double Spacing, float Drop, float SkirtDepth, bool bCorridors, MeshData& Out) const;
		void BuildCorridors(const Box2& Area, bool bNear, MeshData& Out, GroundCheck& Check) const;
		bool RoadOver(const Vec2& Position, Ignore Skip, float& OutTop) const;
		void AddGroundTriangle(MeshData& Out, const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& NA, const Vec3f& NB, const Vec3f& NC,
			const Vec4f& CA, const Vec4f& CB, const Vec4f& CC, GroundCheck& Check) const;
		Vec4f GroundColor(const Vec2& Position, float Height, const Vec3f& Normal) const;
		bool ApplyRoadBake(georoads::RoadGraph& Graph, const std::vector<std::vector<Vec2>>& SamplePoints, const RoadBake& Bake) const;
		void LogFollow(const georoads::RoadGraph& Graph, const std::vector<std::vector<Vec2>>& Points, const std::vector<std::vector<float>>& Solved) const;
		void Log(const std::string& Line) const;
		const georoads::ParallelForFn& ParallelFor() const;

		enum Cover : uint8_t { Grass = 0, Forest = 1, Beach = 2 };

		/** Land cover and water level rasters over Bounds */
		static constexpr double MaskCell = 2000.0;
		Vec2 MaskOrigin;
		int32_t MaskWidth = 0, MaskHeight = 0;
		std::vector<uint8_t> CoverMask;
		std::vector<float> WaterLevel;	// cm, or a very low value where there is no water
		uint8_t CoverAt(const Vec2& Position) const;
		float WaterLevelAt(const Vec2& Position) const;

		/** How far into a forest each mask cell is (cm); buildings (their middles) per mask cell */
		std::vector<float> ForestDepth;
		std::vector<uint16_t> BuildingCount;
		bool IsTown(const Vec2& Position) const;
		float CanopyAt(const Vec2& Position) const;
		mutable float CanopyHeight = 0.0f;

		/** Lake outlines and their surface heights */
		std::vector<std::pair<std::vector<Vec2>, float>> WaterPolygons;

		/** Road segments for shaping the ground, in a spatial hash */
		struct RoadSegment
		{
			Vec2 A, B;
			float HalfWidth = 0.0f;
			int32_t Edge = -1;
			float Reach = 0.0f;		// how far beyond the verge its cuttings/embankments reach (cm)
			float CutSlope = 0.67f;	// rise per unit distance of its cuttings
			bool bBridge = false;	// a deck: the ground only has to stay clear below it
			float Kerb = 0.0f;		// in town: a kerb this high (cm) at its edge
		};
		static constexpr double SegmentCell = 5000.0;
		std::vector<RoadSegment> Segments;
		std::unordered_map<uint64_t, std::vector<int32_t>> SegmentGrid;

		/** The same segments (not bridges) by the road itself only, in finer cells, for "is there road surface here?" */
		static constexpr double RoadCell = 2000.0;
		std::unordered_map<uint64_t, std::vector<int32_t>> RoadGrid;

		bool bFollowLand = true;
		std::vector<float> EdgeReach;	// per road: the furthest its earthworks reach (cm), 0 = no corridor
		std::vector<float> EdgeKerb;	// per road: its kerb height (cm) in town, 0 in the country

		/** Each tunnel's portal cuts and its tube: ground above the road there is cut away */
		struct Portal
		{
			Vec2 Mouth, Inward;
			float HalfWidth = 0.0f;
			float Length = 0.0f;
			int32_t Edge = -1;
		};
		struct Tube
		{
			Box2 Bounds;
			float HalfWidth = 0.0f;
			int32_t Edge = -1;
		};
		std::vector<Portal> Portals;
		std::vector<Tube> Tubes;
		bool InPortalCut(const Vec3f& Point) const;

		/** How far a corridor reaches from a road's middle (cm) for a grid this fine */
		float CorridorExtent(int32_t Edge, double Spacing = NearSpacing) const;

		std::shared_ptr<const georoads::RoadGraph> Roads;
		std::unique_ptr<georoads::RoadSurface> Surface;
	};
}
