// Geodrive Core - GPL (see Core/README.md)

#include "RoadScan.h"

#include "GeoRoads/RoadDesign.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>

namespace geoworld
{
	using georoads::RoadEdge;
	using georoads::RoadGraph;
	using georoads::Vec2;

	namespace
	{
		/** Sample spacing along a road (cm) and the grid cell of the triangle index (cm) */
		constexpr double SampleStep = 50.0;
		constexpr double CellSize = 1000.0;

		/** A height change over one sample this much beyond the local slope is a jolt (cm) */
		constexpr float JoltThreshold = 2.5f;

		/** Grade is compared over this distance either side of a point (samples) to find crests and dips */
		constexpr int32_t GradeWindow = 20;

		/** Vertical acceleration at the road's speed above which a crest/dip counts (m/s^2, ~0.3 g) */
		constexpr float LaunchThreshold = 3.0f;

		/** Ground this far above the road surface counts as grass through the road (cm) */
		constexpr float GrassThreshold = 1.0f;

		/** Faults of the same type closer than this are one fault (cm) */
		constexpr double MergeDistance = 1500.0;

		/** Grade is measured over this many samples (30 m), so a short kink doesn't count as steep */
		constexpr int32_t SteepWindow = 60;

		/** A dead end whose rounded end stands this far above the ground just past it stops in mid-air (cm) */
		constexpr float DeadEndThreshold = 15.0f;

		/** Dead ends this close to the edge of the area are where the map data stops (cm) */
		constexpr double AreaEdgeMargin = 10000.0;

		/** The ground beside a road: every 20 m along it, out to 40 m either side in 5 m steps (cm) */
		constexpr int32_t GroundRowEvery = 40;
		constexpr double GroundStep = 500.0;
		constexpr int32_t GroundSteps = 8;

		/** Cliffs steeper than any cutting where the natural ground is gentle; trenches the natural ground doesn't have;
		 *  only where the road work moved the ground by more than ReshapedBy (cm) */
		constexpr float CliffSlope = 1.6f;
		constexpr float NaturalSlope = 0.5f;
		constexpr float TrenchDepth = 50.0f;
		constexpr float ReshapedBy = 30.0f;

		bool EndsWith(const std::string& S, const char* Suffix)
		{
			const size_t N = std::char_traits<char>::length(Suffix);
			return S.size() >= N && S.compare(S.size() - N, N, Suffix) == 0;
		}
		bool StartsWith(const std::string& S, const char* Prefix) { return S.rfind(Prefix, 0) == 0; }

		/** Typical speed per road class (m/s), for how sharp a crest may be */
		float RoadSpeed(const std::string& Highway)
		{
			if (EndsWith(Highway, "_link")) { return 19.0f; }	// (before motorway: a motorway_link is a slip road)
			if (StartsWith(Highway, "motorway") || Highway == "trunk") { return 33.0f; }
			if (Highway == "primary") { return 28.0f; }
			if (Highway == "secondary") { return 22.0f; }
			if (Highway == "tertiary") { return 19.0f; }
			if (Highway == "residential" || Highway == "unclassified" || Highway == "road") { return 16.0f; }
			return 11.0f;
		}

		Vec2 Lerp(const Vec2& A, const Vec2& B, double T) { return A + (B - A) * T; }

		/** Road triangles in a 2D grid, to find the road surface height at any point */
		struct SurfaceIndex
		{
			std::vector<geoground::Vec3f> Corners;	// three per triangle
			std::vector<int32_t> Owners;			// one per triangle (MeshData::Tags)
			std::map<std::pair<int32_t, int32_t>, std::vector<int32_t>> Cells;

			void Add(const geoground::MeshData& Mesh)
			{
				for (size_t i = 0; i + 2 < Mesh.Indices.size(); i += 3)
				{
					const int32_t Triangle = int32_t(Corners.size() / 3);
					Owners.push_back(i / 3 < Mesh.Tags.size() ? Mesh.Tags[i / 3] : -1);
					double MinX = 1e30, MinY = 1e30, MaxX = -1e30, MaxY = -1e30;
					for (int32_t Corner = 0; Corner < 3; ++Corner)
					{
						const geoground::Vec3f& P = Mesh.Positions[size_t(Mesh.Indices[i + Corner])];
						Corners.push_back(P);
						MinX = std::min(MinX, double(P.X));
						MinY = std::min(MinY, double(P.Y));
						MaxX = std::max(MaxX, double(P.X));
						MaxY = std::max(MaxY, double(P.Y));
					}
					for (int32_t X = int32_t(std::floor(MinX / CellSize)); X <= int32_t(std::floor(MaxX / CellSize)); ++X)
					{
						for (int32_t Y = int32_t(std::floor(MinY / CellSize)); Y <= int32_t(std::floor(MaxY / CellSize)); ++Y)
						{
							Cells[{ X, Y }].push_back(Triangle);
						}
					}
				}
			}

