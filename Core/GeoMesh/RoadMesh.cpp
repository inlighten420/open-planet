// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadMesh.h"
#include "GeoRoads/RoadStacks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

		/** Over another road a deck is only as thick as the room allows: a truck (cm) and a little air must fit under its
		 *  skirts, down to a thin slab at the least */
		constexpr float TruckHeight = 450.0f, TruckAir = 10.0f, ThinSkirt = 30.0f;
		/** A thin deck follows the road under it within this reach (cm) */
		constexpr float ThinReach = 3000.0f;

		/** Off for now: the user preferred roundabouts drawn as rings. Kept for fixing specific junctions later. */
		constexpr bool bPaveComplexJunctions = true;
		/** A group of junctions is paved as one only where it is a cluster (crossings and links side by side, the carriageways of a
		 *  dual road joined every few metres): where the hull of its junctions is more than this much of the roads in it, it
		 *  rings an island (a roundabout) and keeps its ring and its fans */
		constexpr double MaxIslandRatio = 0.7;
		/** And only a cluster of this many junctions or more: two or three along a road are better as their own fans (paved as one
		 *  their hull spills past the road); it takes a tangle of fans over each other to need one surface */
		constexpr size_t MinClusterJunctions = 4;
		/** One flat surface may be no further than this from the heights of the roads it replaces (cm): more is a step at their mouths */
		constexpr float MaxClusterSag = 60.0f;
		/** Junction nodes joined by road pieces shorter than this (cm) are paved as one area, up to MaxJunctionAreaSize across */
		constexpr double JunctionLinkLength = 2500.0;
		/** ...but only where their fans overlap: a link shorter than this many times the widths of the roads at its two ends (the
		 *  half-widths added). Further apart, each junction keeps its own fan, which is right for two junctions along a road */
		constexpr double LinkReach = 2.0;
		constexpr double MaxJunctionAreaSize = 8000.0;
		constexpr float MaxJunctionAreaRise = 150.0f;

		/** Junction areas sit this far (cm) above the road pieces underneath, so those never show through */
		constexpr float JunctionAreaLift = 2.0f;

		/** Junction trims never exceed this multiple of the road's half width */
		constexpr float MaxTrimFactor = 4.0f;

		/** Two roads that leave a junction side by side are a split (an off-ramp, a slip road leaving its main road, a road that
		 *  forks), not a corner to be cut back and rounded: they run on and open apart. Whether a pair is a corner is read from
		 *  the roads themselves: a corner is formed where their facing edges meet, and that has to be near the junction. Where it
		 *  is further out than this many of the wider road's half-widths, the roads run together too far for a corner and one
		 *  would be bent into the other. (A square corner meets at one half-width; 45 degrees at about 2.4, 30 at 3.7.) */
		constexpr double CornerFits = 3.0;
		/** A link road (an off-ramp, a slip road) is a split at a wider angle than that: it is its own road that leaves the main
		 *  one, not a street that joins it (rad) */
		constexpr double LinkSplitGap = 60.0 * (3.14159265358979 / 180.0);
		/** A split is two roads leaving in one direction: no more than this far apart (rad). Roads further apart than that carry on
		 *  through the junction or meet it across (a wide road narrowing into two carriageways): they are cut back and flared
		 *  into each other as a corner, which draws the taper */
		constexpr double SplitMaxGap = 75.0 * (3.14159265358979 / 180.0);

		/** A branch that leaves its road side by side runs a stretch inside the road's own lanes, two surfaces on the same ground a
		 *  little apart in height, one coming through the other. Its ribbon keeps out of the lanes where the other surface is within
		 *  this much of its own height (cm: further apart they are two levels), and grows out of the road's edge as they part */
		constexpr float ForkSameLevel = 150.0f;

		/** Miter scale limit for road corners */
		constexpr float MaxMiter = 2.0f;

		/** A junction's corners are rounded (kerb returns): each road's mouth is set back this much further than where the edges meet
		 *  (cm, at most), by the tangent length of the arc. Radii by class: Red Book 6-10 m, SUMO 4 m (docs/ROAD_RESEARCH.md step 6) */
		constexpr float FilletRoom = 1500.0f;
		constexpr double MinFilletGap = 25.0 * (3.14159265358979 / 180.0);
		/** How far (cm) a rounded corner may pave beyond the point where the two road edges meet. The ground builds a flat verge of
		 *  1.5 m beside each road and slopes away after it: past that the corner would sit on sloping ground (ragged edges, a wedge
		 *  hanging in the air). So tight corners get a smaller radius than the class asks for. */
		constexpr double MaxFilletReach = 200.0;	// (roads closer than this don't get one: the tangent is too long)

		/** The kerb radius (cm) a road of this class asks for at a junction; 0: none (motorways keep their sharp gores) */
		double KerbRadius(const std::string& Highway)
		{
			if (georoads::StartsWith(Highway, "motorway"))
			{
				return 0.0;
			}
			if (Highway == "trunk" || Highway == "primary" || Highway == "trunk_link" || Highway == "primary_link")
			{
				return 1200.0;
			}
			if (Highway == "secondary" || Highway == "secondary_link")
			{
				return 1000.0;
			}
			if (Highway == "tertiary" || Highway == "tertiary_link")
			{
				return 800.0;
			}
			if (Highway == "unclassified" || Highway == "residential" || Highway == "living_street" || Highway == "road")
			{
				return 600.0;
			}
			return 300.0;	// service roads, tracks, the rest
		}

		/** One corner between two neighbouring roads at a junction */
		struct KerbReturn
		{
			bool bArc = false;
			double MitreA = 0.0, MitreB = 0.0;	// where the facing edges meet, along each road (cm from the node)
			double Tangent = 0.0;				// how much further out the arc touches each edge (cm)
			double Gap = 0.0;					// the angle between the two roads (radians)
		};

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
				if (Edge.bBridge || Edge.bTunnel)	// (not tunnels either: side by side under a hill they are often two levels)
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
					if (Edge.bBridge || Edge.bTunnel)	// (not tunnels either: side by side under a hill they are often two levels)
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
										if (Neighbour.Layer != Edge.Layer)
										{
											continue;	// (another level: a gap between them is a drop, not a gap to pave)
										}
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

	void ApplySurfaceToSkirts(const georoads::RoadSurface& Surface, MeshData& Skirts, float Offset, float Depth, const std::vector<std::vector<std::pair<Vec2, float>>>& Rooms, const georoads::ParallelForFn& ParallelFor)
	{
		// each skirt quad is two triangles added as (top A, top B, bottom B) and (top A, bottom B, bottom A)
		static constexpr bool bTop[6] = { true, true, false, true, false, false };
		ParallelFor(static_cast<int32_t>(Skirts.Positions.size() / 6), [&](int32_t Quad)
		{
			const int32_t Owner = size_t(Quad) * 2 < Skirts.Tags.size() ? Skirts.Tags[size_t(Quad) * 2] : -1;
			for (int32_t i = 0; i < 6; ++i)
			{
				Vec3f& P = Skirts.Positions[size_t(Quad) * 6 + i];
				float Hang = Depth;
				if (!bTop[i] && Owner >= 0 && Owner < static_cast<int32_t>(Rooms.size()))
				{
					// (over another road: no thicker than leaves a truck's height under it)
					float Nearest = RoadMeshImpl::ThinReach * RoadMeshImpl::ThinReach;
					for (const std::pair<Vec2, float>& Room : Rooms[Owner])
					{
						const float DX = static_cast<float>(Room.first.X) - P.X, DY = static_cast<float>(Room.first.Y) - P.Y;
						const float D = DX * DX + DY * DY;
						if (D < Nearest)
						{
							Nearest = D;
							Hang = std::clamp(Room.second - RoadMeshImpl::TruckHeight - RoadMeshImpl::TruckAir, RoadMeshImpl::ThinSkirt, Depth);
						}
					}
				}
				P.Z = Surface.Height(Vec2(P.X, P.Y), Owner) + Offset - (bTop[i] ? 0.0f : Hang);
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
		// grouped and paved as a single surface ---
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
		int32_t Splits = 0;	// (junction corners left as splits, for the log)
		int32_t CornersShrunk = 0;	// (kerb corners made smaller or square, not to lie on another road)
		std::vector<uint8_t> InternalEdge(NumEdges, 0);
		auto WidestRoadAt = [&Graph](const RoadNode& Node)
		{
			float Widest = 0.0f;
			for (const int32_t Edge : Node.Edges)
			{
				Widest = std::max(Widest, Graph.Edges[Edge].HalfWidth);
			}
			return static_cast<double>(Widest);
		};
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			const RoadNode& A = Graph.Nodes[Edge.StartNode];
			const RoadNode& B = Graph.Nodes[Edge.EndNode];
			if (!bPaveComplexJunctions || Edge.bBridge || A.Layer != B.Layer || PolylineLength(Edge.Points) > std::min(JunctionLinkLength, LinkReach * (WidestRoadAt(A) + WidestRoadAt(B)))
				|| (A.Edges.size() < 3 && B.Edges.size() < 3) || std::abs(A.Height - B.Height) > MaxJunctionAreaRise)
			{
				continue;
			}
			Group[Find(Edge.StartNode)] = Find(Edge.EndNode);
			InternalEdge[EdgeIndex] = 1;
		}
		{
			std::unordered_map<int32_t, std::vector<int32_t>> Members;	// group -> its junctions
			std::unordered_map<int32_t, double> RoadArea;				// group -> what its links cover (cm2)
			for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
			{
				if (Find(NodeIndex) != NodeIndex || std::any_of(Graph.Nodes[NodeIndex].Edges.begin(), Graph.Nodes[NodeIndex].Edges.end(), [&](int32_t E) { return InternalEdge[E] != 0; }))
				{
					Members[Find(NodeIndex)].push_back(NodeIndex);
				}
			}
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				if (InternalEdge[EdgeIndex])
				{
					const RoadEdge& Edge = Graph.Edges[EdgeIndex];
					RoadArea[Find(Edge.StartNode)] += PolylineLength(Edge.Points) * 2.0 * Edge.HalfWidth;
				}
			}
			std::unordered_set<int32_t> Dissolve;
			int32_t Rings = 0;
			for (const auto& [Root, Nodes] : Members)
			{
				std::vector<Vec2> Sorted;
				for (const int32_t Node : Nodes)
				{
					Sorted.push_back(Graph.Nodes[Node].Position);
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
				double HullArea = 0.0;
				for (size_t i = 0; i < Hull.size(); ++i)
				{
					HullArea += Cross(Hull[i], Hull[(i + 1) % Hull.size()]);
				}
				HullArea = std::abs(HullArea) * 0.5;
				if (Nodes.size() < MinClusterJunctions || HullArea > MaxIslandRatio * RoadArea[Root])
				{
					++Rings;
					for (const int32_t Node : Nodes)
					{
						Dissolve.insert(Node);
					}
				}
			}
			if (Options.Log && !Members.empty())
			{
				Options.Log(georoads::Format("[Roads] %d groups of junctions joined by short links: %d paved as one surface, %d kept as their own fans (roundabouts, and fewer than four junctions)",
					static_cast<int32_t>(Members.size()), static_cast<int32_t>(Members.size()) - Rings, Rings));
			}
			if (!Dissolve.empty())
			{
				for (const int32_t Node : Dissolve)
				{
					Group[Node] = Node;
				}
				for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
				{
					if (InternalEdge[EdgeIndex] && Dissolve.count(Graph.Edges[EdgeIndex].StartNode))
					{
						InternalEdge[EdgeIndex] = 0;
					}
				}
			}
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

				// the most a road may be set back (cm): near-parallel roads produce far-away intersections; the rounded corners add room
				auto AllowedTrim = [&](const Incidence& Road)
				{
					return std::min(static_cast<double>(Road.HalfWidth * MaxTrimFactor) + FilletRoom, PolylineLength(Graph.Edges[Road.Edge].Points) * 0.45);
				};

				// each neighbouring pair: intersect the facing edges of the two roads
				std::vector<KerbReturn> Returns(Roads.size());
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
					const double MitreA = Cross(Delta, B.Direction) / Denominator;
					const double MitreB = Cross(Delta, A.Direction) / Denominator;
					// a split, not a corner: the two roads leave side by side (at a bend, two roads, the corner is cut whatever the angle)
					const bool bLink = georoads::EndsWith(Graph.Edges[A.Edge].Highway, "_link") != georoads::EndsWith(Graph.Edges[B.Edge].Highway, "_link");
					const double Widest = std::max(A.HalfWidth, B.HalfWidth);
					if (Roads.size() >= 3 && Gap < SplitMaxGap && ((bLink && Gap < LinkSplitGap) || std::max(MitreA, MitreB) > CornerFits * Widest))
					{
						++Splits;
						continue;
					}
					A.Trim = std::max(A.Trim, MitreA);
					B.Trim = std::max(B.Trim, MitreB);
					// a junction (three roads or more), not a bend: its corner is to be rounded
					if (Roads.size() >= 3 && Gap >= MinFilletGap && MitreA >= 0.0 && MitreB >= 0.0)
					{
						double Radius = std::min(KerbRadius(Graph.Edges[A.Edge].Highway), KerbRadius(Graph.Edges[B.Edge].Highway));
						// (the arc's nearest point to the meeting of the edges is R * (1 / sin(half the angle) - 1) away from it)
						const double Bulge = 1.0 / std::sin(Gap * 0.5) - 1.0;
						if (Bulge > 0.01)
						{
							Radius = std::min(Radius, MaxFilletReach / Bulge);
						}
						// (the corner's fill, between its arc and the square corner, may not lie on another road of the junction: a radius
						// that would is halved, twice at most, then the corner is left square; BeamNG.Earth's junctions.py: 50 corners
						// on carriageways in 36 tiles down to 4)
						const Vec2 Square = PA + A.Direction * MitreA;
						const Vec2 Inward = (A.Direction + B.Direction).Normal();
						auto OnAnother = [&](const Vec2& P)
						{
							for (size_t k = 0; k < Roads.size(); ++k)
							{
								if (k == i || k == (i + 1) % Roads.size())
								{
									continue;
								}
								const Incidence& C = Roads[k];
								const double Along = Dot(P - Node.Position, C.Direction);
								const double Off = std::abs(Cross(C.Direction, P - Node.Position));
								if (Along > 0.0 && Off < C.HalfWidth - 20.0)
								{
									return true;
								}
							}
							return false;
						};
						for (int32_t Try = 0; Try < 3 && Radius > 0.0; ++Try)
						{
							const double Tangent = Radius / std::tan(Gap * 0.5);
							const Vec2 TA = PA + A.Direction * (MitreA + Tangent), TB = PB + B.Direction * (MitreB + Tangent);
							const Vec2 Middle = Square + Inward * (Radius / std::sin(Gap * 0.5) - Radius);
							if (!OnAnother(TA) && !OnAnother(TB) && !OnAnother(Middle) && !OnAnother((Square + Middle) * 0.5))
							{
								break;
							}
							Radius = Try < 2 ? Radius * 0.5 : 0.0;
							++CornersShrunk;
						}
						if (Radius > 0.0)
						{
							Returns[i].MitreA = MitreA;
							Returns[i].MitreB = MitreB;
							Returns[i].Tangent = Radius / std::tan(Gap * 0.5);
							Returns[i].Gap = Gap;
						}
					}
				}

				// the rounded corners: each road is set back by the arc's tangent length (as far as it has room for), and the arc's radius
				// shrinks to fit when it hasn't
				for (size_t i = 0; i < Roads.size(); ++i)
				{
					KerbReturn& Corner = Returns[i];
					if (Corner.Tangent <= 0.0)
					{
						continue;
					}
					Incidence& A = Roads[i];
					Incidence& B = Roads[(i + 1) % Roads.size()];
					const double Tangent = std::min({ Corner.Tangent, AllowedTrim(A) - Corner.MitreA, AllowedTrim(B) - Corner.MitreB });
					if (Tangent < 50.0)
					{
						Corner.Tangent = 0.0;
						continue;
					}
					Corner.Tangent = Tangent;
					Corner.bArc = true;
					A.Trim = std::max(A.Trim, Corner.MitreA + Tangent);
					B.Trim = std::max(B.Trim, Corner.MitreB + Tangent);
				}

				// clamp trims and record them on the edges
				for (Incidence& Road : Roads)
				{
					const RoadEdge& Edge = Graph.Edges[Road.Edge];
					const double Length = PolylineLength(Edge.Points);
					Road.Trim = std::clamp(Road.Trim, 0.0, AllowedTrim(Road));
					(Road.bAtStart ? StartTrim : EndTrim)[Road.Edge] = Road.Trim;

					Vec2 Direction;
					Road.TrimPoint = PointAlong(Edge, Road.bAtStart ? Road.Trim : Length - Road.Trim, Road.TrimHeight, Direction);
					Road.TrimDirection = Road.bAtStart ? Direction : Direction * -1.0;
					Road.HalfWidth = Edge.HalfWidthAt(Road.bAtStart ? Road.Trim : Length - Road.Trim, Length);	// as wide as its ribbon starts
				}

				// a point on a road's edge, s cm from the node (side +1 its left edge going out, -1 its right), and its height
				auto EdgePoint = [&](const Incidence& Road, double S, double Side, float& Z)
				{
					const RoadEdge& Edge = Graph.Edges[Road.Edge];
					const double Length = PolylineLength(Edge.Points);
					const double Where = Road.bAtStart ? S : Length - S;
					Vec2 Direction;
					float Height = 0.0f;
					const Vec2 Point = PointAlong(Edge, Where, Height, Direction);
					if (!Road.bAtStart)
					{
						Direction = Direction * -1.0;
					}
					Z = Height + RoadOffset;
					return Point + Perp(Direction) * (Side * Edge.HalfWidthAt(Where, Length));
				};

				// junction polygon: the trimmed corners of every road, in angle order, with the rounded corners (arcs, kerb returns)
				// between them, fanned from the node
				JunctionFan Fan;
				float WidestHalfWidth = 0.0f;
				for (size_t RoadIndex = 0; RoadIndex < Roads.size(); ++RoadIndex)
				{
					const Incidence& Road = Roads[RoadIndex];
					// the corners sit exactly where the road's own surface starts
					Fan.Corners.push_back(At(Road.TrimPoint - Perp(Road.TrimDirection) * Road.HalfWidth, Road.TrimHeight + RoadOffset));
					Fan.OpenAfter.push_back(0);	// across the road mouth
					Fan.SegmentsAfter.push_back(LateralSegments(Road.HalfWidth));
					const Vec2 MouthA = Road.TrimPoint + Perp(Road.TrimDirection) * Road.HalfWidth;
					Fan.Corners.push_back(At(MouthA, Road.TrimHeight + RoadOffset));
					Fan.OpenAfter.push_back(1);	// kerb between this road and the next
					Fan.SegmentsAfter.push_back(0);	// by length
					if (Road.HalfWidth > WidestHalfWidth)
					{
						WidestHalfWidth = Road.HalfWidth;
						Fan.Surface = static_cast<int32_t>(Graph.Edges[Road.Edge].Kind);
					}

					const KerbReturn& Corner = Returns[RoadIndex];
					if (!Corner.bArc)
					{
						continue;
					}
					// the corner to the next road: back along this road's edge to where the arc touches it, round the arc, and out along the
					// next road's edge to its mouth
					const Incidence& Next = Roads[(RoadIndex + 1) % Roads.size()];
					float ZA = 0.0f, ZB = 0.0f;
					const Vec2 TA = EdgePoint(Road, Corner.MitreA + Corner.Tangent, 1.0, ZA);
					const Vec2 TB = EdgePoint(Next, Corner.MitreB + Corner.Tangent, -1.0, ZB);
					const Vec2 MouthB = Next.TrimPoint - Perp(Next.TrimDirection) * Next.HalfWidth;
					auto PushCorner = [&](const Vec2& P, float Z)
					{
						Fan.Corners.push_back(At(P, Z));
						Fan.OpenAfter.push_back(1);
						Fan.SegmentsAfter.push_back(0);
					};
					if (georoads::Distance(TA, MouthA) > 20.0)
					{
						PushCorner(TA, ZA);	// (this road's mouth is further out than this corner needs: its edge runs straight back)
					}
					const double Radius = Corner.Tangent * std::tan(Corner.Gap * 0.5);
					const double Half = georoads::Distance(TA, TB) * 0.5;
					if (Half > 1.0 && Half < Radius * 0.999)
					{
						const Vec2 Middle = (TA + TB) * 0.5;
						Vec2 Out = Perp(TB - TA).Normal();
						if (georoads::Dot(Out, Middle - Node.Position) < 0.0)
						{
							Out = Out * -1.0;
						}
						const Vec2 Centre = Middle + Out * std::sqrt(Radius * Radius - Half * Half);	// (inside the block between the two roads)
						const double From = std::atan2(TA.Y - Centre.Y, TA.X - Centre.X);
						double Sweep = std::atan2(TB.Y - Centre.Y, TB.X - Centre.X) - From;
						while (Sweep > geoground::Pi)
						{
							Sweep -= 2.0 * geoground::Pi;
						}
						while (Sweep < -geoground::Pi)
						{
							Sweep += 2.0 * geoground::Pi;
						}
						const int32_t Steps = std::clamp(static_cast<int32_t>(std::ceil(std::abs(Sweep) / (geoground::Pi / 9.0))), 2, 6);
						for (int32_t Step = 0; Step <= Steps; ++Step)
						{
							const double Angle = From + Sweep * Step / Steps;
							const float Z = ZA + (ZB - ZA) * static_cast<float>(Step) / Steps;
							PushCorner(Centre + Vec2(std::cos(Angle), std::sin(Angle)) * Radius, Z);
						}
					}
					else
					{
						PushCorner(TB, ZB);	// (too close for an arc: the corner is the straight line between the two)
					}
					if (georoads::Distance(TB, MouthB) <= 20.0 && !Fan.Corners.empty())
					{
						// (the last point is the next road's own corner, which is pushed with it: not twice)
						Fan.Corners.pop_back();
						Fan.OpenAfter.pop_back();
						Fan.SegmentsAfter.pop_back();
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
			JunctionArea Area;
			for (const Vec3f& Corner : Fan.Corners)
			{
				Area.Corners.push_back(Vec2(Corner.X, Corner.Y));
			}
			Area.Centre = Fan.Position;
			Area.Height = Center.Z;
			Out.Junctions.push_back(std::move(Area));
			};

			std::unordered_set<int32_t> PavedGroups;
			// each paved surface keeps its own plane when every road vertex is given its height from the shared surface (which would
			// level it to one junction's height and leave a step at the mouth of a road that meets it lower)
			struct PavedPlane
			{
				int32_t Surface;
				size_t RoadFirst, RoadLast, SkirtFirst, SkirtLast;
				Vec2 Mean;
				float MeanZ;
				double SlopeX, SlopeY;
			};
			std::vector<PavedPlane> PavedPlanes;
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
			float Sag = 0.0f;
			for (const Vec3f& P : Points)
			{
				Sag = std::max(Sag, std::abs(PlaneZ(Vec2(P.X, P.Y)) - P.Z));
			}
			if (Options.Log)
			{
				std::string Ids;
				for (const JunctionFan& F : Fans) { Ids += georoads::Format(" %d", F.Node); }
				Options.Log(georoads::Format("[Cluster] nodes%s: %d points, sag %.0f cm, %s", Ids.c_str(), int(Points.size()), Sag, Sag > MaxClusterSag ? "kept as fans" : "paved as one"));
			}
			if (Sag > MaxClusterSag)
			{
				for (const JunctionFan& Fan : Fans)
				{
					DrawFan(Fan);
				}
				continue;	// (the roads here are at different heights: one flat surface would leave a step at their mouths)
			}

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
			const size_t RoadFirst = Mesh.Positions.size(), SkirtFirst = Out.Skirts.Positions.size();
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
			PavedPlanes.push_back({ Fans[0].Surface, RoadFirst, Mesh.Positions.size(), SkirtFirst, Out.Skirts.Positions.size(), Mean, MeanZ, SlopeX, SlopeY });
		}

		if (Options.Log && Out.NumJunctionAreas > 0)
		{
			Options.Log(georoads::Format("[Roads] %d tangles of junctions are one paved surface (their roads at about one height)", Out.NumJunctionAreas));
		}
		if (Options.Log && CornersShrunk > 0)
		{
			Options.Log(georoads::Format("[Roads] %d kerb corners made smaller (or square) so they don't lie on another road of their junction", CornersShrunk));
		}
		if (Options.Log && Splits > 0)
		{
			Options.Log(georoads::Format("[Roads] %d junction corners are splits (off-ramps, forks): the roads leave side by side and are not cut back into each other", Splits));
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
		// each road that forks from another (leaves a junction in the same direction, within SplitMaxGap) and is the narrower of the
		// two (the lower numbered if alike): the roads it forks from. The wider road keeps its lanes whole
		std::vector<std::vector<int32_t>> ForkMain(NumEdges);
		for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			const RoadNode& Node = Graph.Nodes[NodeIndex];
			if (Node.Edges.size() < 3)
			{
				continue;
			}
			auto Leaving = [&](int32_t EdgeIndex)
			{
				const RoadEdge& E = Graph.Edges[EdgeIndex];
				if (E.Points.size() < 2)
				{
					return Vec2();
				}
				return E.StartNode == NodeIndex ? (E.Points[1] - E.Points[0]).Normal() : (E.Points[E.Points.size() - 2] - E.Points.back()).Normal();
			};
			for (const int32_t A : Node.Edges)
			{
				for (const int32_t B : Node.Edges)
				{
					const RoadEdge& EA = Graph.Edges[A];
					const RoadEdge& EB = Graph.Edges[B];
					if (A == B || EA.StartNode == EA.EndNode || EB.StartNode == EB.EndNode || georoads::Dot(Leaving(A), Leaving(B)) < std::cos(SplitMaxGap))
					{
						continue;
					}
					if ((EA.HalfWidth < EB.HalfWidth || (EA.HalfWidth == EB.HalfWidth && A > B)) && std::find(ForkMain[A].begin(), ForkMain[A].end(), B) == ForkMain[A].end())
					{
						ForkMain[A].push_back(B);
					}
				}
			}
		}
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

				// a road that forks from a wider one keeps out of its lanes: an edge point inside them (at about its own height) is
				// drawn in along the cross-section to the main road's edge, so the road grows out of that edge as the two part
				for (const int32_t Main : ForkMain[EdgeIndex])
				{
					const RoadEdge& MainEdge = Graph.Edges[Main];
					auto Inside = [&](const Vec2& Q, float OwnZ)
					{
						double Nearest = std::numeric_limits<double>::max();
						for (size_t k = 0; k + 1 < MainEdge.Points.size(); ++k)
						{
							const Vec2 AB = MainEdge.Points[k + 1] - MainEdge.Points[k];
							const double L2 = AB.SizeSquared();
							const double T = L2 > 1.0 ? std::clamp(georoads::Dot(Q - MainEdge.Points[k], AB) / L2, 0.0, 1.0) : 0.0;
							Nearest = std::min(Nearest, georoads::Distance(Q, MainEdge.Points[k] + AB * T));
						}
						return Nearest < MainEdge.HalfWidth - 5.0 && std::abs(SharedSurface.Height(Q, Main) + RoadOffset - OwnZ) < ForkSameLevel;
					};
					for (std::vector<Vec3f>* Side : { &Left, &Right })
					{
						for (int32_t i = 0; i < N; ++i)
						{
							Vec3f& Edge3 = (*Side)[i];
							const Vec2 Q(Edge3.X, Edge3.Y);
							if (!Inside(Q, Edge3.Z))
							{
								continue;
							}
							const Vec2 C = Points[i];
							double Low = 0.0, High = 1.0;	// (how far from the middle towards the edge it may go and be outside the main road)
							if (Inside(C, Edge3.Z))
							{
								High = 0.0;	// (the middle itself is in its lanes: no width on this side)
							}
							for (int32_t Step = 0; Step < 12 && High > 0.0; ++Step)
							{
								const double Mid = 0.5 * (Low + High);
								if (Inside(C + (Q - C) * Mid, Edge3.Z))
								{
									High = Mid;
								}
								else
								{
									Low = Mid;
								}
							}
							const Vec2 Moved = C + (Q - C) * Low;
							Edge3.X = static_cast<float>(Moved.X);
							Edge3.Y = static_cast<float>(Moved.Y);
						}
					}
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
		// where each road is over another: the room between them there (cm), so its deck can be thin where it has to be
		std::vector<std::vector<std::pair<Vec2, float>>> Rooms(Graph.Edges.size());
		for (const georoads::LevelPair& Pair : georoads::FindLevelPairs(Graph, [&Graph](int32_t Edge) -> const std::vector<Vec2>& { return Graph.Edges[Edge].Points; }))
		{
			const RoadEdge& Up = Graph.Edges[Pair.UpperEdge];
			const RoadEdge& Down = Graph.Edges[Pair.LowerEdge];
			if (Up.Heights.size() != Up.Points.size() || Down.Heights.size() != Down.Points.size())
			{
				continue;
			}
			const float UpperHeight = Up.Heights[Pair.UpperSegment] + (Up.Heights[Pair.UpperSegment + 1] - Up.Heights[Pair.UpperSegment]) * static_cast<float>(Pair.UpperT);
			const float LowerHeight = Down.Heights[Pair.LowerSegment] + (Down.Heights[Pair.LowerSegment + 1] - Down.Heights[Pair.LowerSegment]) * static_cast<float>(Pair.LowerT);
			const Vec2 At = Up.Points[Pair.UpperSegment] + (Up.Points[Pair.UpperSegment + 1] - Up.Points[Pair.UpperSegment]) * Pair.UpperT;
			Rooms[Pair.UpperEdge].emplace_back(At, UpperHeight - LowerHeight);
		}
		ApplySurfaceToSkirts(SharedSurface, Out.Skirts, RoadOffset, SkirtDepth, Rooms, ParallelFor);
		for (const PavedPlane& Plane : PavedPlanes)
		{
			auto PlaneHeight = [&Plane](const Vec3f& P) { return static_cast<float>(Plane.MeanZ + Plane.SlopeX * (P.X - Plane.Mean.X) + Plane.SlopeY * (P.Y - Plane.Mean.Y)) + JunctionAreaLift; };
			MeshData& Paved = Out.Roads[Plane.Surface];
			for (size_t i = Plane.RoadFirst; i < Plane.RoadLast; ++i)
			{
				Paved.Positions[i].Z = PlaneHeight(Paved.Positions[i]);
			}
			for (size_t i = Plane.SkirtFirst; i < Plane.SkirtLast; ++i)
			{
				static constexpr bool bTop[6] = { true, true, false, true, false, false };
				Vec3f& P = Out.Skirts.Positions[i];
				P.Z = PlaneHeight(P) - (bTop[(i - Plane.SkirtFirst) % 6] ? 0.0f : SkirtDepth);
			}
		}
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
