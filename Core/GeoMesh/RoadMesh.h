// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The meshes of an area's roads and buildings: road ribbons and junction surfaces (every corner on the shared road
// surface), the skirts hanging from their edges, the gap paving between roads side by side, rounded dead ends, the
// painted lines (SA or UK rules), and buildings, open galleries and tunnel tubes. Built from a solved road network.

#pragma once

#include "GeoGround/Mesh.h"
#include "GeoRoads/RoadGraph.h"
#include "GeoRoads/RoadNetwork.h"
#include "GeoRoads/RoadSurface.h"

#include <functional>
#include <vector>

namespace geomesh
{
	using geoground::MeshData;
	using georoads::RoadGraph;

	/** A junction's paved area (its corners in order, round its middle, with the rounded kerb corners) and how high it is (cm) */
	struct JunctionArea
	{
		std::vector<georoads::Vec2> Corners;
		georoads::Vec2 Centre;
		float Height = 0.0f;
	};

	struct MeshOptions
	{
		/** UK road markings (TSM ch. 5) instead of South African ones */
		bool bUK = false;
		/** Traffic keeps left (the no-overtaking lines on each direction's own side of the dividing line) */
		bool bDriveLeft = true;
		/** Edge lines white, not yellow (most of the world) */
		bool bWhiteEdges = false;
		/** North America's lines (MUTCD): the centre line yellow, edge lines white, a one-way road's left edge yellow */
		bool bYellowCentre = false;

		/** The junction areas the road meshes made, so buildings keep out of them (BuildBuildings only) */
		const std::vector<JunctionArea>* Junctions = nullptr;

		/** The host's job system (empty: plain threads) and log (empty: quiet) */
		georoads::ParallelForFn ParallelFor;
		georoads::LogFn Log;
	};

	/** Number of road surfaces (asphalt, paving, loose) */
	constexpr int32_t NumSurfaces = 3;

	struct RoadMeshes
	{
		MeshData Roads[NumSurfaces];

		/** Hanging from the road edges, so no gap shows where a road meets the ground: visual only (no collision) */
		MeshData Skirts;

		/** Painted lines: white [0] and yellow [1], just above the road */
		MeshData Markings[2];

		int32_t NumJunctions = 0;
		int32_t NumJunctionAreas = 0;

		/** Every junction's paved area (for the buildings to be cut away from) */
		std::vector<JunctionArea> Junctions;
	};

	/** Road ribbons, junction surfaces, skirts, gap paving, dead-end caps and markings (edge Heights must be filled) */
	void BuildRoads(const RoadGraph& Graph, RoadMeshes& Out, const MeshOptions& Options);

	/** Every corner of road meshes takes its height from the shared surface (positions stay where they are) */
	void ApplySurface(const georoads::RoadSurface& Surface, MeshData& Mesh, float Offset, const georoads::ParallelForFn& ParallelFor);

	/** The same for road skirts (built as pairs of triangles hanging Depth below the road edge) */
	void ApplySurfaceToSkirts(const georoads::RoadSurface& Surface, MeshData& Skirts, float Offset, float Depth, const std::vector<std::vector<std::pair<georoads::Vec2, float>>>& Rooms, const georoads::ParallelForFn& ParallelFor);

	/** The painted lines on the roads, SA or UK rules (StartTrim / EndTrim: where each road meets its junction surfaces) */
	void BuildMarkings(const RoadGraph& Graph, const georoads::RoadSurface& Surface, const std::vector<double>& StartTrim, const std::vector<double>& EndTrim,
		MeshData& White, MeshData& Yellow, const MeshOptions& Options);

	/** A building from the map: its id there (OSM's way), its footprint (counter-clockwise), the box round it, and how
	 *  high it stands (cm) */
	struct BuildingOutline
	{
		int64_t Id = 0;
		std::vector<georoads::Vec2> Footprint;
		georoads::Vec2 Min, Max;
		float Base = 0.0f, Top = 0.0f;
	};

	struct BuildingMeshes
	{
		MeshData Buildings;	// (each building's triangles tagged with its outline's number; galleries and tubes -1)
		std::vector<BuildingOutline> Outlines;
		int32_t NumBuildings = 0;
		int32_t NumRaisedBuildings = 0;		// standing over the ground: bridging a street, a canopy, a carport
		int32_t NumBuildingsCut = 0;		// fitted round a road: a passage under it, or the top taken off under a bridge
		int32_t NumBuildingsOnRoads = 0;	// left out: a road ran through or over it and too little was left
	};

	/** Buildings from the map on the ground (and fitted round the roads), open galleries and tunnel tubes (Roads optional) */
	void BuildBuildings(const georoads::OsmData& Data, const std::function<float(const georoads::Vec2&)>& GroundHeight, BuildingMeshes& Out,
		const RoadGraph* Roads, const MeshOptions& Options);
}