			/** Highest road surface at a point within a height band round where this road should be (so a bridge
			 *  overhead or a road underneath doesn't count), or false where there is none */
			bool Height(const Vec2& P, float Expected, float& Out, int32_t& OutOwner, float Band = 0.0f) const
			{
				const auto Found = Cells.find({ int32_t(std::floor(P.X / CellSize)), int32_t(std::floor(P.Y / CellSize)) });
				if (Found == Cells.end())
				{
					return false;
				}
				bool bFound = false;
				for (const int32_t Triangle : Found->second)
				{
					const geoground::Vec3f& A = Corners[size_t(Triangle) * 3];
					const geoground::Vec3f& B = Corners[size_t(Triangle) * 3 + 1];
					const geoground::Vec3f& C = Corners[size_t(Triangle) * 3 + 2];
					const double Denominator = double(B.Y - C.Y) * (A.X - C.X) + double(C.X - B.X) * (A.Y - C.Y);
					if (std::fabs(Denominator) < 1.0e-6)
					{
						continue;
					}
					const double U = (double(B.Y - C.Y) * (P.X - C.X) + double(C.X - B.X) * (P.Y - C.Y)) / Denominator;
					const double V = (double(C.Y - A.Y) * (P.X - C.X) + double(A.X - C.X) * (P.Y - C.Y)) / Denominator;
					const double W = 1.0 - U - V;
					// a little slack: mesh points are floats, good to ~0.5 cm, so a sample exactly on a road's outer edge
					// (the tip of a dead end) can land just outside it
					if (U < -1.0e-2 || V < -1.0e-2 || W < -1.0e-2)
					{
						continue;
					}
					const float Z = float(U * A.Z + V * B.Z + W * C.Z);
					if (Band > 0.0f ? std::fabs(Z - Expected) > Band : (Z < Expected - 100.0f || Z > Expected + 50.0f))
					{
						continue;
					}
					if (!bFound || Z > Out)
					{
						Out = Z;
						OutOwner = Owners[size_t(Triangle)];
					}
					bFound = true;
				}
				return bFound;
			}
		};

		/** The ground the player sees at a point: inside a road corridor its surface (the ground height, at road detail),
		 *  unless the terrain grid sunk beneath it still pokes above it; elsewhere the near terrain's 10 m triangles,
		 *  exactly as the chunks build them */
		float TerrainMeshHeight(const geoground::Terrain& Ground, const Vec2& P, std::map<std::pair<int32_t, int32_t>, float>& Cache)
		{
			const double Spacing = geoground::Terrain::NearSpacing;
			const int32_t GX = int32_t(std::floor(P.X / Spacing));
			const int32_t GY = int32_t(std::floor(P.Y / Spacing));
			const float U = float((P.X - GX * Spacing) / Spacing);
			const float V = float((P.Y - GY * Spacing) / Spacing);
			auto Vertex = [&](int32_t X, int32_t Y)
			{
				const auto Known = Cache.find({ X, Y });
				if (Known != Cache.end())
				{
					return Known->second;
				}
				const Vec2 At(X * Spacing, Y * Spacing);
				const float Sink = Ground.SinksUnderCorridor(At) ? float(Spacing) * 0.8f : 0.0f;	// as the chunks are built
				return Cache[{ X, Y }] = Ground.GroundHeight(At) - Sink;
			};
			const float H00 = Vertex(GX, GY);
			const float H10 = Vertex(GX + 1, GY);
			const float H01 = Vertex(GX, GY + 1);
			const float H11 = Vertex(GX + 1, GY + 1);
			// split along the (0,0)-(1,1) diagonal, like the chunks
			const float Grid = U >= V ? H00 + U * (H10 - H00) + V * (H11 - H10) : H00 + V * (H01 - H00) + U * (H11 - H01);
			return Ground.InCorridor(P) ? std::max(Grid, Ground.GroundHeight(P)) : Grid;
		}

		const char* TypeName(RoadDefect Type)
		{
			switch (Type)
			{
			case RoadDefect::Jolt:      return "jolt";
			case RoadDefect::Launch:    return "launch";
			case RoadDefect::Grass:     return "grass through road";
			case RoadDefect::Hole:      return "hole";
			case RoadDefect::Steep:     return "steeper than the law";
			case RoadDefect::DeadEnd:   return "dead end in mid-air";
			case RoadDefect::OddGround: return "odd ground beside road";
			default:                    return "?";
			}
		}

