// Geodrive Core - GPL (see Core/README.md)
//
// The drivable road network from OSM ways: ways split at shared nodes, nearby nodes merged, widths, lanes, shoulders,
// surfaces and speed limits from tags (SA Red Book / RTMC, or UK DMRB and Manual for Streets), bridges, tunnels and
// galleries, and tapers where one road carries on wider or narrower.

#pragma once

#include "Country.h"
#include "RoadGraph.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace georoads
{
	/** An OSM way with its tags */
	struct OsmWay
	{
		int64_t Id = 0;
		std::vector<int64_t> Nodes;
		std::vector<std::pair<std::string, std::string>> Tags;

		/** The value of a tag, or null */
		const std::string* Tag(const char* Key) const
		{
			for (const auto& T : Tags)
			{
				if (T.first == Key)
				{
					return &T.second;
				}
			}
			return nullptr;
		}
	};

	/** OSM ways with every node already projected to world space (cm) */
	struct OsmData
	{
		std::unordered_map<int64_t, Vec2> Nodes;
		std::vector<OsmWay> Ways;
	};

	struct NetworkOptions
	{
		/** UK roads: UK cross-sections and speed limits (Country's UK style) */
		bool bUK = false;

		/** The country's rules: its side (a one-way way's wide shoulder on the outside), its cross-sections and the
		 *  limits of roads with no maxspeed tag (Country.h) */
		CountryRules Country;

		/** Ease width changes where a road carries on wider or narrower (off only to compare with the old steps) */
		bool bTapers = true;

		LogFn Log;
	};

	/** Whether a way is a road the car can drive (and its highway class) */
	bool IsDrivable(const OsmWay& Way, std::string& OutHighway);

	/** Builds the drivable road graph */
	void BuildRoadGraph(const OsmData& Data, RoadGraph& Out, const NetworkOptions& Options);
}
