// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadSurface.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace georoads
{
	namespace
	{
		/** Grid cell for finding centrelines (cm) */
		constexpr double SurfaceCell = 2000.0;

		/** A centreline counts fully over the middle of its road and fades out by Reach beyond its edge (cm), so surfaces
		 *  blend smoothly where roads meet */
		constexpr float Reach = 300.0f;

		/** Where no road covers a point (the corners of a wide junction), the asking road's own height takes over */
		constexpr double FallbackWeight = 0.01;

		/** Centrelines this far above or below the asking road's own fade out (cm): they're a different level, a bridge
		 *  or a road underneath. A gradual fade, so two roads near the limit never give neighbouring pieces different heights. */
		constexpr float LevelFadeStart = 300.0f;
		constexpr float LevelFadeEnd = 500.0f;

		/** That wide a level only where the two roads really meet: within JoinRadius (cm) of a junction both are part of.
		 *  Anywhere else (a slip road running alongside the motorway as it climbs away, two carriageways side by side)
		 *  only roads at nearly the same height blend: otherwise the bigger road held the slip road down to its own
		 *  height until they were 3 m apart and then let go within a few metres, a 2 m dip and a launch. */
		constexpr double JoinRadius = 4000.0;
		constexpr float ApartFadeStart = 25.0f;
		constexpr float ApartFadeEnd = 100.0f;

		/** How much a road class counts; squared, so a trunk road outweighs a side street ~16:1 */
		float SurfaceClassWeight(const std::string& Highway)
		{
			if (EndsWith(Highway, "_link")) { return 1.5f; }	// slip roads give way to the road they join
			if (StartsWith(Highway, "motorway") || StartsWith(Highway, "trunk")) { return 4.0f; }
			if (StartsWith(Highway, "primary") || StartsWith(Highway, "secondary")) { return 3.0f; }
			if (StartsWith(Highway, "tertiary")) { return 2.0f; }
			return 1.0f;
		}

		float SmoothStepF(float A, float B, float X)
		{
			if (X < A)
			{
				return 0.0f;
			}
			if (X >= B)
			{
				return 1.0f;
			}
			const float T = (X - A) / (B - A);
			return T * T * (3.0f - 2.0f * T);
		}

		float LerpF(float A, float B, float T) { return A + T * (B - A); }

		uint64_t SurfaceCellKey(const Vec2& P)
		{
			const int32_t X = static_cast<int32_t>(std::floor(P.X / SurfaceCell));
			const int32_t Y = static_cast<int32_t>(std::floor(P.Y / SurfaceCell));
			return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y);
		}
	}

	RoadSurface::RoadSurface(const RoadGraph& Graph)
	{
		NodePositions.reserve(Graph.Nodes.size());
		NodeHeights.reserve(Graph.Nodes.size());
		for (const RoadNode& Node : Graph.Nodes)
		{
			NodePositions.push_back(Node.Position);
			NodeHeights.push_back(Node.Height);
		}
		EdgeNodes.reserve(Graph.Edges.size());
		for (const RoadEdge& Edge : Graph.Edges)
		{
			EdgeNodes.emplace_back(Edge.StartNode, Edge.EndNode);
		}

		for (int32_t EdgeIndex = 0; EdgeIndex < static_cast<int32_t>(Graph.Edges.size()); ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (Edge.Heights.size() != Edge.Points.size())
			{
				continue;
			}
			const float Class = SurfaceClassWeight(Edge.Highway);
			const float Weight = Class * Class;
			double EdgeLength = 0.0, Along = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				EdgeLength += Distance(Edge.Points[i], Edge.Points[i + 1]);
			}
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				// as wide as the road is along this segment (wider or narrower where it tapers)
				const double Next = Along + Distance(Edge.Points[i], Edge.Points[i + 1]);
				const float HalfWidth = std::max(Edge.HalfWidthAt(Along, EdgeLength), Edge.HalfWidthAt(Next, EdgeLength));
				Along = Next;
				const int32_t Index = static_cast<int32_t>(Segments.size());
				Segments.push_back({ Edge.Points[i], Edge.Points[i + 1], Edge.Heights[i], Edge.Heights[i + 1], HalfWidth, Weight, EdgeIndex });
				const double Margin = HalfWidth + Reach;
				const int32_t MinX = static_cast<int32_t>(std::floor((std::min(Edge.Points[i].X, Edge.Points[i + 1].X) - Margin) / SurfaceCell));
				const int32_t MinY = static_cast<int32_t>(std::floor((std::min(Edge.Points[i].Y, Edge.Points[i + 1].Y) - Margin) / SurfaceCell));
				const int32_t MaxX = static_cast<int32_t>(std::floor((std::max(Edge.Points[i].X, Edge.Points[i + 1].X) + Margin) / SurfaceCell));
				const int32_t MaxY = static_cast<int32_t>(std::floor((std::max(Edge.Points[i].Y, Edge.Points[i + 1].Y) + Margin) / SurfaceCell));
				for (int32_t X = MinX; X <= MaxX; ++X)
				{
					for (int32_t Y = MinY; Y <= MaxY; ++Y)
					{
						Grid[(static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y)].push_back(Index);
					}
				}
			}
		}
	}

	const std::vector<int32_t>* RoadSurface::Near(const Vec2& Point) const
	{
		const auto Found = Grid.find(SurfaceCellKey(Point));
		return Found != Grid.end() ? &Found->second : nullptr;
	}

	float RoadSurface::OwnHeight(const Vec2& Point, int32_t Owner) const
	{
		if (Owner < 0)
		{
			const int32_t Node = -Owner - 1;
			return Node < static_cast<int32_t>(NodeHeights.size()) ? NodeHeights[Node] : 0.0f;
		}
		float Best = 0.0f;
		double BestDistance = std::numeric_limits<double>::max();
		if (const std::vector<int32_t>* Nearby = Near(Point))
		{
			for (const int32_t Index : *Nearby)
			{
				const Segment& S = Segments[Index];
				if (S.Edge != Owner)
				{
					continue;
				}
				const Vec2 AB = S.B - S.A;
				const double T = AB.SizeSquared() > 1.0 ? std::clamp(Dot(Point - S.A, AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
				const double DistanceSquared = (S.A + AB * T - Point).SizeSquared();
				if (DistanceSquared < BestDistance)
				{
					BestDistance = DistanceSquared;
					Best = LerpF(S.HeightA, S.HeightB, static_cast<float>(T));
				}
			}
		}
		return Best;
	}

	float RoadSurface::Height(const Vec2& Point, int32_t Owner) const
	{
		const float Own = OwnHeight(Point, Owner);
		const std::vector<int32_t>* Nearby = Near(Point);
		if (!Nearby)
		{
			return Own;
		}

		// the junctions the asking piece is part of (a road's two ends, or a junction filler's own junction)
		int32_t OwnNodes[2] = { -1, -1 };
		if (Owner < 0)
		{
			OwnNodes[0] = -Owner - 1;
		}
		else if (Owner < static_cast<int32_t>(EdgeNodes.size()))
		{
			OwnNodes[0] = EdgeNodes[Owner].first;
			OwnNodes[1] = EdgeNodes[Owner].second;
		}
		auto MeetsHere = [&](const Segment& S)
		{
			if (S.Edge == Owner)
			{
				return true;
			}
			const std::pair<int32_t, int32_t>& Other = EdgeNodes[S.Edge];
			for (const int32_t Node : OwnNodes)
			{
				if (Node != -1 && (Other.first == Node || Other.second == Node)
					&& (NodePositions[Node] - Point).SizeSquared() < JoinRadius * JoinRadius)
				{
					return true;
				}
			}
			return false;
		};

		double Sum = FallbackWeight * Own, Total = FallbackWeight;
		for (const int32_t Index : *Nearby)
		{
			const Segment& S = Segments[Index];
			const Vec2 AB = S.B - S.A;
			const double T = AB.SizeSquared() > 1.0 ? std::clamp(Dot(Point - S.A, AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
			const float Gap = static_cast<float>(Distance(S.A + AB * T, Point));
			const float Radius = S.HalfWidth + Reach;
			if (Gap >= Radius)
			{
				continue;
			}
			// beyond its ends a segment carries on at its own grade (not held at its end height): next to a corner of the
			// centreline both segments then agree, and the blend rounds the corner. Held flat, every corner of a hill road
			// was pulled level for a few metres and then climbed twice as steep to catch up: 5.8% / 15.4% steps every 10 m
			// up a 10.6% street, felt as a jolt at each one.
			const double Unclamped = AB.SizeSquared() > 1.0 ? Dot(Point - S.A, AB) / AB.SizeSquared() : 0.0;
			const float SegmentHeight = LerpF(S.HeightA, S.HeightB, static_cast<float>(std::clamp(Unclamped, -1.0, 2.0)));
			const bool bMeets = MeetsHere(S);
			const float Level = 1.0f - SmoothStepF(bMeets ? LevelFadeStart : ApartFadeStart, bMeets ? LevelFadeEnd : ApartFadeEnd, std::abs(SegmentHeight - Own));
			if (Level <= 0.0f)
			{
				continue;	// another level: a bridge above or a road below
			}
			// full weight across the road's own width, then fading smoothly to nothing over Reach beyond its edge
			const float Fade = 1.0f - SmoothStepF(S.HalfWidth * 0.5f, Radius, Gap);
			const double W = S.Weight * Fade * Fade * Level;
			Sum += W * SegmentHeight;
			Total += W;
		}
		return static_cast<float>(Sum / Total);
	}
}
