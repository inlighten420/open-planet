// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Joins the roads of a newly built piece of the world to the roads already loaded from the piece before it. Both are
// solved separately, so the same road comes out a little higher or lower in each. Inside the squares already loaded
// every road takes the loaded heights exactly; beyond them the difference fades out along the roads, so the two pieces
// meet without a step.

#pragma once

#include "RoadGraph.h"

#include <cstdint>
#include <set>
#include <utility>

namespace georoads
{
	struct SeamStats
	{
		int32_t Pinned = 0;			// road points that took the loaded heights
		int32_t Faded = 0;			// road points eased towards them
		float WorstFaded = 0.0f;	// cm: the biggest difference eased out along a road beyond the loaded squares
		Vec2 WorstFadedAt;
		float WorstDelta = 0.0f;	// cm: the biggest difference between the two pieces' solves
	};

	/** Squares (index x CellSize, world cm) already loaded; FadeLength in cm along the roads. ParallelFor may be null.
	 *  Pieces measured from their own points: CellOrigin is where Graph's positions are measured from (so its points are
	 *  in the right squares), LoadedShift takes a point of Graph's to Loaded's. */
	SeamStats JoinSeams(RoadGraph& Graph, const RoadGraph& Loaded, const std::set<std::pair<int32_t, int32_t>>& PinnedCells,
		double CellSize, double FadeLength, const ParallelForFn& ParallelFor = nullptr, const Vec2& CellOrigin = Vec2(), const Vec2& LoadedShift = Vec2());
}
