// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The lines painted on the roads. South Africa: SADC Road Traffic Signs Manual [V1] ch. 7 (dividing lines, no
// overtaking lines from sight distance, yellow edge lines on surfaced shoulders, stop lines). UK: Traffic Signs Manual
// ch. 5 (centre lines and hazard warning lines, double white lines, edge lines, give way lines).

#include "RoadMesh.h"
#include "GeoRoads/RoadDesign.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace geomesh
{
	using georoads::Vec2;
	using georoads::RoadEdge;
	using georoads::RoadNode;
	using geoground::Vec2f;
	using geoground::Vec3f;

	namespace MarkingsImpl
	{
		/** Lines sit this far above the road surface they follow (cm): the road mesh is 3 cm up, lines 2 cm above it */
		constexpr float Lift = 5.0f;

		/** Eye and object heights for sight distance (cm), [V1] 7.2.5.5 */
		constexpr float EyeHeight = 105.0f;
		constexpr float ObjectHeight = 130.0f;

		/** The sight line may not pass further than this beyond the road's edge on a bend (cm): the shoulder break */
		constexpr float SightMargin = 150.0f;

		/** Line widths (cm) and the gap between a dividing line and a no overtaking line beside it */
		constexpr float LineWidth = 10.0f;
		constexpr float MainLineWidth = 15.0f;
		constexpr float PairOffset = 15.0f;

		/** A yellow edge line marks a surfaced shoulder at least this wide (cm), [V1] 7.2.8 */
		constexpr float SurfacedShoulder = 100.0f;

		/** Lines have a point at every road point and at most this far apart in between (cm) */
		constexpr double PointStep = 500.0;

		/** No overtaking zones closer than this are joined (cm), [V1] 7.2.5.9(c) */
		constexpr double JoinGap = 12000.0;

		/** A range along a chain (cm) */
		struct Range
		{
			double X = 0.0, Y = 0.0;
		};

		/** Barrier sight distance (cm) for a design speed, [V1] Table 7.4 */
		double BarrierSightDistance(float SpeedKmh)
		{
			static const float Speeds[] = { 50.0f, 60.0f, 80.0f, 100.0f, 120.0f };
			static const float Distances[] = { 150.0f, 180.0f, 250.0f, 300.0f, 400.0f };
			if (SpeedKmh <= Speeds[0])
			{
				return Distances[0] * 100.0;
			}
			for (int32_t i = 1; i < 5; ++i)
			{
				if (SpeedKmh <= Speeds[i])
				{
					return geoground::LerpF(Distances[i - 1], Distances[i], (SpeedKmh - Speeds[i - 1]) / (Speeds[i] - Speeds[i - 1])) * 100.0;
				}
			}
			return Distances[4] * 100.0;
		}

		/** UK: the visibility a road needs (cm) by its speed band, TSM ch. 5 Table 3-2: below V a double white line, below
		 *  W a warning line. Not interpolated. */
		double UKVisibility(float SpeedKmh, bool bWarning)
		{
			static const double V[] = { 75.0, 95.0, 120.0, 150.0, 175.0 };
			static const double W[] = { 115.0, 160.0, 195.0, 240.0, 275.0 };
			const float Mph = SpeedKmh / 1.609f;
			const int32_t Band = Mph <= 30.5f ? 0 : Mph <= 40.5f ? 1 : Mph <= 50.5f ? 2 : Mph <= 60.5f ? 3 : 4;
			return (bWarning ? W[Band] : V[Band]) * 100.0;
		}

		struct Context
		{
			const georoads::RoadGraph& Graph;
			bool bUK = false;

			georoads::RoadDesign Design(const RoadEdge& Edge) const { return georoads::DesignFor(Edge, bUK); }

			/** The sight a driver needs before the road may be overtaken on (cm); Warning: the UK warning line's */
			double NeededSight(const RoadEdge& Edge, bool bWarning = false) const
			{
				return bUK ? UKVisibility(Edge.SpeedKmh > 0.0f ? Edge.SpeedKmh : Design(Edge).SpeedKmh, bWarning) : BarrierSightDistance(Design(Edge).SpeedKmh);
			}

			/** Rural roads (80 km/h and up, or national roads) use the 12 m module, others the 9 m one */
			bool IsRural(const RoadEdge& Edge) const
			{
				return Design(Edge).SpeedKmh >= 80.0f || georoads::StartsWith(Edge.Highway, "motorway") || georoads::StartsWith(Edge.Highway, "trunk");
			}
		};

		/** UK: faster than 40 mph, where the 9 m module (3 m centre line marks) is used instead of the 6 m one */
		bool IsFastUK(const RoadEdge& Edge)
		{
			return Edge.SpeedKmh > 65.0f;
		}

		/** Roads that get a dividing line and no overtaking lines: through roads, not residential or service streets */
		bool IsMarkedClass(const std::string& Highway)
		{
			return georoads::StartsWith(Highway, "motorway") || georoads::StartsWith(Highway, "trunk") || georoads::StartsWith(Highway, "primary")
				|| georoads::StartsWith(Highway, "secondary") || georoads::StartsWith(Highway, "tertiary") || Highway == "unclassified";
		}

		/** One road followed through its pieces: the same road (by name, or class when unnamed) carrying on straight */
		struct Chain
		{
			struct Piece
			{
				int32_t Edge = -1;
				bool bForward = true;	// travelled from the edge's first point to its last
				double Start = 0.0, End = 0.0;	// along the chain (cm)
			};
			std::vector<Piece> Pieces;

			/** Points along the chain, their height, distance along it, and the edge of the segment that starts there */
			std::vector<Vec2> Points;
			std::vector<float> Heights;
			std::vector<double> Along;
			std::vector<int32_t> SegmentEdge;

			double Length() const { return !Along.empty() ? Along.back() : 0.0; }

			/** The segment a distance along the chain falls in */
			int32_t SegmentAt(double S) const
			{
				const int32_t Index = static_cast<int32_t>(std::upper_bound(Along.begin(), Along.end(), S) - Along.begin()) - 1;
				return std::clamp(Index, 0, std::max(0, static_cast<int32_t>(Points.size()) - 2));
			}

			/** Position and left normal at a distance along the chain; the normal turns smoothly through corners */
			void Sample(double S, Vec2& OutPoint, Vec2& OutLeft, int32_t& OutEdge) const
			{
				const int32_t i = SegmentAt(S);
				const double Span = std::max(Along[i + 1] - Along[i], 1.0);
				const double T = std::clamp((S - Along[i]) / Span, 0.0, 1.0);
				OutPoint = Points[i] + (Points[i + 1] - Points[i]) * T;
				auto Normal = [&](int32_t k)
				{
					const Vec2 In = k > 0 ? (Points[k] - Points[k - 1]).Normal() : (Points[1] - Points[0]).Normal();
					const Vec2 Out = k + 1 < static_cast<int32_t>(Points.size()) ? (Points[k + 1] - Points[k]).Normal() : In;
					const Vec2 Dir = (In + Out).Normal();
					// north = +X, east = +Y: facing +X, the left is -Y
					return Vec2(Dir.Y, -Dir.X);
				};
				const Vec2 A = Normal(i), B = Normal(i + 1);
				OutLeft = (A + (B - A) * T).Normal();
				OutEdge = SegmentEdge[i];
			}
		};

		/** Joins the road pieces into chains */
		std::vector<Chain> BuildChains(const georoads::RoadGraph& Graph)
		{
			std::vector<uint8_t> Used(Graph.Edges.size(), 0);
			std::vector<Chain> Chains;

			auto Direction = [&](int32_t EdgeIndex, bool bLeavingStart)
			{
				const std::vector<Vec2>& P = Graph.Edges[EdgeIndex].Points;
				return bLeavingStart ? (P[1] - P[0]).Normal() : (P[P.size() - 2] - P.back()).Normal();
			};
			auto SameRoad = [](const RoadEdge& A, const RoadEdge& B)
			{
				return A.Name.empty() && B.Name.empty() ? A.Highway == B.Highway : A.Name == B.Name;
			};

			// from a node, the piece that carries on from the one arriving there along Heading (or none)
			auto Continue = [&](int32_t Node, int32_t FromEdge, const Vec2& Heading) -> std::pair<int32_t, bool>
			{
				const RoadNode& N = Graph.Nodes[Node];
				int32_t Best = -1;
				bool bBestForward = true;
				double BestDot = 0.0;
				for (const int32_t Candidate : N.Edges)
				{
					if (Candidate == FromEdge || Used[Candidate] || Graph.Edges[Candidate].Points.size() < 2)
					{
						continue;
					}
					const bool bForward = Graph.Edges[Candidate].StartNode == Node;
					const double Dot = georoads::Dot(Heading, Direction(Candidate, bForward));
					const bool bOk = N.Edges.size() == 2 ? Dot > 0.5 : (Dot > 0.82 && SameRoad(Graph.Edges[FromEdge], Graph.Edges[Candidate]));
					if (bOk && Dot > BestDot)
					{
						Best = Candidate;
						bBestForward = bForward;
						BestDot = Dot;
					}
				}
				return { Best, bBestForward };
			};

			for (int32_t Seed = 0; Seed < static_cast<int32_t>(Graph.Edges.size()); ++Seed)
			{
				if (Used[Seed] || Graph.Edges[Seed].Points.size() < 2)
				{
					continue;
				}
				Used[Seed] = 1;
				std::vector<std::pair<int32_t, bool>> Order = { { Seed, true } };

				// onwards from the seed's end, then backwards from its start
				for (const bool bOnwards : { true, false })
				{
					int32_t Edge = Seed;
					bool bForward = bOnwards;
					while (true)
					{
						const RoadEdge& E = Graph.Edges[Edge];
						const int32_t Node = bForward ? E.EndNode : E.StartNode;
						const Vec2 Heading = Direction(Edge, !bForward) * -1.0;
						const std::pair<int32_t, bool> Next = Continue(Node, Edge, Heading);
						if (Next.first == -1)
						{
							break;
						}
						Used[Next.first] = 1;
						Edge = Next.first;
						bForward = Next.second;
						if (bOnwards)
						{
							Order.emplace_back(Edge, bForward);
						}
						else
						{
							Order.insert(Order.begin(), { Edge, !bForward });
						}
					}
				}

				Chain& C = Chains.emplace_back();
				for (const std::pair<int32_t, bool>& Item : Order)
				{
					const RoadEdge& E = Graph.Edges[Item.first];
					Chain::Piece& P = C.Pieces.emplace_back();
					P.Edge = Item.first;
					P.bForward = Item.second;
					P.Start = C.Length();
					for (size_t k = 0; k < E.Points.size(); ++k)
					{
						const size_t i = Item.second ? k : E.Points.size() - 1 - k;
						if (!C.Points.empty() && k == 0)
						{
							C.SegmentEdge.back() = Item.first;
							continue;	// the joining point is already there
						}
						const double Step = !C.Points.empty() ? georoads::Distance(C.Points.back(), E.Points[i]) : 0.0;
						C.Along.push_back(C.Length() + Step);
						C.Points.push_back(E.Points[i]);
						C.Heights.push_back(i < E.Heights.size() ? E.Heights[i] : 0.0f);
						C.SegmentEdge.push_back(Item.first);
					}
					P.End = C.Length();
				}
			}
			return Chains;
		}

		/** How far ahead (cm) a driver at each point can see a car, going the chain's way. Blocked by the road itself over a
		 *  crest, or by a bend taking the sight line off the road and its shoulder. */
		std::vector<double> SightAhead(const Context& Ctx, const std::vector<Vec2>& P, const std::vector<float>& Z, const std::vector<double>& S,
			const std::vector<int32_t>& SegmentEdge, double MaxDistance)
		{
			const int32_t N = static_cast<int32_t>(P.size());
			std::vector<double> Sight(N, 0.0);
			for (int32_t k = 0; k < N; ++k)
			{
				const double EyeZ = Z[k] + EyeHeight;
				double Visible = 0.0;
				int32_t j = k + 1;
				for (; j < N && S[j] - S[k] <= MaxDistance; ++j)
				{
					const double ObjectZ = Z[j] + (Ctx.bUK ? EyeHeight : ObjectHeight);	// UK: object at 1.05 m too
					const Vec2 Chord = P[j] - P[k];
					const double ChordLength = Chord.Size();
					bool bBlocked = false;
					for (int32_t m = k + 1; m < j && !bBlocked; ++m)
					{
						const double T = ChordLength > 1.0 ? std::clamp(georoads::Dot(P[m] - P[k], Chord) / (ChordLength * ChordLength), 0.0, 1.0) : 0.0;
						const double LineHeight = EyeZ + T * (ObjectZ - EyeZ);
						const double Off = georoads::Distance(P[k] + Chord * T, P[m]);
						const float Margin = Ctx.Graph.Edges[SegmentEdge[std::min(m, static_cast<int32_t>(SegmentEdge.size()) - 1)]].HalfWidth + SightMargin;
						bBlocked = Z[m] > LineHeight || Off > Margin;
					}
					if (bBlocked)
					{
						break;
					}
					Visible = S[j] - S[k];
				}
				// the road runs out before anything blocks the view: as far as can be told, it's clear
				Sight[k] = (j >= N) ? MaxDistance : Visible;
			}
			return Sight;
		}

		/**
		 *  Ranges (cm along the chain) where the sight is short of the barrier sight distance, joined and made long enough.
		 *  Going the chain's way the segment from point i is driven from point i; going the other way, from point i + 1.
		 */
		std::vector<Range> NoOvertakingZones(const Context& Ctx, const Chain& C, const std::vector<double>& Sight, bool bForward, bool bWarning = false)
		{
			const int32_t N = static_cast<int32_t>(C.Points.size());
			// UK: zones are joined across shorter gaps, and at least 60 m long (TSM ch. 5, 3.9)
			const double Join = Ctx.bUK ? 5000.0 : JoinGap;
			std::vector<Range> Zones;
			for (int32_t i = 0; i + 1 < N; ++i)
			{
				const RoadEdge& Edge = Ctx.Graph.Edges[C.SegmentEdge[i]];
				if (Sight[bForward ? i : i + 1] >= Ctx.NeededSight(Edge, bWarning))
				{
					continue;
				}
				if (!Zones.empty() && C.Along[i] - Zones.back().Y <= Join)
				{
					Zones.back().Y = C.Along[i + 1];
				}
				else
				{
					Zones.push_back({ C.Along[i], C.Along[i + 1] });
				}
			}

			// at least 150 m long (60 m on steep mountain roads), stretched evenly both ways, then joined again
			std::vector<Range> Result;
			for (Range Zone : Zones)
			{
				const int32_t i0 = C.SegmentAt(Zone.X), i1 = C.SegmentAt(Zone.Y);
				const double Rise = std::abs(C.Heights[std::min(i1 + 1, N - 1)] - C.Heights[i0]);
				const bool bMountain = Rise / std::max(Zone.Y - Zone.X, 100.0) > 0.06;
				const double MinLength = Ctx.bUK || bMountain ? 6000.0 : 15000.0;
				if (Zone.Y - Zone.X < MinLength)
				{
					const double Grow = 0.5 * (MinLength - (Zone.Y - Zone.X));
					Zone.X = std::max(0.0, Zone.X - Grow);
					Zone.Y = std::min(C.Length(), Zone.Y + Grow);
				}
				if (!Result.empty() && Zone.X - Result.back().Y <= Join)
				{
					Result.back().Y = std::max(Result.back().Y, Zone.Y);
				}
				else
				{
					Result.push_back(Zone);
				}
			}
			return Result;
		}

		/** Sorted by start, overlapping ones merged */
		std::vector<Range> Merge(std::vector<Range> Zones)
		{
			std::stable_sort(Zones.begin(), Zones.end(), [](const Range& A, const Range& B) { return A.X < B.X; });
			std::vector<Range> Merged;
			for (const Range& Zone : Zones)
			{
				if (!Merged.empty() && Zone.X <= Merged.back().Y)
				{
					Merged.back().Y = std::max(Merged.back().Y, Zone.Y);
				}
				else
				{
					Merged.push_back(Zone);
				}
			}
			return Merged;
		}

		/** Paints lines along a chain */
		struct Painter
		{
			const georoads::RoadSurface& Surface;
			const Chain& Along;

			/** Where the road tapers, moves a line from its offset in the road's usual layout to where it is at S */
			std::function<double(double S, double Offset)> Remap;

			/** A line from S0 to S1 at Offset (cm, + = left looking along the chain), Width wide; solid, or Dash on / Gap
			 *  off counted from the chain's start so dashes line up across the road. Except: ranges left out. */
			void Line(MeshData& Out, double S0, double S1, double Offset, float Width, double Dash = 0.0, double Gap = 0.0,
				const std::vector<Range>* Except = nullptr) const
			{
				if (S1 - S0 < 10.0)
				{
					return;
				}
				const Range* Cuts = Except ? Except->data() : nullptr;
				const size_t NumCuts = Except ? Except->size() : 0;
				if (Dash <= 0.0)
				{
					Strip(Out, S0, S1, Offset, Width, Cuts, NumCuts);
					return;
				}
				const double Module = Dash + Gap;
				for (double Start = std::floor(S0 / Module) * Module; Start < S1; Start += Module)
				{
					Strip(Out, std::max(Start, S0), std::min(Start + Dash, S1), Offset, Width, Cuts, NumCuts);
				}
			}

			void Strip(MeshData& Out, double S0, double S1, double Offset, float Width, const Range* Cuts, size_t NumCuts) const
			{
				// cut out the excepted ranges
				for (size_t c = 0; c < NumCuts; ++c)
				{
					const Range& Cut = Cuts[c];
					if (Cut.X <= S0 && Cut.Y >= S1)
					{
						return;
					}
					if (Cut.X > S0 && Cut.X < S1)
					{
						Strip(Out, S0, Cut.X, Offset, Width, nullptr, 0);
						if (Cut.Y < S1)
						{
							Strip(Out, Cut.Y, S1, Offset, Width, Cuts, NumCuts);
						}
						return;
					}
					if (Cut.Y > S0 && Cut.Y < S1)
					{
						S0 = Cut.Y;
					}
				}
				if (S1 - S0 < 10.0)
				{
					return;
				}

				// follow the road: a point at every road point, and every PointStep in between
				std::vector<double> Stops = { S0 };
				for (int32_t i = Along.SegmentAt(S0) + 1; i < static_cast<int32_t>(Along.Points.size()) && Along.Along[i] < S1; ++i)
				{
					for (double S = Stops.back() + PointStep; S < Along.Along[i]; S += PointStep)
					{
						Stops.push_back(S);
					}
					Stops.push_back(Along.Along[i]);
				}
				for (double S = Stops.back() + PointStep; S < S1; S += PointStep)
				{
					Stops.push_back(S);
				}
				Stops.push_back(S1);

				const Vec3f Up(0.0f, 0.0f, 1.0f);
				Vec3f PrevLeft, PrevRight;
				for (size_t i = 0; i < Stops.size(); ++i)
				{
					Vec2 Point, Left;
					int32_t Edge;
					Along.Sample(Stops[i], Point, Left, Edge);
					const double At = Remap ? Remap(Stops[i], Offset) : Offset;
					const Vec2 L = Point + Left * (At + Width * 0.5);
					const Vec2 R = Point + Left * (At - Width * 0.5);
					const Vec3f A(static_cast<float>(L.X), static_cast<float>(L.Y), Surface.Height(L, Edge) + Lift);
					const Vec3f B(static_cast<float>(R.X), static_cast<float>(R.Y), Surface.Height(R, Edge) + Lift);
					if (i > 0)
					{
						Out.AddTriangle(PrevLeft, A, B, Up, Vec2f{ 0, 0 }, Vec2f{ 1, 0 }, Vec2f{ 1, 1 });
						Out.AddTriangle(PrevLeft, B, PrevRight, Up, Vec2f{ 0, 0 }, Vec2f{ 1, 1 }, Vec2f{ 0, 1 });
					}
					PrevLeft = A;
					PrevRight = B;
				}
			}
		};

		/** Adds one mesh's triangles to another */
		void AppendMesh(MeshData& To, const MeshData& From)
		{
			const int32_t Base = static_cast<int32_t>(To.Positions.size());
			To.Positions.insert(To.Positions.end(), From.Positions.begin(), From.Positions.end());
			To.Normals.insert(To.Normals.end(), From.Normals.begin(), From.Normals.end());
			To.UVs.insert(To.UVs.end(), From.UVs.begin(), From.UVs.end());
			To.Colors.insert(To.Colors.end(), From.Colors.begin(), From.Colors.end());
			To.Tags.insert(To.Tags.end(), From.Tags.begin(), From.Tags.end());
			for (const int32_t Index : From.Indices)
			{
				To.Indices.push_back(Base + Index);
			}
		}

		/** Length of a road piece (cm) */
		double EdgeLength(const RoadEdge& Edge)
		{
			double Length = 0.0;
			for (size_t i = 1; i < Edge.Points.size(); ++i)
			{
				Length += georoads::Distance(Edge.Points[i - 1], Edge.Points[i]);
			}
			return Length;
		}
	}

	void BuildMarkings(const RoadGraph& Graph, const georoads::RoadSurface& Surface, const std::vector<double>& StartTrim, const std::vector<double>& EndTrim,
		MeshData& White, MeshData& Yellow, const MeshOptions& Options)
	{
		using namespace MarkingsImpl;
		const auto StartTime = std::chrono::steady_clock::now();
		const georoads::ParallelForFn ParallelFor = Options.ParallelFor ? Options.ParallelFor : georoads::ParallelForFn(&georoads::ThreadsParallelFor);
		const Context Ctx{ Graph, Options.bUK };
		const bool bUK = Options.bUK;

		// --- where smaller roads meet bigger ones: a stop line, and a short no overtaking line before it ---
		struct Stop { int32_t Edge; bool bAtEnd; double At; };
		std::vector<Stop> Stops;
		std::vector<std::vector<Range>> ApproachLines(Graph.Edges.size());	// per edge: ranges along the edge (cm from its first point) of solid centre line
		for (int32_t NodeIndex = 0; NodeIndex < static_cast<int32_t>(Graph.Nodes.size()); ++NodeIndex)
		{
			const RoadNode& Node = Graph.Nodes[NodeIndex];
			if (Node.Edges.size() < 3)
			{
				continue;
			}
			int32_t Top = 0;
			for (const int32_t EdgeIndex : Node.Edges)
			{
				Top = std::max(Top, Ctx.Design(Graph.Edges[EdgeIndex]).Rank);
			}
			for (const int32_t EdgeIndex : Node.Edges)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const bool bAtEnd = Edge.EndNode == NodeIndex;
				// smaller than the biggest road here, surfaced, and bringing traffic into the junction
				if (Ctx.Design(Edge).Rank >= Top || Edge.Kind == georoads::Surface::Loose || Edge.bBridge || (Edge.bOneway && !bAtEnd) || Edge.HalfWidth < 150.0f)
				{
					continue;
				}
				// just short of the junction surface; the dividing line before it turns solid for the last 12 m (rural)
				// or 9 m (town)
				const double Length = EdgeLength(Edge);
				const double Back = (bAtEnd ? EndTrim[EdgeIndex] : StartTrim[EdgeIndex]) + 150.0;
				if (Back + 100.0 > Length)
				{
					continue;
				}
				const double At = bAtEnd ? Length - Back : Back;
				Stops.push_back({ EdgeIndex, bAtEnd, At });
				if (!Edge.bOneway && IsMarkedClass(Edge.Highway) && Edge.HalfWidth * 2.0f >= 550.0f)
				{
					// SA: the dividing line turns solid for the last 12 m (rural) or 9 m; UK: a warning line of at least 5
					// marks (6 m module) or 7 (9 m module) before the give way line
					const double Run = bUK ? (IsFastUK(Edge) ? 6300.0 : 3000.0) : Ctx.IsRural(Edge) ? 1200.0 : 900.0;
					ApproachLines[EdgeIndex].push_back({ bAtEnd ? std::max(0.0, At - Run) : At, bAtEnd ? At : std::min(Length, At + Run) });
				}
			}
		}

		// --- lines along the roads: each chain into its own meshes, joined in chain order (the same every time) ---
		const std::vector<Chain> Chains = BuildChains(Graph);
		struct ChainLines
		{
			MeshData White, Yellow;
			int32_t Zones = 0;
		};
		std::vector<ChainLines> Lines(Chains.size());
		ParallelFor(static_cast<int32_t>(Chains.size()), [&](int32_t ChainIndex)
		{
			const Chain& C = Chains[ChainIndex];
			if (C.Points.size() < 2)
			{
				return;
			}
			bool bAnyMarked = false;
			for (const Chain::Piece& Piece : C.Pieces)
			{
				bAnyMarked |= IsMarkedClass(Graph.Edges[Piece.Edge].Highway) && Graph.Edges[Piece.Edge].Kind != georoads::Surface::Loose;
			}

			MeshData& LocalWhite = Lines[ChainIndex].White;
			MeshData& LocalYellow = Lines[ChainIndex].Yellow;
			Painter Paint{ Surface, C, nullptr };

			// no overtaking zones for each direction (only needed where there's a dividing line)
			std::vector<Range> ZonesForward, ZonesBackward;
			std::vector<Range> WarningZones;	// UK: where the centre line becomes a warning line (sight short of W either way)
			if (bAnyMarked)
			{
				double MaxSight = 0.0;
				for (const Chain::Piece& Piece : C.Pieces)
				{
					MaxSight = std::max(MaxSight, Ctx.NeededSight(Graph.Edges[Piece.Edge], true));
				}
				const std::vector<double> Forward = SightAhead(Ctx, C.Points, C.Heights, C.Along, C.SegmentEdge, MaxSight);

				// the other way: the same chain read backwards
				std::vector<Vec2> P;
				std::vector<float> Z;
				std::vector<double> S;
				std::vector<int32_t> E;
				const int32_t N = static_cast<int32_t>(C.Points.size());
				for (int32_t i = N - 1; i >= 0; --i)
				{
					P.push_back(C.Points[i]);
					Z.push_back(C.Heights[i]);
					S.push_back(C.Length() - C.Along[i]);
					E.push_back(C.SegmentEdge[std::max(i - 1, 0)]);
				}
				const std::vector<double> Reversed = SightAhead(Ctx, P, Z, S, E, MaxSight);
				std::vector<double> Backward(N);
				for (int32_t i = 0; i < N; ++i)
				{
					Backward[i] = Reversed[N - 1 - i];
				}
				ZonesForward = NoOvertakingZones(Ctx, C, Forward, true);
				ZonesBackward = NoOvertakingZones(Ctx, C, Backward, false);
				if (bUK)
				{
					WarningZones = NoOvertakingZones(Ctx, C, Forward, true, true);
					const std::vector<Range> Back = NoOvertakingZones(Ctx, C, Backward, false, true);
					WarningZones.insert(WarningZones.end(), Back.begin(), Back.end());
					WarningZones = Merge(WarningZones);
				}
			}

			for (const Chain::Piece& Piece : C.Pieces)
			{
				const RoadEdge& Edge = Graph.Edges[Piece.Edge];
				if (Edge.Kind == georoads::Surface::Loose || Edge.Highway == "raceway")
				{
					continue;	// (a race track: no road's lines down it)
				}
				// the stretch outside the junction surfaces at its ends, along the chain; where the road simply carries on
				// into the next piece (two roads at the node) the lines run straight across
				const int32_t FirstNode = Piece.bForward ? Edge.StartNode : Edge.EndNode, LastNode = Piece.bForward ? Edge.EndNode : Edge.StartNode;
				const bool bCarriesOnFirst = Graph.Nodes[FirstNode].Edges.size() == 2, bCarriesOnLast = Graph.Nodes[LastNode].Edges.size() == 2;
				const double TrimFirst = bCarriesOnFirst ? 0.0 : (Piece.bForward ? StartTrim[Piece.Edge] : EndTrim[Piece.Edge]);
				const double TrimLast = bCarriesOnLast ? 0.0 : (Piece.bForward ? EndTrim[Piece.Edge] : StartTrim[Piece.Edge]);
				const double S0 = Piece.Start + TrimFirst, S1 = Piece.End - TrimLast;
				if (S1 - S0 < 100.0)
				{
					continue;
				}

				const bool bRural = Ctx.IsRural(Edge);
				const float Width = Edge.HalfWidth * 2.0f;
				// shoulders on the chain's left and right
				const float LeftShoulder = Piece.bForward ? Edge.ShoulderLeft : Edge.ShoulderRight;
				const float RightShoulder = Piece.bForward ? Edge.ShoulderRight : Edge.ShoulderLeft;
				const double LeftEdge = Edge.HalfWidth - LeftShoulder, RightEdge = -(Edge.HalfWidth - RightShoulder);
				const float Travelled = Width - LeftShoulder - RightShoulder;

				// near a taper each line keeps its share of the way across between the edge lines
				const double PieceLength = Piece.End - Piece.Start;
				const bool bForward = Piece.bForward;
				const double PieceStart = Piece.Start;
				Paint.Remap = [&Edge, bForward, PieceStart, PieceLength, LeftEdge, RightEdge](double S, double Offset)
				{
					const double Along = bForward ? S - PieceStart : PieceLength - (S - PieceStart);
					const double Half = Edge.HalfWidthAt(Along, PieceLength);
					const double LeftNow = Half - (bForward ? Edge.ShoulderLeftAt(Along, PieceLength) : Edge.ShoulderRightAt(Along, PieceLength));
					const double RightNow = -(Half - (bForward ? Edge.ShoulderRightAt(Along, PieceLength) : Edge.ShoulderLeftAt(Along, PieceLength)));
					const double Across = LeftEdge - RightEdge;
					const double T = Across > 1.0 ? (Offset - RightEdge) / Across : 0.5;
					return RightNow + T * (LeftNow - RightNow);
				};

				// the stretch where the shoulder on the chain's left or right is wide enough for a yellow edge line, as a
				// range along the chain; X > Y when there's none
				auto ShoulderSpan = [&](bool bChainLeft)
				{
					Range Span{ 1.0, 0.0 };
					for (double S = S0; S < S1 + 200.0; S += 200.0)
					{
						const double At = std::min(S, S1);	// always ending exactly at S1
						const double Along = bForward ? At - PieceStart : PieceLength - (At - PieceStart);
						const bool bEdgeLeft = bChainLeft == bForward;
						const float Shoulder = bEdgeLeft ? Edge.ShoulderLeftAt(Along, PieceLength) : Edge.ShoulderRightAt(Along, PieceLength);
						if (Shoulder >= SurfacedShoulder)
						{
							Span.X = Span.X > Span.Y ? At : Span.X;
							Span.Y = At;
						}
					}
					return Span;
				};

				// the stretches before give way / stop lines on this piece, along the chain
				std::vector<Range> Approaches;
				for (const Range& R : ApproachLines[Piece.Edge])
				{
					Approaches.push_back({ Piece.bForward ? Piece.Start + R.X : Piece.End - R.Y, Piece.bForward ? Piece.Start + R.Y : Piece.End - R.X });
				}

				if (bUK)
				{
					// UK (TSM ch. 5, docs/ROAD_RULES_UK.md): every line white; the 6 m module at 40 mph or less, 9 m above
					const bool bFast = IsFastUK(Edge);
					const bool bStreet = Edge.Highway == "residential" || Edge.Highway == "living_street" || Edge.Highway == "service";
					const bool bHardStrip = std::max(LeftShoulder, RightShoulder) >= 50.0f;
					const bool bMotorway = georoads::StartsWith(Edge.Highway, "motorway");
					// edge lines (1012.1): motorways, roads with hard strips, and fast (unlit, unkerbed) roads
					const bool bEdgeLines = bMotorway || bHardStrip || (bFast && !bStreet);
					const float EdgeWidth = bMotorway ? 20.0f : (Edge.SpeedKmh > 105.0f || bHardStrip) ? 15.0f : 10.0f;
					const float LaneWidthUK = Edge.SpeedKmh > 105.0f ? 15.0f : 10.0f;
					const double LaneDash = bFast ? 200.0 : 100.0, LaneGap = bFast ? 700.0 : 500.0;	// 1005.1 / 1005
					if (!Edge.bOneway)
					{
						if (IsMarkedClass(Edge.Highway) && Width >= 550.0f)
						{
							// centre line 1008.1 (3 m mark, 6 m gap) or 1008 (2/4); where the view ahead is short of W, and before a
							// give way line, the hazard warning line 1004.1 (6/3) or 1004 (4/2) instead: the same module, flipped
							std::vector<Range> Warn = WarningZones;
							Warn.insert(Warn.end(), Approaches.begin(), Approaches.end());
							const std::vector<Range> Merged = Merge(Warn);
							const float CentreWidth = Width >= 1000.0f || Edge.Lanes >= 3 ? 15.0f : 10.0f;
							Paint.Line(LocalWhite, S0, S1, 0.0, CentreWidth, bFast ? 300.0 : 200.0, bFast ? 600.0 : 400.0, &Merged);
							for (const Range& Zone : Merged)
							{
								Paint.Line(LocalWhite, std::max(S0, Zone.X), std::min(S1, Zone.Y), 0.0, CentreWidth, bFast ? 600.0 : 400.0, bFast ? 300.0 : 200.0);
							}
							// double white lines (1013.1): a solid line on the side of whoever may not overtake
							for (const Range& Zone : ZonesForward)
							{
								Paint.Line(LocalWhite, std::max(S0, Zone.X), std::min(S1, Zone.Y), PairOffset, LineWidth);
							}
							for (const Range& Zone : ZonesBackward)
							{
								Paint.Line(LocalWhite, std::max(S0, Zone.X), std::min(S1, Zone.Y), -PairOffset, LineWidth);
							}
						}
						if (bEdgeLines)
						{
							Paint.Line(LocalWhite, S0, S1, LeftEdge - EdgeWidth, EdgeWidth);
							Paint.Line(LocalWhite, S0, S1, RightEdge + EdgeWidth, EdgeWidth);
						}
						const int32_t PerSide = Edge.Lanes / 2;
						const double Lane = Travelled / std::max(Edge.Lanes, 1);
						for (int32_t l = 1; l < PerSide; ++l)
						{
							Paint.Line(LocalWhite, S0, S1, l * Lane, LaneWidthUK, LaneDash, LaneGap);
							Paint.Line(LocalWhite, S0, S1, -l * Lane, LaneWidthUK, LaneDash, LaneGap);
						}
					}
					else
					{
						const double DriverLeft = Piece.bForward ? LeftEdge : RightEdge;
						const double DriverRight = Piece.bForward ? RightEdge : LeftEdge;
						const double Inward = Piece.bForward ? 1.0 : -1.0;
						const bool bDivided = bMotorway || georoads::StartsWith(Edge.Highway, "trunk") || georoads::StartsWith(Edge.Highway, "primary");
						if (bEdgeLines)
						{
							Paint.Line(LocalWhite, S0, S1, DriverLeft - Inward * EdgeWidth, EdgeWidth);
						}
						if (bDivided && !georoads::EndsWith(Edge.Highway, "_link"))
						{
							Paint.Line(LocalWhite, S0, S1, DriverRight + Inward * LineWidth, bMotorway ? 20.0f : LineWidth);
						}
						const double Lane = Travelled / std::max(Edge.Lanes, 1);
						for (int32_t l = 1; l < Edge.Lanes; ++l)
						{
							Paint.Line(LocalWhite, S0, S1, DriverLeft - Inward * l * Lane, LaneWidthUK, LaneDash, LaneGap);
						}
					}
					continue;
				}

				if (!Edge.bOneway)
				{
					// two-way: dividing line where wide enough on through roads, no overtaking lines beside it; yellow edge
					// lines where there are shoulders; lane lines between lanes going the same way
					if (IsMarkedClass(Edge.Highway) && Width >= 550.0f)
					{
						const bool bMultiLane = Edge.Lanes >= 4;
						const double Dash = bMultiLane ? (bRural ? 600.0 : 450.0) : (bRural ? 400.0 : 300.0);
						const double Gap = bMultiLane ? Dash : (bRural ? 800.0 : 600.0);
						Paint.Line(LocalWhite, S0, S1, 0.0, MainLineWidth, Dash, Gap, &Approaches);
						// (each direction's no-overtaking line on its own side: mirrored where traffic keeps right)
						const double Side = Options.bDriveLeft ? 1.0 : -1.0;
						for (const Range& Zone : ZonesForward)
						{
							Paint.Line(LocalWhite, std::max(S0, Zone.X), std::min(S1, Zone.Y), PairOffset * Side, LineWidth);
						}
						for (const Range& Zone : ZonesBackward)
						{
							Paint.Line(LocalWhite, std::max(S0, Zone.X), std::min(S1, Zone.Y), -PairOffset * Side, LineWidth);
						}
					}
					const Range LeftSpan = ShoulderSpan(true), RightSpan = ShoulderSpan(false);
					if (LeftSpan.X < LeftSpan.Y)
					{
						Paint.Line(LocalYellow, LeftSpan.X, LeftSpan.Y, LeftEdge - LineWidth, LineWidth);
					}
					if (RightSpan.X < RightSpan.Y)
					{
						Paint.Line(LocalYellow, RightSpan.X, RightSpan.Y, RightEdge + LineWidth, LineWidth);
					}
					const int32_t PerSide = Edge.Lanes / 2;
					if (PerSide >= 2)
					{
						const double Lane = Travelled / Edge.Lanes;
						for (int32_t l = 1; l < PerSide; ++l)
						{
							Paint.Line(LocalWhite, S0, S1, l * Lane, LineWidth, bRural ? 200.0 : 150.0, bRural ? 400.0 : 300.0);
							Paint.Line(LocalWhite, S0, S1, -l * Lane, LineWidth, bRural ? 200.0 : 150.0, bRural ? 400.0 : 300.0);
						}
					}
				}
				else
				{
					// one-way: yellow edge on the outside (the driver's left keeping left), white edge by the median of a divided
					// road, lane lines (keeping right: all mirrored)
					const bool bOutsideLeft = Piece.bForward == Options.bDriveLeft;
					const double DriverLeft = bOutsideLeft ? LeftEdge : RightEdge;
					const double DriverRight = bOutsideLeft ? RightEdge : LeftEdge;
					const double Inward = bOutsideLeft ? 1.0 : -1.0;	// from the outside towards the median, along the chain's left
					const bool bDivided = georoads::StartsWith(Edge.Highway, "motorway") || georoads::StartsWith(Edge.Highway, "trunk") || georoads::StartsWith(Edge.Highway, "primary");
					const Range DriverLeftSpan = Edge.Highway == "motorway" ? Range{ S0, S1 } : ShoulderSpan(bOutsideLeft);
					if (DriverLeftSpan.X < DriverLeftSpan.Y)
					{
						Paint.Line(LocalYellow, DriverLeftSpan.X, DriverLeftSpan.Y, DriverLeft - Inward * LineWidth, LineWidth);
					}
					if (bDivided && !georoads::EndsWith(Edge.Highway, "_link"))
					{
						Paint.Line(LocalWhite, S0, S1, DriverRight + Inward * LineWidth, LineWidth);
					}
					if (Edge.Lanes >= 2)
					{
						const double Lane = Travelled / Edge.Lanes;
						for (int32_t l = 1; l < Edge.Lanes; ++l)
						{
							Paint.Line(LocalWhite, S0, S1, DriverLeft - Inward * l * Lane, LineWidth, 200.0, 400.0);
						}
					}
				}
			}
			Lines[ChainIndex].Zones = static_cast<int32_t>(ZonesForward.size() + ZonesBackward.size());
		});
		int32_t NumZones = 0;
		for (const ChainLines& Chain : Lines)
		{
			AppendMesh(White, Chain.White);
			AppendMesh(Options.bWhiteEdges ? White : Yellow, Chain.Yellow);	// (most of the world: its edge lines white)
			NumZones += Chain.Zones;
		}

		// --- stop lines across the approach lanes, and the solid line before them ---
		for (const Stop& S : Stops)
		{
			const RoadEdge& Edge = Graph.Edges[S.Edge];
			const std::vector<Vec2>& P = Edge.Points;
			const bool bRural = Ctx.IsRural(Edge);
			const double At = S.At;

			// a one-edge chain to paint with
			Chain Single;
			for (size_t i = 0; i < P.size(); ++i)
			{
				Single.Along.push_back(i == 0 ? 0.0 : Single.Along.back() + georoads::Distance(P[i - 1], P[i]));
				Single.Points.push_back(P[i]);
				Single.Heights.push_back(i < Edge.Heights.size() ? Edge.Heights[i] : 0.0f);
				Single.SegmentEdge.push_back(S.Edge);
			}
			const Painter Paint{ Surface, Single, nullptr };
			const float Thickness = bRural ? 50.0f : 30.0f;

			// across the lanes coming in: the left half of a two-way road (seen arriving), the whole of a one-way one
			const double Left = Edge.HalfWidth - Edge.ShoulderLeft, Right = -(Edge.HalfWidth - Edge.ShoulderRight);
			double From = 0.0, To = 0.0;
			if (Edge.bOneway)
			{
				From = Right;
				To = Left;
			}
			else if (S.bAtEnd)
			{
				From = 0.0;
				To = Left;
			}
			else
			{
				From = Right;
				To = 0.0;
			}
			// the stop line runs across the road: painted as side-by-side strips along it, together as wide as the lanes
			// coming in and as thick as the line
			if (bUK)
			{
				// UK give way line (1003A): two broken lines across, each 200 mm, 300 mm apart, 600 mm marks with 300 mm gaps
				const double Back = S.bAtEnd ? -50.0 : 50.0;	// the second line, further from the junction
				for (const double Row : { At, At + Back })
				{
					for (double Offset = From + 10.0; Offset < To - 5.0; Offset += 20.0)
					{
						if (std::fmod(Offset - From, 90.0) < 60.0)
						{
							Paint.Line(White, Row - 10.0, Row + 10.0, Offset, 20.0f);
						}
					}
				}
				continue;
			}
			for (double Offset = From + 10.0; Offset < To - 5.0; Offset += 20.0)
			{
				Paint.Line(White, At - Thickness * 0.5, At + Thickness * 0.5, Offset, 20.0f);
			}

			// the solid line before it, instead of the broken dividing line
			for (const Range& R : ApproachLines[S.Edge])
			{
				Paint.Line(White, R.X, R.Y, 0.0, MainLineWidth);
			}
		}

		if (Options.Log)
		{
			Options.Log(georoads::Format("[Markings] %d roads followed, %d no overtaking zones, %d stop lines; %d white and %d yellow line triangles, %.2f s",
				int32_t(Chains.size()), NumZones, int32_t(Stops.size()), int32_t(White.Indices.size() / 3), int32_t(Yellow.Indices.size() / 3),
				std::chrono::duration<double>(std::chrono::steady_clock::now() - StartTime).count()));
		}
	}
}
