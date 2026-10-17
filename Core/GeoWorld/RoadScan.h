// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The road scan: drives every road the way the car would feel it, on the surface the wheels touch (the road meshes and
// the ground under them), sampling every 50 cm along each road's centre and both wheel tracks. It finds jolts (a step
// or kink), launches (a crest or dip too sharp for the road's speed), ground poking through the road, holes, stretches
// steeper than the law, dead ends that stop in mid-air and ground beside the road shaped as nature wouldn't (cliffs,
// trenches). The game logs the summary and writes every fault to a CSV, to compare runs or put on a map.

#pragma once

#include "GeoGround/Mesh.h"
#include "GeoGround/Terrain.h"
#include "GeoRoads/RoadGraph.h"

#include <string>
#include <vector>

namespace geoworld
{
	enum class RoadDefect : uint8_t
	{
		Jolt,		// a sudden step or kink within half a metre
		Launch,		// a crest or dip too sharp for the road's speed: the car goes light or flies
		Grass,		// the ground pokes up through the road
		Hole,		// no road surface where the road should be
		Steep,		// steeper than the law allows for this class of road, even in mountains (over 30 m)
		DeadEnd,	// a dead end whose end edge stands above the ground
		OddGround,	// ground beside the road shaped as nature wouldn't: a cliff or a trench
		Count
	};

	struct RoadFault
	{
		RoadDefect Type = RoadDefect::Jolt;
		georoads::Vec2 Position;
		float Height = 0.0f;

		/** Jolt/grass/hole/dead end/odd ground: cm. Launch: vertical acceleration at the road's speed (m/s^2). Steep: % */
		float Size = 0.0f;
		int32_t Edge = -1;
		bool bAtJunction = false;	// within a junction area rather than along open road

		/** Jolts: what the wheel was rolling on round it (0 the road's own surface, 1 a junction surface, 2 another road) */
		uint8_t Cause = 0;
		std::string Detail;
	};

	struct RoadScanResult
	{
		/** Worst first within each type; nearby repeats of the same fault are merged */
		std::vector<RoadFault> Faults;
		double ScannedKm = 0.0;
		double Seconds = 0.0;
		int32_t Count(RoadDefect Type) const;
	};

	/** Scans every road on the road surfaces (the meshes whole, before they're cut into tiles; their triangles tagged by
	 *  road) and the ground builder. Safe on a worker */
	void ScanRoads(const georoads::RoadGraph& Graph, const std::vector<const geoground::MeshData*>& RoadSurfaces, const geoground::Terrain& Ground,
		const georoads::ParallelForFn& ParallelFor, bool bUK, RoadScanResult& Out);

	/** The summary and the worst faults (street names and coordinates), as log lines */
	std::vector<std::string> RoadScanSummary(const georoads::RoadGraph& Graph, const geoground::Terrain& Ground, const RoadScanResult& Scan);

	/** Every fault as CSV: type, size, unit, lat, lon, highway, name, bridge, junction, detail */
	std::string RoadScanCsv(const georoads::RoadGraph& Graph, const geoground::Terrain& Ground, const RoadScanResult& Scan);
}
