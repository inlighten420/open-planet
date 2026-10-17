// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Vegetation.h"
#include "Terrain.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace geoground
{
	uint8_t LandCover::At(double Lat, double Lon) const
	{
		if (!IsValid())
		{
			return LandUnknown;
		}
		const int64_t Col = int64_t(std::floor((Lon - Left) / CellDegrees));
		const int64_t Row = int64_t(std::floor((Top - Lat) / CellDegrees));
		if (Col < 0 || Row < 0 || Col >= Width || Row >= Height)
		{
			return LandUnknown;
		}
		return Classes[size_t(Row) * size_t(Width) + size_t(Col)];
	}

	namespace VegetationImpl
	{
		struct Growth
		{
			uint8_t Trees = 0, Shrubs = 0, Grass = 0;
		};

		// How thick each kind of plant is on each kind of land (0-255): forests closed, shrubland (fynbos, scrub)
		// mostly bushes, grassland with a few bushes and the odd tree, towns with gardens and street trees
		Growth FromClass(uint8_t Class)
		{
			switch (Class)
			{
			case LandTrees: return { 230, 70, 90 };
			case LandShrubs: return { 14, 210, 120 };
			case LandGrass: return { 6, 28, 210 };
			case LandCrops: return { 2, 4, 170 };
			case LandBuilt: return { 28, 34, 80 };
			case LandBare: return { 0, 6, 12 };
			case LandWetland: return { 12, 90, 230 };
			case LandMangrove: return { 210, 40, 40 };
			case LandMoss: return { 0, 24, 120 };
			default: return { 0, 0, 0 };	// snow, water
			}
		}

		uint8_t* TexelAt(VegetationMap& Map, int32_t X, int32_t Y)
		{
			return &Map.Texels[(size_t(X) * size_t(Map.Height) + size_t(Y)) * 4];
		}

		bool InsidePolygon(const std::vector<Vec2>& Polygon, const Vec2& P)
		{
			bool bInside = false;
			for (size_t i = 0, j = Polygon.size() - 1; i < Polygon.size(); j = i++)
			{
				const Vec2& A = Polygon[i];
				const Vec2& B = Polygon[j];
				if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
				{
					bInside = !bInside;
				}
			}
			return bInside;
		}
	}

	void VegetationMap::Sample(const Vec2& Position, float& OutTrees, float& OutShrubs, float& OutGrass) const
	{
		OutTrees = OutShrubs = OutGrass = 0.0f;
		if (!IsValid())
		{
			return;
		}
		// between the four nearest cell middles
		const double FX = std::clamp((Position.X - Min.X) / Cell - 0.5, 0.0, double(Width - 1));
		const double FY = std::clamp((Position.Y - Min.Y) / Cell - 0.5, 0.0, double(Height - 1));
		const int32_t X0 = int32_t(FX), Y0 = int32_t(FY);
		const int32_t X1 = std::min(X0 + 1, Width - 1), Y1 = std::min(Y0 + 1, Height - 1);
		const float TX = float(FX - X0), TY = float(FY - Y0);
		auto Get = [this](int32_t X, int32_t Y, int32_t C) { return float(Texels[(size_t(X) * size_t(Height) + size_t(Y)) * 4 + size_t(C)]) / 255.0f; };
		auto Mix = [&](int32_t C)
		{
			const float A = Get(X0, Y0, C) + (Get(X0, Y1, C) - Get(X0, Y0, C)) * TY;
			const float B = Get(X1, Y0, C) + (Get(X1, Y1, C) - Get(X1, Y0, C)) * TY;
			return A + (B - A) * TX;
		};
		OutTrees = Mix(0);
		OutShrubs = Mix(1);
		OutGrass = Mix(2);
	}

	uint8_t VegetationMap::ClassAt(const Vec2& Position) const
	{
		if (!IsValid())
		{
			return LandUnknown;
		}
		const int32_t X = std::clamp(int32_t(std::floor((Position.X - Min.X) / Cell)), 0, Width - 1);
		const int32_t Y = std::clamp(int32_t(std::floor((Position.Y - Min.Y) / Cell)), 0, Height - 1);
		return Texels[(size_t(X) * size_t(Height) + size_t(Y)) * 4 + 3];
	}

	void BuildVegetation(const LandCover& Cover, const Terrain& Ground, const georoads::RoadGraph* Roads,
		const georoads::OsmData& LandWays, VegetationMap& Out, const VegetationOptions& Options)
	{
		using namespace VegetationImpl;
		const double Cell = VegetationMap::Cell;
		const Box2& Bounds = Ground.Bounds;
		Out.Min = Bounds.Min;
		Out.Width = std::max(1, int32_t(std::ceil((Bounds.Max.X - Bounds.Min.X) / Cell)));
		Out.Height = std::max(1, int32_t(std::ceil((Bounds.Max.Y - Bounds.Min.Y) / Cell)));
		Out.Texels.assign(size_t(Out.Width) * size_t(Out.Height) * 4, 0);
		Out.Room.assign(size_t(Out.Width) * size_t(Out.Height), 255);
		// (a cell's room: the nearer of what it has and Distance, cm, negative inside)
		auto MakeRoom = [&Out](int32_t X, int32_t Y, double Distance)
		{
			uint8_t& Room = Out.Room[size_t(X) * size_t(Out.Height) + size_t(Y)];
			Room = std::min(Room, uint8_t(std::clamp(Distance / 100.0 * VegetationMap::RoomScale + VegetationMap::RoomZero, 0.0, 254.0)));
		};
		const double RoomReach = 2000.0;	// cm: how far out from roads and buildings the room is measured
		Out.bFromSatellite = Cover.IsValid();

		// --- what grows in each cell: the satellite's class (else the map's forest or grass), the map's forests added,
		// the water (lakes and the sea) bare, and trees thinned where the ground is too steep to hold them ---
		std::atomic<int64_t> Counts[3] = { 0, 0, 0 };
		auto Row = [&](int32_t X)
		{
			int64_t Trees = 0, Shrubs = 0, Water = 0;
			for (int32_t Y = 0; Y < Out.Height; ++Y)
			{
				const Vec2 P = Out.Min + Vec2((X + 0.5) * Cell, (Y + 0.5) * Cell);
				uint8_t Class = LandUnknown;
				if (Out.bFromSatellite)
				{
					double Lat = 0.0, Lon = 0.0;
					Ground.Projection.ToLatLon(P, Lat, Lon);
					Class = Cover.At(Lat, Lon);
				}
				const uint8_t Mapped = Ground.MappedCover(P);	// 0 grass (or not mapped), 1 forest, 2 beach
				if (Class == LandUnknown)
				{
					Class = Mapped == 1 ? LandTrees : Mapped == 2 ? LandBare : LandGrass;
				}
				Growth G = FromClass(Class);
				if (Mapped == 1 && G.Trees < 200)
				{
					G.Trees = 215;	// a mapped forest the satellite read as something else (felled, young, or the map's older)
					G.Shrubs = std::max<uint8_t>(G.Shrubs, 60);
				}
				const float Height = Ground.DemHeight(P);
				if (Ground.IsWater(P) || Height < 0.0f)
				{
					G = Growth();
					Class = LandWater;
					++Water;
				}
				else
				{
					// the slope over 10 m: none on the rock faces (the ground shader shows rock from ~30 degrees)
					const float DX = Ground.DemHeight(P + Vec2(500.0, 0.0)) - Ground.DemHeight(P - Vec2(500.0, 0.0));
					const float DY = Ground.DemHeight(P + Vec2(0.0, 500.0)) - Ground.DemHeight(P - Vec2(0.0, 500.0));
					const float Slope = std::sqrt(DX * DX + DY * DY) / 1000.0f;
					// (as the ground turns to rock: trees thin from ~30 degrees and are gone by ~38; shrubs cling on to ~48)
					const float Keep = std::clamp(1.0f - (Slope - 0.58f) / 0.2f, 0.0f, 1.0f);
					G.Trees = uint8_t(G.Trees * Keep);
					G.Shrubs = uint8_t(G.Shrubs * std::clamp(1.0f - (Slope - 0.8f) / 0.3f, 0.0f, 1.0f));
				}
				uint8_t* T = TexelAt(Out, X, Y);
				T[0] = G.Trees;
				T[1] = G.Shrubs;
				T[2] = G.Grass;
				T[3] = Class;
				Trees += G.Trees > 100 ? 1 : 0;
				Shrubs += G.Shrubs > 100 ? 1 : 0;
			}
			Counts[0] += Trees;
			Counts[1] += Shrubs;
			Counts[2] += Water;
		};
		if (Options.ParallelFor)
		{
			Options.ParallelFor(Out.Width, Row);
		}
		else
		{
			for (int32_t X = 0; X < Out.Width; ++X)
			{
				Row(X);
			}
		}

		// --- nothing grows on the roads: every cell whose middle is within a road's half width and 2.5 m of it; the room
		// measured to their edges 20 m out ---
		int64_t RoadCells = 0;
		if (Roads)
		{
			for (const georoads::RoadEdge& Edge : Roads->Edges)
			{
				if (Edge.bTunnel || Edge.Points.size() < 2)
				{
					continue;
				}
				const double Clear = Edge.HalfWidth + 250.0;
				const double Reach = Edge.HalfWidth + RoomReach;
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
					const Vec2 AB = B - A;
					const double Length2 = std::max(AB.X * AB.X + AB.Y * AB.Y, 1.0);
					const int32_t X0 = std::max(0, int32_t(std::floor((std::min(A.X, B.X) - Reach - Out.Min.X) / Cell)));
					const int32_t X1 = std::min(Out.Width - 1, int32_t(std::floor((std::max(A.X, B.X) + Reach - Out.Min.X) / Cell)));
					const int32_t Y0 = std::max(0, int32_t(std::floor((std::min(A.Y, B.Y) - Reach - Out.Min.Y) / Cell)));
					const int32_t Y1 = std::min(Out.Height - 1, int32_t(std::floor((std::max(A.Y, B.Y) + Reach - Out.Min.Y) / Cell)));
					for (int32_t X = X0; X <= X1; ++X)
					{
						for (int32_t Y = Y0; Y <= Y1; ++Y)
						{
							const Vec2 P = Out.Min + Vec2((X + 0.5) * Cell, (Y + 0.5) * Cell);
							const double T = std::clamp(((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / Length2, 0.0, 1.0);
							const Vec2 D = P - (A + AB * T);
							const double Distance2 = D.X * D.X + D.Y * D.Y;
							if (Distance2 > Reach * Reach)
							{
								continue;
							}
							MakeRoom(X, Y, std::sqrt(Distance2) - Edge.HalfWidth);
							if (Distance2 <= Clear * Clear)
							{
								uint8_t* Texel = TexelAt(Out, X, Y);
								RoadCells += Texel[0] + Texel[1] > 0 ? 1 : 0;
								Texel[0] = 0;
								Texel[1] = 0;
								Texel[2] = std::min<uint8_t>(Texel[2], 90);	// (the verge: short grass)
							}
						}
					}
				}
			}
		}

		// --- nor in the buildings: every cell whose middle is inside one, and the cell of any small one ---
		int64_t BuildingCells = 0;
		std::vector<Vec2> Polygon;
		for (const georoads::OsmWay& Way : LandWays.Ways)
		{
			if (!Way.Tag("building") || Way.Nodes.size() < 4)
			{
				continue;
			}
			Polygon.clear();
			Box2 Box;
			for (const int64_t Node : Way.Nodes)
			{
				const auto Found = LandWays.Nodes.find(Node);
				if (Found != LandWays.Nodes.end())
				{
					Polygon.push_back(Found->second);
					Box += Found->second;
				}
			}
			if (Polygon.size() < 3)
			{
				continue;
			}
			// the room round it, to its nearest wall (inside it: negative)
			{
				const double Reach = RoomReach * 0.5;
				const int32_t RX0 = std::max(0, int32_t(std::floor((Box.Min.X - Reach - Out.Min.X) / Cell)));
				const int32_t RX1 = std::min(Out.Width - 1, int32_t(std::floor((Box.Max.X + Reach - Out.Min.X) / Cell)));
				const int32_t RY0 = std::max(0, int32_t(std::floor((Box.Min.Y - Reach - Out.Min.Y) / Cell)));
				const int32_t RY1 = std::min(Out.Height - 1, int32_t(std::floor((Box.Max.Y + Reach - Out.Min.Y) / Cell)));
				for (int32_t X = RX0; X <= RX1; ++X)
				{
					for (int32_t Y = RY0; Y <= RY1; ++Y)
					{
						const Vec2 P = Out.Min + Vec2((X + 0.5) * Cell, (Y + 0.5) * Cell);
						double Nearest2 = 1e30;
						for (size_t i = 0; i < Polygon.size(); ++i)
						{
							const Vec2 A = Polygon[i], AB = Polygon[(i + 1) % Polygon.size()] - A;
							const double T = std::clamp(((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / std::max(AB.X * AB.X + AB.Y * AB.Y, 1.0), 0.0, 1.0);
							const Vec2 D = P - (A + AB * T);
							Nearest2 = std::min(Nearest2, D.X * D.X + D.Y * D.Y);
						}
						const double Nearest = std::sqrt(Nearest2);
						if (Nearest <= Reach)
						{
							MakeRoom(X, Y, InsidePolygon(Polygon, P) ? -Nearest : Nearest);
						}
					}
				}
			}
			const int32_t X0 = std::max(0, int32_t(std::floor((Box.Min.X - Out.Min.X) / Cell)));
			const int32_t X1 = std::min(Out.Width - 1, int32_t(std::floor((Box.Max.X - Out.Min.X) / Cell)));
			const int32_t Y0 = std::max(0, int32_t(std::floor((Box.Min.Y - Out.Min.Y) / Cell)));
			const int32_t Y1 = std::min(Out.Height - 1, int32_t(std::floor((Box.Max.Y - Out.Min.Y) / Cell)));
			const bool bSmall = X1 - X0 <= 1 && Y1 - Y0 <= 1;
			for (int32_t X = X0; X <= X1; ++X)
			{
				for (int32_t Y = Y0; Y <= Y1; ++Y)
				{
					const Vec2 P = Out.Min + Vec2((X + 0.5) * Cell, (Y + 0.5) * Cell);
					if (bSmall || InsidePolygon(Polygon, P))
					{
						uint8_t* Texel = TexelAt(Out, X, Y);
						BuildingCells += Texel[0] + Texel[1] > 0 ? 1 : 0;
						Texel[0] = 0;
						Texel[1] = 0;
						Texel[2] = 0;
					}
				}
			}
		}

		if (Options.Log)
		{
			const double All = double(Out.Width) * double(Out.Height);
			char Line[256];
			std::snprintf(Line, sizeof(Line), "[Vegetation] %d x %d cells (%.0f m) from %s: trees %.0f%%, shrubs %.0f%%, water %.0f%%; cleared %lld on roads, %lld in buildings",
				Out.Width, Out.Height, Cell / 100.0, Out.bFromSatellite ? "the satellite's land cover (ESA WorldCover)" : "the map's forests (no land cover)",
				100.0 * double(Counts[0]) / All, 100.0 * double(Counts[1]) / All, 100.0 * double(Counts[2]) / All,
				(long long)RoadCells, (long long)BuildingCells);
			Options.Log(Line);
		}
	}

	int64_t ClearPlants(VegetationMap& Map, const std::vector<Vec2>& Line, double Reach, bool bArea)
	{
		int64_t Cleared = 0;
		if (!Map.IsValid() || Line.size() < 2)
		{
			return 0;
		}
		const double Cell = VegetationMap::Cell;
		Box2 Box;
		for (const Vec2& P : Line)
		{
			Box += P;
		}
		const int32_t X0 = std::max(0, int32_t(std::floor((Box.Min.X - Reach - Map.Min.X) / Cell)));
		const int32_t X1 = std::min(Map.Width - 1, int32_t(std::floor((Box.Max.X + Reach - Map.Min.X) / Cell)));
		const int32_t Y0 = std::max(0, int32_t(std::floor((Box.Min.Y - Reach - Map.Min.Y) / Cell)));
		const int32_t Y1 = std::min(Map.Height - 1, int32_t(std::floor((Box.Max.Y + Reach - Map.Min.Y) / Cell)));
		for (int32_t X = X0; X <= X1; ++X)
		{
			for (int32_t Y = Y0; Y <= Y1; ++Y)
			{
				const Vec2 P = Map.Min + Vec2((X + 0.5) * Cell, (Y + 0.5) * Cell);
				double Nearest2 = 1e30;
				for (size_t i = 0; i + 1 < Line.size(); ++i)
				{
					const Vec2 A = Line[i], AB = Line[i + 1] - A;
					const double T = std::clamp(((P.X - A.X) * AB.X + (P.Y - A.Y) * AB.Y) / std::max(AB.X * AB.X + AB.Y * AB.Y, 1.0), 0.0, 1.0);
					const Vec2 D = P - (A + AB * T);
					Nearest2 = std::min(Nearest2, D.X * D.X + D.Y * D.Y);
				}
				const bool bInside = bArea && VegetationImpl::InsidePolygon(Line, P);
				if (!bInside && Nearest2 > Reach * Reach)
				{
					continue;
				}
				uint8_t* Texel = &Map.Texels[(size_t(X) * size_t(Map.Height) + size_t(Y)) * 4];
				++Cleared;
				Texel[0] = 0;
				Texel[1] = 0;
				Texel[2] = std::min<uint8_t>(Texel[2], 120);	// (mown grass)
				if (!Map.Room.empty())
				{
					Map.Room[size_t(X) * size_t(Map.Height) + size_t(Y)] = uint8_t(VegetationMap::RoomZero);	// (no room: the blanket dips here too)
				}
			}
		}
		return Cleared;
	}
}
