// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace geomesh
{
	using georoads::Vec2;
	using georoads::RoadEdge;
	using georoads::RoadNode;
	using geoground::Vec2f;
	using geoground::Vec3f;

	namespace RoadMeshImpl
	{
		/** Road surface offset above its profile (cm); the ground is carved below the profile */
		constexpr float RoadOffset = 3.0f;

		/** How far road edges hang down, so no gap shows where the road meets the ground (cm) */
		constexpr float SkirtDepth = 150.0f;

		/** Off for now: the user preferred roundabouts drawn as rings. Kept for fixing specific junctions later. */
		constexpr bool bPaveComplexJunctions = false;
		/** Junction nodes joined by road pieces shorter than this (cm) are paved as one area, up to MaxJunctionAreaSize across */
		constexpr double JunctionLinkLength = 2500.0;
		constexpr double MaxJunctionAreaSize = 8000.0;
		constexpr float MaxJunctionAreaRise = 150.0f;

		/** Junction areas sit this far (cm) above the road pieces underneath, so those never show through */
		constexpr float JunctionAreaLift = 2.0f;

		/** Junction trims never exceed this multiple of the road's half width */
		constexpr float MaxTrimFactor = 4.0f;

		/** Miter scale limit for road corners */
		constexpr float MaxMiter = 2.0f;

		/** Gaps between roads running side by side narrower than this are paved over (cm): mapped as separate lines,
		 *  they're really one tarred area. Wider gaps are real medians or islands. */
		constexpr double MaxGap = 300.0;

		/** Between the two halves of a dual carriageway the median is paved up to this wide (cm), for now */
		constexpr double MaxMedianGap = 600.0;

		/** The gap is measured this often along a road's edge (cm), between roads at most this far apart in height (cm)
		 *  and at most ~30 degrees apart in direction */
		constexpr double GapStep = 200.0;
		constexpr float MaxGapRise = 50.0f;
		constexpr double MinGapAlignment = 0.866;

		/** Gap paving sits this far under the road surfaces, so where it overlaps a road or junction it stays hidden (cm) */
		constexpr float GapDrop = 1.0f;

		/** Spacing (cm) of the points across a road and through a junction surface */
		constexpr float SurfaceStep = 200.0f;

		int32_t LateralSegments(float HalfWidth)
		{
			return std::clamp(static_cast<int32_t>(std::ceil(2.0f * HalfWidth / SurfaceStep)), 2, 8);
		}

		/** Joins two rows of points (both running the same way) with triangles, whatever their counts */
		template <typename PointType, typename AddFunction>
		void Stitch(const std::vector<PointType>& Inner, const std::vector<PointType>& Outer, AddFunction Add)
		{
			const int32_t A = static_cast<int32_t>(Inner.size()) - 1, B = static_cast<int32_t>(Outer.size()) - 1;
			int32_t i = 0, j = 0;
			while (i < A || j < B)
			{
				// advance along whichever row is behind
				const bool bAdvanceOuter = i >= A || (j < B && static_cast<double>(j + 1) / B <= static_cast<double>(i + 1) / std::max(A, 1));
				if (bAdvanceOuter)
				{
					Add(Inner[i], Outer[j], Outer[j + 1]);
					++j;
				}
				else
				{
					Add(Inner[i], Outer[j], Inner[i + 1]);
					++i;
				}
			}
		}

		Vec2 Perp(const Vec2& V) { return Vec2(-V.Y, V.X); }
		double Cross(const Vec2& A, const Vec2& B) { return A.X * B.Y - A.Y * B.X; }
		Vec3f At(const Vec2& P, float Z) { return Vec3f(static_cast<float>(P.X), static_cast<float>(P.Y), Z); }
		float Dist2D(const Vec3f& A, const Vec3f& B) { return std::sqrt((B.X - A.X) * (B.X - A.X) + (B.Y - A.Y) * (B.Y - A.Y)); }
		Vec2f TriangleUV(const Vec3f& P) { return Vec2f{ P.X / 400.0f, P.Y / 400.0f }; }
		double Seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

		/** A polyline with a height at each point */
		struct Profile
		{
			std::vector<Vec2> Points;
			std::vector<float> Heights;
		};

		double PolylineLength(const std::vector<Vec2>& Points)
		{
			double Length = 0.0;
			for (size_t i = 0; i + 1 < Points.size(); ++i)
			{
				Length += georoads::Distance(Points[i], Points[i + 1]);
			}
			return Length;
		}

		/** Point, height and direction at a distance along an edge (clamped) */
		Vec2 PointAlong(const RoadEdge& Edge, double Distance, float& OutHeight, Vec2& OutDirection)
		{
			const std::vector<Vec2>& Points = Edge.Points;
			for (size_t i = 0; i + 1 < Points.size(); ++i)
			{
				const Vec2 Segment = Points[i + 1] - Points[i];
				const double Length = Segment.Size();
				if (Distance <= Length || i + 2 == Points.size())
				{
					const double T = Length > 0.0 ? std::clamp(Distance / Length, 0.0, 1.0) : 0.0;
					OutDirection = Segment.Normal();
					OutHeight = i + 1 < Edge.Heights.size() ? geoground::LerpF(Edge.Heights[i], Edge.Heights[i + 1], static_cast<float>(T)) : 0.0f;
					return Points[i] + Segment * T;
				}
				Distance -= Length;
			}
			OutDirection = Vec2(1.0, 0.0);
			OutHeight = !Edge.Heights.empty() ? Edge.Heights[0] : 0.0f;
			return !Points.empty() ? Points[0] : Vec2();
		}

		/** The part of an edge between two distances from its start, with heights */
		Profile SubProfile(const RoadEdge& Edge, double From, double To)
		{
			Profile Result;
			Vec2 Direction;
			float Height = 0.0f;
			Result.Points.push_back(PointAlong(Edge, From, Height, Direction));
			Result.Heights.push_back(Height);

			double Travelled = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				Travelled += georoads::Distance(Edge.Points[i], Edge.Points[i + 1]);
				if (Travelled > From && Travelled < To)
				{
					Result.Points.push_back(Edge.Points[i + 1]);
					Result.Heights.push_back(i + 1 < Edge.Heights.size() ? Edge.Heights[i + 1] : 0.0f);
				}
			}

			Result.Points.push_back(PointAlong(Edge, To, Height, Direction));
			Result.Heights.push_back(Height);
			return Result;
		}

		/** Adds points every DenseStep within DenseLength of a junction end, so the road can follow the shared surface
		 *  as it curves into the junction (elsewhere 10 m between points is plenty) */
		Profile Densify(const Profile& In, bool bStartJunction, bool bEndJunction)
		{
			constexpr double DenseStep = 250.0, DenseLength = 1500.0;
			const double Total = PolylineLength(In.Points);
			Profile Result;
			Result.Points.push_back(In.Points[0]);
			Result.Heights.push_back(In.Heights[0]);
			double Travelled = 0.0;
			for (size_t i = 0; i + 1 < In.Points.size(); ++i)
			{
				const double Length = georoads::Distance(In.Points[i], In.Points[i + 1]);
				const bool bNearEnd = (bStartJunction && Travelled < DenseLength) || (bEndJunction && Total - Travelled - Length < DenseLength);
				const int32_t Steps = bNearEnd ? std::max(1, static_cast<int32_t>(std::ceil(Length / DenseStep))) : 1;
				for (int32_t Step = 1; Step <= Steps; ++Step)
				{
					const float T = static_cast<float>(Step) / Steps;
					Result.Points.push_back(In.Points[i] + (In.Points[i + 1] - In.Points[i]) * static_cast<double>(T));
					Result.Heights.push_back(geoground::LerpF(In.Heights[i], In.Heights[i + 1], T));
				}
				Travelled += Length;
			}
			return Result;
		}

		/** A vertical strip hanging down from an edge, facing Outward */
		void AddSkirt(MeshData& Mesh, const Vec3f& A, const Vec3f& B, const Vec2& Outward)
		{
			const Vec3f Normal(static_cast<float>(Outward.X), static_cast<float>(Outward.Y), 0.0f);
			const Vec3f Down(0.0f, 0.0f, SkirtDepth);
			Mesh.AddTriangle(A, B, B - Down, Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 0 }, Vec2f{ 1, 1 });
			Mesh.AddTriangle(A, B - Down, A - Down, Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 1 }, Vec2f{ 0, 1 });
		}

		/** Adds one mesh's triangles (and their tags) to the end of another */
		void Append(MeshData& To, const MeshData& From)
		{
			const int32_t Base = static_cast<int32_t>(To.Positions.size());
			To.Positions.insert(To.Positions.end(), From.Positions.begin(), From.Positions.end());
			To.Normals.insert(To.Normals.end(), From.Normals.begin(), From.Normals.end());
			To.UVs.insert(To.UVs.end(), From.UVs.begin(), From.UVs.end());
			To.Colors.insert(To.Colors.end(), From.Colors.begin(), From.Colors.end());
			To.Tags.insert(To.Tags.end(), From.Tags.begin(), From.Tags.end());
			// (room made geometrically: reserving just enough each time copied every index so far on every call, 512 batches
			// joined one after another: ~1 s each for the roads and the gaps between them at Cape Town)
			if (To.Indices.capacity() < To.Indices.size() + From.Indices.size())
			{
				To.Indices.reserve(std::max(To.Indices.size() + From.Indices.size(), To.Indices.capacity() * 2));
			}
			for (const int32_t Index : From.Indices)
			{
				To.Indices.push_back(Base + Index);
			}
		}

		/** How a road leaves a junction */
		struct Incidence
		{
			int32_t Edge = -1;
			bool bAtStart = true;
			Vec2 Direction;
			float HalfWidth = 0.0f;
			double Angle = 0.0;
			double Trim = 0.0;
			float TrimHeight = 0.0f;

			/** Where the road's surface actually starts, and its direction there (pointing away from the junction) */
			Vec2 TrimPoint;
			Vec2 TrimDirection;
		};

		uint64_t CellKey(int32_t X, int32_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); }

		/** Paves narrow gaps (under 3 m) between roads running side by side at the same level */
		void FillGaps(const RoadGraph& Graph, const georoads::RoadSurface& Surface, MeshData& Out, const MeshOptions& Options, const georoads::ParallelForFn& ParallelFor)
		{
			// road segments in a grid, to find the roads alongside each one
			constexpr double Cell = 2000.0;
			struct SegmentRef { int32_t Edge; int32_t Index; };
			std::unordered_map<uint64_t, std::vector<SegmentRef>> Grid;
			const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (Edge.bBridge)
				{
					continue;
				}
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					geoground::Box2 Box;
					Box += Edge.Points[i];
					Box += Edge.Points[i + 1];
					Box = Box.ExpandBy(Edge.HalfWidth + MaxMedianGap);
					for (int32_t X = geoground::FloorToInt(Box.Min.X / Cell); X <= geoground::FloorToInt(Box.Max.X / Cell); ++X)
					{
						for (int32_t Y = geoground::FloorToInt(Box.Min.Y / Cell); Y <= geoground::FloorToInt(Box.Max.Y / Cell); ++Y)
						{
							Grid[CellKey(X, Y)].push_back({ EdgeIndex, static_cast<int32_t>(i) });
						}
					}
				}
			}

			// walk both edges of every road; where another road runs alongside a little way off, pave across to its edge.
			// Each pair of roads is paved from the one with the lower index only, so a gap is never paved twice.
			const Vec3f Up(0.0f, 0.0f, 1.0f);
			const float Lift = RoadOffset - GapDrop;
			// in batches of roads, each into its own mesh, joined in road order (the same result as one at a time)
			struct GapBatch
			{
				MeshData Mesh;
				int32_t Pieces = 0;
				double Length = 0.0;
			};
			const int32_t NumBatches = std::clamp(NumEdges / 128, 1, 512);
			std::vector<GapBatch> Batches(NumBatches);
			ParallelFor(NumBatches, [&](int32_t Batch)
			{
				MeshData& Mesh = Batches[Batch].Mesh;
				int32_t& Pieces = Batches[Batch].Pieces;
				double& Length = Batches[Batch].Length;
				const int32_t FirstEdge = static_cast<int32_t>(static_cast<int64_t>(NumEdges) * Batch / NumBatches);
				const int32_t EndEdge = static_cast<int32_t>(static_cast<int64_t>(NumEdges) * (Batch + 1) / NumBatches);
				for (int32_t EdgeIndex = FirstEdge; EdgeIndex < EndEdge; ++EdgeIndex)
				{
					const RoadEdge& Edge = Graph.Edges[EdgeIndex];
					if (Edge.bBridge)
					{
						continue;
					}
					Mesh.Tag = EdgeIndex;
					for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
					{
						const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
						const double SegmentLength = georoads::Distance(A, B);
						if (SegmentLength < 1.0)
						{
							continue;
						}
						const Vec2 Along = (B - A) * (1.0 / SegmentLength);	// (by the reciprocal, as the engines divide vectors)
						const Vec2 Left(-Along.Y, Along.X);
						const int32_t Steps = std::max(1, static_cast<int32_t>(std::ceil(SegmentLength / GapStep)));
						for (const double Side : { -1.0, 1.0 })
						{
							// the edge point here and the matching edge point of the road alongside (if any)
							bool bHad = false;
							Vec3f PrevOwn, PrevOther;
							for (int32_t Step = 0; Step <= Steps; ++Step)
							{
								const Vec2 Centre = A + (B - A) * (static_cast<double>(Step) / Steps);
								const Vec2 Own = Centre + Left * (Side * Edge.HalfWidth);
								const float OwnHeight = Surface.Height(Own, EdgeIndex);

								bool bFound = false;
								double BestGap = MaxMedianGap;
								Vec2 Other;
								int32_t OtherEdge = -1;
								const auto Nearby = Grid.find(CellKey(geoground::FloorToInt(Own.X / Cell), geoground::FloorToInt(Own.Y / Cell)));
								if (Nearby != Grid.end())
								{
									for (const SegmentRef& Ref : Nearby->second)
									{
										if (Ref.Edge <= EdgeIndex)
										{
											continue;
										}
										const RoadEdge& Neighbour = Graph.Edges[Ref.Edge];
										const Vec2 C = Neighbour.Points[Ref.Index], D = Neighbour.Points[Ref.Index + 1];
										const Vec2 CD = D - C;
										const double LengthSquared = CD.SizeSquared();
										if (LengthSquared < 1.0 || std::abs(georoads::Dot(CD.Normal(), Along)) < MinGapAlignment)
										{
											continue;
										}
										// strictly alongside (not past its ends), on this side of the road
										const double T = georoads::Dot(Own - C, CD) / LengthSquared;
										const Vec2 OnLine = C + CD * std::clamp(T, 0.0, 1.0);
										if (T <= 0.0 || T >= 1.0 || georoads::Dot(OnLine - Centre, Left * Side) <= Edge.HalfWidth)
										{
											continue;
										}
										const double Gap = georoads::Distance(OnLine, Own) - Neighbour.HalfWidth;
										const bool bMedian = Edge.bOneway && Neighbour.bOneway && georoads::Dot(CD.Normal(), Along) < -MinGapAlignment;
										if (Gap <= 5.0 || Gap >= (bMedian ? MaxMedianGap : MaxGap) || Gap >= BestGap)
										{
											continue;
										}
										const Vec2 Facing = OnLine + (Own - OnLine).Normal() * Neighbour.HalfWidth;
										if (std::abs(Surface.Height(Facing, Ref.Edge) - OwnHeight) > MaxGapRise)
										{
											continue;
										}
										bFound = true;
										BestGap = Gap;
										Other = Facing;
										OtherEdge = Ref.Edge;
									}
								}

								if (!bFound)
								{
									bHad = false;
									continue;
								}
								const Vec3f OwnPoint(static_cast<float>(Own.X), static_cast<float>(Own.Y), OwnHeight + Lift);
								const Vec3f OtherPoint(static_cast<float>(Other.X), static_cast<float>(Other.Y), Surface.Height(Other, OtherEdge) + Lift);
								if (bHad)
								{
									Mesh.AddTriangle(PrevOwn, OwnPoint, OtherPoint, Up, TriangleUV(PrevOwn), TriangleUV(OwnPoint), TriangleUV(OtherPoint));
									Mesh.AddTriangle(PrevOwn, OtherPoint, PrevOther, Up, TriangleUV(PrevOwn), TriangleUV(OtherPoint), TriangleUV(PrevOther));
									++Pieces;
									Length += GapStep;
								}
								PrevOwn = OwnPoint;
								PrevOther = OtherPoint;
								bHad = true;
							}
						}
					}
				}
			});
			int32_t Pieces = 0;
			double Length = 0.0;
			for (const GapBatch& Batch : Batches)
			{
				Append(Out, Batch.Mesh);
				Pieces += Batch.Pieces;
				Length += Batch.Length;
			}
			if (Options.Log)
			{
				Options.Log(georoads::Format("[Gaps] paved %.1f km of gaps under %.0f m between roads side by side (%d pieces)", Length / 100000.0, MaxGap / 100.0, Pieces));
			}
		}
	}

	void ApplySurface(const georoads::RoadSurface& Surface, MeshData& Mesh, float Offset, const georoads::ParallelForFn& ParallelFor)
	{
		ParallelFor(static_cast<int32_t>(Mesh.Indices.size() / 3), [&](int32_t Triangle)
		{
			const int32_t Owner = size_t(Triangle) < Mesh.Tags.size() ? Mesh.Tags[Triangle] : -1;
			for (int32_t Corner = 0; Corner < 3; ++Corner)
			{
				Vec3f& P = Mesh.Positions[Mesh.Indices[size_t(Triangle) * 3 + Corner]];
				P.Z = Surface.Height(Vec2(P.X, P.Y), Owner) + Offset;
			}
		});
	}

	void ApplySurfaceToSkirts(const georoads::RoadSurface& Surface, MeshData& Skirts, float Offset, float Depth, const georoads::ParallelForFn& ParallelFor)
	{
		// each skirt quad is two triangles added as (top A, top B, bottom B) and (top A, bottom B, bottom A)
		static constexpr bool bTop[6] = { true, true, false, true, false, false };
		ParallelFor(static_cast<int32_t>(Skirts.Positions.size() / 6), [&](int32_t Quad)
		{
			const int32_t Owner = size_t(Quad) * 2 < Skirts.Tags.size() ? Skirts.Tags[size_t(Quad) * 2] : -1;
			for (int32_t i = 0; i < 6; ++i)
			{
				Vec3f& P = Skirts.Positions[size_t(Quad) * 6 + i];
				P.Z = Surface.Height(Vec2(P.X, P.Y), Owner) + Offset - (bTop[i] ? 0.0f : Depth);
			}
		});
	}

	void BuildRoads(const RoadGraph& Graph, RoadMeshes& Out, const MeshOptions& Options)
	{
		using namespace RoadMeshImpl;
		const georoads::ParallelForFn ParallelFor = Options.ParallelFor ? Options.ParallelFor : georoads::ParallelForFn(&georoads::ThreadsParallelFor);
		const int32_t NumNodes = static_cast<int32_t>(Graph.Nodes.size());
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());

		const Vec3f Up(0.0f, 0.0f, 1.0f);
		std::string Steps;
		double LapTime = Seconds();
		auto Lap = [&Steps, &LapTime](const char* Step)
		{
			const double Now = Seconds();
			Steps += georoads::Format("%s%s %.2fs", Steps.empty() ? "" : ", ", Step, Now - LapTime);
			LapTime = Now;
		};
		const georoads::RoadSurface SharedSurface(Graph);
		Lap("surface");
		std::vector<double> StartTrim(NumEdges, 0.0), EndTrim(NumEdges, 0.0);

		// --- complex junctions: junction nodes joined by short road pieces are one paved area in reality, so they're
		// grouped and paved as a single surface (off for now) ---
		std::vector<int32_t> Group(NumNodes);
		for (int32_t i = 0; i < NumNodes; ++i)
		{
			Group[i] = i;
		}
		auto Find = [&Group](int32_t Node)
		{
			while (Group[Node] != Node)
			{
				Group[Node] = Group[Group[Node]];
				Node = Group[Node];
			}
			return Node;
		};
		std::vector<uint8_t> InternalEdge(NumEdges, 0);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			const RoadNode& A = Graph.Nodes[Edge.StartNode];
			const RoadNode& B = Graph.Nodes[Edge.EndNode];
			if (!bPaveComplexJunctions || Edge.bBridge || A.Layer != B.Layer || PolylineLength(Edge.Points) > JunctionLinkLength
				|| (A.Edges.size() < 3 && B.Edges.size() < 3) || std::abs(A.Height - B.Height) > MaxJunctionAreaRise)
			{
				continue;
			}
			Group[Find(Edge.StartNode)] = Find(Edge.EndNode);
			InternalEdge[EdgeIndex] = 1;
		}

		struct JunctionFan
		{
			std::vector<Vec3f> Corners;
			std::vector<uint8_t> OpenAfter;
			std::vector<int32_t> SegmentsAfter;	// pieces along the polygon edge after each corner (a road mouth matches its road)
			Vec2 Position;
			int32_t Surface = 0;
			int32_t NumRoads = 0;
			int32_t Node = -1;
		};
		std::unordered_map<int32_t, std::vector<JunctionFan>> Groups;
		std::vector<int32_t> GroupOrder;	// in the order first met

		Lap("junction groups");
		// --- junctions: trim each road back to where it meets its neighbours, then fill the gap ---
		std::vector<Incidence> Roads;
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const RoadNode& Node = Graph.Nodes[NodeIndex];
			if (Node.Edges.size() < 2)
			{
				continue;
			}

			Roads.clear();
			for (const int32_t EdgeIndex : Node.Edges)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				for (const bool bAtStart : { true, false })
				{
					if ((bAtStart ? Edge.StartNode : Edge.EndNode) != NodeIndex || Edge.Points.size() < 2)
					{
						continue;
					}
					Incidence& Road = Roads.emplace_back();
					Road.Edge = EdgeIndex;
					Road.bAtStart = bAtStart;
					Road.Direction = bAtStart ? (Edge.Points[1] - Edge.Points[0]).Normal() : (Edge.Points[Edge.Points.size() - 2] - Edge.Points.back()).Normal();
					Road.HalfWidth = Edge.HalfWidth + (bAtStart ? Edge.StartTaper.HalfWidth : Edge.EndTaper.HalfWidth);	// its width at this end
					Road.Angle = std::atan2(Road.Direction.Y, Road.Direction.X);
				}
			}
			if (Roads.size() < 2)
			{
				continue;
			}

			std::sort(Roads.begin(), Roads.end(), [](const Incidence& A, const Incidence& B) { return A.Angle < B.Angle; });

			// each neighbouring pair: intersect the facing edges of the two roads
			for (size_t i = 0; i < Roads.size(); ++i)
			{
				Incidence& A = Roads[i];
				Incidence& B = Roads[(i + 1) % Roads.size()];

				double Gap = B.Angle - A.Angle;
				if (Gap <= 0.0)
				{
					Gap += 2.0 * geoground::Pi;
				}
				if (Gap > 170.0 * (geoground::Pi / 180.0))
				{
					continue;	// roads carry on more or less straight: no trim needed on this side
				}

				const Vec2 PA = Node.Position + Perp(A.Direction) * A.HalfWidth;
				const Vec2 PB = Node.Position - Perp(B.Direction) * B.HalfWidth;
				const double Denominator = Cross(A.Direction, B.Direction);
				if (std::abs(Denominator) < 1.0e-3)
				{
					continue;
				}
				const Vec2 Delta = PB - PA;
				A.Trim = std::max(A.Trim, Cross(Delta, B.Direction) / Denominator);
				B.Trim = std::max(B.Trim, Cross(Delta, A.Direction) / Denominator);
			}

			// clamp trims (near-parallel roads produce far-away intersections) and record them on the edges
			for (Incidence& Road : Roads)
			{
				const RoadEdge& Edge = Graph.Edges[Road.Edge];
				const double Length = PolylineLength(Edge.Points);
				Road.Trim = std::clamp(Road.Trim, 0.0, std::min(static_cast<double>(Road.HalfWidth * MaxTrimFactor), Length * 0.45));
				(Road.bAtStart ? StartTrim : EndTrim)[Road.Edge] = Road.Trim;

				Vec2 Direction;
				Road.TrimPoint = PointAlong(Edge, Road.bAtStart ? Road.Trim : Length - Road.Trim, Road.TrimHeight, Direction);
				Road.TrimDirection = Road.bAtStart ? Direction : Direction * -1.0;
				Road.HalfWidth = Edge.HalfWidthAt(Road.bAtStart ? Road.Trim : Length - Road.Trim, Length);	// as wide as its ribbon starts
			}

			// junction polygon: the trimmed corners of every road, in angle order, fanned from the node
			JunctionFan Fan;
			float WidestHalfWidth = 0.0f;
			for (const Incidence& Road : Roads)
			{
				// the corners sit exactly where the road's own surface starts
				Fan.Corners.push_back(At(Road.TrimPoint - Perp(Road.TrimDirection) * Road.HalfWidth, Road.TrimHeight + RoadOffset));
				Fan.OpenAfter.push_back(0);	// across the road mouth
				Fan.SegmentsAfter.push_back(LateralSegments(Road.HalfWidth));
				Fan.Corners.push_back(At(Road.TrimPoint + Perp(Road.TrimDirection) * Road.HalfWidth, Road.TrimHeight + RoadOffset));
				Fan.OpenAfter.push_back(1);	// kerb between this road and the next
				Fan.SegmentsAfter.push_back(0);	// by length
				if (Road.HalfWidth > WidestHalfWidth)
				{
					WidestHalfWidth = Road.HalfWidth;
					Fan.Surface = static_cast<int32_t>(Graph.Edges[Road.Edge].Kind);
				}
			}
			Fan.Position = Node.Position;
			Fan.Node = NodeIndex;
			Fan.NumRoads = static_cast<int32_t>(Roads.size());
			const int32_t Key = Find(NodeIndex);
			auto Found = Groups.find(Key);
			if (Found == Groups.end())
			{
				GroupOrder.push_back(Key);
				Found = Groups.emplace(Key, std::vector<JunctionFan>()).first;
			}
			Found->second.push_back(std::move(Fan));
		}

		// a single junction: fan from a centre levelled to its roads (a centre at a different height is a jolt)
		auto DrawFan = [&](const JunctionFan& Fan)
		{
			MeshData& Mesh = Out.Roads[Fan.Surface];
			Mesh.Tag = -(Fan.Node + 1);
			float CenterHeight = 0.0f;
			for (const Vec3f& Corner : Fan.Corners)
			{
				CenterHeight += Corner.Z;
			}
			const Vec3f Center = At(Fan.Position, CenterHeight / std::max(static_cast<int32_t>(Fan.Corners.size()), 1));
			Out.Skirts.Tag = Mesh.Tag;

			// the fan is cut into rings around the centre (about 2 m apart), and each ring into pieces about 2 m long, so it
			// follows the shared road surface's curve without creases; the outer edge of a road mouth has exactly the
			// road's own points across it
			float Radius = 0.0f;
			for (const Vec3f& Corner : Fan.Corners)
			{
				Radius = std::max(Radius, Dist2D(Center, Corner));
			}
			const int32_t Rings = std::clamp(static_cast<int32_t>(std::ceil(Radius / SurfaceStep)), 1, 15);
			auto AddPiece = [&](const Vec3f& P, const Vec3f& Q, const Vec3f& R)
			{
				Mesh.AddTriangle(P, Q, R, Up, TriangleUV(P), TriangleUV(Q), TriangleUV(R));
			};
			std::vector<Vec3f> Inner, Row;
			for (size_t i = 0; i < Fan.Corners.size(); ++i)
			{
				const Vec3f& A = Fan.Corners[i];
				const Vec3f& B = Fan.Corners[(i + 1) % Fan.Corners.size()];
				const float EdgeLength = Dist2D(A, B);
				const int32_t OuterSegments = Fan.SegmentsAfter[i] > 0 ? Fan.SegmentsAfter[i] : std::max(1, static_cast<int32_t>(std::ceil(EdgeLength / SurfaceStep)));

				Inner.assign(1, Center);
				for (int32_t Ring = 1; Ring <= Rings; ++Ring)
				{
					const float T = static_cast<float>(Ring) / Rings;
					const Vec3f P = geoground::Lerp(Center, A, T), Q = geoground::Lerp(Center, B, T);
					const int32_t Segments = Ring == Rings ? OuterSegments : std::max(1, static_cast<int32_t>(std::ceil(EdgeLength * T / SurfaceStep)));
					Row.clear();
					for (int32_t k = 0; k <= Segments; ++k)
					{
						Row.push_back(geoground::Lerp(P, Q, static_cast<float>(k) / Segments));
					}
					Stitch(Inner, Row, AddPiece);
					std::swap(Inner, Row);
				}

				if (Fan.OpenAfter[i] && Fan.NumRoads > 2)
				{
					const Vec2 Edge2D(B.X - A.X, B.Y - A.Y);
					AddSkirt(Out.Skirts, A, B, Vec2(Edge2D.Y, -Edge2D.X).Normal());
				}
			}
			++Out.NumJunctions;
		};

		std::unordered_set<int32_t> PavedGroups;
		for (const int32_t Key : GroupOrder)
		{
			const std::vector<JunctionFan>& Fans = Groups[Key];
			std::vector<Vec3f> Points;
			for (const JunctionFan& Fan : Fans)
			{
				Points.insert(Points.end(), Fan.Corners.begin(), Fan.Corners.end());
			}
			geoground::Box2 Extent;
			for (const Vec3f& P : Points)
			{
				Extent += Vec2(P.X, P.Y);
			}

			if (Fans.size() < 2 || Extent.Size().Size() > MaxJunctionAreaSize)
			{
				for (const JunctionFan& Fan : Fans)
				{
					DrawFan(Fan);
				}
				continue;
			}

			// one paved area: the convex hull of every road mouth in the group, on a plane fitted through their heights
			Vec2 Mean;
			float MeanZ = 0.0f;
			for (const Vec3f& P : Points)
			{
				Mean = Mean + Vec2(P.X, P.Y);
				MeanZ += P.Z;
			}
			Mean = Mean * (1.0 / static_cast<double>(Points.size()));
			MeanZ /= static_cast<float>(Points.size());
			double Sxx = 0, Sxy = 0, Syy = 0, Sxz = 0, Syz = 0;
			for (const Vec3f& P : Points)
			{
				const double X = P.X - Mean.X, Y = P.Y - Mean.Y, Z = P.Z - MeanZ;
				Sxx += X * X; Sxy += X * Y; Syy += Y * Y; Sxz += X * Z; Syz += Y * Z;
			}
			const double Determinant = Sxx * Syy - Sxy * Sxy;
			const double SlopeX = std::abs(Determinant) > 1.0 ? (Sxz * Syy - Syz * Sxy) / Determinant : 0.0;
			const double SlopeY = std::abs(Determinant) > 1.0 ? (Syz * Sxx - Sxz * Sxy) / Determinant : 0.0;
			auto PlaneZ = [&](const Vec2& P) { return static_cast<float>(MeanZ + SlopeX * (P.X - Mean.X) + SlopeY * (P.Y - Mean.Y)) + JunctionAreaLift; };

			// convex hull (monotone chain)
			std::vector<Vec2> Sorted;
			for (const Vec3f& P : Points)
			{
				Sorted.emplace_back(P.X, P.Y);
			}
			std::sort(Sorted.begin(), Sorted.end(), [](const Vec2& A, const Vec2& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
			std::vector<Vec2> Hull;
			for (int32_t Pass = 0; Pass < 2; ++Pass)
			{
				const size_t Start = Hull.size();
				for (size_t i = 0; i < Sorted.size(); ++i)
				{
					const Vec2& P = Pass == 0 ? Sorted[i] : Sorted[Sorted.size() - 1 - i];
					while (Hull.size() >= Start + 2 && Cross(Hull.back() - Hull[Hull.size() - 2], P - Hull[Hull.size() - 2]) <= 0.0)
					{
						Hull.pop_back();
					}
					Hull.push_back(P);
				}
				Hull.pop_back();
			}
			if (Hull.size() < 3)
			{
				for (const JunctionFan& Fan : Fans)
				{
					DrawFan(Fan);
				}
				continue;
			}

			MeshData& Mesh = Out.Roads[Fans[0].Surface];
			Mesh.Tag = -(Fans[0].Node + 1);
			Out.Skirts.Tag = Mesh.Tag;
			const Vec3f Center = At(Mean, PlaneZ(Mean));
			for (size_t i = 0; i < Hull.size(); ++i)
			{
				const Vec3f A = At(Hull[i], PlaneZ(Hull[i]));
				const Vec3f B = At(Hull[(i + 1) % Hull.size()], PlaneZ(Hull[(i + 1) % Hull.size()]));
				Mesh.AddTriangle(Center, A, B, Up, TriangleUV(Center), TriangleUV(A), TriangleUV(B));
				const Vec2 Edge2D(B.X - A.X, B.Y - A.Y);
				AddSkirt(Out.Skirts, A, B, Vec2(Edge2D.Y, -Edge2D.X).Normal());
			}
			++Out.NumJunctions;
			++Out.NumJunctionAreas;
			PavedGroups.insert(Key);
		}

		Lap("junctions");
		// --- road ribbons between the trimmed ends: in batches of roads, each into its own meshes, then joined in road
		// order (the same result as one at a time) ---
		std::vector<int32_t> GroupOf(NumNodes);
		for (int32_t i = 0; i < NumNodes; ++i)
		{
			GroupOf[i] = Find(i);
		}
		struct RibbonBatch
		{
			MeshData Roads[NumSurfaces];
			MeshData Skirts;
		};
		const int32_t NumBatches = std::clamp(NumEdges / 128, 1, 512);
		std::vector<RibbonBatch> Batches(NumBatches);
		ParallelFor(NumBatches, [&](int32_t Batch)
		{
			RibbonBatch& Local = Batches[Batch];
			const int32_t FirstEdge = static_cast<int32_t>(static_cast<int64_t>(NumEdges) * Batch / NumBatches);
			const int32_t EndEdge = static_cast<int32_t>(static_cast<int64_t>(NumEdges) * (Batch + 1) / NumBatches);
			std::vector<Vec3f> Left, Right;
			std::vector<Vec2> Normals;
			std::vector<uint8_t> Bent;
			struct RowPoint { Vec3f Position; Vec2f UV; };
			std::vector<std::vector<RowPoint>> Rows;
			for (int32_t EdgeIndex = FirstEdge; EdgeIndex < EndEdge; ++EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const double Length = PolylineLength(Edge.Points);
				const double From = StartTrim[EdgeIndex];
				const double To = Length - EndTrim[EdgeIndex];
				if (To - From < 10.0 || (InternalEdge[EdgeIndex] && PavedGroups.count(GroupOf[Edge.StartNode])))
				{
					continue;	// fully covered by its junctions
				}

				const Profile Shape = Densify(SubProfile(Edge, From, To), Graph.Nodes[Edge.StartNode].Edges.size() >= 3, Graph.Nodes[Edge.EndNode].Edges.size() >= 3);
				const std::vector<Vec2>& Points = Shape.Points;
				const int32_t N = static_cast<int32_t>(Points.size());
				MeshData& Mesh = Local.Roads[static_cast<int32_t>(Edge.Kind)];
				Mesh.Tag = EdgeIndex;
				Local.Skirts.Tag = EdgeIndex;

				Left.clear();
				Right.clear();
				Normals.clear();
				double Along = From;	// distance along the road, for its width where it tapers
				for (int32_t i = 0; i < N; ++i)
				{
					if (i > 0)
					{
						Along += georoads::Distance(Points[i - 1], Points[i]);
					}
					const float HalfWidth = Edge.HalfWidthAt(Along, Length);
					// miter: average the neighbouring segment directions, scaled so the road keeps its width through corners
					const Vec2 In = i > 0 ? (Points[i] - Points[i - 1]).Normal() : (Points[1] - Points[0]).Normal();
					const Vec2 OutDir = i + 1 < N ? (Points[i + 1] - Points[i]).Normal() : In;
					Vec2 Normal = Perp((In + OutDir).Normal());
					if (std::abs(Normal.X) <= 1.0e-4 && std::abs(Normal.Y) <= 1.0e-4)
					{
						Normal = Perp(In);
					}
					const double Miter = std::min(1.0 / std::max(georoads::Dot(Normal, Perp(In)), 0.01), static_cast<double>(MaxMiter));
					const float Z = Shape.Heights[i] + RoadOffset;
					Left.push_back(At(Points[i] - Normal * static_cast<double>(HalfWidth) * Miter, Z));
					Right.push_back(At(Points[i] + Normal * static_cast<double>(HalfWidth) * Miter, Z));
					Normals.push_back(Normal);
				}

				// on a tight bend the inside edge would run backwards and fold the road over itself: an edge point that
				// would go backwards stays where the previous one is instead (the last one never moves)
				for (std::vector<Vec3f>* Side : { &Left, &Right })
				{
					for (int32_t i = 1; i + 1 < N; ++i)
					{
						const Vec2 Forward = (Points[i] - Points[i - 1]).Normal();
						const Vec3f Step = (*Side)[i] - (*Side)[i - 1];
						if (Step.X * Forward.X + Step.Y * Forward.Y < 0.0)
						{
							(*Side)[i] = (*Side)[i - 1];
						}
					}
				}

				// where the shared surface bends across the road the road is cut across into strips so it can follow; where
				// it's level across, one strip is exact. The end row at a junction matches the junction's mouth point for point.
				const int32_t Across = LateralSegments(Edge.HalfWidth);
				const bool bStartJoined = Graph.Nodes[Edge.StartNode].Edges.size() >= 2, bEndJoined = Graph.Nodes[Edge.EndNode].Edges.size() >= 2;
				Bent.assign(N, 0);
				for (int32_t i = 0; i < N; ++i)
				{
					const Vec2 L(Left[i].X, Left[i].Y), R(Right[i].X, Right[i].Y);
					const float HL = SharedSurface.Height(L, EdgeIndex), HR = SharedSurface.Height(R, EdgeIndex);
					for (const double T : { 0.25, 0.5, 0.75 })
					{
						if (std::abs(SharedSurface.Height(L + (R - L) * T, EdgeIndex) - geoground::LerpF(HL, HR, static_cast<float>(T))) > 1.0f)
						{
							for (int32_t j = std::max(0, i - 2); j <= std::min(N - 1, i + 2); ++j)
							{
								Bent[j] = 1;
							}
							break;
						}
					}
				}

				Rows.resize(N);
				double V = 0.0;
				const double TextureLength = Edge.HalfWidth * 2.0;
				for (int32_t i = 0; i < N; ++i)
				{
					if (i > 0)
					{
						V += georoads::Distance(Points[i - 1], Points[i]) / TextureLength;
					}
					const bool bStrips = Bent[i] || (i == 0 && bStartJoined) || (i == N - 1 && bEndJoined);
					const int32_t Count = bStrips ? Across : 1;
					std::vector<RowPoint>& Row = Rows[i];
					Row.clear();
					for (int32_t k = 0; k <= Count; ++k)
					{
						const float U = static_cast<float>(k) / Count;
						Row.push_back({ geoground::Lerp(Left[i], Right[i], U), Vec2f{ U, static_cast<float>(V) } });
					}
				}
				auto AddPiece = [&](const RowPoint& A, const RowPoint& B, const RowPoint& C)
				{
					Mesh.AddTriangle(A.Position, B.Position, C.Position, Up, A.UV, B.UV, C.UV);
				};

				for (int32_t i = 0; i + 1 < N; ++i)
				{
					Stitch(Rows[i + 1], Rows[i], AddPiece);

					// skirts on both edges hide the step down to the carved ground; on a bridge they're the sides of its deck
					AddSkirt(Local.Skirts, Left[i], Left[i + 1], Normals[i] * -1.0);
					AddSkirt(Local.Skirts, Right[i], Right[i + 1], Normals[i]);
				}
			}
		});
		for (RibbonBatch& Local : Batches)
		{
			for (int32_t Surface = 0; Surface < NumSurfaces; ++Surface)
			{
				Append(Out.Roads[Surface], Local.Roads[Surface]);
			}
			Append(Out.Skirts, Local.Skirts);
		}

		Lap("ribbons");
		// every road vertex takes its height from one shared surface, so touching and overlapping pieces always agree
		for (MeshData& Mesh : Out.Roads)
		{
			ApplySurface(SharedSurface, Mesh, RoadOffset, ParallelFor);
		}
		ApplySurfaceToSkirts(SharedSurface, Out.Skirts, RoadOffset, SkirtDepth, ParallelFor);
		Lap("heights from surface");

		FillGaps(Graph, SharedSurface, Out.Roads[0], Options, ParallelFor);
		Lap("gap filler");

		// dead ends finish in a half circle of road (like a turning circle) instead of stopping at an open edge
		int32_t Caps = 0;
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const RoadNode& Node = Graph.Nodes[NodeIndex];
			if (Node.Edges.size() != 1)
			{
				continue;
			}
			const int32_t EdgeIndex = Node.Edges[0];
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (Edge.Points.size() < 2)
			{
				continue;
			}
			const bool bStart = Edge.StartNode == NodeIndex;
			const Vec2 End = bStart ? Edge.Points[0] : Edge.Points.back();
			const Vec2 Outward = (End - (bStart ? Edge.Points[1] : Edge.Points[Edge.Points.size() - 2])).Normal();
			const Vec2 Left(-Outward.Y, Outward.X);
			MeshData& Mesh = Out.Roads[static_cast<int32_t>(Edge.Kind)];
			Mesh.Tag = EdgeIndex;

			auto OnSurface = [&](const Vec2& P) { return Vec3f(static_cast<float>(P.X), static_cast<float>(P.Y), SharedSurface.Height(P, EdgeIndex) + RoadOffset); };
			const Vec3f Centre = OnSurface(End);
			constexpr int32_t Slices = 8;
			Vec3f Previous = OnSurface(End + Left * Edge.HalfWidth);
			for (int32_t Slice = 1; Slice <= Slices; ++Slice)
			{
				const double Angle = geoground::Pi * 0.5 - geoground::Pi * Slice / Slices;
				const Vec2 Direction = Outward * std::cos(Angle) + Left * std::sin(Angle);
				const Vec3f Next = OnSurface(End + Direction * Edge.HalfWidth);
				Mesh.AddTriangle(Centre, Previous, Next, Up, TriangleUV(Centre), TriangleUV(Previous), TriangleUV(Next));
				const Vec2 Middle = (Vec2(Previous.X, Previous.Y) + Vec2(Next.X, Next.Y)) * 0.5 - End;
				AddSkirt(Out.Skirts, Previous, Next, Middle.Normal());
				Previous = Next;
			}
			++Caps;
		}
		if (Options.Log)
		{
			Options.Log(georoads::Format("[Roads] %d dead ends rounded off", Caps));
		}

		// the lines painted on the roads
		Lap("dead ends");
		BuildMarkings(Graph, SharedSurface, StartTrim, EndTrim, Out.Markings[0], Out.Markings[1], Options);
		Lap("markings");
		if (Options.Log)
		{
			Options.Log("[Roads] build steps: " + Steps);
		}
	}
}
