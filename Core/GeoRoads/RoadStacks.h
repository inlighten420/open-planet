// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The stacked roads: tunnels, bridges, roads on other levels, whatever crosses them and the roads leading on to them.
// Roads on the ground follow the ground (RoadHeights.cpp); these are a different problem, solved here on its own:
// a few hard rules (a truck's height under every road, no road steeper or more sharply curved than its class allows,
// nothing under the sea, overlapping surfaces one surface) and one preference (every road as near as it can be to
// where it would be on its own). That is a least-squares problem with linear constraints, and it has one exact
// answer; weights say what gives way first (a tunnel, then the road leading in, then a junction in the open).

#pragma once

#include "RoadGraph.h"
#include "RoadDesign.h"
#include "RoadHeights.h"

#include <functional>
#include <vector>

namespace georoads
{
	/** A road's surface stands at least this far above a road it crosses (cm): SA's ~5.2 m clearance under the deck
	 *  plus the deck itself */
	constexpr double BridgeClearance = 650.0;

	/** No road below this (cm): the sea is a plane at sea level, and a road under it is underwater */
	constexpr double SeaFloor = 50.0;

	/** The height solver's unknowns: every node first, then each road's inner points, in road order */
	struct Numbering
	{
		int32_t NumNodes = 0;
		int32_t Count = 0;
		std::vector<int32_t> EdgeBase, EdgeStart, EdgeEnd, EdgePoints;

		Numbering() = default;
		Numbering(const RoadGraph& Graph, const std::vector<std::vector<Vec2>>& Points);

		/** The unknown of point i of a road */
		int32_t At(int32_t Edge, int32_t Point) const
		{
			return Point == 0 ? EdgeStart[Edge] : Point == EdgePoints[Edge] - 1 ? EdgeEnd[Edge] : EdgeBase[Edge] + Point - 1;
		}
	};

	/** Two surfaces that are one (a slip road merging, the two halves of a dual carriageway across their median): the
	 *  point is at height (1-T) A + T B of the other road's segment */
	struct OverlapTie
	{
		int32_t Point = -1, A = -1, B = -1;
		int32_t PointEdge = -1, OtherEdge = -1;
		double T = 0.0;
		double Weight = 0.0;
	};

	/** Which of two roads the map means to be over the other: a higher layer, and at one layer a bridge over a road over
	 *  a tunnel */
	inline int32_t LevelRank(const RoadEdge& Edge) { return Edge.Layer * 4 + (Edge.bBridge ? 1 : 0) - (Edge.bTunnel ? 1 : 0); }

	/** A place one road is over another by the map's levels: segment UpperSegment of UpperEdge, at UpperT along it,
	 *  crosses (or runs right alongside) segment LowerSegment of LowerEdge at LowerT */
	struct LevelPair
	{
		int32_t UpperEdge = -1, UpperSegment = -1, LowerEdge = -1, LowerSegment = -1;
		double UpperT = 0.0, LowerT = 0.0;
	};

	/** Every level pair in the network: two roads of different rank, not joined to each other, crossing or with their
	 *  lanes overlapping side by side. PointsOf(edge): the road's points (the solver's resampled ones, or the graph's) */
	std::vector<LevelPair> FindLevelPairs(const RoadGraph& Graph, const std::function<const std::vector<Vec2>&(int32_t)>& PointsOf);

	struct StackInput
	{
		const RoadGraph& Graph;
		const std::vector<std::vector<Vec2>>& Points;
		const std::vector<std::vector<float>>& Data;				// the measured ground at each point (cm): a tunnel's cover
		const std::vector<RoadDesign>& Design;
		const std::vector<std::vector<double>>& GradeLimit;		// per segment: the grade the open roads were clipped to
		const std::vector<OverlapTie>& Ties;
		const Numbering& Unknowns;
		ParallelForFn ParallelFor;
		LogFn Log;
		std::vector<StuckRoad>* Stuck = nullptr;	// (out, optional: the ramps it could not fit)
	};

	/**
	 *  Solves the stacked roads. X in: every unknown's height (cm) with the roads on the ground where they stay; out:
	 *  the points of the stacks moved to the nearest heights that obey the rules, everything else untouched. A stack
	 *  that already obeys them comes back exactly as it went in.
	 */
	void SolveStacks(const StackInput& In, std::vector<double>& X);
}
