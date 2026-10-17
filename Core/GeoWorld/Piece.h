// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// One piece of the world (a box some 8-16 km across), built in one go on a worker thread: the map data read out of a
// country's archive, the heights, the road network solved and (next to a piece already loaded) joined to it at the
// seam, the ground builder ready to make any chunk on demand, the roads, lines and buildings cut into 1 km tiles, the
// water and the distant ground, and for a new area where to start and the ground round it. The host streams it in:
// chunks and tiles near the car, nearest first, a few at a time (see the Unreal and Godot clients).
//
// All of it in world cm (x north, y east, z up) of one projection shared by every piece of a drive.

#pragma once

#include "GeoGround/HeightRaster.h"
#include "GeoGround/Mesh.h"
#include "GeoGround/Terrain.h"
#include "GeoGround/Vegetation.h"
#include "GeoMesh/RoadMesh.h"
#include "GeoRoads/Country.h"
#include "GeoRoads/Projection.h"
#include "GeoRoads/RoadGraph.h"
#include "RoadScan.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace geoworld
{
	using Cell = std::pair<int32_t, int32_t>;	// a 1 km square: floor(world / Terrain::ChunkSize)

	/** A 1 km square's roads, lines and buildings */
	struct TileMeshes
	{
		geoground::MeshData Roads[3];	// asphalt, paving, gravel
		geoground::MeshData Skirts;
		geoground::MeshData Markings[2];	// white, yellow
		geoground::MeshData Buildings;
	};

	struct Piece
	{
		int32_t Number = 0;
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;	// degrees

		/** Its own point of the drive's projection (world cm, on the 4 km block grid): everything in it (roads, ground,
		 *  meshes) is measured from here, so single-precision corners stay exact however far the drive goes. Squares
		 *  (Cell) are the drive's, the same for every piece; LocalX/Y give the piece's own for its ground builder. */
		georoads::Vec2 Origin;
		int32_t OriginX = 0, OriginY = 0;	// Origin in squares
		int32_t LocalX(int32_t X) const { return X - OriginX; }
		int32_t LocalY(int32_t Y) const { return Y - OriginY; }

		std::unique_ptr<geoground::HeightRaster> Raster;
		std::unique_ptr<geoground::Terrain> Ground;	// read only once built: any thread may build chunks from it
		std::shared_ptr<const georoads::RoadGraph> Roads;

		/** Its squares (the drive's), and the part trusted to be whole (the box less 1 km all round; drive cm) */
		int32_t MinX = 0, MinY = 0, MaxX = -1, MaxY = -1;
		geoground::Box2 Usable;

		std::map<Cell, TileMeshes> Tiles;
		geoground::MeshData Water;
		std::map<Cell, geoground::MeshData> DistantBlocks;	// by 4 km block
		std::map<Cell, geoground::MeshData> StartChunks;	// near chunks round the start (new areas only)

		/** Where to start (new areas; drive cm): a main road near the middle, its height and heading, and its name */
		georoads::Vec2 Spawn, SpawnHeading = georoads::Vec2(1.0, 0.0);
		float SpawnHeight = 0.0f;
		std::string SpawnRoad;

		/** The road scan, when asked for (PieceRequest::bScanRoads) */
		bool bScanned = false;
		RoadScanResult Scan;

		/** Where plants grow (Vegetation.h): 10 m cells over the box; the ground's colour carries its cover too */
		std::unique_ptr<geoground::VegetationMap> Vegetation;

		/** Which height data it was built from beyond the SRTM tiles (for the credits): Copernicus, LiDAR */
		bool bUsedCopernicus = false;
		bool bUsedLidar = false;
		int FineGrid = 0;	// (its fine heights' grid: geotile::FineGrid; 1 Britain's, the Environment Agency's; 2 UTM 32, Rheinland-Pfalz's)
		bool bUsedWorldCover = false;	// (its plants from the satellite's land cover)

		/** What it holds, for the log */
		int32_t NumJunctions = 0, NumBuildings = 0;
		/** Its buildings from the map (their footprints, the piece's frame): a tile's building triangles are tagged
		 *  with their number here */
		std::vector<geomesh::BuildingOutline> Outlines;
		std::string Summary;

		/** Whether the whole square lies in the trusted part */
		bool HasWhole(const Cell& C) const;
		/** Whether the square is in its box at all */
		bool Has(const Cell& C) const { return C.first >= MinX && C.first <= MaxX && C.second >= MinY && C.second <= MaxY; }
	};

	/** Where nothing grows that the map's land cover doesn't know: an airport's runway (and its strip), taxiway or apron.
	 *  Cut out of the plants as a piece is built, as the roads and buildings are */
	struct PlantClearing
	{
		std::vector<std::pair<double, double>> Line;	// lat, lon
		double Reach = 0.0;	// cm round the line
		bool bArea = false;	// the line's a ring: inside it too
	};

	struct PieceRequest
	{
		int32_t Number = 0;
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
		georoads::Projection Projection;	// the drive's
		bool bUK = false;
		/** The country's road rules (its side, sections, unsigned limits, markings); bUK is its UK style */
		georoads::CountryRules Country;

		/** The country's map archive (packed tiles, mapped by the host) */
		const uint8_t* Region = nullptr;
		size_t RegionSize = 0;

		/** Fills the height raster's SRTM tiles (the host decodes them, fetching any it hasn't got); then the height
		 *  archives (Copernicus base, LiDAR) are applied */
		std::function<void(geoground::HeightRaster& Raster)> FillTiles;
		std::vector<geoground::Archive> HeightArchives;

		/** Fills the land cover over the box (Cover's South..East set; the host fetches ESA WorldCover); left empty, the
		 *  plants follow the map's forests only */
		std::function<void(geoground::LandCover& Cover)> FillLandCover;
		std::shared_ptr<const std::vector<PlantClearing>> PlantClearings;	// (the host's airports; may be none)

		/** Next to a loaded piece: its roads, the squares already loaded from it (their road heights are kept) and its
		 *  forest canopy */
		std::shared_ptr<const Piece> Previous;
		std::set<Cell> PinnedCells;

		/** A new area: find where to start near this point (drive cm) and build the near ground within StartRadius */
		bool bStart = false;
		georoads::Vec2 StartNear;
		double StartRadius = 200000.0;

		/** A dense place gets a smaller box: at most this much to build (roads + buildings / 6; 0 = any), never under MinKm */
		double MaxWork = 0.0;
		double MinKm = 5.0;

		/** The road style: follow the land (smooth out bumps; the default) or legal design grades with engineered cuttings;
		 *  and kerbs with a raised pavement on town roads */
		bool bFollowLand = true;
		bool bKerbs = true;

		/** Scan every road for bumps, jumps and holes once its surfaces are made (RoadScan.h) */
		bool bScanRoads = false;

		georoads::ParallelForFn ParallelFor;
		georoads::LogFn Log;

		/** Told what it starts on next (for a loading screen) */
		std::function<void(const char* What)> Stage;
	};

	/** Builds a piece; nullptr and an error if the map data can't be read */
	std::shared_ptr<Piece> BuildPiece(const PieceRequest& Request, std::string& Error);

	/** Splits a mesh into squares of Size (world cm) by each triangle's centre */
	void SplitIntoTiles(const geoground::MeshData& Mesh, double Size, std::map<Cell, geoground::MeshData>& Out);

	/** The box (degrees) Km across centred on a point */
	void BoxAround(double Lat, double Lon, double Km, double& South, double& West, double& North, double& East);

	/** A piece's size (km) for the memory there is (as the Unreal game picks it): 8, 12 or 16 km by total RAM (GB) */
	double PieceSizeKm(double TotalRamGB);
}