		const char* TypeKey(RoadDefect Type)
		{
			switch (Type)
			{
			case RoadDefect::Jolt:      return "jolt";
			case RoadDefect::Launch:    return "launch";
			case RoadDefect::Grass:     return "grass";
			case RoadDefect::Hole:      return "hole";
			case RoadDefect::Steep:     return "steep";
			case RoadDefect::DeadEnd:   return "deadend";
			case RoadDefect::OddGround: return "oddground";
			default:                    return "?";
			}
		}

		const char* SizeUnit(RoadDefect Type) { return Type == RoadDefect::Launch ? "m/s2" : Type == RoadDefect::Steep ? "%" : "cm"; }

		template <typename... Args>
		std::string Format(const char* Pattern, Args... Values)
		{
			char Buffer[1024];
			std::snprintf(Buffer, sizeof(Buffer), Pattern, Values...);
			return Buffer;
		}

		RoadFault Fault(RoadDefect Type, const Vec2& Position, float Height, float Size, int32_t Edge, bool bAtJunction)
		{
			RoadFault F;
			F.Type = Type;
			F.Position = Position;
			F.Height = Height;
			F.Size = Size;
			F.Edge = Edge;
			F.bAtJunction = bAtJunction;
			return F;
		}

		std::string Quoted(std::string S)
		{
			std::replace(S.begin(), S.end(), '"', '\'');
			return S;
		}
	}

	int32_t RoadScanResult::Count(RoadDefect Type) const
	{
		return int32_t(std::count_if(Faults.begin(), Faults.end(), [Type](const RoadFault& F) { return F.Type == Type; }));
	}

