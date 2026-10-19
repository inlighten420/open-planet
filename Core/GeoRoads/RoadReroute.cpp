// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadReroute.h"
#include "RoadStacks.h"
#include "RoadDesign.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace georoads
{
	namespace
	{
		/** A ramp's join is slid this much further than its climb needs: headroom (cm) */
		constexpr double Headroom = 1000.0;

		/** ...never further than this along its main road (cm), nor nearer than JunctionGap to the main road's next junction */
		constexpr double MaxSlide = 15000.0;
		constexpr double JunctionGap = 2000.0;

		/** A slide shorter than this is not worth a new junction (cm) */
		constexpr double MinSlide = 1000.0;

		/** The ramp moves out to run beside its road over BranchLength, and eases into it over MergeLength (cm, at most half
		 *  of its new run) */
		constexpr double BranchLength = 1000.0;
		constexpr double MergeLength = 3000.0;

		/** The new line is checked for room every CheckStep, and drawn with a point every DrawStep (cm) */
		constexpr double CheckStep = 300.0, DrawStep = 500.0;

		/** A ramp and its main road leave the junction together within 75 degrees (a fork, as RoadMesh's SplitMaxGap); the road
		 *  its join slides along leaves the other way, within 45 degrees of straight on */
		constexpr double ForkCos = 0.2588, CarryOnCos = 0.7071;

		/** A point of the ramp is clear of its main road once this share of their two half widths from its middle */
		constexpr double ClearShare = 0.9;

		/** Grid cell for finding the roads near the new line (cm) */
		constexpr double Cell = 5000.0;

		bool IsRamp(const RoadEdge& Edge) { return EndsWith(Edge.Highway, "_link"); }

		double Cross(const Vec2& A, const Vec2& B) { return A.X * B.Y - A.Y * B.X; }

		double SmoothStep(double T)
		{
			T = std::clamp(T, 0.0, 1.0);
			return T * T * (3.0 - 2.0 * T);
		}

		double PolyLength(const std::vector<Vec2>& P)
		{
			double L = 0.0;
			for (size_t i = 1; i < P.size(); ++i)
			{
				L += Distance(P[i - 1], P[i]);
			}
			return L;
		}

		/** A road's points running away from one of its nodes */
		std::vector<Vec2> FromNode(const RoadEdge& Edge, int32_t Node)
		{
			std::vector<Vec2> P = Edge.Points;
			if (Edge.StartNode != Node)
			{
				std::reverse(P.begin(), P.end());
			}
			return P;
		}

		/** The direction a road leaves a node, towards its point about Reach along (cm) */
		Vec2 Leaving(const RoadGraph& Graph, int32_t Edge, int32_t Node, double Reach = 1000.0)
		{
			const std::vector<Vec2> P = FromNode(Graph.Edges[Edge], Node);
			Vec2 To = P.front();
			for (size_t k = 1; k < P.size(); ++k)
			{
				To = P[k];
				if (Distance(P.front(), To) >= Reach)
				{
					break;
				}
			}
			return (To - P.front()).Normal();
		}

		/** The main road through a junction as one line: in from the far end of the road the ramp forks from, through the
		 *  junction (at 0) and on along the road its join slides along; distances along it are from the junction */
		struct Line
		{
			std::vector<Vec2> P;
			std::vector<double> S;

			Line(const std::vector<Vec2>& In, const std::vector<Vec2>& On)
			{
				P.assign(In.rbegin(), In.rend());
				P.insert(P.end(), On.begin() + 1, On.end());
				S.assign(P.size(), 0.0);
				for (size_t i = 1; i < P.size(); ++i)
				{
					S[i] = S[i - 1] + Distance(P[i - 1], P[i]);
				}
				const double Zero = S[In.size() - 1];
				for (double& Along : S)
				{
					Along -= Zero;
				}
			}

			/** The point at a distance along, and the direction there */
			Vec2 At(double Along, Vec2& Direction) const
			{
				size_t k = 0;
				while (k + 2 < P.size() && S[k + 1] < Along)
				{
					++k;
				}
				const double L = S[k + 1] - S[k];
				const double T = L > 1.0e-6 ? std::clamp((Along - S[k]) / L, 0.0, 1.0) : 0.0;
				Direction = (P[k + 1] - P[k]).Normal();
				return P[k] + (P[k + 1] - P[k]) * T;
			}

			/** The nearest place to a point: how far along, and how far off (+ to the left) */
			void Nearest(const Vec2& Q, double& Along, double& Off) const
			{
				double Best = std::numeric_limits<double>::max();
				for (size_t k = 0; k + 1 < P.size(); ++k)
				{
					const Vec2 AB = P[k + 1] - P[k];
					const double L2 = AB.SizeSquared();
					const double T = L2 > 1.0 ? std::clamp(Dot(Q - P[k], AB) / L2, 0.0, 1.0) : 0.0;
					const Vec2 Foot = P[k] + AB * T;
					const double D = Distance(Foot, Q);
					if (D < Best)
					{
						Best = D;
						Along = S[k] + T * std::sqrt(L2);
						Off = Cross(AB, Q - Foot) >= 0.0 ? D : -D;
					}
				}
			}
		};

		/** Every road's segments in a grid, to find what is beside a new line */
		struct SegmentGrid
		{
			std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Cells;

			static uint64_t Key(int64_t X, int64_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); }

			explicit SegmentGrid(const RoadGraph& Graph)
			{
				for (int32_t Edge = 0; Edge < static_cast<int32_t>(Graph.Edges.size()); ++Edge)
				{
					const std::vector<Vec2>& P = Graph.Edges[Edge].Points;
					for (int32_t i = 0; i + 1 < static_cast<int32_t>(P.size()); ++i)
					{
						const Vec2 Middle = (P[i] + P[i + 1]) * 0.5;
						Cells[Key(static_cast<int64_t>(std::floor(Middle.X / Cell)), static_cast<int64_t>(std::floor(Middle.Y / Cell)))].emplace_back(Edge, i);
					}
				}
			}

			/** The road of this level (other than those left out) with its lanes within HalfWidth of the point (-1: none) */
			int32_t Blocked(const RoadGraph& Graph, const Vec2& Q, int32_t Rank, float HalfWidth, const std::unordered_set<int32_t>& Ignore) const
			{
				const int64_t CX = static_cast<int64_t>(std::floor(Q.X / Cell)), CY = static_cast<int64_t>(std::floor(Q.Y / Cell));
				for (int64_t X = CX - 1; X <= CX + 1; ++X)
				{
					for (int64_t Y = CY - 1; Y <= CY + 1; ++Y)
					{
						const auto Found = Cells.find(Key(X, Y));
						if (Found == Cells.end())
						{
							continue;
						}
						for (const auto& [Edge, Segment] : Found->second)
						{
							const RoadEdge& Other = Graph.Edges[Edge];
							if (Ignore.count(Edge) || LevelRank(Other) != Rank || Segment + 1 >= static_cast<int32_t>(Other.Points.size()))
							{
								continue;
							}
							const Vec2 A = Other.Points[Segment], AB = Other.Points[Segment + 1] - A;
							const double L2 = AB.SizeSquared();
							const double T = L2 > 1.0 ? std::clamp(Dot(Q - A, AB) / L2, 0.0, 1.0) : 0.0;
							if (Distance(A + AB * T, Q) < Other.MaxHalfWidth() + HalfWidth)
							{
								return Edge;
							}
						}
					}
				}
				return -1;
			}
		};
	}

	int32_t SplitEdgeAt(RoadGraph& Graph, int32_t Edge, double Along)
	{
		RoadEdge& Road = Graph.Edges[Edge];
		const double Total = PolyLength(Road.Points);
		if (Road.StartNode == Road.EndNode || Along < 1.0 || Along > Total - 1.0)
		{
			return -1;
		}
		size_t k = 0;
		double S = 0.0;
		while (k + 2 < Road.Points.size() && S + Distance(Road.Points[k], Road.Points[k + 1]) < Along)
		{
			S += Distance(Road.Points[k], Road.Points[k + 1]);
			++k;
		}
		const double L = Distance(Road.Points[k], Road.Points[k + 1]);
		const Vec2 At = Road.Points[k] + (Road.Points[k + 1] - Road.Points[k]) * (L > 1.0e-6 ? (Along - S) / L : 0.0);

		const int32_t NewNode = static_cast<int32_t>(Graph.Nodes.size());
		const int32_t Second = static_cast<int32_t>(Graph.Edges.size());
		RoadEdge Rest = Road;
		Rest.Points.assign(Road.Points.begin() + k + 1, Road.Points.end());
		if (Distance(At, Rest.Points.front()) > 1.0)
		{
			Rest.Points.insert(Rest.Points.begin(), At);
		}
		else
		{
			Rest.Points.front() = At;
		}
		Road.Points.resize(k + 1);
		if (Distance(At, Road.Points.back()) > 1.0)
		{
			Road.Points.push_back(At);
		}
		else
		{
			Road.Points.back() = At;
		}
		if (Road.Points.size() < 2 || Rest.Points.size() < 2)
		{
			return -1;	// (cannot happen with Along inside: kept for safety, before anything is changed in the graph)
		}
		Rest.StartNode = NewNode;
		Rest.StartTaper = RoadEdge::Taper();
		Rest.Heights.clear();
		std::vector<int32_t>& FarEdges = Graph.Nodes[Road.EndNode].Edges;
		std::replace(FarEdges.begin(), FarEdges.end(), Edge, Second);
		Road.EndNode = NewNode;
		Road.EndTaper = RoadEdge::Taper();
		Road.Heights.clear();

		RoadNode Node;
		Node.Position = At;
		Node.Layer = Graph.Nodes[Road.StartNode].Layer;
		Node.Edges = { Edge, Second };
		Graph.Nodes.push_back(std::move(Node));
		Graph.Edges.push_back(std::move(Rest));
		return NewNode;
	}

	int32_t RerouteRamps(RoadGraph& Graph, const std::vector<StuckRoad>& Stuck, const LogFn& Log)
	{
		if (Stuck.empty())
		{
			return 0;
		}
		const SegmentGrid Grid(Graph);
		std::unordered_set<int32_t> Touched;
		int32_t Moved = 0, Refused = 0;
		double Most = 0.0;
		auto Named = [](const RoadEdge& Edge) { return (Edge.Name.empty() ? std::string("unnamed") : Edge.Name) + " " + Edge.Highway; };

		std::vector<StuckRoad> Order = Stuck;
		std::sort(Order.begin(), Order.end(), [](const StuckRoad& A, const StuckRoad& B) { return A.Edge < B.Edge; });
		for (const StuckRoad& Ramp : Order)
		{
			if (Touched.count(Ramp.Edge))
			{
				continue;
			}
			// --- the junction where the ramp meets its main road: out along the ramp (through plain joins of ramp to ramp) from
			// either end, to a junction where a main road on its level leaves with it (a fork) and another carries on the other way ---
			struct Join { int32_t Node = -1, Last = -1, Partner = -1, Along = -1; std::vector<int32_t> Chain; };
			Join Best;
			double BestDistance = std::numeric_limits<double>::max();
			for (const bool bFromStart : { true, false })
			{
				Join Here;
				int32_t Edge = Ramp.Edge;
				int32_t Node = bFromStart ? Graph.Edges[Edge].StartNode : Graph.Edges[Edge].EndNode;
				Here.Chain.push_back(Edge);
				for (int32_t Steps = 0; Steps < 20 && Graph.Nodes[Node].Edges.size() == 2; ++Steps)
				{
					const std::vector<int32_t>& Two = Graph.Nodes[Node].Edges;
					const int32_t Next = Two[0] == Edge ? Two[1] : Two[0];
					if (Next == Edge || !IsRamp(Graph.Edges[Next]) || std::find(Here.Chain.begin(), Here.Chain.end(), Next) != Here.Chain.end())
					{
						break;
					}
					Edge = Next;
					Here.Chain.push_back(Edge);
					Node = Graph.Edges[Edge].StartNode == Node ? Graph.Edges[Edge].EndNode : Graph.Edges[Edge].StartNode;
				}
				Here.Node = Node;
				Here.Last = Edge;
				const RoadEdge& Last = Graph.Edges[Edge];
				if (Last.StartNode == Last.EndNode)
				{
					continue;
				}
				const Vec2 Out = Leaving(Graph, Edge, Node);
				double Together = ForkCos, Apart = -CarryOnCos;
				for (const int32_t Other : Graph.Nodes[Node].Edges)
				{
					const RoadEdge& Road = Graph.Edges[Other];
					if (Other == Edge || IsRamp(Road) || Road.StartNode == Road.EndNode || LevelRank(Road) != LevelRank(Last))
					{
						continue;
					}
					const double D = Dot(Out, Leaving(Graph, Other, Node));
					if (D > Together)
					{
						Together = D;
						Here.Partner = Other;
					}
					if (D < Apart)
					{
						Apart = D;
						Here.Along = Other;
					}
				}
				if (Here.Partner < 0 || Here.Along < 0 || Here.Partner == Here.Along)
				{
					continue;
				}
				const double D = Distance(Graph.Nodes[Node].Position, Ramp.At);
				if (D < BestDistance)
				{
					BestDistance = D;
					Best = Here;
				}
			}
			if (Best.Node < 0)
			{
				continue;	// (not a ramp forking from a road on its level: a ramp onto a roundabout, a T junction)
			}
			const int32_t J = Best.Node;
			// the whole ramp (each way from the stuck piece, through plain joins of ramp to ramp), and the road its join slides
			// along, on through plain joins (a road is often several pieces between two junctions)
			auto RunOn = [&](int32_t First, int32_t From, bool bRamps, std::vector<int32_t>& Edges, std::vector<int32_t>* Entered)
			{
				int32_t Edge = First, Node = From;
				for (int32_t Steps = 0; Steps < 20; ++Steps)
				{
					Edges.push_back(Edge);
					if (Entered)
					{
						Entered->push_back(Node);
					}
					const RoadEdge& Road = Graph.Edges[Edge];
					Node = Road.StartNode == Node ? Road.EndNode : Road.StartNode;
					const std::vector<int32_t>& Here = Graph.Nodes[Node].Edges;
					if (Here.size() != 2)
					{
						return;
					}
					const int32_t Next = Here[0] == Edge ? Here[1] : Here[0];
					if (Next == Edge || IsRamp(Graph.Edges[Next]) != bRamps || LevelRank(Graph.Edges[Next]) != LevelRank(Road) || std::find(Edges.begin(), Edges.end(), Next) != Edges.end())
					{
						return;
					}
					Edge = Next;
				}
			};
			std::vector<int32_t> WholeRamp, AlongRun, Entered;
			RunOn(Ramp.Edge, Graph.Edges[Ramp.Edge].StartNode, true, WholeRamp, nullptr);
			RunOn(Ramp.Edge, Graph.Edges[Ramp.Edge].EndNode, true, WholeRamp, nullptr);
			RunOn(Best.Along, J, false, AlongRun, &Entered);
			bool bTaken = Touched.count(Best.Partner) > 0;
			for (const std::vector<int32_t>* Edges : { &Best.Chain, &WholeRamp, &AlongRun })
			{
				for (const int32_t Edge : *Edges)
				{
					bTaken |= Touched.count(Edge) > 0;
				}
			}
			if (bTaken)
			{
				continue;
			}
			std::vector<Vec2> AlongPoints;
			for (size_t k = 0; k < AlongRun.size(); ++k)
			{
				const std::vector<Vec2> P = FromNode(Graph.Edges[AlongRun[k]], Entered[k]);
				AlongPoints.insert(AlongPoints.end(), P.begin() + (k == 0 ? 0 : 1), P.end());
			}

			// --- the main road through the junction as one line, and where the ramp first stands clear of it ---
			const Line Main(FromNode(Graph.Edges[Best.Partner], J), AlongPoints);
			const RoadEdge& Last = Graph.Edges[Best.Last];
			const RoadEdge& AlongRoad = Graph.Edges[Best.Along];
			const double Beside = AlongRoad.MaxHalfWidth() + Last.MaxHalfWidth();
			const std::vector<Vec2> Ramp3 = FromNode(Last, J);	// (from the junction out)
			int32_t Q = -1;
			double QAlong = 0.0, QOff = 0.0, Kept = 0.0;
			for (int32_t i = 1; i < static_cast<int32_t>(Ramp3.size()); ++i)
			{
				Kept += Distance(Ramp3[i - 1], Ramp3[i]);
				double Along = 0.0, Off = 0.0;
				Main.Nearest(Ramp3[i], Along, Off);
				if (std::abs(Off) >= ClearShare * Beside)
				{
					Q = i;
					QAlong = Along;
					QOff = Off;
					break;
				}
			}
			if (Q < 0 || QAlong > 0.0)
			{
				++Refused;
				if (Log)
				{
					Log(Format("[Reroute]   %s (road %d): lies on its road all the way to the junction, left as mapped", Named(Last).c_str(), Best.Last));
				}
				continue;
			}

			// --- how far: the run it had from there to the junction, plus what its climb needs at its own grade, plus headroom ---
			const double Grade = std::max(Ramp.Grade, 0.02);
			double To = QAlong + Kept + Ramp.Excess / Grade + Headroom;
			To = std::min({ To, MaxSlide, PolyLength(AlongPoints) - JunctionGap });
			const double Side = QOff >= 0.0 ? 1.0 : -1.0;
			auto OffAt = [&](double Along, double End)
			{
				const double Out = QOff + (Side * Beside - QOff) * SmoothStep((Along - QAlong) / BranchLength);
				const double Merge = std::min(MergeLength, 0.5 * (End - QAlong));
				return Out * (1.0 - SmoothStep((Along - (End - Merge)) / Merge));
			};
			// (the room beside the road: the new line stops short of anything else on its level)
			std::unordered_set<int32_t> Ignore(Best.Chain.begin(), Best.Chain.end());
			Ignore.insert(WholeRamp.begin(), WholeRamp.end());
			Ignore.insert(AlongRun.begin(), AlongRun.end());
			Ignore.insert(Best.Partner);
			for (const int32_t Edge : Graph.Nodes[J].Edges)
			{
				Ignore.insert(Edge);
			}
			const int32_t Rank = LevelRank(Last);
			for (double Along = QAlong + CheckStep; Along < To; Along += CheckStep)
			{
				Vec2 Direction;
				const Vec2 Middle = Main.At(Along, Direction);
				const Vec2 Left(-Direction.Y, Direction.X);
				const int32_t By = Grid.Blocked(Graph, Middle + Left * OffAt(Along, To), Rank, Last.MaxHalfWidth(), Ignore);
				if (By >= 0)
				{
					if (Log)
					{
						Log(Format("[Reroute]     %s (road %d) is in the way %.0f m along (from %.0f m, side %.0f, beside %.1f m)", Named(Graph.Edges[By]).c_str(), By, Along / 100.0, QAlong / 100.0, Side, Beside / 100.0));
					}
					To = Along - CheckStep;
					break;
				}
			}
			if (To < MinSlide)
			{
				++Refused;
				if (Log)
				{
					Log(Format("[Reroute]   %s (road %d): no room beside %s for a longer line, left as mapped", Named(Last).c_str(), Best.Last, Named(AlongRoad).c_str()));
				}
				continue;
			}

			// --- the new line: as mapped out to where it stands clear, then on beside the road, easing into it where it joins ---
			std::vector<Vec2> Line3(Ramp3.begin() + Q, Ramp3.end());
			std::reverse(Line3.begin(), Line3.end());	// (now from its far end in to where it leaves the map's line)
			for (double Along = QAlong + DrawStep; Along < To - DrawStep * 0.5; Along += DrawStep)
			{
				Vec2 Direction;
				const Vec2 Middle = Main.At(Along, Direction);
				Line3.push_back(Middle + Vec2(-Direction.Y, Direction.X) * OffAt(Along, To));
			}

			// --- the graph: the main road split where the ramp now joins, the ramp's end moved there ---
			// (the piece of the road it now joins, and how far along that piece)
			const int32_t Ramped = Best.Last;
			int32_t AlongEdge = AlongRun.back();
			double SplitAt = 0.0, Before = 0.0;
			for (size_t k = 0; k < AlongRun.size(); ++k)
			{
				const double Piece = PolyLength(Graph.Edges[AlongRun[k]].Points);
				if (To < Before + Piece || k + 1 == AlongRun.size())
				{
					AlongEdge = AlongRun[k];
					SplitAt = Graph.Edges[AlongEdge].StartNode == Entered[k] ? To - Before : Piece - (To - Before);
					break;
				}
				Before += Piece;
			}
			const int32_t N = SplitEdgeAt(Graph, AlongEdge, SplitAt);
			if (N < 0)
			{
				++Refused;
				continue;
			}
			Line3.push_back(Graph.Nodes[N].Position);
			RoadEdge& Edge = Graph.Edges[Ramped];
			std::vector<int32_t>& AtJ = Graph.Nodes[J].Edges;
			AtJ.erase(std::remove(AtJ.begin(), AtJ.end(), Ramped), AtJ.end());
			if (Edge.EndNode == J)
			{
				Edge.Points = std::move(Line3);
				Edge.EndNode = N;
			}
			else
			{
				std::reverse(Line3.begin(), Line3.end());
				Edge.Points = std::move(Line3);
				Edge.StartNode = N;
			}
			Edge.Heights.clear();
			Edge.Rerouted = static_cast<float>(To);
			Graph.Nodes[N].Edges.push_back(Ramped);

			Touched.insert(Best.Chain.begin(), Best.Chain.end());
			Touched.insert(WholeRamp.begin(), WholeRamp.end());
			Touched.insert(AlongRun.begin(), AlongRun.end());
			Touched.insert(Best.Partner);
			Touched.insert(AlongEdge);
			Touched.insert(static_cast<int32_t>(Graph.Edges.size()) - 1);
			++Moved;
			Most = std::max(Most, To);
			if (Log)
			{
				const RoadEdge& Main2 = Graph.Edges[AlongEdge];
				Log(Format("[Reroute]   %s (road %d) now joins %s %.0f m further on, beside it (it was %.1f m out%s, at x %.0f y %.0f)",
					Named(Graph.Edges[Ramped]).c_str(), Ramped, Named(Main2).c_str(), To / 100.0, Ramp.Excess / 100.0,
					Ramp.Other >= 0 ? (", too close to " + Named(Graph.Edges[Ramp.Other])).c_str() : "", Ramp.At.X, Ramp.At.Y));
			}
		}
		if (Log)
		{
			Log(Format("[Reroute] %d ramps stuck; %d given a longer line beside their road (furthest %.0f m), %d left as mapped", static_cast<int32_t>(Stuck.size()), Moved, Most / 100.0, Refused));
		}
		return Moved;
	}

	int32_t SeparateStacked(RoadGraph& Graph, const LogFn& Log)
	{
		constexpr double SampleStep = 300.0;	// cm along a road between checks
		constexpr double Parallel = 0.94;		// (cos 20 degrees: lying along each other, not crossing)
		constexpr double OverlapShare = 0.9;	// lanes overlap where their middles are nearer than this share of their two half widths
		constexpr double MinRun = 1000.0;		// cm of that before it is a road lying on another (not a crossing)
		constexpr double ShortRoom = 550.0;		// cm between their surfaces under which there is too little room for both (a real underpass
												// has its 5 m and the deck: the solve makes 5 m, so only one barely made is a stack drawn wrong)
		constexpr double MaxPush = 1000.0;		// cm a road is moved aside at most (further, it is more likely really under the other)
		constexpr double Gap = 100.0;			// cm between their lanes' edges once apart
		constexpr double Ease = 2000.0;			// cm over which the moved road eases out to its new line and back
		constexpr double NearJoin = 1000.0;		// cm: lying along a road it joins, this near the junction, is a fork, not a stack
		const SegmentGrid Grid(Graph);
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
		auto Trust = [](const RoadEdge& Edge)	// (higher stays: in the open over in a tunnel, then the bigger class)
		{
			return (Edge.bTunnel ? 0 : 100) + DesignFor(Edge, false).Rank * 4 + static_cast<int32_t>(Edge.MaxHalfWidth() / 100.0f);
		};
		auto HeightAt = [](const RoadEdge& Edge, int32_t Segment, double T)
		{
			return Edge.Heights.size() == Edge.Points.size() ? Edge.Heights[Segment] + (Edge.Heights[Segment + 1] - Edge.Heights[Segment]) * T : 0.0;
		};
		struct Move { int32_t Other = -1; double From = 0.0, To = 0.0, Offset = 0.0, Push = 0.0; };
		std::vector<Move> Moves(NumEdges);
		for (int32_t A = 0; A < NumEdges; ++A)
		{
			const RoadEdge& Road = Graph.Edges[A];
			if (Road.Points.size() < 2 || Road.StartNode == Road.EndNode || !(Road.bTunnel || Road.Layer < 0))
			{
				continue;	// (only a road underground moves: nobody could see it to draw it; one in the open was traced from photos)
			}
			// along the road, every SampleStep: the road on another level nearest it, if they lie along each other, lanes over
			// lanes, with too little room between their surfaces
			int32_t RunOther = -1;
			double RunFrom = 0.0, RunTo = 0.0, OffsetSum = 0.0;
			int32_t RunCount = 0;
			auto Close = [&]()
			{
				if (RunOther >= 0 && RunTo - RunFrom >= MinRun && Trust(Road) < Trust(Graph.Edges[RunOther]))
				{
					const double Offset = OffsetSum / RunCount;
					const double Push = Road.MaxHalfWidth() + Graph.Edges[RunOther].MaxHalfWidth() + Gap - std::abs(Offset);
					if (Push > 50.0 && Push <= MaxPush && Push > Moves[A].Push)
					{
						Moves[A] = { RunOther, RunFrom, RunTo, Offset, Push };
					}
				}
				RunOther = -1;
				OffsetSum = 0.0;
				RunCount = 0;
			};
			double Along = 0.0;
			for (size_t i = 0; i + 1 < Road.Points.size(); ++i)
			{
				const Vec2 P0 = Road.Points[i], P1 = Road.Points[i + 1];
				const double L = Distance(P0, P1);
				const Vec2 Dir = (P1 - P0).Normal();
				for (double S = 0.0; S < L; S += SampleStep)
				{
					const Vec2 Q = P0 + Dir * S;
					const double QAlong = Along + S;
					const double ZA = HeightAt(Road, static_cast<int32_t>(i), L > 1.0 ? S / L : 0.0);
					int32_t Best = -1;
					double BestD = std::numeric_limits<double>::max(), BestOff = 0.0;
					const int64_t CX = static_cast<int64_t>(std::floor(Q.X / Cell)), CY = static_cast<int64_t>(std::floor(Q.Y / Cell));
					for (int64_t X = CX - 1; X <= CX + 1; ++X)
					{
						for (int64_t Y = CY - 1; Y <= CY + 1; ++Y)
						{
							const auto Found = Grid.Cells.find(SegmentGrid::Key(X, Y));
							if (Found == Grid.Cells.end())
							{
								continue;
							}
							for (const auto& [B, Segment] : Found->second)
							{
								const RoadEdge& Other = Graph.Edges[B];
								if (B == A || LevelRank(Other) == LevelRank(Road) || Segment + 1 >= static_cast<int32_t>(Other.Points.size()))
								{
									continue;
								}
								const Vec2 BA = Other.Points[Segment], BB = Other.Points[Segment + 1] - BA;
								const Vec2 BDir = BB.Normal();
								if (std::abs(Dot(BDir, Dir)) < Parallel)
								{
									continue;
								}
								const double L2 = BB.SizeSquared();
								const double T = L2 > 1.0 ? std::clamp(Dot(Q - BA, BB) / L2, 0.0, 1.0) : 0.0;
								const Vec2 Foot = BA + BB * T;
								const double D = Distance(Foot, Q);
								if (D >= OverlapShare * (Road.MaxHalfWidth() + Other.MaxHalfWidth()) || D >= BestD
									|| std::abs(HeightAt(Other, Segment, T) - ZA) >= ShortRoom)
								{
									continue;
								}
								// (a road it joins, near the junction they share: a fork, not a stack)
								bool bFork = false;
								for (const int32_t Node : { Road.StartNode, Road.EndNode })
								{
									bFork |= (Node == Other.StartNode || Node == Other.EndNode) && Distance(Graph.Nodes[Node].Position, Q) < NearJoin;
								}
								if (bFork)
								{
									continue;
								}
								Best = B;
								BestD = D;
								BestOff = Cross(BDir, Q - Foot) >= 0.0 ? D : -D;
							}
						}
					}
					if (Best >= 0 && Best == RunOther && QAlong - RunTo <= 2.0 * SampleStep)
					{
						RunTo = QAlong;
						OffsetSum += BestOff;
						++RunCount;
					}
					else
					{
						Close();
						if (Best >= 0)
						{
							RunOther = Best;
							RunFrom = RunTo = QAlong;
							OffsetSum = BestOff;
							RunCount = 1;
						}
					}
				}
				Along += L;
			}
			Close();
		}

		// each road to move: pushed out to the side it already leans to (or the side with room), in full along the stretch
		// it lay on the other, easing out over Ease either side and from its own junctions, which stay where they are
		int32_t Moved = 0, Refused = 0;
		for (int32_t A = 0; A < NumEdges; ++A)
		{
			const Move& M = Moves[A];
			if (M.Other < 0)
			{
				continue;
			}
			RoadEdge& Road = Graph.Edges[A];
			const RoadEdge& Other = Graph.Edges[M.Other];
			const double Total = PolyLength(Road.Points);
			std::unordered_set<int32_t> Ignore = { A, M.Other };
			for (const int32_t Node : { Road.StartNode, Road.EndNode })
			{
				Ignore.insert(Graph.Nodes[Node].Edges.begin(), Graph.Nodes[Node].Edges.end());
			}
			int32_t Blocker = -1;
			double BlockedAt = 0.0;
			auto Shifted = [&](double Side, double Push, std::vector<Vec2>& Out)
			{
				Out = Road.Points;
				double Along = 0.0;
				for (size_t i = 0; i < Out.size(); ++i)
				{
					if (i > 0)
					{
						Along += Distance(Road.Points[i - 1], Road.Points[i]);
					}
					const double Outside = Along < M.From ? M.From - Along : Along > M.To ? Along - M.To : 0.0;
					double W = 1.0 - SmoothStep(Outside / Ease);
					W *= SmoothStep(std::min(Along, Total - Along) / Ease);	// (its junctions stay)
					if (W <= 0.0)
					{
						continue;
					}
					// (sideways from the other road's line where it is nearest)
					Vec2 Across;
					double BestD = std::numeric_limits<double>::max();
					for (size_t k = 0; k + 1 < Other.Points.size(); ++k)
					{
						const Vec2 BA = Other.Points[k], BB = Other.Points[k + 1] - BA;
						const double L2 = BB.SizeSquared();
						const double T = L2 > 1.0 ? std::clamp(Dot(Road.Points[i] - BA, BB) / L2, 0.0, 1.0) : 0.0;
						const double D = Distance(BA + BB * T, Road.Points[i]);
						if (D < BestD)
						{
							BestD = D;
							const Vec2 N = BB.Normal();
							Across = Vec2(-N.Y, N.X);
						}
					}
					Out[i] = Road.Points[i] + Across * (Side * Push * W);
					const int32_t By = W > 0.3 ? Grid.Blocked(Graph, Out[i], LevelRank(Road), Road.MaxHalfWidth(), Ignore) : -1;
					if (By >= 0)
					{
						Blocker = By;
						BlockedAt = Along;
						if (Log)
						{
							Log(Format("[Reroute]     road %d to side %+.0f: %s %s (road %d, layer %d) in the way %.0f m along", A, Side,
								Graph.Edges[By].Name.empty() ? "unnamed" : Graph.Edges[By].Name.c_str(), Graph.Edges[By].Highway.c_str(), By, Graph.Edges[By].Layer, Along / 100.0));
						}
						return false;
					}
				}
				return true;
			};
			// (first to the side it leans to, the least move; else across the other road's line to its far side)
			const double Lean = M.Offset >= 0.0 ? 1.0 : -1.0;
			std::vector<Vec2> Line;
			double Side = Lean, Push = M.Push;
			if (!Shifted(Side, Push, Line))
			{
				Side = -Lean;
				Push = M.Push + 2.0 * std::abs(M.Offset);
				if (Push > MaxPush || !Shifted(Side, Push, Line))
				{
					++Refused;
					if (Log)
					{
						Log(Format("[Reroute]   %s %s (road %d) lies on %s %s (road %d) with too little room, but %s (road %d) is beside it %.0f m along: left as mapped",
							Road.Name.empty() ? "unnamed" : Road.Name.c_str(), Road.Highway.c_str(), A, Other.Name.empty() ? "unnamed" : Other.Name.c_str(), Other.Highway.c_str(), M.Other,
							Blocker >= 0 ? (Graph.Edges[Blocker].Name.empty() ? "unnamed" : Graph.Edges[Blocker].Name.c_str()) : "-", Blocker, BlockedAt / 100.0));
					}
					continue;
				}
			}
			Road.Points = std::move(Line);
			Road.Heights.clear();
			Road.Rerouted = static_cast<float>(Push);
			++Moved;
			if (Log)
			{
				Log(Format("[Reroute]   %s %s (road %d) lay on %s %s for %.0f m with too little room: moved %.1f m to its side",
					Road.Name.empty() ? "unnamed" : Road.Name.c_str(), Road.Highway.c_str(), A, Other.Name.empty() ? "unnamed" : Other.Name.c_str(),
					Other.Highway.c_str(), (M.To - M.From) / 100.0, Push / 100.0));
			}
		}
		if (Log)
		{
			Log(Format("[Reroute] %d roads lying on another level's with too little room moved beside it, %d left as mapped (no room)", Moved, Refused));
		}
		return Moved;
	}
}
