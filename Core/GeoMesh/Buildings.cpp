// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadMesh.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <unordered_map>

namespace geomesh
{
	using georoads::Vec2;
	using georoads::RoadEdge;
	using geoground::Vec2f;
	using geoground::Vec3f;
	using geoground::Vec4f;

	namespace BuildingsImpl
	{
		/** Building storey height (cm) */
		constexpr float LevelHeight = 300.0f;

		Vec3f At(const Vec2& P, float Z) { return Vec3f(static_cast<float>(P.X), static_cast<float>(P.Y), Z); }

		/** True if P is inside the polygon (even-odd rule) */
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

		/** A tag's number ("12", "12 m", "12m"): false (and 0) if it isn't one or isn't above 0 */
		bool TagNumber(const georoads::OsmWay& Way, const char* Key, float& Out, bool bZeroIfNot)
		{
			const std::string* Value = Way.Tag(Key);
			if (!Value)
			{
				return false;
			}
			std::string Clean;
			for (const char C : *Value)
			{
				if (C != 'm')
				{
					Clean += C;
				}
			}
			size_t First = 0, Last = Clean.size();
			while (First < Last && std::isspace(static_cast<unsigned char>(Clean[First]))) { ++First; }
			while (Last > First && std::isspace(static_cast<unsigned char>(Clean[Last - 1]))) { --Last; }
			Clean = Clean.substr(First, Last - First);
			size_t i = (!Clean.empty() && (Clean[0] == '-' || Clean[0] == '+')) ? 1 : 0;
			bool bDot = false, bNumeric = true;
			for (; i < Clean.size(); ++i)
			{
				if (Clean[i] == '.' && !bDot)
				{
					bDot = true;
				}
				else if (!std::isdigit(static_cast<unsigned char>(Clean[i])))
				{
					bNumeric = false;
					break;
				}
			}
			if (!bNumeric)
			{
				if (bZeroIfNot)
				{
					Out = 0.0f;
				}
				return false;
			}
			Out = static_cast<float>(std::atof(Clean.c_str()));
			return Out > 0.0f;
		}

		/**
		 *  How high above the ground a building starts (cm), 0 for most. Some stand over a street: a building that bridges
		 *  it, a petrol station's canopy, a carport. Tagged min_height / building:min_level first; otherwise a bridge or a
		 *  raised layer clears a lorry, and a roof or canopy is just its roof.
		 */
		float BuildingMinHeight(const georoads::OsmWay& Way, float Height)
		{
			float Value = 0.0f;
			if (TagNumber(Way, "min_height", Value, true))
			{
				return std::min(Value * 100.0f, Height - 50.0f);
			}
			if (TagNumber(Way, "building:min_level", Value, true))
			{
				return std::min(Value * 300.0f, Height - 50.0f);
			}
			const std::string* Type = Way.Tag("building");
			if (Type && (*Type == "roof" || *Type == "carport" || *Type == "canopy"))
			{
				return std::max(Height - 60.0f, 0.0f);
			}
			const std::string* Layer = Way.Tag("layer");
			if ((Type && *Type == "bridge") || (Layer && std::atoi(Layer->c_str()) >= 1))
			{
				return std::min(500.0f, Height - 50.0f);
			}
			return 0.0f;
		}

		float BuildingHeight(const georoads::OsmWay& Way)
		{
			float Value = 0.0f;
			if (TagNumber(Way, "height", Value, false))
			{
				return std::min(Value * 100.0f, 50000.0f);
			}
			if (TagNumber(Way, "building:levels", Value, false))
			{
				return std::min(Value * LevelHeight + 100.0f, 50000.0f);
			}
			static const std::unordered_map<std::string, float> Defaults = {
				{ "house", 600.0f }, { "detached", 600.0f }, { "semidetached_house", 600.0f }, { "terrace", 700.0f },
				{ "residential", 700.0f }, { "garage", 300.0f }, { "garages", 300.0f }, { "shed", 300.0f },
				{ "roof", 400.0f }, { "apartments", 1500.0f }, { "commercial", 1200.0f }, { "retail", 800.0f },
				{ "office", 1500.0f }, { "industrial", 900.0f }, { "warehouse", 900.0f }, { "church", 1500.0f } };
			const std::string* Type = Way.Tag("building");
			if (Type)
			{
				const auto Found = Defaults.find(*Type);
				if (Found != Defaults.end())
				{
					return Found->second;
				}
			}
			return 800.0f;
		}

