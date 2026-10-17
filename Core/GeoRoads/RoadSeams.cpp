// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadSeams.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace georoads
{
	namespace RoadSeamsImpl
	{
		/** The loaded road's height at a point (the nearest spot along it, within 3 m), or false */
		bool LoadedHeight(const RoadEdge& Edge, const Vec2& P, float& OutHeight)
		{
			double Best = 300.0 * 300.0;
			bool bFound = false;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
				const Vec2 AB = B - A;
				const double Length2 = AB.X * AB.X + AB.Y * AB.Y;
				const double T = Length2 > 0.0 ? std::clamp(((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / Length2, 0.0, 1.0) : 0.0;
				const Vec2 On = A + AB * T;
				const double Distance2 = (P.X - On.X) * (P.X - On.X) + (P.Y - On.Y) * (P.Y - On.Y);
				if (Distance2 < Best)
				{
					Best = Distance2;
					OutHeight = Edge.Heights[i] + (Edge.Heights[i + 1] - Edge.Heights[i]) * static_cast<float>(T);
					bFound = true;
				}
			}
			return bFound;
		}
	}

	SeamStats JoinSeams(RoadGraph& Graph, const RoadGraph& Loaded, const std::set<std::pair<int32_t, int32_t>>& PinnedCells,
		double CellSize, double FadeLength, const ParallelForFn& ParallelFor, const Vec2& CellOrigin, const Vec2& LoadedShift)
	{
		using namespace RoadSeamsImpl;
		SeamStats Stats;
		if (PinnedCells.empty())
		{
			return Stats;
		}
		auto IsPinned = [&](const Vec2& P)
		{
			return PinnedCells.count({ int32_t(std::floor((P.X + CellOrigin.X) / CellSize)), int32_t(std::floor((P.Y + CellOrigin.Y) / CellSize)) }) > 0;
		};

		// the same OSM way (and layer) in the loaded piece
		std::unordered_multimap<int64_t, int32_t> LoadedByWay;
		for (int32_t EdgeIndex = 0; EdgeIndex < int32_t(Loaded.Edges.size()); ++EdgeIndex)
		{
			if (Loaded.Edges[EdgeIndex].Heights.size() == Loaded.Edges[EdgeIndex].Points.size())
			{
				LoadedByWay.emplace(Loaded.Edges[EdgeIndex].WayId, EdgeIndex);
			}
		}

		// one point per node, then each road's inner points (like the solver)
		const int32_t NumNodes = int32_t(Graph.Nodes.size());
		const int32_t NumEdges = int32_t(Graph.Edges.size());
		std::vector<int32_t> EdgeBase(size_t(NumEdges), 0);
		int32_t NumVertices = NumNodes;
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			EdgeBase[EdgeIndex] = NumVertices;
			NumVertices += std::max(0, int32_t(Graph.Edges[EdgeIndex].Points.size()) - 2);
		}
		auto Vertex = [&](int32_t EdgeIndex, int32_t Point)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			return Point == 0 ? Edge.StartNode : Point == int32_t(Edge.Points.size()) - 1 ? Edge.EndNode : EdgeBase[EdgeIndex] + Point - 1;
		};

		// --- the difference to the loaded heights at every road point inside the loaded squares ---
		std::vector<std::vector<float>> PointDelta(static_cast<size_t>(NumEdges));
		std::vector<std::vector<uint8_t>> PointPinned(static_cast<size_t>(NumEdges));
		auto Body = [&](int32_t EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			PointDelta[EdgeIndex].assign(Edge.Points.size(), 0.0f);
			PointPinned[EdgeIndex].assign(Edge.Points.size(), 0);
			const auto Range = LoadedByWay.equal_range(Edge.WayId);
			if (Range.first == Range.second || Edge.Heights.size() != Edge.Points.size())
			{
				return;
			}
			for (size_t i = 0; i < Edge.Points.size(); ++i)
			{
				if (!IsPinned(Edge.Points[i]))
				{
					continue;
				}
				for (auto It = Range.first; It != Range.second; ++It)
				{
					float Height = 0.0f;
					const RoadEdge& Candidate = Loaded.Edges[It->second];
					if (Candidate.Layer == Edge.Layer && LoadedHeight(Candidate, Edge.Points[i] + LoadedShift, Height))
					{
						PointDelta[EdgeIndex][i] = Height - Edge.Heights[i];
						PointPinned[EdgeIndex][i] = 1;
						break;
					}
				}
			}
		};
		if (ParallelFor)
		{
			ParallelFor(NumEdges, Body);
		}
		else
		{
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				Body(EdgeIndex);
			}
		}

		// per vertex (a node's difference is the average of the roads meeting there)
		std::vector<double> Delta(size_t(NumVertices), 0.0), Weight(size_t(NumVertices), 0.0);
		std::vector<double> Distance(size_t(NumVertices), std::numeric_limits<double>::max());
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			for (size_t i = 0; i < PointDelta[EdgeIndex].size(); ++i)
			{
				if (PointPinned[EdgeIndex][i])
				{
					const int32_t V = Vertex(EdgeIndex, int32_t(i));
					Delta[V] += PointDelta[EdgeIndex][i];
					Weight[V] += 1.0;
				}
			}
		}

		// --- spread outwards along the roads (nearest loaded point wins), fading to nothing over FadeLength ---
		std::vector<double> Source(size_t(NumVertices), 0.0);	// the difference carried from the nearest pinned point
		using Entry = std::pair<double, int32_t>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> Open;
		for (int32_t V = 0; V < NumVertices; ++V)
		{
			if (Weight[V] > 0.0)
			{
				Source[V] = Delta[V] / Weight[V];
				Distance[V] = 0.0;
				Open.push({ 0.0, V });
				Stats.WorstDelta = std::max(Stats.WorstDelta, static_cast<float>(std::fabs(Source[V])));
			}
		}
		// which road an inner vertex is on (nodes list their roads)
		std::vector<int32_t> VertexEdge(size_t(NumVertices - NumNodes), 0);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			for (int32_t i = 1; i + 1 < int32_t(Graph.Edges[EdgeIndex].Points.size()); ++i)
			{
				VertexEdge[size_t(EdgeBase[EdgeIndex] + i - 1 - NumNodes)] = EdgeIndex;
			}
		}
		auto Visit = [&](int32_t From, int32_t EdgeIndex, int32_t Point, const Vec2& FromPosition)
		{
			const int32_t To = Vertex(EdgeIndex, Point);
			const Vec2 P = Graph.Edges[EdgeIndex].Points[Point];
			const double Cost = Distance[From] + std::sqrt((P.X - FromPosition.X) * (P.X - FromPosition.X) + (P.Y - FromPosition.Y) * (P.Y - FromPosition.Y));
			if (Cost < Distance[To] && Cost < FadeLength)
			{
				Distance[To] = Cost;
				Source[To] = Source[From];
				Open.push({ Cost, To });
			}
		};
		while (!Open.empty())
		{
			const Entry Current = Open.top();
			Open.pop();
			const int32_t V = Current.second;
			if (Current.first > Distance[V])
			{
				continue;
			}
			if (V < NumNodes)
			{
				for (const int32_t EdgeIndex : Graph.Nodes[V].Edges)
				{
					const RoadEdge& Edge = Graph.Edges[EdgeIndex];
					if (Edge.Points.size() < 2)
					{
						continue;
					}
					const bool bStart = Edge.StartNode == V;
					const int32_t Last = int32_t(Edge.Points.size()) - 1;
					Visit(V, EdgeIndex, bStart ? 1 : Last - 1, Edge.Points[bStart ? 0 : Last]);
				}
				continue;
			}
			const int32_t EdgeIndex = VertexEdge[size_t(V - NumNodes)];
			const int32_t Point = V - EdgeBase[EdgeIndex] + 1;
			const Vec2 Here = Graph.Edges[EdgeIndex].Points[Point];
			Visit(V, EdgeIndex, Point - 1, Here);
			Visit(V, EdgeIndex, Point + 1, Here);
		}

		// --- apply: exact inside, fading beyond ---
		auto Fade = [&](int32_t V)
		{
			if (Distance[V] >= FadeLength)
			{
				return 0.0;
			}
			const double T = Distance[V] / FadeLength;
			return Source[V] * (1.0 - T * T * (3.0 - 2.0 * T));	// smooth at both ends: no kink where it starts or stops
		};
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			RoadEdge& Edge = Graph.Edges[EdgeIndex];
			for (size_t i = 0; i < Edge.Heights.size() && i < Edge.Points.size(); ++i)
			{
				const int32_t V = Vertex(EdgeIndex, int32_t(i));
				const double Change = Fade(V);
				Edge.Heights[i] += static_cast<float>(Change);
				if (Distance[V] == 0.0)
				{
					++Stats.Pinned;
				}
				else if (Change != 0.0)
				{
					++Stats.Faded;
					if (std::fabs(Source[V]) > Stats.WorstFaded)
					{
						Stats.WorstFaded = static_cast<float>(std::fabs(Source[V]));
						Stats.WorstFadedAt = Edge.Points[i];
					}
				}
			}
		}
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			Graph.Nodes[NodeIndex].Height += static_cast<float>(Fade(NodeIndex));
		}
		return Stats;
	}
}
