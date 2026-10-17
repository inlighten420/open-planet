// Geodrive Core - GPL (see Core/README.md)
//
// One height for the road surface at any point, shared by every piece of road mesh.
//
// Road pieces (stretches, junction fillers, the inside of tight bends) overlap and touch each other. If each set its
// own heights they would disagree by a few centimetres wherever they meet, which the car feels as a jolt. Instead,
// every corner of every road triangle asks this field for its height: a smooth blend of the road centrelines nearby,
// with bigger roads counting far more (so a main road stays true through a junction and side roads bend to meet it).
// Only centrelines near the asking road's own height take part, so a bridge never pulls on the road beneath it.

#pragma once

#include "RoadGraph.h"

#include <unordered_map>
#include <vector>

namespace georoads
{
	class RoadSurface
	{
	public:

		/** From a solved network (every road's Points and Heights filled); keeps what it needs, not the graph */
		explicit RoadSurface(const RoadGraph& Graph);

		/** Surface height (cm) at a point, for a piece of road owned by Owner: a road (edge index), or a junction
		 *  filler (-(node + 1)) */
		float Height(const Vec2& Point, int32_t Owner) const;

	private:

		struct Segment
		{
			Vec2 A, B;
			float HeightA = 0.0f, HeightB = 0.0f;
			float HalfWidth = 0.0f;
			float Weight = 1.0f;
			int32_t Edge = -1;
		};

		/** Height of the owner's own centreline nearest the point (the reference other roads must be close to) */
		float OwnHeight(const Vec2& Point, int32_t Owner) const;

		const std::vector<int32_t>* Near(const Vec2& Point) const;

		std::vector<Segment> Segments;
		std::unordered_map<uint64_t, std::vector<int32_t>> Grid;

		/** What it needs of the graph: each node's position and height, each road's two nodes */
		std::vector<Vec2> NodePositions;
		std::vector<float> NodeHeights;
		std::vector<std::pair<int32_t, int32_t>> EdgeNodes;
	};
}
