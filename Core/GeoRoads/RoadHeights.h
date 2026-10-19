// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Gives the whole road network its heights in one go, so slopes flow smoothly through every join and junction.

#pragma once

#include "RoadGraph.h"

#include <vector>

namespace georoads
{
	/** A ramp (a *_link road) the stacked roads' solve could not fit: steeper than its rule allows, or too close under or
	 *  over another road. Excess: the height it is short by, summed along it (cm); At: the worst place; Other: the road it
	 *  is too close to (-1: only too steep) */
	struct StuckRoad
	{
		int32_t Edge = -1;
		int32_t Other = -1;
		double Excess = 0.0;
		double Grade = 0.0;
		Vec2 At;
	};

	struct HeightOptions
	{
		/** "Land" style: roads follow the land the way the real ones do, only short bumps from the height data are cut.
		 *  Otherwise ("rules") they are held to the legal design grades with cuttings and embankments. */
		bool bFollowLand = true;

		/** UK roads: the speed limit alone sets the design speed */
		bool bUK = false;

		/** Clip every road to its grade limit (off only to compare with the old behaviour) */
		bool bGradeLimit = true;

		/** Rounds of "solve, check the rules, tighten where they're broken" (1 = no rule rounds) */
		int32_t RuleRounds = 1;

		/** The host's job system (empty: plain threads) and log (empty: quiet) */
		ParallelForFn ParallelFor;
		LogFn Log;

		/** Out (optional): the ramps the solve could not fit, for RerouteRamps */
		std::vector<StuckRoad>* Stuck = nullptr;
	};

	/**
	 *  Each road point is pulled towards its measured ground height (Data, cm) and, more strongly, kept on a smooth
	 *  curve: sudden changes of slope are penalised along every road and straight through every node where roads
	 *  continue, sized by road class (a trunk road curves gently over hundreds of metres, a track may follow the
	 *  ground closely). Bridge and tunnel points have no ground height of their own, so they span between their ends.
	 *  Then every road is clipped to its legal grade (within an earthworks budget), and bridges are lifted to clear the
	 *  roads they cross.
	 *
	 *  Points[e] / Data[e]: each edge's points from start node to end node and their measured heights.
	 *  Out[e] receives the solved heights, and every node's Height is set. Safe on a worker thread.
	 */
	void SolveHeights(RoadGraph& Graph, const std::vector<std::vector<Vec2>>& Points, const std::vector<std::vector<float>>& Data,
		std::vector<std::vector<float>>& Out, const HeightOptions& Options);
}
