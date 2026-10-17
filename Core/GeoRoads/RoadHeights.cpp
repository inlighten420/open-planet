// Geodrive Core - GPL (see Core/README.md)

#include "RoadHeights.h"
#include "RoadDesign.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace georoads
{
	namespace
	{
		/**
		 *  How far (cm) a road class spreads a change of slope. The penalty on bending is this length to the fourth
		 *  power relative to the pull towards the ground, so it acts like a smoothing window a few times this long.
		 *  This is the everyday smoothing; the design rules (steepest grade, roundest crest) are enforced on top.
		 */
		double BendLength(const std::string& Highway)
		{
			if (StartsWith(Highway, "motorway") || StartsWith(Highway, "trunk")) { return 7000.0; }
			if (StartsWith(Highway, "primary") || StartsWith(Highway, "secondary")) { return 5000.0; }
			if (StartsWith(Highway, "tertiary")) { return 4000.0; }
			if (Highway == "residential" || Highway == "unclassified" || Highway == "living_street" || Highway == "road") { return 3000.0; }
			return 2500.0;
		}

		/** Bigger roads pull a junction's height towards their own */
		double ClassWeight(const std::string& Highway)
		{
			if (StartsWith(Highway, "motorway") || StartsWith(Highway, "trunk")) { return 4.0; }
			if (StartsWith(Highway, "primary") || StartsWith(Highway, "secondary")) { return 3.0; }
			if (StartsWith(Highway, "tertiary")) { return 2.0; }
			return 1.0;
		}

		/** How much a broken rule's term is tightened each round, and the most it can be */
		constexpr double BoostStep = 4.0;
		constexpr double MaxBoost = 256.0;

		/** A grade term starts this many times stronger than the ground pull over the same length */
		constexpr double GradeStartWeight = 10.0;

		/** Small tolerances so rules met to within rounding don't keep tightening */
		constexpr double GradeTolerance = 1.02;
		constexpr double CurveTolerance = 1.1;

		/** The most any road may leave the measured ground to obey its grade (cm): beyond that it follows the land, which
		 *  the player knows better than the rules (Kaaimans goes down the way it does) */
		constexpr double MaxEarthworks = 300.0;

		/** The two halves of a dual carriageway are tied onto one plane across a median up to this wide (cm) */
		constexpr double MedianTie = 600.0;

		/** "Land" style: the land's own slope is measured over this far either side (cm), a road is allowed that slope
		 *  times LandTrendMargin (plus 1%) before a bump counts, and it may leave the measured ground by LandEarthworks (cm) */
		constexpr double LandTrendHalfLength = 10000.0;
		constexpr double LandTrendMargin = 1.15;
		constexpr double LandEarthworks = 150.0;

		/** A bridge's road surface stands at least this far above a road it crosses (cm): SA's ~5.2 m clearance under the
		 *  deck plus the deck itself */
		constexpr double BridgeClearance = 650.0;
		constexpr double TunnelClearance = 750.0;	// cm from a tunnel's road to the road over it: its 6.5 m tube and its roof

		/** The lift that makes a bridge clear a road eases out at most this fraction of the bridge's grade limit */
		constexpr double BridgeRampFraction = 0.8;

		double Seconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		double SmoothStep(double A, double B, double X)
		{
			if (X < A)
			{
				return 0.0;
			}
			if (X >= B)
			{
				return 1.0;
			}
			const double T = (X - A) / (B - A);
			return T * T * (3.0 - 2.0 * T);
		}

		double Sign(double X) { return X > 0.0 ? 1.0 : X < 0.0 ? -1.0 : 0.0; }
		double Lerp(double A, double B, double T) { return A + T * (B - A); }
		int32_t FloorToInt(double X) { return static_cast<int32_t>(std::floor(X)); }

		/** A grid cell, as one hashable number */
		uint64_t CellKey(int32_t X, int32_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); }

		using Entry = std::tuple<int32_t, int32_t, double>;

		/** A symmetric sparse matrix in compressed rows, built from (row, column, value) entries */
		struct Sparse
		{
			std::vector<int32_t> RowStart;
			std::vector<int32_t> Columns;
			std::vector<double> Values;

			void Build(int32_t NumRows, std::vector<Entry>& Entries)
			{
				std::sort(Entries.begin(), Entries.end(), [](const Entry& A, const Entry& B)
				{
					return std::get<0>(A) != std::get<0>(B) ? std::get<0>(A) < std::get<0>(B) : std::get<1>(A) < std::get<1>(B);
				});
				// entries for the same cell are added together
				RowStart.assign(NumRows + 1, 0);
				Columns.clear();
				Values.clear();
				int32_t PreviousRow = -1, PreviousColumn = -1;
				for (const Entry& E : Entries)
				{
					if (std::get<0>(E) == PreviousRow && std::get<1>(E) == PreviousColumn)
					{
						Values.back() += std::get<2>(E);
						continue;
					}
					Columns.push_back(std::get<1>(E));
					Values.push_back(std::get<2>(E));
					++RowStart[std::get<0>(E) + 1];
					PreviousRow = std::get<0>(E);
					PreviousColumn = std::get<1>(E);
				}
				for (int32_t Row = 0; Row < NumRows; ++Row)
				{
					RowStart[Row + 1] += RowStart[Row];
				}
			}

			void Multiply(const ParallelForFn& ParallelFor, const std::vector<double>& X, std::vector<double>& Y) const
			{
				ParallelFor(static_cast<int32_t>(RowStart.size()) - 1, [&](int32_t Row)
				{
					double Sum = 0.0;
					for (int32_t i = RowStart[Row]; i < RowStart[Row + 1]; ++i)
					{
						Sum += Values[i] * X[Columns[i]];
					}
					Y[Row] = Sum;
				});
			}

			double At(int32_t Row, int32_t Column) const
			{
				for (int32_t i = RowStart[Row]; i < RowStart[Row + 1]; ++i)
				{
					if (Columns[i] == Column)
					{
						return Values[i];
					}
				}
				return 0.0;
			}
		};

		double DotProduct(const std::vector<double>& A, const std::vector<double>& B)
		{
			double Sum = 0.0;
			for (size_t i = 0; i < A.size(); ++i)
			{
				Sum += A[i] * B[i];
			}
			return Sum;
		}

		/** Coefficients of the change of slope (curvature, 1/cm) at the middle of three points */
		void BendCoefficients(double LengthAB, double LengthBC, double (&Out)[3])
		{
			const double Span = 0.5 * (LengthAB + LengthBC);
			Out[0] = 1.0 / (LengthAB * Span);
			Out[1] = -(1.0 / LengthAB + 1.0 / LengthBC) / Span;
			Out[2] = 1.0 / (LengthBC * Span);
		}

		/** One road segment between two unknowns, and the most its two ends may differ in height (cm) */
		struct GradeLink
		{
			int32_t Other = -1;
			double MaxRise = 0.0;
		};

		using MinHeap = std::priority_queue<std::pair<double, int32_t>, std::vector<std::pair<double, int32_t>>, std::greater<std::pair<double, int32_t>>>;

		/**
		 *  The lowest heights that keep every segment within its limit and never go above Heights:
		 *  Out[i] = min over all j of (Heights[j] + the most the road may rise on the way from j to i).
		 *  A shortest-path search over the network, started from every point at once.
		 */
		void LowerEnvelope(const std::vector<double>& Heights, const std::vector<std::vector<GradeLink>>& Links, std::vector<double>& Out)
		{
			Out = Heights;
			std::vector<std::pair<double, int32_t>> Start;
			Start.reserve(Heights.size());
			for (int32_t i = 0; i < static_cast<int32_t>(Heights.size()); ++i)
			{
				Start.emplace_back(Heights[i], i);
			}
			MinHeap Heap(std::greater<std::pair<double, int32_t>>(), std::move(Start));
			while (!Heap.empty())
			{
				const std::pair<double, int32_t> Top = Heap.top();
				Heap.pop();
				if (Top.first > Out[Top.second])
				{
					continue;	// a stale entry: this point was lowered since
				}
				for (const GradeLink& Link : Links[Top.second])
				{
					const double Candidate = Top.first + Link.MaxRise;
					if (Candidate < Out[Link.Other])
					{
						Out[Link.Other] = Candidate;
						Heap.emplace(Candidate, Link.Other);
					}
				}
			}
		}

		/**
		 *  Heights that obey every segment's grade limit exactly: the middle between the lowest and highest heights that
		 *  obey it while staying below and above the given ones. Where the road already obeys, nothing moves; elsewhere
		 *  the top of a rise is cut down and the dip filled by the same amount, the way a road is built with cut and fill.
		 */
		void LimitGrades(std::vector<double>& Heights, const std::vector<std::vector<GradeLink>>& Links)
		{
			std::vector<double> Lower, Negated(Heights.size()), Upper;
			LowerEnvelope(Heights, Links, Lower);
			for (size_t i = 0; i < Heights.size(); ++i)
			{
				Negated[i] = -Heights[i];
			}
			LowerEnvelope(Negated, Links, Upper);
			for (size_t i = 0; i < Heights.size(); ++i)
			{
				Heights[i] = 0.5 * (Lower[i] - Upper[i]);
			}
		}

		/**
		 *  The steepest a road may be where it follows the ground because obeying its design grade would need too much
		 *  earthworks: old passes and town streets really are steeper than today's rules. National roads stay within 10%
		 *  (the N2 down Kaaimans); beyond that they're on a big embankment or a bridge.
		 */
		double RelaxedGradeCap(const RoadDesign& Rules, const std::string& Highway)
		{
			if (Highway == "track") { return 0.35; }
			if (Highway == "raceway") { return 0.20; }	// (a circuit follows its hill: the Nordschleife's steepest is ~17%)
			switch (Rules.Rank)
			{
			case 4:  return std::max(static_cast<double>(Rules.MaxGradeMountain), 0.10);	// old national road passes (Kaaimans)
			case 3:  return 0.12;
			case 2:  return 0.20;
			default: return 0.30;
			}
		}
	}

	void SolveHeights(RoadGraph& Graph, const std::vector<std::vector<Vec2>>& Points, const std::vector<std::vector<float>>& Data,
		std::vector<std::vector<float>>& Out, const HeightOptions& Options)
	{
		const double Start = Seconds();
		const ParallelForFn ParallelFor = Options.ParallelFor ? Options.ParallelFor : ParallelForFn(&ThreadsParallelFor);
		auto Log = [&Options](const std::string& Line) { if (Options.Log) { Options.Log(Line); } };

		// --- unknowns: one per node, plus each edge's inner points ---
		const int32_t NumNodes = static_cast<int32_t>(Graph.Nodes.size());
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
		std::vector<int32_t> EdgeBase(NumEdges);
		int32_t NumUnknowns = NumNodes;
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			EdgeBase[EdgeIndex] = NumUnknowns;
			NumUnknowns += std::max(0, static_cast<int32_t>(Points[EdgeIndex].size()) - 2);
		}
		auto Unknown = [&](int32_t EdgeIndex, int32_t Point)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			return Point == 0 ? Edge.StartNode : Point == static_cast<int32_t>(Points[EdgeIndex].size()) - 1 ? Edge.EndNode : EdgeBase[EdgeIndex] + Point - 1;
		};
		auto Count = [&Points](int32_t EdgeIndex) { return static_cast<int32_t>(Points[EdgeIndex].size()); };

		// --- the design rules of every road, and the grade each of its segments may have ---
		std::vector<RoadDesign> Design(NumEdges);
		std::vector<std::vector<double>> GradeLimit(NumEdges);	// per segment
		{
			// the biggest road at each node: a smaller road arriving there is a side road and levels out before it
			std::vector<int32_t> NodeRank(NumNodes, 0);
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				Design[EdgeIndex] = DesignFor(Graph.Edges[EdgeIndex], Options.bUK);
				for (const int32_t Node : { Graph.Edges[EdgeIndex].StartNode, Graph.Edges[EdgeIndex].EndNode })
				{
					NodeRank[Node] = std::max(NodeRank[Node], Design[EdgeIndex].Rank);
				}
			}

			ParallelFor(NumEdges, [&](int32_t EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const RoadDesign& Rules = Design[EdgeIndex];
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const std::vector<float>& D = Data[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				std::vector<double>& Limit = GradeLimit[EdgeIndex];
				Limit.resize(std::max(0, N - 1));

				std::vector<double> Along(1, 0.0);
				for (int32_t i = 1; i < N; ++i)
				{
					Along.push_back(Along.back() + Distance(P[i - 1], P[i]));
				}

				// the land is mountainous where the ground itself climbs faster than a flat-country road may, measured over
				// ~100 m so that noise in the height data doesn't count
				const bool bSideAtStart = Graph.Nodes[Edge.StartNode].Edges.size() >= 3 && NodeRank[Edge.StartNode] > Rules.Rank;
				const bool bSideAtEnd = Graph.Nodes[Edge.EndNode].Edges.size() >= 3 && NodeRank[Edge.EndNode] > Rules.Rank;
				int32_t Low = 0, High = 0;
				for (int32_t i = 0; i + 1 < N; ++i)
				{
					const double Middle = 0.5 * (Along[i] + Along[i + 1]);
					while (Low + 1 < N && Along[Low + 1] < Middle - 5000.0) { ++Low; }
					while (High + 1 < N && Along[High] < Middle + 5000.0) { ++High; }
					const double Run = Along[High] - Along[Low];
					const double LandGrade = Run > 1000.0 ? std::abs(D[High] - D[Low]) / Run : 0.0;
					double Grade = LandGrade > Rules.MaxGrade ? Rules.MaxGradeMountain : Rules.MaxGrade;

					if ((bSideAtStart && Along[i] < Rules.LandingLength) || (bSideAtEnd && Along.back() - Along[i + 1] < Rules.LandingLength))
					{
						Grade = std::min(Grade, static_cast<double>(Rules.LandingGrade));
					}
					Limit[i] = Grade;
				}
			});
		}

		// --- the pull towards the measured ground (before any tightening) ---
		std::vector<double> BaseWeight(NumUnknowns, 0.0), BaseRhs(NumUnknowns, 0.0), Target, Guess(NumUnknowns, 0.0);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			const std::vector<Vec2>& P = Points[EdgeIndex];
			const std::vector<float>& D = Data[EdgeIndex];
			const int32_t N = Count(EdgeIndex);

			// bridges float over the valley: instead of the ground, a faint pull towards a straight line between their ends
			// (keeps long spans quick to solve without bending them). Their ends are the road leading on, which holds them.
			std::vector<double> Along(1, 0.0);
			for (int32_t i = 1; i < N; ++i)
			{
				Along.push_back(Along.back() + Distance(P[i - 1], P[i]));
			}
			for (int32_t i = 0; i < N; ++i)
			{
				const double Before = i > 0 ? Distance(P[i - 1], P[i]) : 0.0;
				const double After = i + 1 < N ? Distance(P[i], P[i + 1]) : 0.0;
				const int32_t U = Unknown(EdgeIndex, i);

				const bool bEnd = i == 0 || i == N - 1;
				// (a bridge or a tunnel: straight between its ends, whatever the valley below or the mountain above)
				const bool bSpan = Edge.bBridge || Edge.bTunnel;
				const double Height = bSpan ? Lerp(static_cast<double>(D[0]), static_cast<double>(D.back()), Along[i] / std::max(Along.back(), 1.0)) : D[i];
				const double W = bSpan ? (bEnd ? 0.0 : 0.02 * 0.5 * (Before + After)) : 0.5 * (Before + After) * (bEnd ? ClassWeight(Edge.Highway) : 1.0);
				BaseWeight[U] += W;
				BaseRhs[U] += W * Height;
				Guess[U] = bEnd ? Guess[U] : Height;
			}
		}

		// nodes start at the average of their roads' ends; a node only bridges touch starts from their measured ends
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			if (BaseWeight[NodeIndex] > 0.0)
			{
				Guess[NodeIndex] = BaseRhs[NodeIndex] / BaseWeight[NodeIndex];
				continue;
			}
			double Sum = 0.0;
			int32_t Number = 0;
			for (const int32_t EdgeIndex : Graph.Nodes[NodeIndex].Edges)
			{
				Sum += !Data[EdgeIndex].empty() ? (Graph.Edges[EdgeIndex].StartNode == NodeIndex ? Data[EdgeIndex][0] : Data[EdgeIndex].back()) : 0.0;
				++Number;
			}
			Guess[NodeIndex] = Number > 0 ? Sum / Number : 0.0;
		}
		// where the road is meant to be: the measured ground (anything with no ground pull keeps its starting guess)
		Target.resize(NumUnknowns);
		for (int32_t U = 0; U < NumUnknowns; ++U)
		{
			Target[U] = BaseWeight[U] > 0.0 ? BaseRhs[U] / BaseWeight[U] : Guess[U];
		}

		// --- roads whose surfaces overlap (a slip road merging, a wide junction, a car park) must be at the same height
		// where they overlap, or the car meets a ledge where one surface rises above the other. These never change. ---
		std::vector<Entry> FixedEntries;
		{
			constexpr double Cell = 2000.0;
			std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Grid;	// cell -> (edge, segment start)
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const std::vector<Vec2>& P = Points[EdgeIndex];
				for (int32_t i = 0; i + 1 < Count(EdgeIndex); ++i)
				{
					const Vec2 Mid = (P[i] + P[i + 1]) * 0.5;
					Grid[CellKey(FloorToInt(Mid.X / Cell), FloorToInt(Mid.Y / Cell))].emplace_back(EdgeIndex, i);
				}
			}

			int32_t NumCouplings = 0;
			struct Near { int32_t Edge; double Distance; int32_t Segment; double T; };
			std::vector<Near> Nearest;
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				for (int32_t i = 0; i < N; ++i)
				{
					const int32_t HomeX = FloorToInt(P[i].X / Cell), HomeY = FloorToInt(P[i].Y / Cell);
					const double Spacing = 0.5 * ((i > 0 ? Distance(P[i - 1], P[i]) : 0.0) + (i + 1 < N ? Distance(P[i], P[i + 1]) : 0.0));

					// the nearest overlapping piece of each other road (in the order they're first met)
					Nearest.clear();
					for (int32_t X = HomeX - 1; X <= HomeX + 1; ++X)
					{
						for (int32_t Y = HomeY - 1; Y <= HomeY + 1; ++Y)
						{
							const auto Found = Grid.find(CellKey(X, Y));
							if (Found == Grid.end())
							{
								continue;
							}
							for (const std::pair<int32_t, int32_t>& Entry : Found->second)
							{
								const RoadEdge& Other = Graph.Edges[Entry.first];
								if (Entry.first == EdgeIndex || Other.Layer != Edge.Layer || (Other.bBridge || Other.bTunnel) != (Edge.bBridge || Edge.bTunnel))
								{
									continue;
								}
								const std::vector<Vec2>& Q = Points[Entry.first];
								const Vec2 AB = Q[Entry.second + 1] - Q[Entry.second];
								const double T = AB.SizeSquared() > 1.0 ? std::clamp(Dot(P[i] - Q[Entry.second], AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
								const double Gap = Distance(Q[Entry.second] + AB * T, P[i]);

								// the two halves of a dual carriageway (one-way roads side by side going opposite ways)
								// are built on one plane, across a median of up to MedianTie; where they really split onto
								// two levels they're further apart than that
								const Vec2 Own = (P[std::min(i + 1, N - 1)] - P[std::max(i - 1, 0)]).Normal();
								const bool bCarriageways = Edge.bOneway && Other.bOneway && Dot(Own, AB.Normal()) < -0.85;
								const double Reach = bCarriageways ? Edge.HalfWidth + Other.HalfWidth + MedianTie : 0.8 * (Edge.HalfWidth + Other.HalfWidth);
								if (Gap > Reach)
								{
									continue;
								}
								auto Best = std::find_if(Nearest.begin(), Nearest.end(), [&](const Near& Item) { return Item.Edge == Entry.first; });
								if (Best == Nearest.end())
								{
									Nearest.push_back({ Entry.first, Gap, Entry.second, T });
								}
								else if (Gap < Best->Distance)
								{
									*Best = { Entry.first, Gap, Entry.second, T };
								}
							}
						}
					}

					const int32_t U = Unknown(EdgeIndex, i);
					for (const Near& Item : Nearest)
					{
						const int32_t A = Unknown(Item.Edge, Item.Segment);
						const int32_t B = Unknown(Item.Edge, Item.Segment + 1);
						if (A == U || B == U)
						{
							continue;	// the same junction node: already one height
						}
						// strong: overlapping surfaces really are one surface
						const double W = 20.0 * std::max(Spacing, 100.0);
						const int32_t Index[3] = { U, A, B };
						const double Coefficient[3] = { 1.0, -(1.0 - Item.T), -Item.T };
						for (int32_t Row = 0; Row < 3; ++Row)
						{
							for (int32_t Column = 0; Column < 3; ++Column)
							{
								FixedEntries.emplace_back(Index[Row], Index[Column], W * Coefficient[Row] * Coefficient[Column]);
							}
						}
						++NumCouplings;
					}
				}
			}
			Log(Format("[Heights] %d places where roads overlap tied to one height", NumCouplings));
		}

		// --- bending straight through nodes: where two roads carry on (more or less) in a line, the slope carries on too.
		// Between roads of different size only the smaller one's bending counts, so the main road keeps its own shape. ---
		struct NodeBend { int32_t A, B, C; double LengthAB, LengthBC, Weight; };
		std::vector<NodeBend> NodeBends;
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const RoadNode& Node = Graph.Nodes[NodeIndex];
			struct Arm { int32_t Neighbour; double Length; Vec2 Direction; double Bend; };
			std::vector<Arm> Arms;
			for (const int32_t EdgeIndex : Node.Edges)
			{
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				if (N < 2)
				{
					continue;
				}
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				for (const bool bAtStart : { true, false })
				{
					if ((bAtStart ? Edge.StartNode : Edge.EndNode) != NodeIndex)
					{
						continue;
					}
					const int32_t Next = bAtStart ? 1 : N - 2;
					const Vec2 From = bAtStart ? P[0] : P.back();
					Arms.push_back({ Unknown(EdgeIndex, Next), Distance(From, P[Next]), (P[Next] - From).Normal(), std::pow(BendLength(Edge.Highway), 4.0) });
				}
			}
			for (size_t i = 0; i < Arms.size(); ++i)
			{
				for (size_t j = i + 1; j < Arms.size(); ++j)
				{
					// 1 when the arms point in opposite directions (a straight line), fading to 0 for a right angle or less
					const double Straightness = std::clamp(-Dot(Arms[i].Direction, Arms[j].Direction), 0.0, 1.0);
					const double Weight = std::min(Arms[i].Bend, Arms[j].Bend) * Straightness * Straightness;
					if (Weight > 0.0)
					{
						NodeBends.push_back({ Arms[i].Neighbour, NodeIndex, Arms[j].Neighbour, Arms[i].Length, Arms[j].Length, Weight });
					}
				}
			}
		}

		// --- what the rounds tighten: bending (where a crest or dip is sharper than the rules allow) and grade (where a
		// road is steeper than allowed) ---
		std::vector<double> X = Guess;
		std::vector<std::vector<double>> BendBoost(NumEdges), GradeWeight(NumEdges);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			BendBoost[EdgeIndex].assign(Count(EdgeIndex), 1.0);
			GradeWeight[EdgeIndex].assign(std::max(0, Count(EdgeIndex) - 1), 0.0);
		}

		std::vector<Entry> Entries;
		std::vector<double> Rhs;
		Sparse Matrix;
		std::vector<double> D, L1, L2, R, Z, Dir, ADir;
		int32_t TotalIterations = 0;
		double Residual = 0.0;
		int32_t Round = 0;
		int32_t NumOverGrade = 0, NumSharpCurve = 0, NumHeldToGround = 0;

		// One smooth solve: heights near Goal (pulled with the ground pull's weights times PullScale), bending as little as
		// each road's class allows, overlapping roads at one height, plus whatever the rule rounds have tightened
		auto SolveSystem = [&](const std::vector<double>& Goal, double PullScale)
		{
			// --- assemble ---
			Entries = FixedEntries;
			Rhs.assign(NumUnknowns, 0.0);

			auto AddBend = [&](int32_t A, int32_t B, int32_t C, double LengthAB, double LengthBC, double Weight)
			{
				if (A == B || B == C || A == C || LengthAB < 1.0 || LengthBC < 1.0 || Weight <= 0.0)
				{
					return;
				}
				double Coefficient[3];
				BendCoefficients(LengthAB, LengthBC, Coefficient);
				const int32_t Index[3] = { A, B, C };
				const double W = Weight * 0.5 * (LengthAB + LengthBC);	// integrated over the length it covers
				for (int32_t i = 0; i < 3; ++i)
				{
					for (int32_t j = 0; j < 3; ++j)
					{
						Entries.emplace_back(Index[i], Index[j], W * Coefficient[i] * Coefficient[j]);
					}
				}
			};

			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				const double Bend = std::pow(BendLength(Edge.Highway), 4.0);
				for (int32_t i = 1; i + 1 < N; ++i)
				{
					AddBend(Unknown(EdgeIndex, i - 1), Unknown(EdgeIndex, i), Unknown(EdgeIndex, i + 1),
						Distance(P[i - 1], P[i]), Distance(P[i], P[i + 1]), Bend * BendBoost[EdgeIndex][i]);
				}

				// grade: (h[b] - h[a]) pulled to the steepest allowed slope, in the direction it was going
				for (int32_t i = 0; i + 1 < N; ++i)
				{
					const double W = GradeWeight[EdgeIndex][i];
					if (W <= 0.0)
					{
						continue;
					}
					const int32_t A = Unknown(EdgeIndex, i), B = Unknown(EdgeIndex, i + 1);
					const double Length = Distance(P[i], P[i + 1]);
					const double Rise = Sign(X[B] - X[A]) * GradeLimit[EdgeIndex][i] * Length;
					Entries.emplace_back(A, A, W);
					Entries.emplace_back(B, B, W);
					Entries.emplace_back(A, B, -W);
					Entries.emplace_back(B, A, -W);
					Rhs[A] -= W * Rise;
					Rhs[B] += W * Rise;
				}
			}
			for (const NodeBend& Bend : NodeBends)
			{
				AddBend(Bend.A, Bend.B, Bend.C, Bend.LengthAB, Bend.LengthBC, Bend.Weight);
			}
			for (int32_t U = 0; U < NumUnknowns; ++U)
			{
				// anything with no ground pull at all (a bridge on its own) gets a faint one so the system stays solvable
				const double W = (BaseWeight[U] > 0.0 ? BaseWeight[U] : 1.0e-3) * PullScale;
				Rhs[U] += W * Goal[U];
				Entries.emplace_back(U, U, W);
			}

			Matrix.Build(NumUnknowns, Entries);

			// --- conjugate gradients. The preconditioner solves each road's own points exactly (their bending terms link
			// each point only to its two neighbours either side: a banded system, factorised once), so the iterations only
			// have to settle how roads connect at nodes and where they overlap, which takes far fewer of them ---
			D.assign(NumUnknowns, 0.0);
			L1.assign(NumUnknowns, 0.0);
			L2.assign(NumUnknowns, 0.0);
			for (int32_t Node = 0; Node < NumNodes; ++Node)
			{
				D[Node] = Matrix.At(Node, Node);
			}
			ParallelFor(NumEdges, [&](int32_t EdgeIndex)
			{
				const int32_t Base = EdgeBase[EdgeIndex];
				const int32_t Inner = std::max(0, Count(EdgeIndex) - 2);
				for (int32_t k = 0; k < Inner; ++k)
				{
					const int32_t I = Base + k;
					if (k >= 2)
					{
						L2[I] = Matrix.At(I, I - 2) / D[I - 2];
					}
					if (k >= 1)
					{
						L1[I] = (Matrix.At(I, I - 1) - (k >= 2 ? L2[I] * L1[I - 1] * D[I - 2] : 0.0)) / D[I - 1];
					}
					D[I] = Matrix.At(I, I) - (k >= 1 ? L1[I] * L1[I] * D[I - 1] : 0.0) - (k >= 2 ? L2[I] * L2[I] * D[I - 2] : 0.0);
					D[I] = std::max(D[I], 1.0e-9);
				}
			});

			auto Precondition = [&](const std::vector<double>& In, std::vector<double>& Result)
			{
				for (int32_t Node = 0; Node < NumNodes; ++Node)
				{
					Result[Node] = In[Node] / D[Node];
				}
				ParallelFor(NumEdges, [&](int32_t EdgeIndex)
				{
					const int32_t Base = EdgeBase[EdgeIndex];
					const int32_t Inner = std::max(0, Count(EdgeIndex) - 2);
					for (int32_t k = 0; k < Inner; ++k)
					{
						const int32_t I = Base + k;
						Result[I] = In[I] - (k >= 1 ? L1[I] * Result[I - 1] : 0.0) - (k >= 2 ? L2[I] * Result[I - 2] : 0.0);
					}
					for (int32_t k = 0; k < Inner; ++k)
					{
						Result[Base + k] /= D[Base + k];
					}
					for (int32_t k = Inner - 1; k >= 0; --k)
					{
						const int32_t I = Base + k;
						Result[I] -= (k + 1 < Inner ? L1[I + 1] * Result[I + 1] : 0.0) + (k + 2 < Inner ? L2[I + 2] * Result[I + 2] : 0.0);
					}
				});
			};

			// warm start from the last round's answer
			R.assign(NumUnknowns, 0.0);
			Z.assign(NumUnknowns, 0.0);
			ADir.assign(NumUnknowns, 0.0);
			Matrix.Multiply(ParallelFor, X, ADir);
			for (int32_t i = 0; i < NumUnknowns; ++i)
			{
				R[i] = Rhs[i] - ADir[i];
			}
			Precondition(R, Z);
			Dir = Z;
			double RZ = DotProduct(R, Z);
			const double RhsNorm = std::max(std::sqrt(DotProduct(Rhs, Rhs)), 1.0);
			Residual = std::sqrt(DotProduct(R, R)) / RhsNorm;
			// (the iterations' sweeps over every point in parallel too: in 64 fixed blocks, their sums added in order, so the
			// answer is the same on any machine, whatever its threads)
			constexpr int32_t NumBlocks = 64;
			double Part[NumBlocks], PartB[NumBlocks];
			auto Sweep = [&](auto&& Fn)
			{
				ParallelFor(NumBlocks, [&](int32_t Block)
				{
					Fn(int32_t(int64_t(NumUnknowns) * Block / NumBlocks), int32_t(int64_t(NumUnknowns) * (Block + 1) / NumBlocks), Block);
				});
			};
			auto Total = [](const double (&Parts)[NumBlocks])
			{
				double Sum = 0.0;
				for (const double P : Parts)
				{
					Sum += P;
				}
				return Sum;
			};
			for (int32_t Iteration = 0; Iteration < 400 && Residual > 1.0e-3; ++Iteration, ++TotalIterations)
			{
				Matrix.Multiply(ParallelFor, Dir, ADir);
				Sweep([&](int32_t Lo, int32_t Hi, int32_t Block)
				{
					double Sum = 0.0;
					for (int32_t i = Lo; i < Hi; ++i)
					{
						Sum += Dir[i] * ADir[i];
					}
					Part[Block] = Sum;
				});
				const double Alpha = RZ / std::max(Total(Part), 1.0e-300);
				Sweep([&](int32_t Lo, int32_t Hi, int32_t)
				{
					for (int32_t i = Lo; i < Hi; ++i)
					{
						X[i] += Alpha * Dir[i];
						R[i] -= Alpha * ADir[i];
					}
				});
				Precondition(R, Z);
				Sweep([&](int32_t Lo, int32_t Hi, int32_t Block)
				{
					double SumRZ = 0.0, SumRR = 0.0;
					for (int32_t i = Lo; i < Hi; ++i)
					{
						SumRZ += R[i] * Z[i];
						SumRR += R[i] * R[i];
					}
					Part[Block] = SumRZ;
					PartB[Block] = SumRR;
				});
				const double NewRZ = Total(Part);
				const double Beta = NewRZ / std::max(RZ, 1.0e-300);
				RZ = NewRZ;
				Sweep([&](int32_t Lo, int32_t Hi, int32_t)
				{
					for (int32_t i = Lo; i < Hi; ++i)
					{
						Dir[i] = Z[i] + Beta * Dir[i];
					}
				});
				Residual = std::sqrt(Total(PartB)) / RhsNorm;
			}
		};

		for (const int32_t NumRounds = std::max(1, Options.RuleRounds); Round < NumRounds; ++Round)
		{
			SolveSystem(Target, 1.0);

			// --- check the rules and tighten where they're broken. A road may leave the ground only so far (its
			// embankments and cuttings): beyond that the real road really is that steep, so the rules ease off there
			// (never pinning single points, which would make steps) ---
			std::vector<uint8_t> TooFar(NumUnknowns, 0);
			int32_t NewGround = 0;
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				if (Graph.Edges[EdgeIndex].bBridge || Graph.Edges[EdgeIndex].bTunnel)
				{
					continue;
				}
				const int32_t N = Count(EdgeIndex);
				for (int32_t i = 0; i < N; ++i)
				{
					const int32_t U = Unknown(EdgeIndex, i);
					if (BaseWeight[U] <= 0.0 || std::abs(X[U] - Target[U]) <= Design[EdgeIndex].MaxLift)
					{
						continue;
					}
					TooFar[U] = 1;
					for (int32_t j = std::max(0, i - 2); j <= std::min(N - 1, i + 2); ++j)
					{
						if (BendBoost[EdgeIndex][j] > 1.0)
						{
							BendBoost[EdgeIndex][j] = std::max(1.0, BendBoost[EdgeIndex][j] / BoostStep);
							++NewGround;
						}
						if (j < static_cast<int32_t>(GradeWeight[EdgeIndex].size()) && GradeWeight[EdgeIndex][j] > 0.0)
						{
							GradeWeight[EdgeIndex][j] /= BoostStep;
							++NewGround;
						}
					}
				}
			}

			NumOverGrade = 0;
			NumSharpCurve = 0;
			int32_t NewTightened = 0;
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				if (Graph.Edges[EdgeIndex].bBridge || Graph.Edges[EdgeIndex].bTunnel)
				{
					continue;
				}
				const RoadDesign& Rules = Design[EdgeIndex];
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				for (int32_t i = 0; i + 1 < N; ++i)
				{
					const int32_t A = Unknown(EdgeIndex, i), B = Unknown(EdgeIndex, i + 1);
					const double Length = Distance(P[i], P[i + 1]);
					if (Length < 100.0 || std::abs(X[B] - X[A]) / Length <= GradeLimit[EdgeIndex][i] * GradeTolerance)
					{
						continue;
					}
					++NumOverGrade;
					if (!TooFar[A] && !TooFar[B])
					{
						double& W = GradeWeight[EdgeIndex][i];
						W = W <= 0.0 ? GradeStartWeight * Length : std::min(W * BoostStep, MaxBoost * Length);
						++NewTightened;
					}
				}

				// K (m per 1% of grade change) -> the largest change of slope per cm
				const double CrestLimit = 1.0e-4 / Rules.CrestK;
				const double SagLimit = 1.0e-4 / Rules.SagK;
				for (int32_t i = 1; i + 1 < N; ++i)
				{
					const double LengthAB = Distance(P[i - 1], P[i]);
					const double LengthBC = Distance(P[i], P[i + 1]);
					if (LengthAB < 100.0 || LengthBC < 100.0)
					{
						continue;
					}
					double Coefficient[3];
					BendCoefficients(LengthAB, LengthBC, Coefficient);
					const int32_t A = Unknown(EdgeIndex, i - 1), B = Unknown(EdgeIndex, i), C = Unknown(EdgeIndex, i + 1);
					const double Curvature = Coefficient[0] * X[A] + Coefficient[1] * X[B] + Coefficient[2] * X[C];
					const double Limit = Curvature < 0.0 ? CrestLimit : SagLimit;
					if (std::abs(Curvature) <= Limit * CurveTolerance)
					{
						continue;
					}
					++NumSharpCurve;
					if (!TooFar[A] && !TooFar[B] && !TooFar[C])
					{
						// spread over the neighbours too, so the crest is rounded over a length rather than one point
						for (int32_t j = i - 1; j <= i + 1; ++j)
						{
							BendBoost[EdgeIndex][j] = std::min(BendBoost[EdgeIndex][j] * (j == i ? BoostStep : 2.0), MaxBoost);
						}
						++NewTightened;
					}
				}
			}
			NumHeldToGround = static_cast<int32_t>(std::count_if(TooFar.begin(), TooFar.end(), [](uint8_t Value) { return Value != 0; }));

			Log(Format("[Heights] round %d: %d segments too steep, %d crests/dips too sharp, %d points held to the ground (%d tightened)",
				Round + 1, NumOverGrade, NumSharpCurve, NumHeldToGround, NewTightened));
			if (NewTightened == 0 && NewGround == 0)
			{
				++Round;
				break;
			}
		}

		// --- hard grade limits: the solve above only smooths, so steep stretches of the height data (or forest misread as
		// ground) still come through. The road is clipped to its legal grade; the ground is reshaped to meet it later.
		// But a road may only leave the ground by so much (its earthworks budget): where obeying the rule would need more
		// cut or fill than that, the real road is that steep (an old mountain pass), so there it may follow the ground's own
		// long-run slope. Then the network is smoothed again so the clipped corners become vertical curves, and clipped
		// once more so the smoothing can't push anything back over.
		// In the "land" style the road follows the land, the way the real roads do: the limit is never below the land's own
		// slope over ~200 m, so the real climbs stay and only short bumps from the height data get cut; and it may leave
		// the ground by little. In the "rules" style it's held to the legal grades. ---
		const bool bFollowLand = Options.bFollowLand;
		Log(Format("[Heights] road style: %s", bFollowLand ? "land" : "rules"));
		if (Options.bGradeLimit)
		{
			const double LimitStart = Seconds();
			const std::vector<double> Smooth = X;
			const double LandWindow = bFollowLand ? LandTrendHalfLength : 5000.0;

			// the ground's own slope around each segment (from the smooth solve, so noise doesn't count)
			std::vector<std::vector<double>> LandGrade(NumEdges);
			ParallelFor(NumEdges, [&](int32_t EdgeIndex)
			{
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const int32_t N = Count(EdgeIndex);
				std::vector<double> Along(1, 0.0);
				for (int32_t i = 1; i < N; ++i)
				{
					Along.push_back(Along.back() + Distance(P[i - 1], P[i]));
				}
				LandGrade[EdgeIndex].assign(std::max(0, N - 1), 0.0);
				int32_t Low = 0, High = 0;
				for (int32_t i = 0; i + 1 < N; ++i)
				{
					const double Middle = 0.5 * (Along[i] + Along[i + 1]);
					while (Low + 1 < N && Along[Low + 1] < Middle - LandWindow) { ++Low; }
					while (High + 1 < N && Along[High] < Middle + LandWindow) { ++High; }
					const double Run = Along[High] - Along[Low];
					LandGrade[EdgeIndex][i] = Run > 100.0 ? std::abs(Smooth[Unknown(EdgeIndex, High)] - Smooth[Unknown(EdgeIndex, Low)]) / Run : 0.0;
				}
			});

			std::vector<std::vector<double>> Limit = GradeLimit;
			if (bFollowLand)
			{
				// never flatter than the land's own trend: only bumps steeper than it are cut
				for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
				{
					for (size_t i = 0; i < Limit[EdgeIndex].size(); ++i)
					{
						Limit[EdgeIndex][i] = std::max(static_cast<double>(Design[EdgeIndex].MaxGradeMountain), LandGrade[EdgeIndex][i] * LandTrendMargin + 0.01);
					}
				}
			}
			std::vector<std::vector<GradeLink>> Links;
			auto BuildLinks = [&]()
			{
				Links.assign(NumUnknowns, {});
				for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
				{
					const std::vector<Vec2>& P = Points[EdgeIndex];
					for (int32_t i = 0; i + 1 < Count(EdgeIndex); ++i)
					{
						const int32_t A = Unknown(EdgeIndex, i), B = Unknown(EdgeIndex, i + 1);
						const double MaxRise = Limit[EdgeIndex][i] * Distance(P[i], P[i + 1]);
						Links[A].push_back({ B, MaxRise });
						Links[B].push_back({ A, MaxRise });
					}
				}
			};

			// the slope a road may take where it has to follow the ground: in the land style the ground's own slope right
			// there (from the smooth solve), in the rules style its slope over ~100 m
			auto OwnGrade = [&](int32_t EdgeIndex, int32_t Segment)
			{
				if (!bFollowLand)
				{
					return LandGrade[EdgeIndex][Segment];
				}
				const std::vector<Vec2>& P = Points[EdgeIndex];
				const double Length = std::max(Distance(P[Segment], P[Segment + 1]), 1.0);
				return std::abs(Smooth[Unknown(EdgeIndex, Segment + 1)] - Smooth[Unknown(EdgeIndex, Segment)]) / Length;
			};

			// find the limits: clip, and wherever a road ends up further from the ground than its budget, let the stretch
			// around it (60 m either way) be as steep as the ground there, then try again
			int32_t Relaxed = 0, Passes = 0;
			std::vector<double> Clipped;
			for (; Passes < 6; ++Passes)
			{
				BuildLinks();
				Clipped = Smooth;
				LimitGrades(Clipped, Links);
				int32_t NewlyRelaxed = 0;
				for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
				{
					const std::vector<Vec2>& P = Points[EdgeIndex];
					const int32_t N = Count(EdgeIndex);
					const double Budget = bFollowLand ? LandEarthworks : std::clamp(static_cast<double>(Design[EdgeIndex].MaxLift), 150.0, MaxEarthworks);
					const double Cap = bFollowLand ? 0.5 : RelaxedGradeCap(Design[EdgeIndex], Graph.Edges[EdgeIndex].Highway);
					for (int32_t i = 0; i < N; ++i)
					{
						const int32_t U = Unknown(EdgeIndex, i);
						if (std::abs(Clipped[U] - Smooth[U]) <= Budget)
						{
							continue;
						}
						double Reach = 0.0;
						for (int32_t j = i; j > 0 && Reach < 6000.0; --j)
						{
							Reach += Distance(P[j - 1], P[j]);
							const double Steeper = std::max(Limit[EdgeIndex][j - 1], std::min(OwnGrade(EdgeIndex, j - 1) * 1.05, Cap));
							NewlyRelaxed += Steeper > Limit[EdgeIndex][j - 1] ? 1 : 0;
							Limit[EdgeIndex][j - 1] = Steeper;
						}
						Reach = 0.0;
						for (int32_t j = i; j + 1 < N && Reach < 6000.0; ++j)
						{
							Reach += Distance(P[j], P[j + 1]);
							const double Steeper = std::max(Limit[EdgeIndex][j], std::min(OwnGrade(EdgeIndex, j) * 1.05, Cap));
							NewlyRelaxed += Steeper > Limit[EdgeIndex][j] ? 1 : 0;
							Limit[EdgeIndex][j] = Steeper;
						}
					}
				}
				Relaxed += NewlyRelaxed;
				if (NewlyRelaxed == 0)
				{
					break;
				}
			}

			// round the clipped corners: solve the whole network again, pulled to the clipped heights like it was to the
			// ground (through junctions too, where the bending carries on along straight-through roads), then the final clip
			SolveSystem(Clipped, 1.0);
			BuildLinks();
			LimitGrades(X, Links);

			// how far the roads now sit from the measured ground: the ground will be cut and filled to meet them
			int32_t Moved1m = 0, Moved3m = 0, Moved10m = 0;
			double Largest = 0.0;
			for (int32_t i = 0; i < NumUnknowns; ++i)
			{
				const double Moved = std::abs(X[i] - Smooth[i]);
				Moved1m += Moved > 100.0 ? 1 : 0;
				Moved3m += Moved > 300.0 ? 1 : 0;
				Moved10m += Moved > 1000.0 ? 1 : 0;
				Largest = std::max(Largest, Moved);
			}
			Log(Format("[Heights] grade limits: %d segments allowed the ground's own slope (earthworks budget, %d passes); %d points moved over 1 m, %d over 3 m, %d over 10 m (most %.1f m), %.2f s",
				Relaxed, Passes + 1, Moved1m, Moved3m, Moved10m, Largest / 100.0, Seconds() - LimitStart));
		}

		// --- grade separation: a bridge over another road must clear it. The height data alone can't tell (30 m cells
		// smooth an interchange's embankments away), so a flyover would come out level with the road beneath it, a
		// crossroads. Wherever a bridge crosses a road on a lower layer, the deck is lifted to clear it, and the lift
		// eases out along the bridge and the roads leading onto it, gentler than their grade limit and rounded at both
		// ends. The road underneath stays where it is. ---
		{
			const double SeparationStart = Seconds();
			struct Link { int32_t Other; double Length; };
			std::vector<std::vector<Link>> Adjacent(NumUnknowns);
			constexpr double Cell = 5000.0;
			std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Grid;	// cell -> (edge, segment)
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const std::vector<Vec2>& P = Points[EdgeIndex];
				for (int32_t i = 0; i + 1 < Count(EdgeIndex); ++i)
				{
					const int32_t A = Unknown(EdgeIndex, i), B = Unknown(EdgeIndex, i + 1);
					const double Length = Distance(P[i], P[i + 1]);
					Adjacent[A].push_back({ B, Length });
					Adjacent[B].push_back({ A, Length });
					for (int32_t CX = FloorToInt(std::min(P[i].X, P[i + 1].X) / Cell); CX <= FloorToInt(std::max(P[i].X, P[i + 1].X) / Cell); ++CX)
					{
						for (int32_t CY = FloorToInt(std::min(P[i].Y, P[i + 1].Y) / Cell); CY <= FloorToInt(std::max(P[i].Y, P[i + 1].Y) / Cell); ++CY)
						{
							Grid[CellKey(CX, CY)].emplace_back(EdgeIndex, i);
						}
					}
				}
			}

			// every place a bridge segment crosses a segment of a road on a lower layer, and how far short it falls
			std::vector<double> Lift(NumUnknowns, 0.0);
			int32_t Crossings = 0, Lifted = 0;
			double MostLift = 0.0;
			std::unordered_set<int32_t> Underneath;	// the points of roads passed over, at each crossing
			std::unordered_set<int32_t> DeckPoints;	// the bridge points over them
			std::vector<double> Reached;
			auto Named = [](const std::string& Name, const char* Otherwise) { return Name.empty() ? std::string(Otherwise) : Name; };
			for (int32_t DeckEdge = 0; DeckEdge < NumEdges; ++DeckEdge)
			{
				const RoadEdge& Deck = Graph.Edges[DeckEdge];
				if (!Deck.bBridge)
				{
					continue;
				}
				const std::vector<Vec2>& P = Points[DeckEdge];
				for (int32_t i = 0; i + 1 < Count(DeckEdge); ++i)
				{
					const Vec2 A = P[i], AB = P[i + 1] - P[i];
					std::unordered_set<uint64_t> Seen;
					for (int32_t CX = FloorToInt(std::min(P[i].X, P[i + 1].X) / Cell); CX <= FloorToInt(std::max(P[i].X, P[i + 1].X) / Cell); ++CX)
					{
						for (int32_t CY = FloorToInt(std::min(P[i].Y, P[i + 1].Y) / Cell); CY <= FloorToInt(std::max(P[i].Y, P[i + 1].Y) / Cell); ++CY)
						{
							const auto Nearby = Grid.find(CellKey(CX, CY));
							if (Nearby == Grid.end())
							{
								continue;
							}
							for (const std::pair<int32_t, int32_t>& Other : Nearby->second)
							{
								const RoadEdge& Below = Graph.Edges[Other.first];
								if (Other.first == DeckEdge || Below.Layer >= Deck.Layer || !Seen.insert(CellKey(Other.first, Other.second)).second)
								{
									continue;
								}
								const std::vector<Vec2>& Q = Points[Other.first];
								const Vec2 C = Q[Other.second], CD = Q[Other.second + 1] - Q[Other.second];
								const double Denominator = AB.X * CD.Y - AB.Y * CD.X;
								if (std::abs(Denominator) < 1.0)
								{
									continue;	// parallel
								}
								const double T = ((C.X - A.X) * CD.Y - (C.Y - A.Y) * CD.X) / Denominator;
								const double U = ((C.X - A.X) * AB.Y - (C.Y - A.Y) * AB.X) / Denominator;
								// a real crossing passes through both segments; a bridge only touching its own approach road
								// at its end (they share a point) is not one
								const int32_t DeckA = Unknown(DeckEdge, i), DeckB = Unknown(DeckEdge, i + 1);
								const int32_t BelowA = Unknown(Other.first, Other.second), BelowB = Unknown(Other.first, Other.second + 1);
								if (T <= 0.01 || T >= 0.99 || U <= 0.01 || U >= 0.99
									|| DeckA == BelowA || DeckA == BelowB || DeckB == BelowA || DeckB == BelowB)
								{
									continue;
								}
								++Crossings;
								const double Gap = Lerp(X[DeckA], X[DeckB], T) - Lerp(X[BelowA], X[BelowB], U);
								const double Short = BridgeClearance - Gap;
								if (Short <= 1.0)
								{
									Log(Format("[Heights]   %s (%s) already clears %s (%s) by %.1f m", Named(Deck.Name, "unnamed bridge").c_str(), Deck.Highway.c_str(),
										Named(Below.Name, "an unnamed road").c_str(), Below.Highway.c_str(), Gap / 100.0));
									continue;
								}
								++Lifted;
								Underneath.insert(BelowA);
								Underneath.insert(BelowB);
								DeckPoints.insert(DeckA);
								DeckPoints.insert(DeckB);
								Log(Format("[Heights]   %s (%s) lifted %.1f m to clear %s (%s)", Named(Deck.Name, "unnamed bridge").c_str(), Deck.Highway.c_str(), Short / 100.0,
									Named(Below.Name, "an unnamed road").c_str(), Below.Highway.c_str()));
								MostLift = std::max(MostLift, Short);

								// ease the lift out over the distance a gentler-than-allowed grade needs (a smoothstep's
								// steepest slope is 1.5 x its height over its length)
								const double Grade = std::max(0.02, Design[DeckEdge].MaxGrade * BridgeRampFraction);
								const double Reach = 1.5 * Short / Grade;
								const double SegmentLength = AB.Size();
								Reached.assign(NumUnknowns, std::numeric_limits<double>::max());
								MinHeap Heap;
								Reached[DeckA] = T * SegmentLength;
								Reached[DeckB] = (1.0 - T) * SegmentLength;
								Heap.emplace(Reached[DeckA], DeckA);
								Heap.emplace(Reached[DeckB], DeckB);
								while (!Heap.empty())
								{
									const std::pair<double, int32_t> Top = Heap.top();
									Heap.pop();
									if (Top.first > Reached[Top.second] || Top.first > Reach)
									{
										continue;
									}
									// the road underneath never rises with the bridge over it
									if (Top.second == BelowA || Top.second == BelowB)
									{
										continue;
									}
									Lift[Top.second] = std::max(Lift[Top.second], Short * SmoothStep(0.0, Reach, Reach - Top.first));
									for (const Link& Next : Adjacent[Top.second])
									{
										const double Along = Top.first + Next.Length;
										if (Along < Reached[Next.Other])
										{
											Reached[Next.Other] = Along;
											Heap.emplace(Along, Next.Other);
										}
									}
								}
							}
						}
					}
				}
			}
			// the lifts are rounded where they meet: two spreading round an interchange loop meet in a V, and where two
			// overlap the higher wins with a crease (each a sharp dip a car is thrown out of). A few passes of averaging
			// along the roads round them over ~40 m, far shorter than a lift, which keeps its shape; the decks keep their
			// clearance, and the roads underneath stay put.
			{
				std::vector<double> Smooth = Lift;
				for (int32_t Pass = 0; Pass < 12; ++Pass)
				{
					std::vector<double> Next = Smooth;
					for (int32_t U = 0; U < NumUnknowns; ++U)
					{
						if (Underneath.count(U) || DeckPoints.count(U))
						{
							continue;	// held: what the bridge must clear, and what it passes over
						}
						double Sum = Smooth[U], Number = 1.0;
						bool bNearLift = Smooth[U] > 0.0;
						for (const Link& Item : Adjacent[U])
						{
							if (!Underneath.count(Item.Other))
							{
								Sum += Smooth[Item.Other];
								Number += 1.0;
								bNearLift |= Smooth[Item.Other] > 0.0;
							}
						}
						if (bNearLift)
						{
							Next[U] = Sum / Number;
						}
					}
					Smooth = std::move(Next);
				}
				Lift = std::move(Smooth);
			}
			for (int32_t U = 0; U < NumUnknowns; ++U)
			{
				X[U] += Lift[U];
			}
			Log(Format("[Heights] grade separation: %d places a bridge crosses a lower road, %d lifted to clear it (most %.1f m), %.2f s",
				Crossings, Lifted, MostLift / 100.0, Seconds() - SeparationStart));
		}

		// --- and the other way: a tunnel under another road must clear it, its 6.5 m tube and its roof under that road's
		// surface. Its road runs straight from mouth to mouth, so under a road at the ground's level (the Nordschleife's
		// straight, a London street) its tube came up through it. Wherever a tunnel passes under a road on a higher layer
		// too close, it dips there, easing out along it (and into its approaches) gentler than its grade limit; the road
		// above stays where it is. ---
		{
			const double DipStart = Seconds();
			struct Link { int32_t Other; double Length; };
			std::vector<std::vector<Link>> Adjacent(NumUnknowns);
			constexpr double Cell = 5000.0;
			std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Grid;	// cell -> (edge, segment)
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const std::vector<Vec2>& P = Points[EdgeIndex];
				for (int32_t i = 0; i + 1 < Count(EdgeIndex); ++i)
				{
					const int32_t A = Unknown(EdgeIndex, i), B = Unknown(EdgeIndex, i + 1);
					const double Length = Distance(P[i], P[i + 1]);
					Adjacent[A].push_back({ B, Length });
					Adjacent[B].push_back({ A, Length });
					for (int32_t CX = FloorToInt(std::min(P[i].X, P[i + 1].X) / Cell); CX <= FloorToInt(std::max(P[i].X, P[i + 1].X) / Cell); ++CX)
					{
						for (int32_t CY = FloorToInt(std::min(P[i].Y, P[i + 1].Y) / Cell); CY <= FloorToInt(std::max(P[i].Y, P[i + 1].Y) / Cell); ++CY)
						{
							Grid[CellKey(CX, CY)].emplace_back(EdgeIndex, i);
						}
					}
				}
			}
			std::vector<double> Dip(NumUnknowns, 0.0);
			int32_t Crossings = 0, Dipped = 0;
			double MostDip = 0.0;
			std::unordered_set<int32_t> Above;	// the points of the roads passed under: held
			std::unordered_set<int32_t> TubePoints;	// the tunnel's points under them
			std::vector<double> Reached;
			auto Named = [](const std::string& Name, const char* Otherwise) { return Name.empty() ? std::string(Otherwise) : Name; };
			for (int32_t TubeEdge = 0; TubeEdge < NumEdges; ++TubeEdge)
			{
				const RoadEdge& Tube = Graph.Edges[TubeEdge];
				if (!Tube.bTunnel)
				{
					continue;
				}
				const std::vector<Vec2>& P = Points[TubeEdge];
				for (int32_t i = 0; i + 1 < Count(TubeEdge); ++i)
				{
					const Vec2 A = P[i], AB = P[i + 1] - P[i];
					std::unordered_set<uint64_t> Seen;
					for (int32_t CX = FloorToInt(std::min(P[i].X, P[i + 1].X) / Cell); CX <= FloorToInt(std::max(P[i].X, P[i + 1].X) / Cell); ++CX)
					{
						for (int32_t CY = FloorToInt(std::min(P[i].Y, P[i + 1].Y) / Cell); CY <= FloorToInt(std::max(P[i].Y, P[i + 1].Y) / Cell); ++CY)
						{
							const auto Nearby = Grid.find(CellKey(CX, CY));
							if (Nearby == Grid.end())
							{
								continue;
							}
							for (const std::pair<int32_t, int32_t>& Other : Nearby->second)
							{
								const RoadEdge& Over = Graph.Edges[Other.first];
								if (Other.first == TubeEdge || Over.Layer <= Tube.Layer || Over.bTunnel || !Seen.insert(CellKey(Other.first, Other.second)).second)
								{
									continue;
								}
								const std::vector<Vec2>& Q = Points[Other.first];
								const Vec2 C = Q[Other.second], CD = Q[Other.second + 1] - Q[Other.second];
								const double Denominator = AB.X * CD.Y - AB.Y * CD.X;
								if (std::abs(Denominator) < 1.0)
								{
									continue;	// parallel
								}
								const double T = ((C.X - A.X) * CD.Y - (C.Y - A.Y) * CD.X) / Denominator;
								const double U = ((C.X - A.X) * AB.Y - (C.Y - A.Y) * AB.X) / Denominator;
								const int32_t TubeA = Unknown(TubeEdge, i), TubeB = Unknown(TubeEdge, i + 1);
								const int32_t OverA = Unknown(Other.first, Other.second), OverB = Unknown(Other.first, Other.second + 1);
								if (T <= 0.01 || T >= 0.99 || U <= 0.01 || U >= 0.99
									|| TubeA == OverA || TubeA == OverB || TubeB == OverA || TubeB == OverB)
								{
									continue;
								}
								++Crossings;
								const double Gap = Lerp(X[OverA], X[OverB], U) - Lerp(X[TubeA], X[TubeB], T);
								const double Short = TunnelClearance - Gap;
								if (Short <= 1.0)
								{
									continue;
								}
								++Dipped;
								Above.insert(OverA);
								Above.insert(OverB);
								TubePoints.insert(TubeA);
								TubePoints.insert(TubeB);
								Log(Format("[Heights]   %s (%s, a tunnel) lowered %.1f m to pass under %s (%s)", Named(Tube.Name, "an unnamed tunnel").c_str(),
									Tube.Highway.c_str(), Short / 100.0, Named(Over.Name, "an unnamed road").c_str(), Over.Highway.c_str()));
								MostDip = std::max(MostDip, Short);
								const double Grade = std::max(0.02, Design[TubeEdge].MaxGrade * BridgeRampFraction);
								const double Reach = 1.5 * Short / Grade;
								const double SegmentLength = AB.Size();
								Reached.assign(NumUnknowns, std::numeric_limits<double>::max());
								MinHeap Heap;
								Reached[TubeA] = T * SegmentLength;
								Reached[TubeB] = (1.0 - T) * SegmentLength;
								Heap.emplace(Reached[TubeA], TubeA);
								Heap.emplace(Reached[TubeB], TubeB);
								while (!Heap.empty())
								{
									const std::pair<double, int32_t> Top = Heap.top();
									Heap.pop();
									if (Top.first > Reached[Top.second] || Top.first > Reach)
									{
										continue;
									}
									if (Top.second == OverA || Top.second == OverB)
									{
										continue;	// the road above never sinks with the tunnel under it
									}
									Dip[Top.second] = std::max(Dip[Top.second], Short * SmoothStep(0.0, Reach, Reach - Top.first));
									for (const Link& Next : Adjacent[Top.second])
									{
										const double Along = Top.first + Next.Length;
										if (Along < Reached[Next.Other])
										{
											Reached[Next.Other] = Along;
											Heap.emplace(Along, Next.Other);
										}
									}
								}
							}
						}
					}
				}
			}
			// (rounded where two meet, as the lifts are; the tunnel under and the road over held)
			{
				std::vector<double> Smooth = Dip;
				for (int32_t Pass = 0; Pass < 12; ++Pass)
				{
					std::vector<double> Next = Smooth;
					for (int32_t U = 0; U < NumUnknowns; ++U)
					{
						if (Above.count(U) || TubePoints.count(U))
						{
							continue;
						}
						double Sum = Smooth[U], Number = 1.0;
						bool bNearDip = Smooth[U] > 0.0;
						for (const Link& Item : Adjacent[U])
						{
							if (!Above.count(Item.Other))
							{
								Sum += Smooth[Item.Other];
								Number += 1.0;
								bNearDip |= Smooth[Item.Other] > 0.0;
							}
						}
						if (bNearDip)
						{
							Next[U] = Sum / Number;
						}
					}
					Smooth = std::move(Next);
				}
				Dip = std::move(Smooth);
			}
			for (int32_t U = 0; U < NumUnknowns; ++U)
			{
				X[U] -= Dip[U];
			}
			Log(Format("[Heights] tunnels: %d places one passes under a road, %d lowered to clear it (most %.1f m), %.2f s",
				Crossings, Dipped, MostDip / 100.0, Seconds() - DipStart));
		}

		// --- results ---
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			Graph.Nodes[NodeIndex].Height = static_cast<float>(X[NodeIndex]);
		}
		Out.assign(NumEdges, {});
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			Out[EdgeIndex].resize(Count(EdgeIndex));
			for (int32_t i = 0; i < Count(EdgeIndex); ++i)
			{
				Out[EdgeIndex][i] = static_cast<float>(X[Unknown(EdgeIndex, i)]);
			}
		}

		Log(Format("[Heights] road network solved: %d points, %d rounds, %d iterations, residual %.1e, %.2f s",
			NumUnknowns, Round, TotalIterations, Residual, Seconds() - Start));
	}
}