	void ScanRoads(const RoadGraph& Graph, const std::vector<const geoground::MeshData*>& RoadSurfaces, const geoground::Terrain& Ground,
		const georoads::ParallelForFn& ParallelFor, bool bUK, RoadScanResult& Out)
	{
		const auto Start = std::chrono::steady_clock::now();
		SurfaceIndex Surface;
		for (const geoground::MeshData* Mesh : RoadSurfaces)
		{
			if (Mesh)
			{
				Surface.Add(*Mesh);
			}
		}

		std::vector<std::vector<RoadFault>> PerEdge(Graph.Edges.size());
		std::vector<double> Lengths(Graph.Edges.size(), 0.0);
		const geoground::Box2 Inside(Ground.Bounds.Min + Vec2(AreaEdgeMargin, AreaEdgeMargin), Ground.Bounds.Max - Vec2(AreaEdgeMargin, AreaEdgeMargin));

		auto ScanEdge = [&](int32_t EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[size_t(EdgeIndex)];
			const int32_t NP = int32_t(Edge.Points.size());
			if (NP < 2 || Edge.Heights.size() != Edge.Points.size())
			{
				return;
			}

			// walk the centreline at fixed spacing. The sideways direction (for the wheel lines) turns smoothly through
			// corners, like a wheel does; a sudden switch at a sharp corner would jump the line sideways (a false step).
			std::vector<Vec2> Centre, Side, CornerNormals;
			std::vector<float> Expected;
			for (int32_t i = 0; i < NP; ++i)
			{
				const Vec2 In = i > 0 ? (Edge.Points[size_t(i)] - Edge.Points[size_t(i) - 1]).Normal() : Vec2();
				const Vec2 Next = i + 1 < NP ? (Edge.Points[size_t(i) + 1] - Edge.Points[size_t(i)]).Normal() : Vec2();
				const Vec2 Along = (In + Next).Normal();
				CornerNormals.push_back(Vec2(-Along.Y, Along.X));
			}
			for (int32_t i = 0; i + 1 < NP; ++i)
			{
				const Vec2 A = Edge.Points[size_t(i)], B = Edge.Points[size_t(i) + 1];
				const double Length = georoads::Distance(A, B);
				Lengths[size_t(EdgeIndex)] += Length;
				const int32_t Steps = std::max(1, int32_t(std::ceil(Length / SampleStep)));
				for (int32_t Step = 0; Step < Steps; ++Step)
				{
					const double T = double(Step) / Steps;
					Centre.push_back(Lerp(A, B, T));
					Expected.push_back(Edge.Heights[size_t(i)] + (Edge.Heights[size_t(i) + 1] - Edge.Heights[size_t(i)]) * float(T));
					Side.push_back(Lerp(CornerNormals[size_t(i)], CornerNormals[size_t(i) + 1], T).Normal());
				}
			}
			Centre.push_back(Edge.Points.back());
			Expected.push_back(Edge.Heights.back());
			Side.push_back(CornerNormals.back());
			const int32_t N = int32_t(Centre.size());

			std::vector<RoadFault>& Found = PerEdge[size_t(EdgeIndex)];
			std::map<std::pair<int32_t, int32_t>, float> TerrainCache;

			// how far each sample is from a junction at either end of this road
			const bool bStartJunction = Graph.Nodes[size_t(Edge.StartNode)].Edges.size() >= 3;
			const bool bEndJunction = Graph.Nodes[size_t(Edge.EndNode)].Edges.size() >= 3;
			const double JunctionReach = Edge.HalfWidth * 4.0 + 1000.0;
			auto AtJunction = [&](int32_t i)
			{
				return (bStartJunction && i * SampleStep < JunctionReach) || (bEndJunction && (N - 1 - i) * SampleStep < JunctionReach);
			};
			const float Speed = RoadSpeed(Edge.Highway);
			std::vector<float> CentreHeights;
			std::vector<char> CentreOnRoad;

			// three lines for what the car feels (the middle and roughly where the wheels run), plus the two outer edges,
			// where only the ground is checked (it must stay under the whole width of the road)
			for (const float Offset : { 0.0f, -0.55f, 0.55f, -0.95f, 0.95f })
			{
				const bool bEdgeLine = std::fabs(Offset) > 0.9f;
				std::vector<float> Heights(size_t(N), 0.0f);
				std::vector<char> OnRoad(size_t(N), 0);
				std::vector<int32_t> Owner(size_t(N), -1);
				for (int32_t i = 0; i < N; ++i)
				{
					const Vec2 P = Centre[size_t(i)] + Side[size_t(i)] * (Edge.HalfWidth * Offset);
					float Road = 0.0f;
					OnRoad[size_t(i)] = Surface.Height(P, Expected[size_t(i)], Road, Owner[size_t(i)]) ? 1 : 0;
					Heights[size_t(i)] = Road;

					// the ground is checked along every line every 2 m (in a tunnel the mountain is above the road, rightly)
					if (OnRoad[size_t(i)] && i % 4 == 0 && !Edge.bTunnel)
					{
						const float GroundAt = TerrainMeshHeight(Ground, P, TerrainCache);
						if (GroundAt > Road + GrassThreshold)
						{
							RoadFault F = Fault(RoadDefect::Grass, P, Road, GroundAt - Road, EdgeIndex, AtJunction(i));
							F.Detail = Format("offset %.2f, corridor %s, shaped ground %.2f m, road %.2f m", Offset, Ground.InCorridor(P) ? "yes" : "no",
								Ground.GroundHeight(P) / 100.0f, Road / 100.0f);
							Found.push_back(F);
						}
					}
					if (Offset == 0.0f && !OnRoad[size_t(i)])
					{
						// size: how far the nearest road surface is from where this road should be (10000 = none at all)
						float Nearest = 0.0f;
						int32_t NearestOwner = -1;
						float Size = 10000.0f;
						for (const float Band : { 300.0f, 1000.0f, 5000.0f })
						{
							if (Surface.Height(P, Expected[size_t(i)], Nearest, NearestOwner, Band))
							{
								Size = std::fabs(Nearest - Expected[size_t(i)]);
								break;
							}
						}
						Found.push_back(Fault(RoadDefect::Hole, P, 0.0f, Size, EdgeIndex, AtJunction(i)));
					}
				}

				if (Offset == 0.0f)
				{
					CentreHeights = Heights;
					CentreOnRoad = OnRoad;
				}
				if (bEdgeLine)
				{
					continue;
				}

				for (int32_t i = 1; i + 1 < N; ++i)
				{
					if (!OnRoad[size_t(i) - 1] || !OnRoad[size_t(i)] || !OnRoad[size_t(i) + 1])
					{
						continue;
					}
					// jolt: the change over one sample against the average of its neighbours' changes
					const float Bend = Heights[size_t(i) + 1] - 2.0f * Heights[size_t(i)] + Heights[size_t(i) - 1];
					if (std::fabs(Bend) > JoltThreshold)
					{
						RoadFault F = Fault(RoadDefect::Jolt, Centre[size_t(i)], Heights[size_t(i)], std::fabs(Bend), EdgeIndex, AtJunction(i));
						for (int32_t j = i - 1; j <= i + 1; ++j)
						{
							F.Cause = std::max<uint8_t>(F.Cause, Owner[size_t(j)] < 0 ? 1 : Owner[size_t(j)] != EdgeIndex ? 2 : 0);
						}
						if (F.Size > 30.0f)
						{
							for (int32_t j = std::max(0, i - 3); j <= std::min(N - 1, i + 3); ++j)
							{
								F.Detail += Format(" [%.2f m, owner %d]", Heights[size_t(j)] / 100.0f, Owner[size_t(j)]);
							}
							F.Detail += Format(" offset %.2f, expected %.2f m", Offset, Expected[size_t(i)] / 100.0f);
						}
						Found.push_back(F);
					}

					// launch: grade before vs grade after, over 10 m each side, at the road's speed
					if (Offset == 0.0f && i >= GradeWindow && i + GradeWindow < N && OnRoad[size_t(i - GradeWindow)] && OnRoad[size_t(i + GradeWindow)])
					{
						const double Before = georoads::Distance(Centre[size_t(i - GradeWindow)], Centre[size_t(i)]);
						const double After = georoads::Distance(Centre[size_t(i)], Centre[size_t(i + GradeWindow)]);
						if (Before > 1.0 && After > 1.0)
						{
							const double GradeBefore = (Heights[size_t(i)] - Heights[size_t(i - GradeWindow)]) / Before;
							const double GradeAfter = (Heights[size_t(i + GradeWindow)] - Heights[size_t(i)]) / After;
							const double Curvature = (GradeAfter - GradeBefore) / ((Before + After) * 0.5 / 100.0);	// per metre
							const float Acceleration = float(std::fabs(Curvature) * Speed * Speed);
							if (Acceleration > LaunchThreshold)
							{
								// the profile round it (heights and who owns the surface), to see what shapes it
								RoadFault F = Fault(RoadDefect::Launch, Centre[size_t(i)], Heights[size_t(i)], Acceleration, EdgeIndex, AtJunction(i));
								const double Step = georoads::Distance(Centre[size_t(i)], Centre[size_t(std::min(i + 1, N - 1))]) / 100.0;
								for (int32_t j = std::max(0, i - 2 * GradeWindow); j <= std::min(N - 1, i + 2 * GradeWindow); j += std::max(1, GradeWindow / 2))
								{
									F.Detail += Format(" [%.0f m: %.2f m%s]", (j - i) * Step, Heights[size_t(j)] / 100.0f,
										Owner[size_t(j)] == EdgeIndex ? "" : Owner[size_t(j)] < 0 ? " junction" : " other road");
								}
								F.Detail += Format(" centreline %.2f m, %.0f km/h", Expected[size_t(i)] / 100.0f, Speed * 3.6f);
								Found.push_back(F);
							}
						}
					}
				}
			}

			// steeper than the law: grade over 30 m on the surface the car drives on, against the class's mountain limit
			if (!Edge.bBridge && !Edge.bTunnel)
			{
				const georoads::RoadDesign Design = georoads::DesignFor(Edge, bUK);
				for (int32_t i = 0; i + SteepWindow < N; i += SteepWindow / 6)
				{
					if (!CentreOnRoad[size_t(i)] || !CentreOnRoad[size_t(i + SteepWindow)])
					{
						continue;
					}
					const double Run = georoads::Distance(Centre[size_t(i)], Centre[size_t(i + SteepWindow)]);
					const float Grade = Run > 1.0 ? float(std::fabs(CentreHeights[size_t(i + SteepWindow)] - CentreHeights[size_t(i)]) / Run) : 0.0f;
					if (Grade > Design.MaxGradeMountain + 0.005f)
					{
						RoadFault F = Fault(RoadDefect::Steep, Centre[size_t(i + SteepWindow / 2)], CentreHeights[size_t(i)], Grade * 100.0f, EdgeIndex, AtJunction(i));
						F.Detail = Format("limit %.0f%% (flat country %.0f%%)", Design.MaxGradeMountain * 100.0f, Design.MaxGrade * 100.0f);
						Found.push_back(F);
					}
				}
			}

			// dead ends: does the road's end edge stand above the ground just past it?
			for (const bool bStart : { true, false })
			{
				const georoads::RoadNode& Node = Graph.Nodes[size_t(bStart ? Edge.StartNode : Edge.EndNode)];
				const int32_t Last = N - 1;
				const int32_t End = bStart ? 0 : Last;
				const int32_t Inner = bStart ? std::min(2, Last) : std::max(0, Last - 2);
				if (Node.Edges.size() != 1 || Edge.bBridge || !Inside.IsInside(Centre[size_t(End)]) || End == Inner)
				{
					continue;
				}
				const Vec2 Outward = (Centre[size_t(End)] - Centre[size_t(Inner)]).Normal();
				const float GroundAt = TerrainMeshHeight(Ground, Centre[size_t(End)] + Outward * (Edge.HalfWidth + 100.0), TerrainCache);
				const float Drop = Expected[size_t(End)] - GroundAt;
				if (Drop > DeadEndThreshold)
				{
					Found.push_back(Fault(RoadDefect::DeadEnd, Centre[size_t(End)], Expected[size_t(End)], Drop, EdgeIndex, false));
				}
			}

			// the ground beside the road, every 20 m along it: cliffs and trenches. Points on any road surface or in
			// water are skipped.
			if (!Edge.bBridge && !Edge.bTunnel)
			{
				for (int32_t i = GroundRowEvery / 2; i < N; i += GroundRowEvery)
				{
					if (!CentreOnRoad[size_t(i)])
					{
						continue;
					}
					for (const double SideSign : { -1.0, 1.0 })
					{
						float Shaped[GroundSteps + 1], Natural[GroundSteps + 1];
						bool bValid[GroundSteps + 1];
						Vec2 Points[GroundSteps + 1];
						for (int32_t k = 0; k <= GroundSteps; ++k)
						{
							Points[k] = Centre[size_t(i)] + Side[size_t(i)] * (SideSign * (Edge.HalfWidth + 300.0 + k * GroundStep));
							Shaped[k] = TerrainMeshHeight(Ground, Points[k], TerrainCache);
							Natural[k] = Ground.BareHeight(Points[k]);
							float OtherRoad = 0.0f;
							int32_t OtherOwner = -1;
							bValid[k] = !Ground.IsWater(Points[k]) && !Surface.Height(Points[k], Shaped[k], OtherRoad, OtherOwner, 500.0f);
						}
						for (int32_t k = 1; k <= GroundSteps; ++k)
						{
							if (!bValid[k - 1] || !bValid[k])
							{
								continue;
							}
							const float Rise = std::fabs(Shaped[k] - Shaped[k - 1]);
							const float NaturalRise = std::fabs(Natural[k] - Natural[k - 1]);
							const bool bReshaped = std::fabs(Shaped[k] - Natural[k]) > ReshapedBy || std::fabs(Shaped[k - 1] - Natural[k - 1]) > ReshapedBy;
							if (bReshaped && Rise > CliffSlope * GroundStep && NaturalRise < NaturalSlope * GroundStep)
							{
								RoadFault F = Fault(RoadDefect::OddGround, Points[k], Shaped[k], Rise - NaturalRise, EdgeIndex, AtJunction(i));
								F.Detail = Format("cliff %.0f-%.0f m from the road", (300.0 + (k - 1) * GroundStep) / 100.0, (300.0 + k * GroundStep) / 100.0);
								Found.push_back(F);
							}
							if (k < GroundSteps && bValid[k + 1])
							{
								const float Depth = std::min(Shaped[k - 1], Shaped[k + 1]) - Shaped[k];
								const float NaturalDepth = std::min(Natural[k - 1], Natural[k + 1]) - Natural[k];
								if (Shaped[k] < Natural[k] - ReshapedBy && Depth - std::max(0.0f, NaturalDepth) > TrenchDepth)
								{
									RoadFault F = Fault(RoadDefect::OddGround, Points[k], Shaped[k], Depth, EdgeIndex, AtJunction(i));
									F.Detail = Format("trench %.0f m from the road (seen %.1f/%.1f/%.1f, shaped %.1f/%.1f/%.1f, natural %.1f/%.1f/%.1f m, road %.1f)", (300.0 + k * GroundStep) / 100.0,
										Shaped[k - 1] / 100.0f, Shaped[k] / 100.0f, Shaped[k + 1] / 100.0f,
										Ground.GroundHeight(Points[k - 1]) / 100.0f, Ground.GroundHeight(Points[k]) / 100.0f, Ground.GroundHeight(Points[k + 1]) / 100.0f, Natural[k - 1] / 100.0f, Natural[k] / 100.0f, Natural[k + 1] / 100.0f,
										CentreHeights[size_t(i)] / 100.0f);
									Found.push_back(F);
								}
							}
						}
					}
				}
			}
		};
		if (ParallelFor)
		{
			ParallelFor(int32_t(Graph.Edges.size()), ScanEdge);
		}
		else
		{
			for (int32_t e = 0; e < int32_t(Graph.Edges.size()); ++e)
			{
				ScanEdge(e);
			}
		}

		// merge repeats: within a road, faults of one type close together are one fault (the worst of them)
		for (std::vector<RoadFault>& Found : PerEdge)
		{
			std::stable_sort(Found.begin(), Found.end(), [](const RoadFault& A, const RoadFault& B) { return A.Size > B.Size; });
			std::vector<RoadFault> Kept;
			for (const RoadFault& F : Found)
			{
				const bool bRepeat = std::any_of(Kept.begin(), Kept.end(), [&F](const RoadFault& Other)
				{
					return Other.Type == F.Type && (Other.Position - F.Position).SizeSquared() < MergeDistance * MergeDistance;
				});
				if (!bRepeat)
				{
					Kept.push_back(F);
				}
			}
			Out.Faults.insert(Out.Faults.end(), Kept.begin(), Kept.end());
		}
		std::stable_sort(Out.Faults.begin(), Out.Faults.end(), [](const RoadFault& A, const RoadFault& B)
		{
			return A.Type != B.Type ? A.Type < B.Type : A.Size > B.Size;
		});
		for (const double Length : Lengths)
		{
			Out.ScannedKm += Length / 100000.0;
		}
		Out.Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	}