		// What a building looks like, for a client's own shader (as its corners' colour; a plain material ignores it):
		// R its kind (0.1 a house, 0.3 industrial or a shed, 0.5 flats, 0.7 offices and shops, 0.9 a tower over 60 m),
		// G a number of its own (0-1: its colour, its windows), B its walls when the map says (0 not said, 0.2 brick,
		// 0.4 stone, 0.6 concrete, 0.8 glass, 1 plaster), A 1
		Vec4f BuildingLook(const georoads::OsmWay& Way, float Height)
		{
			static const std::unordered_map<std::string, float> Kinds = {
				{ "house", 0.1f }, { "detached", 0.1f }, { "semidetached_house", 0.1f }, { "terrace", 0.1f }, { "bungalow", 0.1f },
				{ "farm", 0.1f }, { "hut", 0.1f }, { "cabin", 0.1f },
				{ "industrial", 0.3f }, { "warehouse", 0.3f }, { "shed", 0.3f }, { "garage", 0.3f }, { "garages", 0.3f },
				{ "barn", 0.3f }, { "farm_auxiliary", 0.3f }, { "storage_tank", 0.3f }, { "service", 0.3f }, { "roof", 0.3f },
				{ "apartments", 0.5f }, { "dormitory", 0.5f },
				{ "commercial", 0.7f }, { "office", 0.7f }, { "retail", 0.7f }, { "hotel", 0.7f }, { "supermarket", 0.7f },
				{ "public", 0.7f }, { "civic", 0.7f }, { "government", 0.7f }, { "hospital", 0.7f }, { "school", 0.7f },
				{ "university", 0.7f }, { "college", 0.7f } };
			static const std::unordered_map<std::string, float> Walls = {
				{ "brick", 0.2f }, { "stone", 0.4f }, { "sandstone", 0.4f }, { "limestone", 0.4f }, { "concrete", 0.6f },
				{ "glass", 0.8f }, { "plaster", 1.0f }, { "render", 1.0f }, { "cement_block", 0.6f } };
			float Kind = Height >= 1300.0f ? 0.5f : 0.1f;	// (not said: flats if tall, else a house)
			if (const std::string* Type = Way.Tag("building"))
			{
				const auto Found = Kinds.find(*Type);
				Kind = Found != Kinds.end() ? Found->second : Kind;
			}
			if (Height >= 6000.0f)
			{
				Kind = 0.9f;
			}
			else if (Kind < 0.5f && Height >= 1300.0f && Kind != 0.3f)
			{
				Kind = 0.5f;	// (a "house" four floors up is flats)
			}
			float Wall = 0.0f;
			if (const std::string* Material = Way.Tag("building:material"))
			{
				const auto Found = Walls.find(*Material);
				Wall = Found != Walls.end() ? Found->second : 0.0f;
			}
			const uint64_t Mixed = static_cast<uint64_t>(Way.Id) * 0x9E3779B97F4A7C15ull;
			const float Own = static_cast<float>((Mixed >> 40) & 0xFFFF) / 65535.0f;
			return Vec4f{ Kind, Own, Wall, 1.0f };
		}

