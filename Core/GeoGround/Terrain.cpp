// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Terrain.h"
#include "Vegetation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iterator>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace geoground
{
	using georoads::RoadEdge;
	using georoads::RoadGraph;
	using georoads::RoadNode;

	namespace
	{
		constexpr float NoWater = -1.0e9f;
		constexpr float Sqrt2 = 1.4142135623730950488016887242097f;	// (a float, as the game had it)

		/** Road heights are sampled this often along each road (cm), and output at twice that spacing */
		constexpr double ProfileStep = 500.0;

		/** Forest canopy: eased in over this far from the forest edge (cm); at most this tall, or this when it can't be measured */
		constexpr float CanopyRamp = 6000.0f;
		constexpr float MaxCanopy = 2000.0f;
		constexpr float DefaultCanopy = 800.0f;

		/** Beside the road (its verge) the ground sits this far below the road edge (cm): a tarred edge, not a ledge */
		constexpr float CarveDepth = 8.0f;

		/** In town: a kerb this high (cm, UK 100-125 mm) whose top is this far out from the road edge, and the pavement
		 *  beyond rising this much over the verge width (2%). From the LiDAR cross-sections (docs/sections). */
		constexpr float KerbHeight = 12.0f;
		constexpr float KerbOut = 30.0f;
		constexpr float PavementRise = 3.0f;

		/** A kerb drops (back to the verge) within this distance of another road's edge (cm): junction mouths, crossings */
		constexpr float KerbClear = 500.0f;

		/** Ground within 5 cm above or this far below sea level is put this far under it (cm), clear of the sea surface */
		constexpr float SeaClear = 50.0f;

		/** Under a bridge the ground stays at least this far below the deck (cm), rising at 45 degrees beside it, over
		 *  BridgeReach beyond its edge */
		constexpr float BridgeClearance = 300.0f;
		constexpr float TunnelCover = 850.0f;		// cm over a tunnel's road: its 6.5 m tube and 2 m of earth
		constexpr float TunnelCoverSlope = 0.6f;	// its sides fall this much a cm out (~31 degrees)
		constexpr float TunnelCoverReach = 1500.0f;	// cm beside it the cover can reach
		constexpr float TunnelCut = 700.0f;		// cm over its road the ground's cut away in its tube (its roof 6.5 m)
		constexpr float BridgeReach = 2000.0f;

		/** Under the road itself the ground sits this far down, out of sight below the road and its side skirts (cm) */
		constexpr float BedDepth = 50.0f;

		/** Ground may come no closer than this under a road surface anywhere (cm); a ground triangle that does is left out */
		constexpr float GroundUnderRoad = 1.0f;

		/** A corridor piece with another road under it is cut into this many pieces each way, each point kept under the road */
		constexpr int32_t QuadCuts = 8;

		/** Road corridors: rows across the road this far apart near the car (cm, three times that further out); the strip
		 *  of natural ground beyond the earthworks that blends into the land; how far the grid sinks under a corridor,
		 *  per cm of grid spacing */
		constexpr double CorridorRowStep = 1000.0;
		constexpr float CorridorBlend = 1500.0f;
		constexpr float CorridorSinkPerSpacing = 0.8f;

		/** The verge: flat ground this wide beside the road edge (cm), before the cutting or embankment starts */
		constexpr float Verge = 150.0f;

		/** Cuttings rise 1:1.5 (steeper on steep hillsides, up to 1:0.67 like rock) and embankments fall 1:2 */
		constexpr float CutSlope = 0.67f;
		constexpr float MaxCutSlope = 1.5f;
		constexpr float FillSlope = 0.5f;

		/** Earthworks reach 5-60 m beyond the verge (cm), and hand back to the natural ground over the last FadeLength */
		constexpr float MinReach = 500.0f;
		constexpr float MaxReach = 6000.0f;
		constexpr float FadeLength = 1000.0f;

		/** "Land" style: the ground beside a road eases back to natural over twice the height it has to make up, 5-30 m */
		constexpr float LandMaxBlend = 3000.0f;

		/** Lakes: ground inside is pushed this far below the surface (cm) */
		constexpr float LakeDepth = 50.0f;

		/** Ground colours (linear; squared, roughly, as they end up gamma-encoded on the way to the material) */
		const Vec4f GrassColor{ 0.020f, 0.045f, 0.008f, 1.0f };
		const Vec4f ForestColor{ 0.004f, 0.016f, 0.003f, 1.0f };
		const Vec4f SandColor{ 0.30f, 0.23f, 0.11f, 1.0f };
		const Vec4f RockColor{ 0.045f, 0.040f, 0.033f, 1.0f };
		const Vec4f SeabedColor{ 0.10f, 0.08f, 0.04f, 1.0f };

		uint64_t Key(int32_t X, int32_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); }
		double SmoothStepD(double A, double B, double X)
		{
			if (X < A) { return 0.0; }
			if (X >= B) { return 1.0; }
			const double T = (X - A) / (B - A);
			return T * T * (3.0 - 2.0 * T);
		}
		double Seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

		/** Closed OSM ways as polygons */
		bool WayPolygon(const georoads::OsmData& Data, const georoads::OsmWay& Way, std::vector<Vec2>& Out)
		{
			if (Way.Nodes.size() < 4 || Way.Nodes[0] != Way.Nodes.back())
			{
				return false;
			}
			for (size_t i = 0; i + 1 < Way.Nodes.size(); ++i)
			{
				const auto Found = Data.Nodes.find(Way.Nodes[i]);
				if (Found != Data.Nodes.end())
				{
					Out.push_back(Found->second);
				}
			}
			return Out.size() >= 3;
		}

		/** Median over a 7-sample window removes single-sample spikes from the elevation data */
		void Median(std::vector<float>& Values)
		{
			const std::vector<float> Source = Values;
			for (int32_t i = 0; i < static_cast<int32_t>(Values.size()); ++i)
			{
				std::array<float, 7> Window;
				int32_t Count = 0;
				for (int32_t j = std::max(0, i - 3); j <= std::min(static_cast<int32_t>(Values.size()) - 1, i + 3); ++j)
				{
					Window[Count++] = Source[j];
				}
				std::sort(Window.begin(), Window.begin() + Count);
				Values[i] = Window[Count / 2];
			}
		}

		/** Distance from a point to a segment (cm) */
		double SegmentDistance(const Vec2& P, const Vec2& A, const Vec2& B)
		{
			const Vec2 AB = B - A;
			const double LengthSquared = AB.SizeSquared();
			const double T = LengthSquared > 1.0 ? std::clamp(georoads::Dot(P - A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
			return georoads::Distance(A + AB * T, P);
		}

		/** The same as the engines measure it in 3D (with z = 0): to the nearest point of the segment */
		double PointDistToSegment(const Vec2& P, const Vec2& Start, const Vec2& End)
		{
			const Vec2 Segment = End - Start, ToPoint = P - Start;
			const double Dot1 = georoads::Dot(ToPoint, Segment);
			Vec2 Closest;
			if (Dot1 <= 0.0)
			{
				Closest = Start;
			}
			else
			{
				const double Dot2 = georoads::Dot(Segment, Segment);
				Closest = Dot2 <= Dot1 ? End : Start + Segment * (Dot1 / Dot2);
			}
			return (P - Closest).Size();
		}

		/** How far from a road's middle (cm) the grid sinks under its corridor: its earthworks, and at least far enough
		 *  that no grid triangle reaching the road keeps a corner up */
		double SinkRadius(float HalfWidth, float Reach, double Spacing)
		{
			return std::max(static_cast<double>(HalfWidth + Verge + Reach), HalfWidth + Sqrt2 * Spacing);
		}

		/** How far beyond that the corridor reaches (cm): more than a grid triangle, so every sunk triangle is covered */
		double CoverMargin(double Spacing)
		{
			return std::max(static_cast<double>(CorridorBlend), Sqrt2 * Spacing + 100.0);
		}
	}

	void Terrain::Log(const std::string& Line) const
	{
		if (Options.Heights.Log)
		{
			Options.Heights.Log(Line);
		}
	}

	const georoads::ParallelForFn& Terrain::ParallelFor() const
	{
		static const georoads::ParallelForFn Threads(&georoads::ThreadsParallelFor);
		return Options.Heights.ParallelFor ? Options.Heights.ParallelFor : Threads;
	}

	float Terrain::DemHeight(const Vec2& Position) const
	{
		if (!Dem)
		{
			return 0.0f;
		}
		double Lat, Lon;
		Projection.ToLatLon(Position, Lat, Lon);
		const float Height = Dem->Sample(Lat, Lon) * 100.0f;
		// ground right at sea level would sit exactly on the sea surface (0 m) and flicker through it in patches: just
		// under it goes a little lower, so the sea is always clearly on top
		return Height > -SeaClear && Height < 5.0f ? -SeaClear : Height;
	}

	uint8_t Terrain::CoverAt(const Vec2& Position) const
	{
		const int32_t X = FloorToInt((Position.X - MaskOrigin.X) / MaskCell);
		const int32_t Y = FloorToInt((Position.Y - MaskOrigin.Y) / MaskCell);
		return (X >= 0 && Y >= 0 && X < MaskWidth && Y < MaskHeight) ? CoverMask[size_t(Y) * MaskWidth + X] : Grass;
	}

	bool Terrain::IsWater(const Vec2& Position) const
	{
		return WaterLevelAt(Position) != NoWater;
	}

	float Terrain::WaterLevelAt(const Vec2& Position) const
	{
		const int32_t X = FloorToInt((Position.X - MaskOrigin.X) / MaskCell);
		const int32_t Y = FloorToInt((Position.Y - MaskOrigin.Y) / MaskCell);
		return (X >= 0 && Y >= 0 && X < MaskWidth && Y < MaskHeight) ? WaterLevel[size_t(Y) * MaskWidth + X] : NoWater;
	}

	void Terrain::BuildMasks(const georoads::OsmData& Data)
	{
		MaskOrigin = Bounds.Min;
		MaskWidth = std::max(1, CeilToInt(Bounds.Size().X / MaskCell));
		MaskHeight = std::max(1, CeilToInt(Bounds.Size().Y / MaskCell));
		CoverMask.assign(size_t(MaskWidth) * MaskHeight, 0);
		WaterLevel.assign(size_t(MaskWidth) * MaskHeight, NoWater);

		// buildings per cell (their middles), for telling town roads from country ones
		BuildingCount.assign(size_t(MaskWidth) * MaskHeight, 0);
		for (const georoads::OsmWay& Way : Data.Ways)
		{
			if (!Way.Tag("building") || Way.Nodes.empty())
			{
				continue;
			}
			Vec2 Sum;
			int32_t Count = 0;
			for (const int64_t Id : Way.Nodes)
			{
				const auto Found = Data.Nodes.find(Id);
				if (Found != Data.Nodes.end())
				{
					Sum = Sum + Found->second;
					++Count;
				}
			}
			if (Count > 0)
			{
				const Vec2 Middle = Sum * (1.0 / Count);	// (by the reciprocal, as the engines divide vectors)
				const int32_t X = FloorToInt((Middle.X - MaskOrigin.X) / MaskCell), Y = FloorToInt((Middle.Y - MaskOrigin.Y) / MaskCell);
				if (X >= 0 && Y >= 0 && X < MaskWidth && Y < MaskHeight && BuildingCount[size_t(Y) * MaskWidth + X] < UINT16_MAX)
				{
					++BuildingCount[size_t(Y) * MaskWidth + X];
				}
			}
		}

		// scanline fill: for each mask row, find where the polygon's edges cross it and fill between pairs
		auto Fill = [this](const std::vector<Vec2>& Polygon, const auto& SetCell)
		{
			Box2 Box;
			for (const Vec2& P : Polygon)
			{
				Box += P;
			}
			const int32_t RowMin = std::max(0, FloorToInt((Box.Min.Y - MaskOrigin.Y) / MaskCell));
			const int32_t RowMax = std::min(MaskHeight - 1, FloorToInt((Box.Max.Y - MaskOrigin.Y) / MaskCell));
			std::vector<double> Crossings;
			for (int32_t Row = RowMin; Row <= RowMax; ++Row)
			{
				const double Y = MaskOrigin.Y + (Row + 0.5) * MaskCell;
				Crossings.clear();
				for (size_t i = 0; i < Polygon.size(); ++i)
				{
					const Vec2& A = Polygon[i];
					const Vec2& B = Polygon[(i + 1) % Polygon.size()];
					if ((A.Y <= Y) != (B.Y <= Y))
					{
						Crossings.push_back(A.X + (Y - A.Y) / (B.Y - A.Y) * (B.X - A.X));
					}
				}
				std::sort(Crossings.begin(), Crossings.end());
				for (size_t i = 0; i + 1 < Crossings.size(); i += 2)
				{
					const int32_t ColMin = std::max(0, CeilToInt((Crossings[i] - MaskOrigin.X) / MaskCell - 0.5));
					const int32_t ColMax = std::min(MaskWidth - 1, FloorToInt((Crossings[i + 1] - MaskOrigin.X) / MaskCell - 0.5));
					for (int32_t Col = ColMin; Col <= ColMax; ++Col)
					{
						SetCell(Row * MaskWidth + Col);
					}
				}
			}
		};

		for (const georoads::OsmWay& Way : Data.Ways)
		{
			const std::string* Natural = Way.Tag("natural");
			const std::string* Landuse = Way.Tag("landuse");
			const bool bWater = Natural && *Natural == "water";
			const bool bForest = (Landuse && *Landuse == "forest") || (Natural && *Natural == "wood");
			const bool bBeach = Natural && (*Natural == "beach" || *Natural == "sand");
			if (!bWater && !bForest && !bBeach)
			{
				continue;
			}

			std::vector<Vec2> Polygon;
			if (!WayPolygon(Data, Way, Polygon))
			{
				continue;
			}

			if (bWater)
			{
				// a lake's surface sits at its lowest shoreline height (estuaries end up at about sea level)
				float Level = std::numeric_limits<float>::max();
				for (const Vec2& Point : Polygon)
				{
					Level = std::min(Level, DemHeight(Point));
				}
				Level = std::max(Level, 0.0f);
				Fill(Polygon, [this, Level](int32_t Cell) { WaterLevel[Cell] = WaterLevel[Cell] == NoWater ? Level : std::min(WaterLevel[Cell], Level); });

				if (SignedArea(Polygon) < 0.0)
				{
					std::reverse(Polygon.begin(), Polygon.end());
				}
				WaterPolygons.emplace_back(std::move(Polygon), Level);
			}
			else
			{
				const uint8_t Value = bForest ? Forest : Beach;
				Fill(Polygon, [this, Value](int32_t Cell) { CoverMask[Cell] = Value; });
			}
		}

		NumWaterPolygons = static_cast<int32_t>(WaterPolygons.size());
		NumForestCells = static_cast<int32_t>(std::count(CoverMask.begin(), CoverMask.end(), uint8_t(Forest)));

		// how deep into a forest each cell is (cm to the nearest open cell): two-pass chamfer distance
		ForestDepth.assign(size_t(MaskWidth) * MaskHeight, 0.0f);
		const float Straight = static_cast<float>(MaskCell), Diagonal = static_cast<float>(MaskCell * Sqrt2);
		for (size_t i = 0; i < CoverMask.size(); ++i)
		{
			ForestDepth[i] = CoverMask[i] == Forest ? std::numeric_limits<float>::max() : 0.0f;
		}
		auto Relax = [&](int32_t X, int32_t Y, int32_t DX, int32_t DY, float Cost)
		{
			const int32_t NX = X + DX, NY = Y + DY;
			if (NX >= 0 && NY >= 0 && NX < MaskWidth && NY < MaskHeight)
			{
				float& Depth = ForestDepth[size_t(Y) * MaskWidth + X];
				Depth = std::min(Depth, ForestDepth[size_t(NY) * MaskWidth + NX] + Cost);
			}
		};
		for (int32_t Y = 0; Y < MaskHeight; ++Y)
		{
			for (int32_t X = 0; X < MaskWidth; ++X)
			{
				Relax(X, Y, -1, 0, Straight); Relax(X, Y, 0, -1, Straight); Relax(X, Y, -1, -1, Diagonal); Relax(X, Y, 1, -1, Diagonal);
			}
		}
		for (int32_t Y = MaskHeight - 1; Y >= 0; --Y)
		{
			for (int32_t X = MaskWidth - 1; X >= 0; --X)
			{
				Relax(X, Y, 1, 0, Straight); Relax(X, Y, 0, 1, Straight); Relax(X, Y, 1, 1, Diagonal); Relax(X, Y, -1, 1, Diagonal);
			}
		}
		for (float& Depth : ForestDepth)
		{
			Depth = std::min(Depth, 1.0e6f);	// a forest touching nothing open (the whole area) still has a finite depth
		}
	}

	float Terrain::CanopyAt(const Vec2& Position) const
	{
		if (CanopyHeight <= 0.0f || ForestDepth.empty())
		{
			return 0.0f;
		}
		// depth sampled bilinearly between cell centres, so the correction has no steps at cell edges
		const double FX = (Position.X - MaskOrigin.X) / MaskCell - 0.5, FY = (Position.Y - MaskOrigin.Y) / MaskCell - 0.5;
		const int32_t X0 = std::clamp(FloorToInt(FX), 0, MaskWidth - 1), Y0 = std::clamp(FloorToInt(FY), 0, MaskHeight - 1);
		const int32_t X1 = std::min(X0 + 1, MaskWidth - 1), Y1 = std::min(Y0 + 1, MaskHeight - 1);
		const float TX = static_cast<float>(std::clamp(FX - X0, 0.0, 1.0)), TY = static_cast<float>(std::clamp(FY - Y0, 0.0, 1.0));
		const float Depth = LerpF(LerpF(ForestDepth[size_t(Y0) * MaskWidth + X0], ForestDepth[size_t(Y0) * MaskWidth + X1], TX),
			LerpF(ForestDepth[size_t(Y1) * MaskWidth + X0], ForestDepth[size_t(Y1) * MaskWidth + X1], TX), TY);
		const float Canopy = CanopyHeight * SmoothStepF(0.0f, CanopyRamp, Depth);
		// bare-earth LiDAR has no trees on it: nothing to take off where it is used
		if (Canopy > 0.0f && Dem && Dem->HasFine() && !Dem->bIgnoreFine)
		{
			double Lat, Lon;
			Projection.ToLatLon(Position, Lat, Lon);
			float Measured;
			if (Dem->SampleFine(Lat, Lon, Measured))
			{
				return 0.0f;
			}
		}
		return Canopy;
	}

	float Terrain::BareHeight(const Vec2& Position) const
	{
		return DemHeight(Position) - CanopyAt(Position);
	}

	bool Terrain::ApplyRoadBake(RoadGraph& Graph, const std::vector<std::vector<Vec2>>& SamplePoints, const RoadBake& Bake) const
	{
		const double Start = Seconds();

		// every road piece must be in the bake with the same points (the same OSM data and sampling), or none are used:
		// the heights only fit together as one solved network
		std::vector<std::vector<Vec2>> Points(Graph.Edges.size());
		std::vector<const std::vector<float>*> Heights(Graph.Edges.size(), nullptr);
		int32_t Missing = 0, Different = 0;
		for (size_t EdgeIndex = 0; EdgeIndex < Graph.Edges.size(); ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			const std::pair<int64_t, int32_t> Way(Edge.WayId, Edge.WayPart);
			const auto Baked = Bake.Heights.find(Way);
			if (Baked == Bake.Heights.end())
			{
				++Missing;
				continue;
			}
			const std::vector<Vec2>& All = SamplePoints[EdgeIndex];
			for (size_t i = 0; i < All.size(); ++i)
			{
				if (i % 2 == 0 || i == All.size() - 1)
				{
					Points[EdgeIndex].push_back(All[i]);
				}
			}
			const auto BakedStart = Bake.Starts.find(Way);
			const std::pair<double, double> StartLatLon = BakedStart != Bake.Starts.end() ? BakedStart->second : std::pair<double, double>(0.0, 0.0);
			if (Baked->second.size() != Points[EdgeIndex].size()
				|| georoads::Distance(Projection.ToWorld(StartLatLon.first, StartLatLon.second), Points[EdgeIndex][0]) > 100.0)
			{
				++Different;
				continue;
			}
			Heights[EdgeIndex] = &Baked->second;
		}
		if (Missing > 0 || Different > 0)
		{
			Log(georoads::Format("[Heights] baked roads not used: %d of %d road pieces missing from the bake, %d different (solving instead)",
				Missing, int32_t(Graph.Edges.size()), Different));
			return false;
		}

		for (size_t EdgeIndex = 0; EdgeIndex < Graph.Edges.size(); ++EdgeIndex)
		{
			Graph.Edges[EdgeIndex].Points = std::move(Points[EdgeIndex]);
			Graph.Edges[EdgeIndex].Heights = *Heights[EdgeIndex];
		}
		// junction heights from the pieces meeting there (the solver made them meet; the bake rounds to whole cm)
		float WorstJoin = 0.0f;
		for (size_t NodeIndex = 0; NodeIndex < Graph.Nodes.size(); ++NodeIndex)
		{
			RoadNode& Node = Graph.Nodes[NodeIndex];
			bool bFirst = true;
			for (const int32_t EdgeIndex : Node.Edges)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const float End = Edge.StartNode == static_cast<int32_t>(NodeIndex) ? Edge.Heights[0] : Edge.Heights.back();
				if (bFirst)
				{
					Node.Height = End;
					bFirst = false;
				}
				WorstJoin = std::max(WorstJoin, std::abs(End - Node.Height));
			}
		}
		CanopyHeight = Bake.CanopyHeight;
		Log(georoads::Format("[Heights] baked roads used: %d road pieces, canopy %.1f m, worst join %.0f cm, %.2f s (no solve)",
			int32_t(Graph.Edges.size()), CanopyHeight / 100.0f, WorstJoin, Seconds() - Start));
		return true;
	}

	void Terrain::ComputeRoadProfiles(RoadGraph& Graph, const RoadBake* Bake) const
	{
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());

		// --- 1. sample every road every 5 m ---
		std::vector<std::vector<Vec2>> SamplePoints(NumEdges);
		std::vector<std::vector<float>> SampleHeights(NumEdges);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			std::vector<Vec2>& Points = SamplePoints[EdgeIndex];

			// resample, always keeping the original corner points so the road shape doesn't change
			Points.push_back(Edge.Points[0]);
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				const Vec2 A = Edge.Points[i];
				const Vec2 B = Edge.Points[i + 1];
				const int32_t Steps = std::max(1, CeilToInt(georoads::Distance(A, B) / ProfileStep));
				for (int32_t Step = 1; Step <= Steps; ++Step)
				{
					Points.push_back(A + (B - A) * (static_cast<double>(Step) / Steps));
				}
			}
		}

		// baked roads: the solved heights are already known
		if (Bake && ApplyRoadBake(Graph, SamplePoints, *Bake))
		{
			return;
		}

		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			for (const Vec2& Point : SamplePoints[EdgeIndex])
			{
				SampleHeights[EdgeIndex].push_back(DemHeight(Point));
			}
		}

		// roads on measured ground (LiDAR under 80% of them or more): their heights are trusted (RoadDesign)
		const bool bFine = Dem && Dem->HasFine() && !Dem->bIgnoreFine;
		int32_t MeasuredRoads = 0;
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			RoadEdge& Edge = Graph.Edges[EdgeIndex];
			int32_t Measured = 0;
			if (bFine && !Edge.bBridge && !Edge.bTunnel)
			{
				for (const Vec2& Point : SamplePoints[EdgeIndex])
				{
					double Lat, Lon;
					Projection.ToLatLon(Point, Lat, Lon);
					float Height;
					Measured += Dem->SampleFine(Lat, Lon, Height) ? 1 : 0;
				}
			}
			Edge.bMeasured = bFine && !Edge.bBridge && !Edge.bTunnel && Measured >= 0.8 * SamplePoints[EdgeIndex].size();
			MeasuredRoads += Edge.bMeasured ? 1 : 0;
		}
		if (bFine)
		{
			Log(georoads::Format("[Heights] %d of %d road pieces on measured ground (LiDAR): followed closely, comfort curves only", MeasuredRoads, NumEdges));
		}

		// --- 2. the elevation data reads the tops of trees in forest. How tall they are is measured where roads cross a
		// short stretch of forest with open ground either side: the forest reads this much higher than a straight line
		// between the two (the median over every such crossing). The canopy is then taken off the elevation everywhere in
		// forest, easing in from the forest edge, for the roads and the ground alike. ---
		{
			constexpr int32_t MinRun = 20, MaxRun = 80, EdgeSkip = 8;	// 100-400 m of forest, ignoring 40 m at each end
			std::vector<float> Estimates;
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const std::vector<Vec2>& Points = SamplePoints[EdgeIndex];
				const std::vector<float>& Heights = SampleHeights[EdgeIndex];
				int32_t RunStart = -1;
				for (int32_t i = 0; i < static_cast<int32_t>(Points.size()); ++i)
				{
					const bool bForest = CoverAt(Points[i]) == Forest;
					if (bForest && RunStart == -1)
					{
						RunStart = i;
					}
					if (bForest || RunStart == -1)
					{
						continue;
					}
					// a run of forest from RunStart to i - 1, open at RunStart - 1 and at i
					const int32_t Length = i - RunStart;
					if (RunStart > 0 && Length >= MinRun && Length <= MaxRun)
					{
						const float Before = Heights[RunStart - 1], After = Heights[i];
						std::vector<float> Residuals;
						for (int32_t j = RunStart + EdgeSkip; j < i - EdgeSkip; ++j)
						{
							Residuals.push_back(Heights[j] - LerpF(Before, After, static_cast<float>(j - RunStart + 1) / (Length + 1)));
						}
						std::sort(Residuals.begin(), Residuals.end());
						Estimates.push_back(Residuals[Residuals.size() / 2]);
					}
					RunStart = -1;
				}
			}
			std::sort(Estimates.begin(), Estimates.end());
			CanopyHeight = Estimates.size() >= 20 ? std::clamp(Estimates[Estimates.size() / 2], 0.0f, MaxCanopy) : DefaultCanopy;
			if (ForcedCanopy >= 0.0f)
			{
				CanopyHeight = ForcedCanopy;	// a piece next to one already loaded: the same ground under the trees
			}
			Log(georoads::Format("[Heights] forest canopy reads %.1f m above the ground (from %d forest crossings%s)",
				CanopyHeight / 100.0f, int32_t(Estimates.size()), Estimates.size() >= 20 ? "" : ", too few: using the default"));
		}

		// --- 3. canopy off, then single-sample spikes removed ---
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			std::vector<float>& Heights = SampleHeights[EdgeIndex];
			for (size_t i = 0; i < Heights.size(); ++i)
			{
				Heights[i] -= CanopyAt(SamplePoints[EdgeIndex][i]);
			}
			Median(Heights);
		}

		// --- 4. the whole network solved together: close to the ground, but slopes flow smoothly through every join and
		// junction. Every other sample (10 m) plus the ends is plenty. ---
		std::vector<std::vector<Vec2>> Points(NumEdges);
		std::vector<std::vector<float>> Measured(NumEdges);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const std::vector<Vec2>& All = SamplePoints[EdgeIndex];
			for (size_t i = 0; i < All.size(); ++i)
			{
				if (i % 2 == 0 || i == All.size() - 1)
				{
					Points[EdgeIndex].push_back(All[i]);
					Measured[EdgeIndex].push_back(SampleHeights[EdgeIndex][i]);
				}
			}
		}

		std::vector<std::vector<float>> Solved;
		georoads::HeightOptions HeightOptions = Options.Heights;
		HeightOptions.bFollowLand = Options.bFollowLand;
		georoads::SolveHeights(Graph, Points, Measured, Solved, HeightOptions);
		LogFollow(Graph, Points, Solved);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			Graph.Edges[EdgeIndex].Points = std::move(Points[EdgeIndex]);
			Graph.Edges[EdgeIndex].Heights = std::move(Solved[EdgeIndex]);
		}
	}

	void Terrain::LogFollow(const RoadGraph& Graph, const std::vector<std::vector<Vec2>>& Points, const std::vector<std::vector<float>>& Solved) const
	{
		if (!Options.Heights.Log)
		{
			return;
		}
		// how well the roads follow the land: stretches where a road runs well above the raw elevation data (floating
		// over a dip the real road goes down into) or below it (cutting through a rise), longest first. Bridges are left out.
		struct Stretch { int32_t Edge; Vec2 Where; double Length; float Worst; float ForestShare; };
		std::vector<Stretch> Stretches;
		double Total[2] = { 0.0, 0.0 };	// km over 3 m, over 10 m
		for (int32_t EdgeIndex = 0; EdgeIndex < static_cast<int32_t>(Graph.Edges.size()); ++EdgeIndex)
		{
			if (Graph.Edges[EdgeIndex].bBridge || Graph.Edges[EdgeIndex].bTunnel)
			{
				continue;
			}
			const std::vector<Vec2>& P = Points[EdgeIndex];
			Stretch Current{ EdgeIndex, Vec2(), 0.0, 0.0f, 0.0f };
			int32_t InForest = 0, Count = 0;
			for (size_t i = 0; i < P.size(); ++i)
			{
				const float Off = Solved[EdgeIndex][i] - BareHeight(P[i]);
				const double Step = i > 0 ? georoads::Distance(P[i - 1], P[i]) : 0.0;
				if (std::abs(Off) > 300.0f)
				{
					Total[0] += Step / 100000.0;
					Total[1] += std::abs(Off) > 1000.0f ? Step / 100000.0 : 0.0;
					if (std::abs(Off) > std::abs(Current.Worst))
					{
						Current.Worst = Off;
						Current.Where = P[i];
					}
					Current.Length += Step;
					InForest += CoverAt(P[i]) == Forest ? 1 : 0;
					++Count;
					continue;
				}
				if (Current.Length > 0.0)
				{
					Current.ForestShare = static_cast<float>(InForest) / std::max(Count, 1);
					Stretches.push_back(Current);
				}
				Current = Stretch{ EdgeIndex, Vec2(), 0.0, 0.0f, 0.0f };
				InForest = Count = 0;
			}
			if (Current.Length > 0.0)
			{
				Current.ForestShare = static_cast<float>(InForest) / std::max(Count, 1);
				Stretches.push_back(Current);
			}
		}
		std::sort(Stretches.begin(), Stretches.end(), [](const Stretch& A, const Stretch& B) { return A.Length > B.Length; });
		const int32_t Floating = static_cast<int32_t>(std::count_if(Stretches.begin(), Stretches.end(), [](const Stretch& S) { return S.Worst > 0.0f; }));
		const int32_t Forested = static_cast<int32_t>(std::count_if(Stretches.begin(), Stretches.end(), [](const Stretch& S) { return S.ForestShare > 0.5f; }));
		Log(georoads::Format("[Follow] roads off the elevation data by over 3 m: %.1f km (over 10 m: %.1f km) in %d stretches, %d above the ground, %d below, %d mostly in forest",
			Total[0], Total[1], int32_t(Stretches.size()), Floating, int32_t(Stretches.size()) - Floating, Forested));
		for (size_t i = 0; i < std::min<size_t>(10, Stretches.size()); ++i)
		{
			const Stretch& S = Stretches[i];
			const RoadEdge& Edge = Graph.Edges[S.Edge];
			double Lat = 0.0, Lon = 0.0;
			Projection.ToLatLon(S.Where, Lat, Lon);
			Log(georoads::Format("[Follow]   %.0f m %s by up to %.1f m on %s (%s), %.0f%% forest, at %.5f,%.5f",
				S.Length / 100.0, S.Worst > 0.0f ? "above" : "below", std::abs(S.Worst) / 100.0f,
				Edge.Name.empty() ? "unnamed road" : Edge.Name.c_str(), Edge.Highway.c_str(), S.ForestShare * 100.0f, Lat, Lon));
		}
	}

	void Terrain::BuildRoadIndex(std::shared_ptr<const RoadGraph> Graph)
	{
		Roads = Graph;
		bFollowLand = Options.bFollowLand;
		Surface = std::make_unique<georoads::RoadSurface>(*Graph);
		const int32_t NumEdges = static_cast<int32_t>(Graph->Edges.size());
		EdgeReach.assign(NumEdges, 0.0f);
		EdgeKerb.assign(NumEdges, 0.0f);
		Segments.clear();
		SegmentGrid.clear();
		RoadGrid.clear();

		// the portal cut at each end of each tunnel: up to 30 m in (half a short tunnel), the road's width and a little more
		Portals.clear();
		Tubes.clear();
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph->Edges[EdgeIndex];
			if (!Edge.bTunnel || Edge.Points.size() < 2)
			{
				continue;
			}
			double Length = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				Length += georoads::Distance(Edge.Points[i], Edge.Points[i + 1]);
			}
			for (const bool bStart : { true, false })
			{
				// (only where it comes out: where it carries on into another tunnel piece there's no mouth)
				const int32_t Node = bStart ? Edge.StartNode : Edge.EndNode;
				int32_t Underground = 0;
				if (Node >= 0 && Node < int32_t(Graph->Nodes.size()))
				{
					for (const int32_t Other : Graph->Nodes[Node].Edges)
					{
						Underground += Graph->Edges[Other].bTunnel ? 1 : 0;
					}
				}
				if (Underground >= 2)
				{
					continue;
				}
				Portal Cut;
				Cut.Mouth = bStart ? Edge.Points[0] : Edge.Points.back();
				const Vec2 Next = bStart ? Edge.Points[1] : Edge.Points[Edge.Points.size() - 2];
				Cut.Inward = (Next - Cut.Mouth).Normal();
				Cut.HalfWidth = Edge.HalfWidth + 150.0f;
				Cut.Length = static_cast<float>(std::min(3000.0, Length * 0.5));
				Cut.Edge = EdgeIndex;
				Portals.push_back(Cut);
			}
			Tube& Bore = Tubes.emplace_back();
			Bore.HalfWidth = Edge.HalfWidth + 150.0f;
			Bore.Edge = EdgeIndex;
			for (const Vec2& Point : Edge.Points)
			{
				Bore.Bounds += Point;
			}
			Bore.Bounds = Bore.Bounds.ExpandBy(Bore.HalfWidth);
		}
		int32_t TownRoads = 0;

		// 1. every road's segments, in road order
		std::vector<int32_t> EdgeFirst(NumEdges + 1, -1);
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph->Edges[EdgeIndex];
			EdgeFirst[EdgeIndex] = static_cast<int32_t>(Segments.size());
			// (a tunnel shapes no ground: the mountain above it stays, and nothing is "over the road" there)
			if (Edge.bTunnel || Edge.Heights.size() != Edge.Points.size() || Edge.Points.size() < 2)
			{
				continue;
			}
			double EdgeLength = 0.0, Along = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				EdgeLength += georoads::Distance(Edge.Points[i], Edge.Points[i + 1]);
			}
			// in town (buildings close by its middle and both ends), a street or main road gets a kerb and a pavement;
			// motorways, service roads and tracks don't
			const bool bKerbed = Options.bKerbs && !Edge.bBridge && !Edge.bTunnel && !georoads::StartsWith(Edge.Highway, "motorway") && Edge.Highway != "service" && Edge.Highway != "track"
				&& IsTown(Edge.Points[0]) && IsTown(Edge.Points.back()) && IsTown(Edge.Points[Edge.Points.size() / 2]);
			EdgeKerb[EdgeIndex] = bKerbed ? KerbHeight : 0.0f;
			TownRoads += bKerbed ? 1 : 0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				RoadSegment& Segment = Segments.emplace_back();
				Segment.A = Edge.Points[i];
				Segment.B = Edge.Points[i + 1];
				// as wide as the road is along this segment (wider or narrower where it tapers)
				const double Next = Along + georoads::Distance(Segment.A, Segment.B);
				Segment.HalfWidth = std::max(Edge.HalfWidthAt(Along, EdgeLength), Edge.HalfWidthAt(Next, EdgeLength));
				Along = Next;
				Segment.Edge = EdgeIndex;
				Segment.Kerb = EdgeKerb[EdgeIndex];
				if (Edge.bBridge)
				{
					// the valley under a bridge keeps its natural shape, only kept clear below the deck; no corridor
					Segment.Reach = BridgeReach;
					Segment.bBridge = true;
				}
			}
		}
		EdgeFirst.back() = static_cast<int32_t>(Segments.size());
		Log(georoads::Format("[Heights] %d of %d roads in town: kerbs and pavements (the country ones keep a verge)", TownRoads, NumEdges));

		// 2. how far each segment's earthworks reach: until a 1:2 embankment from the road would meet the natural ground,
		// judged from the ground beside the road out to 45 m; and how steep its cuttings are: steeper on steep hillsides.
		// The slow part (36 height samples a segment), so in parallel.
		ParallelFor()(static_cast<int32_t>(Segments.size()), [&](int32_t Index)
		{
			RoadSegment& Segment = Segments[Index];
			if (Segment.bBridge)
			{
				return;
			}
			const RoadEdge& Edge = Graph->Edges[Segment.Edge];
			const int32_t i = Index - EdgeFirst[Segment.Edge];
			const Vec2 Along = (Segment.B - Segment.A).Normal();
			const Vec2 Side(-Along.Y, Along.X);
			float Difference = 0.0f, CrossSlope = 0.0f;
			for (const double T : { 0.0, 0.5, 1.0 })
			{
				const Vec2 Centre = Segment.A + (Segment.B - Segment.A) * T;
				const float Road = LerpF(Edge.Heights[i], Edge.Heights[i + 1], static_cast<float>(T));
				for (const double Sign : { -1.0, 1.0 })
				{
					const Vec2 VergeEdge = Centre + Side * (Sign * (Edge.HalfWidth + Verge));
					const float AtVerge = BareHeight(VergeEdge);
					CrossSlope = std::max(CrossSlope, std::abs(BareHeight(VergeEdge + Side * (Sign * 2500.0)) - AtVerge) / 2500.0f);
					for (const double Out : { 0.0, 1000.0, 2500.0, 4500.0 })
					{
						Difference = std::max(Difference, std::abs(BareHeight(VergeEdge + Side * (Sign * Out)) - Road));
					}
				}
			}
			Segment.CutSlope = std::clamp(CrossSlope + 0.25f, CutSlope, MaxCutSlope);
			Segment.Reach = bFollowLand ? std::clamp(2.0f * Difference, MinReach, LandMaxBlend) : std::clamp(Difference / FillSlope, MinReach, MaxReach);
		});
		for (const RoadSegment& Segment : Segments)
		{
			if (!Segment.bBridge)
			{
				EdgeReach[Segment.Edge] = std::max(EdgeReach[Segment.Edge], Segment.Reach);
			}
		}

		// 3. register each segment in every cell its corridor covers, and in a finer grid by the road itself (for the
		// checks that ask "is there road surface here?", which run for every ground triangle near a road)
		for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			const RoadEdge& Edge = Graph->Edges[EdgeIndex];
			const int32_t First = EdgeFirst[EdgeIndex], End = EdgeFirst[EdgeIndex + 1];
			if (First == End)
			{
				continue;
			}
			const double Reach = Edge.bBridge ? Edge.HalfWidth + BridgeReach : CorridorExtent(EdgeIndex, FarSpacing);	// the widest a corridor gets
			for (int32_t Index = First; Index < End; ++Index)
			{
				Box2 Box;
				Box += Segments[Index].A;
				Box += Segments[Index].B;
				if (!Edge.bBridge)
				{
					const double Width = Segments[Index].HalfWidth;
					for (int32_t X = FloorToInt((Box.Min.X - Width) / RoadCell); X <= FloorToInt((Box.Max.X + Width) / RoadCell); ++X)
					{
						for (int32_t Y = FloorToInt((Box.Min.Y - Width) / RoadCell); Y <= FloorToInt((Box.Max.Y + Width) / RoadCell); ++Y)
						{
							RoadGrid[Key(X, Y)].push_back(Index);
						}
					}
				}
				const int32_t X0 = FloorToInt((Box.Min.X - Reach) / SegmentCell), X1 = FloorToInt((Box.Max.X + Reach) / SegmentCell);
				const int32_t Y0 = FloorToInt((Box.Min.Y - Reach) / SegmentCell), Y1 = FloorToInt((Box.Max.Y + Reach) / SegmentCell);
				for (int32_t X = X0; X <= X1; ++X)
				{
					for (int32_t Y = Y0; Y <= Y1; ++Y)
					{
						SegmentGrid[Key(X, Y)].push_back(Index);
					}
				}
			}
		}
	}

	float Terrain::CorridorExtent(int32_t Edge, double Spacing) const
	{
		return static_cast<float>(SinkRadius(Roads->Edges[Edge].HalfWidth, EdgeReach[Edge], Spacing) + CoverMargin(Spacing));
	}

	float Terrain::GroundHeight(const Vec2& Position) const
	{
		float Natural = BareHeight(Position);

		// over a tunnel the ground's at least its cover above the road (its 6.5 m tube and 2 m of earth), sloping down at
		// its sides: a shallow one under the map's smoothed heights (the 30 m ones: Monaco's are 2 m under them, or out)
		// had its tube standing on the ground. Where it isn't that deep, the ground's raised: cut and cover, as it's built
		if (!Tubes.empty() && Surface)
		{
			for (const Tube& Bore : Tubes)
			{
				if (!Bore.Bounds.ExpandBy(TunnelCoverReach).IsInside(Position))
				{
					continue;
				}
				const std::vector<Vec2>& Points = Roads->Edges[Bore.Edge].Points;
				for (size_t i = 0; i + 1 < Points.size(); ++i)
				{
					const Vec2 AB = Points[i + 1] - Points[i];
					const double LengthSquared = AB.SizeSquared();
					const double T = LengthSquared > 1.0 ? georoads::Dot(Position - Points[i], AB) / LengthSquared : 0.0;
					if (T < 0.0 || T > 1.0)
					{
						continue;	// (not past its mouths: the portal's there)
					}
					const Vec2 Closest = Points[i] + AB * T;
					const float Beside = std::max(0.0f, static_cast<float>(georoads::Distance(Closest, Position)) - Bore.HalfWidth);
					if (Beside >= TunnelCoverReach)
					{
						continue;
					}
					const float Cover = Surface->Height(Closest, Bore.Edge) + TunnelCover - Beside * TunnelCoverSlope;
					Natural = std::max(Natural, Cover);
				}
			}
		}

		// lakes: the bed sits below the water surface
		const float Water = WaterLevelAt(Position);
		if (Water != NoWater)
		{
			Natural = std::min(Natural, Water - LakeDepth);
		}

		const auto Found = SegmentGrid.find(Key(FloorToInt(Position.X / SegmentCell), FloorToInt(Position.Y / SegmentCell)));
		if (Found == SegmentGrid.end() || !Surface)
		{
			return Natural;
		}
		const std::vector<int32_t>& Nearby = Found->second;

		// Every road nearby limits the ground: no higher than its cutting and no lower than its embankment, which meet
		// at the road (just under its surface, out to the verge) and open out at their slopes from there. The ground is
		// the natural ground kept within all of them. Where two roads disagree, the nearer one wins.
		float Lowest = -std::numeric_limits<float>::max();	// highest embankment floor
		float Highest = std::numeric_limits<float>::max();	// lowest cutting ceiling
		float NearestOut = std::numeric_limits<float>::max();
		float NearestHeight = Natural;
		float UnderBridges = std::numeric_limits<float>::max();	// the ground always stays clear below a bridge deck
		float BestWeight = 0.0f;
		float BestHeight = Natural;
		int32_t BestEdge = -1;

		// land style: the lowest 45-degree ceiling from any road (and which road), and the lowest from roads other than it
		float LowestCeiling = std::numeric_limits<float>::max(), OtherCeiling = std::numeric_limits<float>::max();
		int32_t LowestCeilingEdge = -1;

		// how far this point is beside each nearby road's carriageway (cm): a kerbed road drops its kerb within KerbClear
		// of another road's edge, as kerbs drop at junction mouths
		thread_local std::vector<std::pair<int32_t, float>> BesideRoads;
		BesideRoads.clear();
		for (const int32_t Index : Nearby)
		{
			const RoadSegment& Segment = Segments[Index];
			if (!Segment.bBridge)
			{
				const Vec2 AB = Segment.B - Segment.A;
				const double LengthSquared = AB.SizeSquared();
				const double T = LengthSquared > 1.0 ? std::clamp(georoads::Dot(Position - Segment.A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
				BesideRoads.emplace_back(Segment.Edge, static_cast<float>(georoads::Distance(Segment.A + AB * T, Position)) - Segment.HalfWidth);
			}
		}
		auto NearOtherRoad = [](int32_t Edge)
		{
			return std::any_of(BesideRoads.begin(), BesideRoads.end(), [Edge](const std::pair<int32_t, float>& Other) { return Other.first != Edge && Other.second < KerbClear; });
		};

		for (const int32_t Index : Nearby)
		{
			const RoadSegment& Segment = Segments[Index];
			const Vec2 AB = Segment.B - Segment.A;
			const double LengthSquared = AB.SizeSquared();
			const double T = LengthSquared > 1.0 ? std::clamp(georoads::Dot(Position - Segment.A, AB) / LengthSquared, 0.0, 1.0) : 0.0;
			const Vec2 Closest = Segment.A + AB * T;
			const double Distance = georoads::Distance(Closest, Position);
			if (Segment.bBridge)
			{
				// under a bridge: at least BridgeClearance below the deck, rising at 45 degrees beyond its edge
				const float Beside = std::max(0.0f, static_cast<float>(Distance) - Segment.HalfWidth);
				if (Beside < Segment.Reach)
				{
					const Vec2 UnderDeck = Distance <= Segment.HalfWidth ? Position : Closest + (Position - Closest) * (Segment.HalfWidth / Distance);
					UnderBridges = std::min(UnderBridges, Surface->Height(UnderDeck, Segment.Edge) - BridgeClearance + Beside);
				}
				continue;
			}
			const float Out = static_cast<float>(Distance) - Segment.HalfWidth - Verge;	// beyond the verge
			if (Out >= Segment.Reach)
			{
				continue;
			}

			// the road surface at this point, or at the road's edge nearest it
			const Vec2 OnRoad = Distance <= Segment.HalfWidth ? Position : Closest + (Position - Closest) * (Segment.HalfWidth / Distance);
			// beside it: a verge just below its edge in the country; a kerb and the pavement above it in town
			const float Beside = static_cast<float>(Distance) - Segment.HalfWidth;
			const float Road = Surface->Height(OnRoad, Segment.Edge) - (Beside <= 0.0f ? BedDepth
				: Segment.Kerb > 0.0f && !NearOtherRoad(Segment.Edge) ? -(Segment.Kerb + PavementRise * std::clamp((Beside - KerbOut) / Verge, 0.0f, 1.0f)) : CarveDepth);

			const float Beyond = std::max(0.0f, Out);

			// "land" style: the ground eases back to natural over a width that grows with how far it has to go, the
			// strongest road nearby winning (where two hold it fully, the lower one, so it stays under both)
			if (bFollowLand)
			{
				const float Width = std::clamp(2.0f * std::abs(Road - Natural), MinReach, Segment.Reach);
				const float Weight = 1.0f - SmoothStepF(0.0f, Width, Beyond);
				const float Candidate = LerpF(Natural, Road, Weight);
				if (Weight > BestWeight + 1.0e-3f || (Weight > BestWeight - 1.0e-3f && Candidate < BestHeight))
				{
					BestWeight = Weight;
					BestHeight = Candidate;
					BestEdge = Segment.Edge;
				}
				// and never rising faster than 45 degrees above another road nearby, so next to one road it can't stand
				// above a neighbour a little lower (at junctions). The road the ground belongs to keeps its natural cliffs.
				const float Ceiling = Road + Beyond;
				if (Segment.Edge == LowestCeilingEdge)
				{
					LowestCeiling = std::min(LowestCeiling, Ceiling);
				}
				else if (Ceiling < LowestCeiling)
				{
					OtherCeiling = std::min(OtherCeiling, LowestCeiling);
					LowestCeiling = Ceiling;
					LowestCeilingEdge = Segment.Edge;
				}
				else
				{
					OtherCeiling = std::min(OtherCeiling, Ceiling);
				}
				continue;
			}

			float Ceiling = Road + Segment.CutSlope * Beyond;
			float Floor = Road - FillSlope * Beyond;

			// near the end of its reach the road lets go of the ground gradually
			const float Fade = SmoothStepF(std::max(Segment.Reach - FadeLength, 0.5f * Segment.Reach), Segment.Reach, Beyond);
			Ceiling = std::max(Ceiling, LerpF(Ceiling, Natural, Fade));
			Floor = std::min(Floor, LerpF(Floor, Natural, Fade));

			Lowest = std::max(Lowest, Floor);
			Highest = std::min(Highest, Ceiling);
			if (Out < NearestOut)
			{
				NearestOut = Out;
				NearestHeight = std::clamp(Natural, Floor, Ceiling);
			}
		}

		// a bridge deck wins over everything: even at its ends, where the road leading on would raise the ground, the
		// ground stays clear below it
		const float LandCap = BestEdge == LowestCeilingEdge ? OtherCeiling : LowestCeiling;
		const float Shaped = bFollowLand ? std::min(BestWeight > 0.0f ? BestHeight : Natural, LandCap) : Lowest <= Highest ? std::clamp(Natural, Lowest, Highest) : NearestHeight;
		return std::min(Shaped, UnderBridges);
	}

	bool Terrain::InCorridor(const Vec2& Position) const
	{
		const auto Found = SegmentGrid.find(Key(FloorToInt(Position.X / SegmentCell), FloorToInt(Position.Y / SegmentCell)));
		if (Found != SegmentGrid.end())
		{
			for (const int32_t Index : Found->second)
			{
				const RoadSegment& Segment = Segments[Index];
				if (!Segment.bBridge && SegmentDistance(Position, Segment.A, Segment.B) <= CorridorExtent(Segment.Edge))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool Terrain::SinksUnderCorridor(const Vec2& Position, double Spacing) const
	{
		// within a road's sink radius: its corridor reaches more than a grid triangle further, so every triangle touching
		// this vertex is covered
		const auto Found = SegmentGrid.find(Key(FloorToInt(Position.X / SegmentCell), FloorToInt(Position.Y / SegmentCell)));
		if (Found != SegmentGrid.end())
		{
			for (const int32_t Index : Found->second)
			{
				const RoadSegment& Segment = Segments[Index];
				if (!Segment.bBridge && SegmentDistance(Position, Segment.A, Segment.B) <= SinkRadius(Segment.HalfWidth, Segment.Reach, Spacing))
				{
					return true;
				}
			}
		}
		return false;
	}

	Vec4f Terrain::GroundColor(const Vec2& Position, float Height, const Vec3f& Normal) const
	{
		const uint8_t Land = CoverAt(Position);
		Vec4f Result = Land == Forest ? ForestColor : Land == Beach ? SandColor : GrassColor;
		if (Height < 0.0f)
		{
			Result = SeabedColor;
		}
		else if (Height < 300.0f && Land != Forest)
		{
			// sand along the shoreline
			Result = Lerp(SandColor, Result, Height / 300.0f);
		}
		// bare rock on steep slopes (over ~35 degrees)
		const float Steep = std::clamp((0.86f - Normal.Z) / 0.1f, 0.0f, 1.0f);
		Vec4f Out = Lerp(Result, RockColor, Steep);
		if (Vegetation)
		{
			// how covered it is by trees and shrubs, for the far-off canopy (alpha: 1 bare, 0 closed forest)
			float Trees = 0.0f, Shrubs = 0.0f, Grass = 0.0f;
			Vegetation->Sample(Position, Trees, Shrubs, Grass);
			Out.W = 1.0f - std::clamp(std::max(Trees, Shrubs * 0.7f), 0.0f, 1.0f);
		}
		return Out;
	}

	bool Terrain::RoadOver(const Vec2& Position, Ignore Skip, float& OutTop) const
	{
		const auto Found = RoadGrid.find(Key(FloorToInt(Position.X / RoadCell), FloorToInt(Position.Y / RoadCell)));
		if (Found == RoadGrid.end() || !Surface)
		{
			return false;
		}
		bool bFound = false;
		for (const int32_t Index : Found->second)
		{
			const RoadSegment& Segment = Segments[Index];
			if (Segment.bBridge || Skip.Contains(Segment.Edge) || SegmentDistance(Position, Segment.A, Segment.B) > Segment.HalfWidth)
			{
				continue;
			}
			const float Top = Surface->Height(Position, Segment.Edge);
			OutTop = bFound ? std::min(OutTop, Top) : Top;
			bFound = true;
		}
		return bFound;
	}

	void Terrain::AddGroundTriangle(MeshData& Out, const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& NA, const Vec3f& NB, const Vec3f& NC,
		const Vec4f& CA, const Vec4f& CB, const Vec4f& CC, GroundCheck& Check) const
	{
		// check the corners, the middle of each side and the centre against any road surface there; a big triangle (a long
		// one up a cutting) every ~1.5 m across it as well, or one could reach over the road between its checks: nothing
		// but the road may be in the road's space
		std::array<Vec3f, 7 + 91> Weights;
		int32_t NumWeights = 0;
		for (const Vec3f& W : { Vec3f(1, 0, 0), Vec3f(0, 1, 0), Vec3f(0, 0, 1), Vec3f(0.5f, 0.5f, 0), Vec3f(0, 0.5f, 0.5f), Vec3f(0.5f, 0, 0.5f), Vec3f(1.0f / 3, 1.0f / 3, 1.0f / 3) })
		{
			Weights[NumWeights++] = W;
		}
		auto Distance2f = [](const Vec3f& P, const Vec3f& Q) { const float DX = Q.X - P.X, DY = Q.Y - P.Y; return std::sqrt(DX * DX + DY * DY); };
		const float Longest = std::max({ Distance2f(A, B), Distance2f(B, C), Distance2f(C, A) });
		if (Longest > 300.0f)
		{
			const int32_t N = std::clamp(CeilToInt(Longest / 150.0f), 3, 12);
			for (int32_t i = 0; i <= N; ++i)
			{
				for (int32_t j = 0; i + j <= N; ++j)
				{
					Weights[NumWeights++] = Vec3f(float(i) / N, float(j) / N, float(N - i - j) / N);
				}
			}
		}
		for (int32_t w = 0; w < NumWeights; ++w)
		{
			const Vec3f& W = Weights[w];
			const Vec3f P = A * W.X + B * W.Y + C * W.Z;
			float Top = 0.0f;
			// (and ground above the road in a tunnel's portal cut: the mouth must be open)
			if ((RoadOver(Vec2(P.X, P.Y), {}, Top) && P.Z > Top - GroundUnderRoad) || InPortalCut(P))
			{
				++Check.ByKind[Check.Kind];
				if (Check.Removed++ == 0)
				{
					Check.First = Vec2(P.X, P.Y);
					Check.Detail = georoads::Format("kind %d, sample %.2f/%.2f/%.2f: ground %.2f m vs road %.2f m (corners %.2f %.2f %.2f m)",
						Check.Kind, W.X, W.Y, W.Z, P.Z / 100.0f, Top / 100.0f, A.Z / 100.0f, B.Z / 100.0f, C.Z / 100.0f);
				}
				return;
			}
		}
		Out.AddTriangle(A, B, C, NA, NB, NC, CA, CB, CC);
	}

	void Terrain::BuildCorridors(const Box2& Area, bool bNear, MeshData& Out, GroundCheck& Check) const
	{
		if (!Roads)
		{
			return;
		}
		const RoadGraph& Graph = *Roads;
		const double RowStep = bNear ? CorridorRowStep : CorridorRowStep * 3.0;

		// every road with a corridor reaching into the area (in the order first met)
		std::vector<int32_t> Edges;
		std::unordered_set<int32_t> EdgeSeen;
		for (int32_t X = FloorToInt(Area.Min.X / SegmentCell); X <= FloorToInt(Area.Max.X / SegmentCell); ++X)
		{
			for (int32_t Y = FloorToInt(Area.Min.Y / SegmentCell); Y <= FloorToInt(Area.Max.Y / SegmentCell); ++Y)
			{
				const auto Cell = SegmentGrid.find(Key(X, Y));
				if (Cell == SegmentGrid.end())
				{
					continue;
				}
				for (const int32_t Index : Cell->second)
				{
					if (!Segments[Index].bBridge && EdgeSeen.insert(Segments[Index].Edge).second)
					{
						Edges.push_back(Segments[Index].Edge);
					}
				}
			}
		}

		// distances from a road's middle where a cross-section has points: the bed, the verge, then the earthworks in
		// widening steps out to the corridor's edge (fewer far away)
		auto Offsets = [&](float HalfWidth, float Extent, std::vector<double>& Result, float Kerb)
		{
			Result.clear();
			const float VergeEdge = HalfWidth + Verge;
			if (HalfWidth > 50.0f)
			{
				Result.push_back(HalfWidth - 30.0f);
			}
			if (Kerb > 0.0f)
			{
				Result.push_back(HalfWidth + KerbOut);	// the top of the kerb: the ground climbs to it from under the road
			}
			Result.push_back(VergeEdge);
			static const float NearSteps[] = { 300.0f, 700.0f, 1200.0f, 2000.0f, 3000.0f, 4500.0f, 6000.0f };
			static const float FarSteps[] = { 1000.0f, 3000.0f, 6000.0f, 10000.0f, 15000.0f };
			const float* Steps = bNear ? NearSteps : FarSteps;
			const size_t NumSteps = bNear ? std::size(NearSteps) : std::size(FarSteps);
			for (size_t s = 0; s < NumSteps; ++s)
			{
				if (VergeEdge + Steps[s] < Extent - 200.0f)
				{
					Result.push_back(VergeEdge + Steps[s]);
				}
			}
			Result.push_back(Extent);
		};

		auto Normal = [](const Vec3f& Across, const Vec3f& Along)
		{
			const Vec3f N = Cross(Across, Along).Normal();
			return N.Z < 0.0f ? -N : N;
		};

		// a corridor point: the ground there, never above a road surface it lands on
		auto GroundPoint = [&](const Vec2& P)
		{
			float Height = GroundHeight(P);
			float Top = 0.0f;
			if (RoadOver(P, {}, Top))
			{
				Height = std::min(Height, Top - BedDepth);
			}
			return Vec3f(static_cast<float>(P.X), static_cast<float>(P.Y), Height);
		};

		// near the car every corridor triangle is checked against the roads; far away (2.5 km+) slivers can't be seen and
		// the checks would slow streaming
		auto Emit = [&](MeshData& Mesh, GroundCheck& Checked, const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& NA, const Vec3f& NB,
			const Vec3f& NC, const Vec4f& CA, const Vec4f& CB, const Vec4f& CC)
		{
			if (bNear)
			{
				AddGroundTriangle(Mesh, A, B, C, NA, NB, NC, CA, CB, CC, Checked);
			}
			else
			{
				Mesh.AddTriangle(A, B, C, NA, NB, NC, CA, CB, CC);
			}
		};

		// the roads' strips, then the fans round their ends, are built in batches on the worker pool, each into its own mesh
		// and check, joined in order after (the same triangles as one at a time): a dense square's corridors took 2-4 s
		// on one thread, the longest wait as the ground comes in
		struct CorridorBatch
		{
			MeshData Mesh;
			GroundCheck Checked;
		};
		auto Join = [&](std::vector<CorridorBatch>& Batches)
		{
			for (CorridorBatch& Batch : Batches)
			{
				const int32_t Base = static_cast<int32_t>(Out.Positions.size());
				Out.Positions.insert(Out.Positions.end(), Batch.Mesh.Positions.begin(), Batch.Mesh.Positions.end());
				Out.Normals.insert(Out.Normals.end(), Batch.Mesh.Normals.begin(), Batch.Mesh.Normals.end());
				Out.UVs.insert(Out.UVs.end(), Batch.Mesh.UVs.begin(), Batch.Mesh.UVs.end());
				Out.Colors.insert(Out.Colors.end(), Batch.Mesh.Colors.begin(), Batch.Mesh.Colors.end());
				Out.Tags.insert(Out.Tags.end(), Batch.Mesh.Tags.begin(), Batch.Mesh.Tags.end());
				for (const int32_t Index : Batch.Mesh.Indices)
				{
					Out.Indices.push_back(Base + Index);
				}
				if (Batch.Checked.Removed > 0 && Check.Removed == 0)
				{
					Check.First = Batch.Checked.First;
					Check.Detail = Batch.Checked.Detail;
				}
				Check.Removed += Batch.Checked.Removed;
				for (int32_t k = 0; k < 3; ++k)
				{
					Check.ByKind[k] += Batch.Checked.ByKind[k];
				}
			}
		};
		auto Batched = [&](size_t Count, int32_t Kind, auto&& Build)
		{
			std::vector<CorridorBatch> Batches(std::clamp(static_cast<int32_t>(Count / 8), 1, 64));
			const int32_t NumBatches = static_cast<int32_t>(Batches.size());
			ParallelFor()(NumBatches, [&](int32_t Index)
			{
				CorridorBatch& Batch = Batches[Index];
				Batch.Mesh.Tag = Out.Tag;
				Batch.Checked.Kind = Kind;
				Build(Count * Index / NumBatches, Count * (Index + 1) / NumBatches, Batch.Mesh, Batch.Checked);
			});
			Join(Batches);
			Check.Kind = Kind;
		};

		// One four-sided piece of corridor (corners: row 0 inner, row 0 outer, row 1 inner, row 1 outer). Pieces are up to
		// ~10 m long, so another road can cross one between its rows; a piece with another road under it is cut into a
		// fine grid whose every point is kept under that road.
		static const Vec2 CrossChecks[] = { Vec2(0.5, 0.5), Vec2(0.5, 0.0), Vec2(0.5, 1.0), Vec2(0.0, 0.5), Vec2(1.0, 0.5),
			Vec2(0.25, 0.25), Vec2(0.75, 0.25), Vec2(0.25, 0.75), Vec2(0.75, 0.75) };
		auto AddQuad = [&](MeshData& Mesh, GroundCheck& Checked, const Vec3f& P00, const Vec3f& P01, const Vec3f& P10, const Vec3f& P11, Ignore Skip)
		{
			// cut only where the piece as it is would come up through the road crossing it: on level ground (a city) the
			// piece already lies under the other road
			float Top = 0.0f;
			bool bCrossed = false;
			if (bNear)
			{
				for (const Vec2& Weight : CrossChecks)
				{
					const Vec3f P = Lerp(Lerp(P00, P01, static_cast<float>(Weight.X)), Lerp(P10, P11, static_cast<float>(Weight.X)), static_cast<float>(Weight.Y));
					bCrossed |= RoadOver(Vec2(P.X, P.Y), Skip, Top) && P.Z > Top - BedDepth * 0.5f;
				}
			}
			const int32_t Cuts = bCrossed ? QuadCuts : 1;
			std::array<Vec3f, (QuadCuts + 1) * (QuadCuts + 1)> Grid;
			int32_t NumGrid = 0;
			for (int32_t j = 0; j <= Cuts; ++j)
			{
				for (int32_t i = 0; i <= Cuts; ++i)
				{
					const float U = static_cast<float>(i) / Cuts, V = static_cast<float>(j) / Cuts;
					// inside: the true ground (kept under any road); on the edges: straight between the corners, so a
					// neighbouring piece that isn't cut still meets it, but pushed under a road it lands on
					Vec3f P = Lerp(Lerp(P00, P01, U), Lerp(P10, P11, U), V);
					if (Cuts > 1 && i > 0 && i < Cuts && j > 0 && j < Cuts)
					{
						P = GroundPoint(Vec2(P.X, P.Y));
					}
					else if (Cuts > 1 && RoadOver(Vec2(P.X, P.Y), {}, Top))
					{
						P.Z = std::min(P.Z, Top - BedDepth);
					}
					Grid[NumGrid++] = P;
				}
			}
			for (int32_t j = 0; j < Cuts; ++j)
			{
				for (int32_t i = 0; i < Cuts; ++i)
				{
					const Vec3f& A = Grid[j * (Cuts + 1) + i];
					const Vec3f& B = Grid[j * (Cuts + 1) + i + 1];
					const Vec3f& C = Grid[(j + 1) * (Cuts + 1) + i];
					const Vec3f& D = Grid[(j + 1) * (Cuts + 1) + i + 1];
					const Vec3f N = Normal(B - A, C - A);
					const Vec3f M = Normal(D - C, C - A);
					const Vec4f CA = GroundColor(Vec2(A.X, A.Y), A.Z, N), CB = GroundColor(Vec2(B.X, B.Y), B.Z, N);
					const Vec4f CC = GroundColor(Vec2(C.X, C.Y), C.Z, M), CD = GroundColor(Vec2(D.X, D.Y), D.Z, M);
					Emit(Mesh, Checked, A, B, D, N, N, M, CA, CB, CD);
					Emit(Mesh, Checked, A, D, C, N, M, M, CA, CD, CC);
				}
			}
		};

		// how far out (cm) a strip reaches towards To before it meets another road: within the road's own bed and verge it
		// always goes on; past that it stops just short of the next road's edge, found to ~1 cm, and is Ended
		auto StripEnd = [&](const Vec2& Centre, const Vec2& Dir, double From, double To, double Formation, Ignore Skip, bool& bEnded)
		{
			float Top = 0.0f;
			if (To <= Formation || !RoadOver(Centre + Dir * To, Skip, Top))
			{
				return To;
			}
			bEnded = true;
			double Free = std::max(From, Formation), OnRoad = To;
			for (int32_t Step = 0; Step < 10 && OnRoad - Free > 1.0; ++Step)
			{
				const double Middle = 0.5 * (Free + OnRoad);
				(RoadOver(Centre + Dir * Middle, Skip, Top) ? OnRoad : Free) = Middle;
			}
			return Free;
		};

		// --- strips along each road ---
		std::vector<int32_t> Nodes;	// (the road ends in the area, for the fans: in the order first met)
		std::unordered_set<int32_t> NodeSeen;
		for (const int32_t EdgeIndex : Edges)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			for (const int32_t Node : { Edge.StartNode, Edge.EndNode })
			{
				if (Area.IsInside(Graph.Nodes[Node].Position) && NodeSeen.insert(Node).second)
				{
					Nodes.push_back(Node);
				}
			}
		}
		Batched(Edges.size(), 1, [&](size_t FirstEdge, size_t EndEdge, MeshData& Mesh, GroundCheck& Checked)
		{
			std::vector<double> Side;
			std::vector<Vec2> Centres, Across;
			std::vector<double> Mitre, InnerReach;
			std::vector<int32_t> InnerSide;
			std::vector<Vec3f> Row0, Row1;
			for (size_t e = FirstEdge; e < EndEdge; ++e)
			{
				const int32_t EdgeIndex = Edges[e];
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const float Extent = CorridorExtent(EdgeIndex, bNear ? NearSpacing : FarSpacing);
				Offsets(Edge.HalfWidth, Extent, Side, EdgeIndex < static_cast<int32_t>(EdgeKerb.size()) ? EdgeKerb[EdgeIndex] : 0.0f);

				// rows across the road at its points and in between, at most RowStep apart; the direction across turns smoothly
				// through corners
				Centres.clear();
				Across.clear();
				Mitre.clear();	// at a corner the road mesh is this much wider (MaxMiter 2): the verge and kerb move out with it
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
					const Vec2 Dir = (B - A).Normal();
					const Vec2 In = i > 0 ? (A - Edge.Points[i - 1]).Normal() : Dir;
					const Vec2 Corner = (In + Dir).Normal();
					const int32_t Steps = std::max(1, CeilToInt(georoads::Distance(A, B) / RowStep));
					for (int32_t Step = 0; Step < Steps; ++Step)
					{
						Centres.push_back(A + (B - A) * (static_cast<double>(Step) / Steps));
						const Vec2 D = Step == 0 ? Corner : Dir;
						Across.emplace_back(-D.Y, D.X);
						Mitre.push_back(Step == 0 ? std::min(1.0 / std::max(georoads::Dot(Corner, In), 0.01), 2.0) : 1.0);
					}
				}
				Centres.push_back(Edge.Points.back());
				const Vec2 LastDir = (Edge.Points.back() - Edge.Points[Edge.Points.size() - 2]).Normal();
				Across.emplace_back(-LastDir.Y, LastDir.X);
				Mitre.push_back(1.0);

				// on the inside of a bend the rows across the road fan in and would cross each other past its radius, folding the
				// ground back over the road: there they stop short of the radius. +1: the bend turns left
				InnerReach.assign(Centres.size(), std::numeric_limits<double>::max());
				InnerSide.assign(Centres.size(), 0);
				for (size_t r = 1; r + 1 < Centres.size(); ++r)
				{
					const Vec2 Before(Across[r - 1].Y, -Across[r - 1].X), After(Across[r + 1].Y, -Across[r + 1].X);
					const double Turn = std::atan2(Before.X * After.Y - Before.Y * After.X, georoads::Dot(Before, After));
					const double Length = georoads::Distance(Centres[r - 1], Centres[r]) + georoads::Distance(Centres[r], Centres[r + 1]);
					if (std::abs(Turn) > 0.02 && Length > 1.0)
					{
						InnerReach[r] = 0.85 * Length / std::abs(Turn);	// the bend's radius, less a little
						InnerSide[r] = Turn > 0.0 ? 1 : -1;
					}
				}

				// only the pieces whose middle is in this area, so neighbouring chunks never both build one
				const int32_t NumSide = static_cast<int32_t>(Side.size());
				const int32_t Columns = NumSide * 2 + 1;
				const int32_t Own[] = { EdgeIndex };
				const Ignore OwnRoad{ Own, 1 };
				auto BuildRow = [&](size_t r, std::vector<Vec3f>& Row)
				{
					Row.resize(Columns);
					Row[NumSide] = GroundPoint(Centres[r]);
					for (const int32_t Sign : { -1, 1 })
					{
						// outwards from the road: a strip ends where it reaches another road (that road has its own); any
						// columns past that collapse onto the end point
						double Previous = 0.0;
						bool bEnded = false;
						Vec3f Last = Row[NumSide];
						for (int32_t k = 0; k < NumSide; ++k)
						{
							const int32_t Column = NumSide + Sign * (k + 1);
							if (!bEnded)
							{
								// the points by the road (bed, kerb, verge) keep their distance from its mitred edge
								const double Widen = Side[k] <= Edge.HalfWidth + Verge + 1.0 ? Edge.HalfWidth * (Mitre[r] - 1.0) : 0.0;
								const double Formation = Edge.HalfWidth * Mitre[r] + Verge;
								const double Wanted = std::max(Previous, Sign == InnerSide[r] ? std::min(Side[k] + Widen, std::max(InnerReach[r], Formation)) : Side[k] + Widen);
								const double Offset = StripEnd(Centres[r], Across[r] * Sign, Previous, Wanted, Formation, OwnRoad, bEnded);
								Last = GroundPoint(Centres[r] + Across[r] * (Sign * Offset));
								Previous = Offset;
							}
							Row[Column] = Last;
						}
					}
				};
				int64_t Built = -2;
				for (size_t r = 0; r + 1 < Centres.size(); ++r)
				{
					if (!Area.IsInside((Centres[r] + Centres[r + 1]) * 0.5))
					{
						continue;
					}
					if (Built != int64_t(r))
					{
						BuildRow(r, Row0);
					}
					BuildRow(r + 1, Row1);
					Built = int64_t(r) + 1;
					for (int32_t c = 0; c + 1 < Columns; ++c)
					{
						AddQuad(Mesh, Checked, Row0[c], Row0[c + 1], Row1[c], Row1[c + 1], OwnRoad);
					}
					std::swap(Row0, Row1);
				}
			}
		});

		// --- fans around road ends: wherever the roads at a node leave more than a half-turn open (a dead end, the
		// outside of a bend, the far side of a T, where a road goes onto a bridge), the corridor wraps round the node ---
		Batched(Nodes.size(), 2, [&](size_t FirstNode, size_t EndNode, MeshData& Mesh, GroundCheck& Checked)
		{
			std::vector<double> Ring;
			for (size_t n = FirstNode; n < EndNode; ++n)
			{
				const int32_t NodeIndex = Nodes[n];
				const RoadNode& Node = Graph.Nodes[NodeIndex];
				struct Arm { double Angle; float HalfWidth; float Extent; float Kerb; };
				std::vector<Arm> Arms;
				for (const int32_t EdgeIndex : Node.Edges)
				{
					const RoadEdge& Edge = Graph.Edges[EdgeIndex];
					if (EdgeReach[EdgeIndex] <= 0.0f)
					{
						continue;
					}
					const Vec2 Next = Edge.StartNode == NodeIndex ? Edge.Points[1] : Edge.Points[Edge.Points.size() - 2];
					const Vec2 Dir = Next - Node.Position;
					Arms.push_back({ std::atan2(Dir.Y, Dir.X), Edge.HalfWidth, CorridorExtent(EdgeIndex, bNear ? NearSpacing : FarSpacing), EdgeKerb[EdgeIndex] });
				}
				if (Arms.empty())
				{
					continue;
				}
				std::sort(Arms.begin(), Arms.end(), [](const Arm& A, const Arm& B) { return A.Angle < B.Angle; });
				for (size_t i = 0; i < Arms.size(); ++i)
				{
					const Arm& From = Arms[i];
					const Arm& To = Arms[(i + 1) % Arms.size()];
					double Gap = To.Angle - From.Angle;
					Gap = Arms.size() == 1 ? 2.0 * Pi : (Gap <= 0.0 ? Gap + 2.0 * Pi : Gap);
					if (Gap <= Pi + 0.02)
					{
						continue;	// the two roads' own strips already meet here
					}
					// from the first road's left side round to the next road's right side
					const double Start = From.Angle + Pi * 0.5;
					const double Sweep = Gap - Pi;
					const int32_t Steps = std::max(1, CeilToInt(Sweep / (20.0 * (Pi / 180.0))));
					Offsets(std::max(From.HalfWidth, To.HalfWidth), std::max(From.Extent, To.Extent), Ring, std::max(From.Kerb, To.Kerb));
					Ring.insert(Ring.begin(), 0.0);

					// like the strips, a spoke ends where it meets a road other than the ones meeting here
					const double Formation = std::max(From.HalfWidth, To.HalfWidth) + Verge;
					const Ignore Meeting{ Node.Edges.data(), Node.Edges.size() };
					std::vector<Vec3f> Spoke0, Spoke1;
					auto BuildSpoke = [&](int32_t s, std::vector<Vec3f>& Spoke)
					{
						const double Angle = Start + Sweep * s / Steps;
						const Vec2 Dir(std::cos(Angle), std::sin(Angle));
						Spoke.resize(Ring.size());
						bool bEnded = false;
						double Previous = 0.0;
						for (size_t k = 0; k < Ring.size(); ++k)
						{
							if (bEnded)
							{
								Spoke[k] = Spoke[k - 1];
								continue;
							}
							const double Reach = StripEnd(Node.Position, Dir, Previous, Ring[k], Formation, Meeting, bEnded);
							Spoke[k] = GroundPoint(Node.Position + Dir * Reach);
							Previous = Reach;
						}
					};
					BuildSpoke(0, Spoke0);
					for (int32_t s = 1; s <= Steps; ++s)
					{
						BuildSpoke(s, Spoke1);
						for (size_t k = 0; k + 1 < Ring.size(); ++k)
						{
							const Vec3f N = Normal(Spoke0[k + 1] - Spoke0[k], Spoke1[k + 1] - Spoke0[k + 1]);
							const Vec4f C0 = GroundColor(Vec2(Spoke0[k].X, Spoke0[k].Y), Spoke0[k].Z, N);
							const Vec4f C1 = GroundColor(Vec2(Spoke0[k + 1].X, Spoke0[k + 1].Y), Spoke0[k + 1].Z, N);
							const Vec4f C2 = GroundColor(Vec2(Spoke1[k + 1].X, Spoke1[k + 1].Y), Spoke1[k + 1].Z, N);
							const Vec4f C3 = GroundColor(Vec2(Spoke1[k].X, Spoke1[k].Y), Spoke1[k].Z, N);
							Emit(Mesh, Checked, Spoke0[k], Spoke0[k + 1], Spoke1[k + 1], N, N, N, C0, C1, C2);
							if (k > 0)
							{
								Emit(Mesh, Checked, Spoke0[k], Spoke1[k + 1], Spoke1[k], N, N, N, C0, C2, C3);
							}
						}
						std::swap(Spoke0, Spoke1);
					}
				}
			}
		});
	}

	void Terrain::BuildChunk(int32_t ChunkX, int32_t ChunkY, bool bNear, MeshData& Out, bool bPlain) const
	{
		// coarse chunks are dropped slightly; skirts hide cracks between levels; both carry the road corridors (but plain
		// ones, far off)
		const double Spacing = bNear ? NearSpacing : FarSpacing;
		BuildGrid(Vec2(ChunkX * ChunkSize, ChunkY * ChunkSize), RoundToInt(ChunkSize / Spacing), Spacing, bNear ? 0.0f : 50.0f, bNear ? 100.0f : 300.0f, bNear || !bPlain, Out);
	}

	void Terrain::BuildDistantBlock(int32_t BlockX, int32_t BlockY, MeshData& Out) const
	{
		const double BlockSize = ChunkSize * ChunksPerBlock;
		BuildGrid(Vec2(BlockX * BlockSize, BlockY * BlockSize), RoundToInt(BlockSize / DistantSpacing), DistantSpacing, 300.0f, 3000.0f, false, Out);
	}

	void Terrain::BuildGrid(const Vec2& Min, int32_t Cells, double Spacing, float Drop, float SkirtDepth, bool bCorridors, MeshData& Out) const
	{
		const int32_t Size = Cells + 3;	// one extra ring each side, for normals
		const Vec2 Origin = Min - Vec2(Spacing, Spacing);

		// under a corridor the grid sinks out of sight, by more than its triangles can bulge above the corridor's own
		// finer surface (a straight triangle across the foot of a cutting)
		const float Sink = static_cast<float>(Spacing) * CorridorSinkPerSpacing;
		const double TimeStart = Seconds();
		std::vector<float> Heights(size_t(Size) * Size);
		std::vector<uint8_t> Sunk(size_t(Size) * Size);
		for (int32_t Y = 0; Y < Size; ++Y)
		{
			for (int32_t X = 0; X < Size; ++X)
			{
				const Vec2 P = Origin + Vec2(X * Spacing, Y * Spacing);
				Sunk[size_t(Y) * Size + X] = bCorridors && SinksUnderCorridor(P, Spacing);
				Heights[size_t(Y) * Size + X] = GroundHeight(P) - Drop - (Sunk[size_t(Y) * Size + X] ? Sink : 0.0f);
			}
		}
		const double TimeHeights = Seconds();
		auto IsSunk = [&](int32_t X, int32_t Y) { return Sunk[size_t(Y) * Size + X] != 0; };

		// grid triangles reaching under a corridor are checked against the roads (one with no corner sunk can't reach a
		// road: the sink radius is wider than a triangle)
		GroundCheck Check;
		auto AddGrid = [&](bool bNearRoad, const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& NA, const Vec3f& NB, const Vec3f& NC,
			const Vec4f& CA, const Vec4f& CB, const Vec4f& CC)
		{
			// (and any over a tunnel: it has no corridor, but its tube and mouths must be clear)
			auto OverTube = [&]()
			{
				const Vec3f Middle = (A + B + C) * (1.0f / 3.0f);
				const double Reach = Spacing * 1.5;
				for (const Tube& Bore : Tubes)
				{
					if (!Bore.Bounds.ExpandBy(Reach).IsInside(Vec2(Middle.X, Middle.Y)))
					{
						continue;
					}
					const std::vector<Vec2>& Points = Roads->Edges[Bore.Edge].Points;
					for (size_t i = 0; i + 1 < Points.size(); ++i)
					{
						if (PointDistToSegment(Vec2(Middle.X, Middle.Y), Points[i], Points[i + 1]) <= Bore.HalfWidth + Reach)
						{
							return true;
						}
					}
				}
				return false;
			};
			if ((bNearRoad || (!Tubes.empty() && OverTube())) && Spacing <= NearSpacing)
			{
				AddGroundTriangle(Out, A, B, C, NA, NB, NC, CA, CB, CC, Check);
			}
			else
			{
				Out.AddTriangle(A, B, C, NA, NB, NC, CA, CB, CC);
			}
		};

		auto H = [&](int32_t X, int32_t Y) { return Heights[size_t(Y) * Size + X]; };
		auto Vertex = [&](int32_t X, int32_t Y)
		{
			const Vec2 P = Origin + Vec2(X * Spacing, Y * Spacing);
			return Vec3f(static_cast<float>(P.X), static_cast<float>(P.Y), H(X, Y));
		};
		auto Normal = [&](int32_t X, int32_t Y)
		{
			const float DX = H(X + 1, Y) - H(X - 1, Y);
			const float DY = H(X, Y + 1) - H(X, Y - 1);
			return Vec3f(-DX, -DY, static_cast<float>(2.0 * Spacing)).Normal();
		};
		auto Color = [&](int32_t X, int32_t Y, const Vec3f& N)
		{
			return GroundColor(Origin + Vec2(X * Spacing, Y * Spacing), H(X, Y) + Drop, N);
		};

		for (int32_t Y = 1; Y <= Cells; ++Y)
		{
			for (int32_t X = 1; X <= Cells; ++X)
			{
				const Vec3f P00 = Vertex(X, Y), P10 = Vertex(X + 1, Y), P01 = Vertex(X, Y + 1), P11 = Vertex(X + 1, Y + 1);
				const Vec3f N00 = Normal(X, Y), N10 = Normal(X + 1, Y), N01 = Normal(X, Y + 1), N11 = Normal(X + 1, Y + 1);
				const Vec4f C00 = Color(X, Y, N00), C10 = Color(X + 1, Y, N10), C01 = Color(X, Y + 1, N01), C11 = Color(X + 1, Y + 1, N11);
				// a triangle wholly under a road corridor is left out: the corridor is the ground there
				if (!(IsSunk(X, Y) && IsSunk(X + 1, Y) && IsSunk(X + 1, Y + 1)))
				{
					AddGrid(IsSunk(X, Y) || IsSunk(X + 1, Y) || IsSunk(X + 1, Y + 1), P00, P10, P11, N00, N10, N11, C00, C10, C11);
				}
				if (!(IsSunk(X, Y) && IsSunk(X + 1, Y + 1) && IsSunk(X, Y + 1)))
				{
					AddGrid(IsSunk(X, Y) || IsSunk(X + 1, Y + 1) || IsSunk(X, Y + 1), P00, P11, P01, N00, N11, N01, C00, C11, C01);
				}
			}
		}

		// skirts around the edge hide cracks where two levels of detail meet
		const Vec3f Down(0.0f, 0.0f, SkirtDepth);
		auto Skirt = [&](int32_t XA, int32_t YA, int32_t XB, int32_t YB, const Vec3f& Outward)
		{
			const Vec3f A = Vertex(XA, YA), B = Vertex(XB, YB);
			const Vec4f CA = Color(XA, YA, Normal(XA, YA)), CB = Color(XB, YB, Normal(XB, YB));
			Out.AddTriangle(A, B, B - Down, Outward, Outward, Outward, CA, CB, CB);
			Out.AddTriangle(A, B - Down, A - Down, Outward, Outward, Outward, CA, CB, CA);
		};
		for (int32_t i = 1; i <= Cells; ++i)
		{
			Skirt(i, 1, i + 1, 1, Vec3f(0, -1, 0));
			Skirt(i, Cells + 1, i + 1, Cells + 1, Vec3f(0, 1, 0));
			Skirt(1, i, 1, i + 1, Vec3f(-1, 0, 0));
			Skirt(Cells + 1, i, Cells + 1, i + 1, Vec3f(1, 0, 0));
		}

		const double TimeGrid = Seconds();
		if (bCorridors)
		{
			BuildCorridors(Box2(Min, Min + Vec2(Cells * Spacing, Cells * Spacing)), Spacing <= NearSpacing, Out, Check);
			// a slow chunk says where its time went (dense cities)
			const double TimeEnd = Seconds();
			if (TimeEnd - TimeStart > 0.3)
			{
				Log(georoads::Format("[Terrain] slow chunk at X=%.3f Y=%.3f: heights %.2fs, grid %.2fs (%d left out), corridors %.2fs, %d triangles",
					Min.X, Min.Y, TimeHeights - TimeStart, TimeGrid - TimeHeights, Check.Removed, TimeEnd - TimeGrid, int32_t(Out.Indices.size() / 3)));
			}
			if (Check.Removed > 0)
			{
				double Lat = 0.0, Lon = 0.0;
				Projection.ToLatLon(Check.First, Lat, Lon);
				static std::atomic<int32_t> Shown{ 0 };
				if (Shown++ < 15)
				{
					Log(georoads::Format("[GroundCheck] %d ground triangles left out of a chunk (grid %d, strips %d, fans %d), first at %.5f,%.5f: %s",
						Check.Removed, Check.ByKind[0], Check.ByKind[1], Check.ByKind[2], Lat, Lon, Check.Detail.c_str()));
				}
				GroundRemoved += Check.Removed;
			}
		}
	}

	void Terrain::BuildWater(MeshData& Out) const
	{
		const Vec3f Up(0.0f, 0.0f, 1.0f);

		// the sea: one plane at sea level reaching well past the area
		const Box2 Sea = Bounds.ExpandBy(3000000.0);
		const Vec3f A(float(Sea.Min.X), float(Sea.Min.Y), 0.0f), B(float(Sea.Max.X), float(Sea.Min.Y), 0.0f), C(float(Sea.Max.X), float(Sea.Max.Y), 0.0f), D(float(Sea.Min.X), float(Sea.Max.Y), 0.0f);
		Out.AddTriangle(A, B, C, Up, Vec2f{ 0, 0 }, Vec2f{ 1, 0 }, Vec2f{ 1, 1 });
		Out.AddTriangle(A, C, D, Up, Vec2f{ 0, 0 }, Vec2f{ 1, 1 }, Vec2f{ 0, 1 });

		// lakes at their own levels (estuaries at sea level are covered by the sea already)
		for (const std::pair<std::vector<Vec2>, float>& Lake : WaterPolygons)
		{
			if (Lake.second < 10.0f)
			{
				continue;
			}
			std::vector<int32_t> Indices;
			Triangulate(Lake.first, Indices);
			for (size_t i = 0; i + 2 < Indices.size(); i += 3)
			{
				const Vec2& P0 = Lake.first[Indices[i]];
				const Vec2& P1 = Lake.first[Indices[i + 1]];
				const Vec2& P2 = Lake.first[Indices[i + 2]];
				Out.AddTriangle(Vec3f(float(P0.X), float(P0.Y), Lake.second), Vec3f(float(P1.X), float(P1.Y), Lake.second), Vec3f(float(P2.X), float(P2.Y), Lake.second), Up,
					Vec2f{ 0, 0 }, Vec2f{ 1, 0 }, Vec2f{ 0, 1 });
			}
		}
	}

	int64_t Terrain::DataBytes() const
	{
		int64_t Bytes = int64_t(CoverMask.capacity() + WaterLevel.capacity() * sizeof(float) + ForestDepth.capacity() * sizeof(float)
			+ Segments.capacity() * sizeof(RoadSegment) + EdgeReach.capacity() * sizeof(float) + EdgeKerb.capacity() * sizeof(float) + BuildingCount.capacity() * 2);
		for (const auto& Cell : SegmentGrid)
		{
			Bytes += int64_t(32 + Cell.second.capacity() * sizeof(int32_t));
		}
		for (const auto& Cell : RoadGrid)
		{
			Bytes += int64_t(32 + Cell.second.capacity() * sizeof(int32_t));
		}
		for (const auto& Water : WaterPolygons)
		{
			Bytes += int64_t(Water.first.capacity() * sizeof(Vec2));
		}
		return Bytes;
	}

	bool Terrain::IsTown(const Vec2& Position) const
	{
		// 3 or more buildings within two mask cells (about 40 m) each way
		if (BuildingCount.empty())
		{
			return false;
		}
		const int32_t CX = FloorToInt((Position.X - MaskOrigin.X) / MaskCell), CY = FloorToInt((Position.Y - MaskOrigin.Y) / MaskCell);
		int32_t Count = 0;
		for (int32_t Y = std::max(0, CY - 2); Y <= std::min(MaskHeight - 1, CY + 2); ++Y)
		{
			for (int32_t X = std::max(0, CX - 2); X <= std::min(MaskWidth - 1, CX + 2); ++X)
			{
				Count += BuildingCount[size_t(Y) * MaskWidth + X];
			}
		}
		return Count >= 3;
	}

	bool Terrain::InPortalCut(const Vec3f& Point) const
	{
		const Vec2 P(Point.X, Point.Y);
		for (const Portal& Cut : Portals)
		{
			const Vec2 D = P - Cut.Mouth;
			const double Along = georoads::Dot(D, Cut.Inward);
			const double AcrossDistance = std::abs(D.X * Cut.Inward.Y - D.Y * Cut.Inward.X);
			// (only what's in the tube's way: the ground over its roof stays, its cover; a short tunnel was all cut, its tube
			// standing in the open)
			if (Along >= 0.0 && Along <= Cut.Length && AcrossDistance <= Cut.HalfWidth && Surface)
			{
				const float Road = Surface->Height(P, Cut.Edge);
				if (Point.Z > Road - 50.0f && Point.Z < Road + TunnelCut)
				{
					return true;
				}
			}
		}
		// inside the tube anywhere along it (up to a little over its 6.5 m ceiling: not its cover above)
		for (const Tube& Bore : Tubes)
		{
			if (!Bore.Bounds.IsInside(P) || !Surface)
			{
				continue;
			}
			const std::vector<Vec2>& Points = Roads->Edges[Bore.Edge].Points;
			for (size_t i = 0; i + 1 < Points.size(); ++i)
			{
				if (PointDistToSegment(P, Points[i], Points[i + 1]) <= Bore.HalfWidth)
				{
					const float Road = Surface->Height(P, Bore.Edge);
					if (Point.Z > Road - 50.0f && Point.Z < Road + TunnelCut)
					{
						return true;
					}
					break;
				}
			}
		}
		return false;
	}
}
