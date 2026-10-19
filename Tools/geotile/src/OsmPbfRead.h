// Geodrive tile tool - AGPL-3.0 (see LICENSE)
//
// Reads OSM's standard compact file format (.osm.pbf), as downloaded for whole countries (Geofabrik), keeping only
// what the game builds from: the same ways the game asks the Overpass service for (roads, buildings, water, forest),
// and the nodes they use. Two passes: ways first (they come after the nodes in the file), then just their nodes,
// so a whole country fits in memory.
// Spec: https://wiki.openstreetmap.org/wiki/PBF_Format

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "OsmTypes.h"
#include "Progress.h"

namespace geotile
{
	struct PbfReadStats
	{
		uint64_t Blocks = 0;
		uint64_t NodesSeen = 0;
		uint64_t WaysSeen = 0;
		uint64_t WaysKept = 0;
		uint64_t NodesKept = 0;
		uint64_t MissingNodes = 0;
	};

	/** Which ways to keep: may also trim their tags */
	using WayFilter = std::function<bool(std::vector<Tag>&)>;

	/** The ways the game builds from (as asked of Overpass), with their tags filtered to the kept keys */
	bool GameWays(std::vector<Tag>& Tags);

	/** Reads a .osm.pbf, keeping the ways Keep wants and their nodes */
	OsmSet ReadOsmPbf(const std::string& Path, PbfReadStats& Stats, const WayFilter& Keep = GameWays);

	/**
	 *  A .osm.pbf too big to hold whole (England: 1.7 GB, over 16 GB of ways and nodes): the kept ways' node positions
	 *  are read once, as two sorted arrays, then the ways are read again band by band. A band gets every kept way with
	 *  a point inside it (whole), and all of that way's nodes. The file is mapped, not read into memory.
	 */
	class PbfBands
	{
	public:
		/** Box (1e-7 degrees): only the ways with a point inside it (an area out of a country, for a PC with little RAM) */
		PbfBands(const std::string& Path, PbfReadStats& Stats, const WayFilter& Keep = GameWays, const LatLon* BoxMin = nullptr, const LatLon* BoxMax = nullptr);
		~PbfBands();

		/** Every node a kept way uses, sorted, and where it is (Found 0: not in the file) */
		std::vector<int64_t> NodeIds;
		std::vector<LatLon> Positions;
		std::vector<uint8_t> Found;

		/** Nodes used more than once by the kept ways, sorted (junctions, closed rings): they keep their ids in tiles */
		std::vector<int64_t> Shared;

		/** The kept ways with a point Inside, and their nodes */
		OsmSet ReadBand(const std::function<bool(const LatLon&)>& Inside, const ProgressSpan* Span = nullptr) const;

	private:
		struct Impl;
		std::unique_ptr<Impl> P;
		WayFilter Keep;
	};

	/** A named place (city, town, village, suburb...) */
	struct Place
	{
		std::string Name;
		std::string Kind;	// OSM place= (or "aerodrome", for an airport's point)
		LatLon Position;
		int64_t Population = 0;
		std::string Icao, Iata;	// (airports)
	};

	/** Every named place node of a .osm.pbf; with bAerodromes, the airports' points (aeroway=aerodrome) instead */
	std::vector<Place> ReadPlaces(const std::string& Path, bool bAerodromes = false);
}
