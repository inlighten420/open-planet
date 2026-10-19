// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Ramps given a better line. The map draws a slip road bending into its main road where it is easiest to draw, which can be
// sooner than a real one could: the height solver then has to squeeze the climb into the last few metres, or leave it too
// close under a road above. A real ramp runs on straight beside its road until it has the height and the room, then joins.
// So after a first height solve, each ramp left stuck (StuckRoad) has its join slid along the main road, just far enough for
// its climb at its own grade plus a little headroom, if there is clear room beside the road; then the heights are solved again.

#pragma once

#include "RoadGraph.h"
#include "RoadHeights.h"

#include <vector>

namespace georoads
{
	/** Slides the joins of the stuck ramps along their main roads (the graph's edges and nodes are edited: the main road split
	 *  where the ramp now joins, the ramp's line run on beside it). Main roads keep their own line. Returns how many moved;
	 *  heights are cleared on the roads it changes (solve again). */
	int32_t RerouteRamps(RoadGraph& Graph, const std::vector<StuckRoad>& Stuck, const LogFn& Log);

	/** Two roads on different levels mapped lying along each other, lanes over lanes, with too little room between them (a
	 *  tunnel drawn right under another, Tunnel Dsc Fontvieille under the road out of Giratoire Canton): the less trusted
	 *  one (in a tunnel, then the smaller class) is moved sideways until their lanes no longer overlap, easing out from its
	 *  junctions, where there is room. Heights must be solved (the room between them is read from them); the moved roads'
	 *  heights are cleared (solve again). Returns how many roads moved. */
	int32_t SeparateStacked(RoadGraph& Graph, const LogFn& Log);

	/** Splits a road at a distance along it (cm from its start): the road keeps the first part, a new road the rest; returns
	 *  the new node between them (-1 when the distance is not inside the road) */
	int32_t SplitEdgeAt(RoadGraph& Graph, int32_t Edge, double Along);
}