	std::vector<std::string> RoadScanSummary(const RoadGraph& Graph, const geoground::Terrain& Ground, const RoadScanResult& Scan)
	{
		std::vector<std::string> Lines;
		Lines.push_back(Format("[RoadScan] %.0f km scanned in %.1f s: %d jolts, %d launches, %d grass through road, %d holes", Scan.ScannedKm, Scan.Seconds,
			Scan.Count(RoadDefect::Jolt), Scan.Count(RoadDefect::Launch), Scan.Count(RoadDefect::Grass), Scan.Count(RoadDefect::Hole)));
		Lines.push_back(Format("[RoadScan] patrol: %d stretches steeper than the law, %d dead ends in mid-air, %d spots of odd ground beside roads",
			Scan.Count(RoadDefect::Steep), Scan.Count(RoadDefect::DeadEnd), Scan.Count(RoadDefect::OddGround)));
		for (int32_t Type = 0; Type < int32_t(RoadDefect::Count); ++Type)
		{
			const int32_t AtJunctions = int32_t(std::count_if(Scan.Faults.begin(), Scan.Faults.end(), [Type](const RoadFault& F) { return int32_t(F.Type) == Type && F.bAtJunction; }));
			Lines.push_back(Format("[RoadScan]   %s: %d at junctions, %d along open road", TypeName(RoadDefect(Type)), AtJunctions, Scan.Count(RoadDefect(Type)) - AtJunctions));
		}
		const int32_t Gaps = int32_t(std::count_if(Scan.Faults.begin(), Scan.Faults.end(), [](const RoadFault& F) { return F.Type == RoadDefect::Hole && F.Size >= 10000.0f; }));
		const int32_t Lifted = int32_t(std::count_if(Scan.Faults.begin(), Scan.Faults.end(), [](const RoadFault& F) { return F.Type == RoadDefect::Hole && F.Size < 300.0f; }));
		Lines.push_back(Format("[RoadScan]   holes: %d with no surface at all, %d with a surface within 3 m of the road's height (pulled off by a neighbour), %d further",
			Gaps, Lifted, Scan.Count(RoadDefect::Hole) - Gaps - Lifted));

		int32_t Causes[3] = { 0, 0, 0 };
		for (const RoadFault& F : Scan.Faults)
		{
			Causes[std::min<int32_t>(F.Cause, 2)] += F.Type == RoadDefect::Jolt ? 1 : 0;
		}
		Lines.push_back(Format("[RoadScan]   jolts by surface: %d within a road's own surface, %d on junction surfaces, %d where another road overlaps", Causes[0], Causes[1], Causes[2]));
		for (uint8_t Cause = 0; Cause < 3; ++Cause)
		{
			int32_t Bins[4] = { 0, 0, 0, 0 };
			for (const RoadFault& F : Scan.Faults)
			{
				if (F.Type == RoadDefect::Jolt && F.Cause == Cause)
				{
					++Bins[F.Size < 5.0f ? 0 : F.Size < 10.0f ? 1 : F.Size < 25.0f ? 2 : 3];
				}
			}
			Lines.push_back(Format("[RoadScan]     cause %d sizes: %d under 5 cm, %d 5-10 cm, %d 10-25 cm, %d over 25 cm", Cause, Bins[0], Bins[1], Bins[2], Bins[3]));
		}

		// per road class, so the main roads can be judged on their own
		std::map<std::string, std::vector<int32_t>> PerClass;
		for (const RoadFault& F : Scan.Faults)
		{
			std::vector<int32_t>& Counts = PerClass[Graph.Edges[size_t(F.Edge)].Highway];
			Counts.resize(size_t(RoadDefect::Count), 0);
			++Counts[size_t(F.Type)];
		}
		for (const auto& [Highway, Counts] : PerClass)
		{
			std::string Line;
			for (size_t Type = 0; Type < Counts.size(); ++Type)
			{
				Line += Format("  %s %5d", TypeKey(RoadDefect(Type)), Counts[Type]);
			}
			Lines.push_back(Format("[RoadScan]   %-15s%s", Highway.c_str(), Line.c_str()));
		}

		// the worst jolts per cause, with the road's bend there (sharp bends fold the inside edge of a road)
		for (uint8_t Cause = 0; Cause < 3; ++Cause)
		{
			int32_t Shown = 0;
			for (const RoadFault& F : Scan.Faults)
			{
				if (F.Type != RoadDefect::Jolt || F.Cause != Cause || Shown >= 5)
				{
					continue;
				}
				++Shown;
				const RoadEdge& Edge = Graph.Edges[size_t(F.Edge)];
				double Turn = 0.0;
				for (size_t i = 1; i + 1 < Edge.Points.size(); ++i)
				{
					if (georoads::Distance(Edge.Points[i], F.Position) < 1500.0)
					{
						const Vec2 In = (Edge.Points[i] - Edge.Points[i - 1]).Normal(), Next = (Edge.Points[i + 1] - Edge.Points[i]).Normal();
						Turn = std::max(Turn, std::acos(std::clamp(georoads::Dot(In, Next), -1.0, 1.0)) * 180.0 / 3.14159265358979);
					}
				}
				double Lat = 0.0, Lon = 0.0;
				Ground.Projection.ToLatLon(F.Position, Lat, Lon);
				Lines.push_back(Format("[RoadScan]   jolt (cause %d) %.1f cm on %s (%s, %.1f m wide, %d points, sharpest turn nearby %.0f deg%s) at %.5f,%.5f",
					Cause, F.Size, Edge.Name.empty() ? "unnamed road" : Edge.Name.c_str(), Edge.Highway.c_str(), Edge.HalfWidth * 0.02f, int(Edge.Points.size()), Turn,
					F.bAtJunction ? ", at a junction" : "", Lat, Lon));
				if (!F.Detail.empty())
				{
					Lines.push_back("[RoadScan]     surface:" + F.Detail);
				}
			}
		}

		// the worst few of each type, with where to find them
		for (int32_t Type = 0; Type < int32_t(RoadDefect::Count); ++Type)
		{
			int32_t Shown = 0;
			for (const RoadFault& F : Scan.Faults)
			{
				if (int32_t(F.Type) != Type || Shown >= 6)
				{
					continue;
				}
				++Shown;
				const RoadEdge& Edge = Graph.Edges[size_t(F.Edge)];
				double Lat = 0.0, Lon = 0.0;
				Ground.Projection.ToLatLon(F.Position, Lat, Lon);
				const bool bDetail = F.Type >= RoadDefect::Steep && !F.Detail.empty();
				Lines.push_back(Format("[RoadScan]   worst %s: %.1f %s on %s (%s%s) at %.5f,%.5f%s%s", TypeName(F.Type), F.Size, SizeUnit(F.Type),
					Edge.Name.empty() ? "unnamed road" : Edge.Name.c_str(), Edge.Highway.c_str(), Edge.bBridge ? ", bridge" : "", Lat, Lon,
					bDetail ? ", " : "", bDetail ? F.Detail.c_str() : ""));
			}
		}
		return Lines;
	}

	std::string RoadScanCsv(const RoadGraph& Graph, const geoground::Terrain& Ground, const RoadScanResult& Scan)
	{
		std::string Csv = "type,size,unit,lat,lon,highway,name,bridge,junction,detail\n";
		for (const RoadFault& F : Scan.Faults)
		{
			const RoadEdge& Edge = Graph.Edges[size_t(F.Edge)];
			double Lat = 0.0, Lon = 0.0;
			Ground.Projection.ToLatLon(F.Position, Lat, Lon);
			Csv += Format("%s,%.1f,%s,%.6f,%.6f,%s,\"%s\",%d,%d,\"%s\"\n", TypeKey(F.Type), F.Size, SizeUnit(F.Type), Lat, Lon, Edge.Highway.c_str(),
				Quoted(Edge.Name).c_str(), Edge.bBridge ? 1 : 0, F.bAtJunction ? 1 : 0, Quoted(F.Detail).c_str());
		}
		return Csv;
	}
}