		uint64_t CellKey(int32_t X, int32_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); }
	}

	void BuildBuildings(const georoads::OsmData& Data, const std::function<float(const Vec2&)>& GroundHeight, BuildingMeshes& Out,
		const RoadGraph* Roads, const MeshOptions& Options)
	{
		using namespace BuildingsImpl;
		MeshData& Mesh = Out.Buildings;

		// drivable road segments by 50 m cell, with their heights and widths, to find roads that run through a building
		// (a passage under it) or over it (a bridge or a raised road: the building must fit under the deck)
		static constexpr double PassageCell = 5000.0;
		struct RoadPiece
		{
			Vec2 A, B;
			float HeightA = 0.0f, HeightB = 0.0f;
			float HalfWidth = 0.0f;
		};
		std::unordered_map<uint64_t, std::vector<RoadPiece>> RoadCells;
		if (Roads)
		{
			for (const RoadEdge& Edge : Roads->Edges)
			{
				if (Edge.bTunnel || Edge.Heights.size() != Edge.Points.size())
				{
					continue;	// a tunnel is underground
				}
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					const RoadPiece Piece{ Edge.Points[i], Edge.Points[i + 1], Edge.Heights[i], Edge.Heights[i + 1], Edge.HalfWidth };
					const int32_t MinX = geoground::FloorToInt((std::min(Piece.A.X, Piece.B.X) - Piece.HalfWidth) / PassageCell);
					const int32_t MinY = geoground::FloorToInt((std::min(Piece.A.Y, Piece.B.Y) - Piece.HalfWidth) / PassageCell);
					const int32_t MaxX = geoground::FloorToInt((std::max(Piece.A.X, Piece.B.X) + Piece.HalfWidth) / PassageCell);
					const int32_t MaxY = geoground::FloorToInt((std::max(Piece.A.Y, Piece.B.Y) + Piece.HalfWidth) / PassageCell);
					for (int32_t X = MinX; X <= MaxX; ++X)
					{
						for (int32_t Y = MinY; Y <= MaxY; ++Y)
						{
							RoadCells[CellKey(X, Y)].push_back(Piece);
						}
					}
				}
			}
		}
		// the road surface inside a footprint, every metre down the middle and along both edges: its height at each spot
		auto RoadsInside = [&RoadCells](const std::vector<Vec2>& Footprint, std::vector<float>& OutHeights)
		{
			geoground::Box2 Box;
			for (const Vec2& P : Footprint)
			{
				Box += P;
			}
			const int32_t MinX = geoground::FloorToInt(Box.Min.X / PassageCell), MinY = geoground::FloorToInt(Box.Min.Y / PassageCell);
			const int32_t MaxX = geoground::FloorToInt(Box.Max.X / PassageCell), MaxY = geoground::FloorToInt(Box.Max.Y / PassageCell);
			for (int32_t X = MinX; X <= MaxX; ++X)
			{
				for (int32_t Y = MinY; Y <= MaxY; ++Y)
				{
					const auto Found = RoadCells.find(CellKey(X, Y));
					if (Found == RoadCells.end())
					{
						continue;
					}
					for (const RoadPiece& Piece : Found->second)
					{
						const double Length = georoads::Distance(Piece.A, Piece.B);
						if (Length < 1.0)
						{
							continue;
						}
						const Vec2 Side = Vec2(Piece.A.Y - Piece.B.Y, Piece.B.X - Piece.A.X) * (1.0 / Length);
						for (double S = 50.0; S < Length; S += 100.0)
						{
							const Vec2 Middle = Piece.A + (Piece.B - Piece.A) * (S / Length);
							for (const double Across : { 0.0, -0.8, 0.8 })
							{
								const Vec2 P = Middle + Side * (Across * Piece.HalfWidth);
								if (Box.IsInside(P) && InsidePolygon(Footprint, P))
								{
									OutHeights.push_back(geoground::LerpF(Piece.HeightA, Piece.HeightB, static_cast<float>(S / Length)));
								}
							}
						}
					}
				}
			}
		};

		// the buildings in batches on the worker pool, each into its own mesh, joined in order after (the same mesh as one
		// at a time): ~35,000 of them in a city's piece took ~2 s on one thread
		struct BuildingBatch
		{
			MeshData Mesh;
			std::vector<BuildingOutline> Outlines;	// (its triangles tagged with these, numbered in the batch)
			int32_t NumBuildings = 0, NumRaised = 0, NumCut = 0, NumOnRoads = 0;
		};
		const int32_t NumWays = static_cast<int32_t>(Data.Ways.size());
		std::vector<BuildingBatch> Batches(std::clamp(NumWays / 256, 1, 256));
		const int32_t NumBatches = static_cast<int32_t>(Batches.size());
		const georoads::ParallelForFn ParallelFor = Options.ParallelFor ? Options.ParallelFor : georoads::ParallelForFn(&georoads::ThreadsParallelFor);
		ParallelFor(NumBatches, [&](int32_t BatchIndex)
		{
			BuildingBatch& Batch = Batches[size_t(BatchIndex)];
			MeshData& Mesh = Batch.Mesh;	// (this batch's)
			std::vector<Vec2> Footprint;
			std::vector<float> Ground, RoadHeights;
			std::vector<int32_t> RoofIndices;
			const int32_t FirstWay = static_cast<int32_t>(static_cast<int64_t>(NumWays) * BatchIndex / NumBatches);
			const int32_t EndWay = static_cast<int32_t>(static_cast<int64_t>(NumWays) * (BatchIndex + 1) / NumBatches);
			for (int32_t WayIndex = FirstWay; WayIndex < EndWay; ++WayIndex)
			{
				const georoads::OsmWay& Way = Data.Ways[size_t(WayIndex)];
				if (!Way.Tag("building") || Way.Nodes.size() < 4)
				{
					continue;
				}

				Footprint.clear();
				for (const int64_t NodeId : Way.Nodes)
				{
					const auto Found = Data.Nodes.find(NodeId);
					if (Found != Data.Nodes.end())
					{
						const Vec2& Position = Found->second;
						if (Footprint.empty() || std::abs(Footprint.back().X - Position.X) > 1.0 || std::abs(Footprint.back().Y - Position.Y) > 1.0)
						{
							Footprint.push_back(Position);
						}
					}
				}
				if (Footprint.size() > 1 && std::abs(Footprint[0].X - Footprint.back().X) <= 1.0 && std::abs(Footprint[0].Y - Footprint.back().Y) <= 1.0)
				{
					Footprint.pop_back();
				}
				if (Footprint.size() < 3)
				{
					continue;
				}

				// counter-clockwise footprint, so walls face outwards and ear clipping works
				const double Area = geoground::SignedArea(Footprint);
				if (std::abs(Area) < 50.0)
				{
					continue;
				}
				if (Area < 0.0)
				{
					std::reverse(Footprint.begin(), Footprint.end());
				}

				// sit on the slope: floor near the median ground height, foundation down to the lowest point
				Ground.clear();
				for (const Vec2& Point : Footprint)
				{
					Ground.push_back(GroundHeight(Point));
				}
				std::sort(Ground.begin(), Ground.end());
				const float Lowest = Ground[0];
				const float Floor = std::max(Lowest, Ground[Ground.size() / 2] - 100.0f);
				const float Height = BuildingHeight(Way);
				const float Raised = BuildingMinHeight(Way, Height);
				float Base = Raised > 0.0f ? Floor + Raised : Lowest - 50.0f;
				float Top = Floor + Height;

				// nothing but the road in the road's space: a road through the building at ground level goes through a passage
				// 4.5 m high under it; a road over it (a bridge, a raised road) takes the top off it, down to 1.5 m under the
				// deck. What is left too low to be a building (under 2.5 m, or nothing above the passage) goes.
				bool bRaised = Raised > 0.0f;
				if (Roads)
				{
					RoadHeights.clear();
					RoadsInside(Footprint, RoadHeights);
					int32_t AtGround = 0, Above = 0;
					float GroundRoadTop = -std::numeric_limits<float>::max(), Deck = std::numeric_limits<float>::max();
					for (const float H : RoadHeights)
					{
						if (H < Floor + 300.0f)
						{
							++AtGround;
							GroundRoadTop = std::max(GroundRoadTop, H);
						}
						else
						{
							++Above;
							Deck = std::min(Deck, H);
						}
					}
					bool bTouched = false;
					if (AtGround >= 6)	// more than a corner clipped: about 2 m of road really goes through
					{
						Base = std::max(Base, std::max(Floor, GroundRoadTop) + 450.0f);
						bRaised = bTouched = true;
					}
					if (Above >= 2 && Top > Deck - 150.0f)
					{
						Top = Deck - 150.0f;
						bTouched = true;
					}
					if (bTouched && Top - Base < 250.0f)
					{
						++Batch.NumOnRoads;
						continue;
					}
					Batch.NumCut += bTouched ? 1 : 0;
				}
				Batch.NumRaised += bRaised ? 1 : 0;

				// which it is (its triangles tagged with it: so one can be taken out of the map's tiles)
				BuildingOutline& Outline = Batch.Outlines.emplace_back();
				Outline.Id = Way.Id;
				Outline.Footprint = Footprint;
				Outline.Min = Outline.Max = Footprint[0];
				for (const Vec2& Point : Footprint)
				{
					Outline.Min = Vec2(std::min(Outline.Min.X, Point.X), std::min(Outline.Min.Y, Point.Y));
					Outline.Max = Vec2(std::max(Outline.Max.X, Point.X), std::max(Outline.Max.Y, Point.Y));
				}
				Outline.Base = Base;
				Outline.Top = Top;
				Mesh.Tag = static_cast<int32_t>(Batch.Outlines.size()) - 1;

				// its look, as its corners' colour (white for everything else in this mesh)
				const size_t FirstCorner = Mesh.Positions.size();
				Mesh.Colors.resize(FirstCorner, Vec4f{ 1, 1, 1, 1 });
				const Vec4f Look = BuildingLook(Way, Top - Base);

				// walls
				for (size_t i = 0; i < Footprint.size(); ++i)
				{
					const Vec2& A = Footprint[i];
					const Vec2& B = Footprint[(i + 1) % Footprint.size()];
					const Vec2 Edge = B - A;
					const Vec2 Outward = Vec2(Edge.Y, -Edge.X).Normal();
					const Vec3f Normal(static_cast<float>(Outward.X), static_cast<float>(Outward.Y), 0.0f);

					const Vec3f A0 = At(A, Base), B0 = At(B, Base), A1 = At(A, Top), B1 = At(B, Top);
					const float U = static_cast<float>(Edge.Size() / 300.0);
					const float VTop = (Top - Base) / 300.0f;
					Mesh.AddTriangle(A0, B0, B1, Normal, Vec2f{ 0, 0 }, Vec2f{ U, 0 }, Vec2f{ U, VTop });
					Mesh.AddTriangle(A0, B1, A1, Normal, Vec2f{ 0, 0 }, Vec2f{ U, VTop }, Vec2f{ 0, VTop });
				}

				// flat roof
				RoofIndices.clear();
				geoground::Triangulate(Footprint, RoofIndices);
				const Vec3f Up(0.0f, 0.0f, 1.0f);
				for (size_t i = 0; i + 2 < RoofIndices.size(); i += 3)
				{
					const Vec2& A = Footprint[RoofIndices[i]];
					const Vec2& B = Footprint[RoofIndices[i + 1]];
					const Vec2& C = Footprint[RoofIndices[i + 2]];
					Mesh.AddTriangle(At(A, Top), At(B, Top), At(C, Top), Up,
						Vec2f{ float(A.X / 300.0), float(A.Y / 300.0) }, Vec2f{ float(B.X / 300.0), float(B.Y / 300.0) }, Vec2f{ float(C.X / 300.0), float(C.Y / 300.0) });
					if (Raised > 0.0f)
					{
						// the underside, seen from the street below
						Mesh.AddTriangle(At(A, Base), At(C, Base), At(B, Base), -Up,
							Vec2f{ float(A.X / 300.0), float(A.Y / 300.0) }, Vec2f{ float(C.X / 300.0), float(C.Y / 300.0) }, Vec2f{ float(B.X / 300.0), float(B.Y / 300.0) });
					}
				}

				Mesh.Colors.resize(Mesh.Positions.size());
				std::fill(Mesh.Colors.begin() + std::ptrdiff_t(FirstCorner), Mesh.Colors.end(), Look);
				++Batch.NumBuildings;
			}
		});
		for (const BuildingBatch& Batch : Batches)
		{
			const int32_t Base = static_cast<int32_t>(Mesh.Positions.size());
			Mesh.Positions.insert(Mesh.Positions.end(), Batch.Mesh.Positions.begin(), Batch.Mesh.Positions.end());
			Mesh.Normals.insert(Mesh.Normals.end(), Batch.Mesh.Normals.begin(), Batch.Mesh.Normals.end());
			Mesh.UVs.insert(Mesh.UVs.end(), Batch.Mesh.UVs.begin(), Batch.Mesh.UVs.end());
			Mesh.Colors.resize(Base, Vec4f{ 1, 1, 1, 1 });
			Mesh.Colors.insert(Mesh.Colors.end(), Batch.Mesh.Colors.begin(), Batch.Mesh.Colors.end());
			const int32_t FirstOutline = static_cast<int32_t>(Out.Outlines.size());
			for (const int32_t Tag : Batch.Mesh.Tags)
			{
				Mesh.Tags.push_back(Tag >= 0 ? Tag + FirstOutline : Tag);
			}
			Out.Outlines.insert(Out.Outlines.end(), Batch.Outlines.begin(), Batch.Outlines.end());
			for (const int32_t Index : Batch.Mesh.Indices)
			{
				Mesh.Indices.push_back(Base + Index);
			}
			Out.NumBuildings += Batch.NumBuildings;
			Out.NumRaisedBuildings += Batch.NumRaised;
			Out.NumBuildingsCut += Batch.NumCut;
			Out.NumBuildingsOnRoads += Batch.NumOnRoads;
		}

		if (!Roads)
		{
			return;
		}
		// (what's made from here on is infrastructure, not buildings: R 0 in its corners' colour, bare concrete in the
		// buildings' shader, no windows; without a colour of its own it was white, a glass tower's: windows in a tunnel)
		const size_t FirstWorks = Mesh.Positions.size();

		// open galleries over roads (tunnel=avalanche_protector, the Chapman's Peak half-tunnel): a concrete roof from the
		// cliff out over the road, on square columns along the open side every few metres. The open side is the lower one.
		constexpr float Clearance = 450.0f, Thickness = 50.0f, ColumnSize = 50.0f, ColumnStep = 500.0f, IntoCliff = 150.0f, Overhang = 60.0f;
		auto Quad = [&Mesh](const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& D, const Vec3f& Normal)
		{
			Mesh.AddTriangle(A, B, C, Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 0 }, Vec2f{ 1, 1 });
			Mesh.AddTriangle(A, C, D, Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 1 }, Vec2f{ 0, 1 });
		};
		auto P = [](const Vec2& XY, float Z) { return Vec3f(float(XY.X), float(XY.Y), Z); };
		// stations along a road every Step at most: position, direction, road height
		auto Stations = [](const RoadEdge& Edge, float Step, std::vector<Vec2>& Points, std::vector<Vec2>& Dirs, std::vector<float>& Heights)
		{
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
				const int32_t Steps = std::max(1, static_cast<int32_t>(std::ceil(georoads::Distance(A, B) / Step)));
				for (int32_t s = 0; s < Steps; ++s)
				{
					const float T = float(s) / Steps;
					Points.push_back(A + (B - A) * double(T));
					Dirs.push_back((B - A).Normal());
					Heights.push_back(geoground::LerpF(Edge.Heights[i], Edge.Heights[i + 1], T));
				}
			}
			Points.push_back(Edge.Points.back());
			const Vec2 LastDir = Dirs.back();
			Dirs.push_back(LastDir);
			Heights.push_back(Edge.Heights.back());
		};
		int32_t Galleries = 0;
		for (const RoadEdge& Edge : Roads->Edges)
		{
			if (!Edge.bGallery || Edge.Points.size() < 2 || Edge.Heights.size() != Edge.Points.size())
			{
				continue;
			}
			// which side is open: the lower ground beside the middle of the gallery
			const size_t Mid = Edge.Points.size() / 2;
			const Vec2 MidDir = (Edge.Points[std::min(Mid + 1, Edge.Points.size() - 1)] - Edge.Points[Mid > 0 ? Mid - 1 : 0]).Normal();
			const Vec2 MidLeft(-MidDir.Y, MidDir.X);
			const float Reach = Edge.HalfWidth + 800.0f;
			const float OpenSign = GroundHeight(Edge.Points[Mid] + MidLeft * Reach) < GroundHeight(Edge.Points[Mid] - MidLeft * Reach) ? 1.0f : -1.0f;

			std::vector<Vec2> Points, Dirs;
			std::vector<float> Heights;
			Stations(Edge, ColumnStep, Points, Dirs, Heights);
			for (size_t i = 0; i < Points.size(); ++i)
			{
				const Vec2 Open = Vec2(-Dirs[i].Y, Dirs[i].X) * OpenSign;
				const float Road = Heights[i];
				// the roof: from inside the cliff to just past the columns, as a slab between this station and the next
				if (i + 1 < Points.size())
				{
					const Vec2 OpenNext = Vec2(-Dirs[i + 1].Y, Dirs[i + 1].X) * OpenSign;
					const Vec2 InA = Points[i] - Open * (Edge.HalfWidth + IntoCliff), OutA = Points[i] + Open * (Edge.HalfWidth + Overhang);
					const Vec2 InB = Points[i + 1] - OpenNext * (Edge.HalfWidth + IntoCliff), OutB = Points[i + 1] + OpenNext * (Edge.HalfWidth + Overhang);
					const float TopA = Road + Clearance + Thickness, TopB = Heights[i + 1] + Clearance + Thickness;
					const float LowA = Road + Clearance, LowB = Heights[i + 1] + Clearance;
					const Vec3f Up(0, 0, 1), Down(0, 0, -1), Side(float(Open.X), float(Open.Y), 0.0f);
					Quad(P(InA, TopA), P(InB, TopB), P(OutB, TopB), P(OutA, TopA), Up);
					Quad(P(InA, LowA), P(OutA, LowA), P(OutB, LowB), P(InB, LowB), Down);
					Quad(P(OutA, LowA), P(OutA, TopA), P(OutB, TopB), P(OutB, LowB), Side);
				}
				// a column on the open side, from below the road edge up to the roof
				const Vec2 Foot = Points[i] + Open * (Edge.HalfWidth + ColumnSize * 0.5f + 20.0f);
				const Vec2 Along = Dirs[i] * (ColumnSize * 0.5f), Across = Open * (ColumnSize * 0.5f);
				const Vec2 Corners[4] = { Foot - Along - Across, Foot + Along - Across, Foot + Along + Across, Foot - Along + Across };
				const float Bottom = Road - 100.0f, Top = Road + Clearance;
				for (int32_t c = 0; c < 4; ++c)
				{
					const Vec2& A = Corners[c];
					const Vec2& B = Corners[(c + 1) % 4];
					const Vec2 Normal2 = Vec2((B - A).Y, -(B - A).X).Normal();
					Quad(Vec3f(float(A.X), float(A.Y), Bottom), Vec3f(float(B.X), float(B.Y), Bottom), Vec3f(float(B.X), float(B.Y), Top),
						Vec3f(float(A.X), float(A.Y), Top), Vec3f(float(Normal2.X), float(Normal2.Y), 0.0f));
				}
			}
			++Galleries;
		}
		if (Galleries > 0 && Options.Log)
		{
			Options.Log(georoads::Format("[Buildings] %d open galleries over roads (roof on columns along the open side)", Galleries));
		}

		// tunnels: a tube along the road (walls and a ceiling, seen from inside and out: in the portal cut its outside is in
		// the open) and at each mouth a portal wall round the opening, bigger than the cut so it hides its edges
		// (the portal a frame round the tube: it was 12 m high and 6 m out each side, into the roads round a city's mouths)
		constexpr float TubeHeight = 650.0f, TubeMargin = 50.0f, StationStep = 1000.0f, PortalSide = 200.0f, PortalTop = 750.0f, PortalBelow = 300.0f;	// (top: the road over it, at its clearance)
		auto DoubleQuad = [&Mesh](const Vec3f& A, const Vec3f& B, const Vec3f& C, const Vec3f& D, const Vec3f& Normal)
		{
			Mesh.AddTriangle(A, B, C, Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 0 }, Vec2f{ 1, 1 });
			Mesh.AddTriangle(A, C, D, Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 1 }, Vec2f{ 0, 1 });
			Mesh.AddTriangle(A, C, B, -Normal, Vec2f{ 0, 0 }, Vec2f{ 1, 1 }, Vec2f{ 1, 0 });
			Mesh.AddTriangle(A, D, C, -Normal, Vec2f{ 0, 0 }, Vec2f{ 0, 1 }, Vec2f{ 1, 1 });
		};
		int32_t Tunnels = 0;
		auto CarriesOnUnderground = [](const georoads::RoadGraph& Graph, const RoadEdge& Edge, bool bStart)
		{
			const int32_t Node = bStart ? Edge.StartNode : Edge.EndNode;
			if (Node < 0 || Node >= int32_t(Graph.Nodes.size()))
			{
				return false;
			}
			int32_t Tunnels = 0;
			for (const int32_t Other : Graph.Nodes[Node].Edges)
			{
				Tunnels += Graph.Edges[Other].bTunnel ? 1 : 0;
			}
			return Tunnels >= 2;	// (this one and another)
		};
		for (const RoadEdge& Edge : Roads->Edges)
		{
			if (!Edge.bTunnel || Edge.Points.size() < 2 || Edge.Heights.size() != Edge.Points.size())
			{
				continue;
			}
			std::vector<Vec2> Points, Dirs;
			std::vector<float> Heights;
			Stations(Edge, StationStep * 0.4f, Points, Dirs, Heights);	// (4 m: round a bend, a roundabout's, the walls follow it)

			const float Half = Edge.HalfWidth + TubeMargin;
			// where it meets others underground (a junction, a roundabout's entries), a wall standing in another tunnel road's
			// lanes isn't there, so they meet; the rest of its walls stay (open, you saw out into the ground)
			auto InOtherTunnel = [&](const Vec2& Where)
			{
				for (const RoadEdge& Other : Roads->Edges)
				{
					if (&Other == &Edge || !Other.bTunnel || Other.Points.size() < 2)
					{
						continue;
					}
					for (size_t k = 0; k + 1 < Other.Points.size(); ++k)
					{
						const Vec2 A = Other.Points[k], AB = Other.Points[k + 1] - A;
						const double Length2 = AB.SizeSquared();
						const double T = Length2 > 1.0 ? std::clamp(georoads::Dot(Where - A, AB) / Length2, 0.0, 1.0) : 0.0;
						if (georoads::Distance(A + AB * T, Where) < Other.HalfWidth + 50.0f)
						{
							return true;
						}
					}
				}
				return false;
			};
			for (size_t i = 0; i + 1 < Points.size(); ++i)
			{
				const Vec2 LA(-Dirs[i].Y, Dirs[i].X), LB(-Dirs[i + 1].Y, Dirs[i + 1].X);
				const float ZA = Heights[i], ZB = Heights[i + 1];
				const Vec2 LeftA = Points[i] + LA * Half, LeftB = Points[i + 1] + LB * Half;
				const Vec2 RightA = Points[i] - LA * Half, RightB = Points[i + 1] - LB * Half;
				const Vec3f Side(float(LA.X), float(LA.Y), 0.0f);
				if (!InOtherTunnel((LeftA + LeftB) * 0.5))
				{
					DoubleQuad(P(LeftA, ZA - 50.0f), P(LeftB, ZB - 50.0f), P(LeftB, ZB + TubeHeight), P(LeftA, ZA + TubeHeight), -Side);
				}
				if (!InOtherTunnel((RightA + RightB) * 0.5))
				{
					DoubleQuad(P(RightA, ZA - 50.0f), P(RightB, ZB - 50.0f), P(RightB, ZB + TubeHeight), P(RightA, ZA + TubeHeight), Side);
				}
				DoubleQuad(P(LeftA, ZA + TubeHeight), P(LeftB, ZB + TubeHeight), P(RightB, ZB + TubeHeight), P(RightA, ZA + TubeHeight), Vec3f(0, 0, -1));
			}
			// the portal walls, across the road at each mouth: where it comes out, not where it carries on underground (a
			// tunnel's mapped in pieces, an underground roundabout in many: a wall at every one was a wall across the road)
			for (const bool bStart : { true, false })
			{
				if (CarriesOnUnderground(*Roads, Edge, bStart))
				{
					continue;
				}
				const Vec2 Mouth = bStart ? Points[0] : Points.back();
				const Vec2 Dir = bStart ? Dirs[0] : Dirs.back();
				const float Z = bStart ? Heights[0] : Heights.back();
				const Vec2 Left(-Dir.Y, Dir.X);
				const float Outer = Half + PortalSide;
				const Vec3f Facing(float(Dir.X), float(Dir.Y), 0.0f);
				auto AtMouth = [&](float Across, float Up) { return P(Mouth + Left * Across, Z + Up); };
				DoubleQuad(AtMouth(-Outer, -PortalBelow), AtMouth(-Half, -PortalBelow), AtMouth(-Half, PortalTop), AtMouth(-Outer, PortalTop), Facing);
				DoubleQuad(AtMouth(Half, -PortalBelow), AtMouth(Outer, -PortalBelow), AtMouth(Outer, PortalTop), AtMouth(Half, PortalTop), Facing);
				DoubleQuad(AtMouth(-Half, TubeHeight), AtMouth(Half, TubeHeight), AtMouth(Half, PortalTop), AtMouth(-Half, PortalTop), Facing);
			}
			++Tunnels;
		}
		if (Tunnels > 0 && Options.Log)
		{
			Options.Log(georoads::Format("[Buildings] %d tunnels: a tube with a portal at each end", Tunnels));
		}
		Mesh.Colors.resize(FirstWorks, Vec4f{ 1, 1, 1, 1 });
		Mesh.Colors.resize(Mesh.Positions.size(), Vec4f{ 0.0f, 0.5f, 0.6f, 1.0f });
	}
}
