// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadMesh.h"

#include <atomic>
#include <algorithm>
#include <tuple>
#include <set>
#include <map>
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

		/** The part of a polygon on one side of a line: kept where Dot(Keep, P - Through) >= 0 (Sutherland-Hodgman, one half-plane) */
		std::vector<Vec2> ClipHalfPlane(const std::vector<Vec2>& Polygon, const Vec2& Through, const Vec2& Keep)
		{
			std::vector<Vec2> Out;
			const size_t Count = Polygon.size();
			for (size_t i = 0; i < Count; ++i)
			{
				const Vec2& A = Polygon[i];
				const Vec2& B = Polygon[(i + 1) % Count];
				const double DA = georoads::Dot(Keep, A - Through), DB = georoads::Dot(Keep, B - Through);
				const bool bInA = DA >= 0.0, bInB = DB >= 0.0;
				if (bInA)
				{
					Out.push_back(A);
				}
				if (bInA != bInB)
				{
					Out.push_back(A + (B - A) * (DA / (DA - DB)));
				}
			}
			return Out;
		}

		/** A polygon cleaned up for building: points on top of each other taken out; empty if too small (under 0.5 square metres) to be worth a wall */
		std::vector<Vec2> Tidy(const std::vector<Vec2>& Polygon)
		{
			std::vector<Vec2> Out;
			for (const Vec2& P : Polygon)
			{
				if (Out.empty() || georoads::Distance(Out.back(), P) > 2.0)
				{
					Out.push_back(P);
				}
			}
			while (Out.size() > 1 && georoads::Distance(Out.front(), Out.back()) <= 2.0)
			{
				Out.pop_back();
			}
			if (Out.size() < 3 || std::abs(geoground::SignedArea(Out)) < 5000.0)
			{
				Out.clear();
			}
			return Out;
		}

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

		/** A triangle's three half-planes (a point on each line and the direction that is inside) */
		std::vector<std::pair<Vec2, Vec2>> TrianglePlanes(const Vec2& V0, const Vec2& V1, const Vec2& V2)
		{
			std::vector<std::pair<Vec2, Vec2>> Planes;
			const Vec2 Corners[3] = { V0, V1, V2 };
			// (a triangle with two corners on one point, or in a line, has no inside: a plane with no direction counts every point as
			// inside and outside at once, and what is cut by it is kept twice, once cut away and once whole)
			if (std::abs((V1.X - V0.X) * (V2.Y - V0.Y) - (V1.Y - V0.Y) * (V2.X - V0.X)) < 100.0)
			{
				return Planes;
			}
			for (int32_t Edge = 0; Edge < 3; ++Edge)
			{
				const Vec2& From = Corners[Edge];
				const Vec2& To = Corners[(Edge + 1) % 3];
				const Vec2& Third = Corners[(Edge + 2) % 3];
				Vec2 Inside = Vec2(-(To.Y - From.Y), To.X - From.X).Normal();
				if (georoads::Dot(Inside, Third - From) < 0.0)
				{
					Inside = Inside * -1.0;
				}
				Planes.push_back({ From, Inside });
			}
			return Planes;
		}

		/** What of a polygon is inside all the half-planes (cleaned up; empty if nothing worth a wall) */
		std::vector<Vec2> InsideOf(std::vector<Vec2> Poly, const std::vector<std::pair<Vec2, Vec2>>& Planes)
		{
			for (size_t Side = 0; Side < Planes.size() && !Poly.empty(); ++Side)
			{
				Poly = ClipHalfPlane(Poly, Planes[Side].first, Planes[Side].second);
			}
			return Tidy(Poly);
		}

		/** True if two polygons share ground: a corner of one inside the other, their sides cross, or the middle of one of the
		 *  triangles of one is inside the other (a building inside another with its corners on the other's walls has no corner
		 *  inside and no side crossing) */
		bool PolygonsOverlap(const std::vector<Vec2>& A, const std::vector<Vec2>& B)
		{
			for (const std::vector<Vec2>* Poly : { &A, &B })
			{
				const std::vector<Vec2>& Other = Poly == &A ? B : A;
				std::vector<int32_t> Indices;
				geoground::Triangulate(*Poly, Indices);
				for (size_t i = 0; i + 2 < Indices.size(); i += 3)
				{
					const Vec2 Middle = ((*Poly)[size_t(Indices[i])] + (*Poly)[size_t(Indices[i + 1])] + (*Poly)[size_t(Indices[i + 2])]) * (1.0 / 3.0);
					if (InsidePolygon(Other, Middle))
					{
						return true;
					}
				}
			}
			for (const Vec2& P : A)
			{
				if (InsidePolygon(B, P))
				{
					return true;
				}
			}
			for (const Vec2& P : B)
			{
				if (InsidePolygon(A, P))
				{
					return true;
				}
			}
			auto Cross = [](const Vec2& P, const Vec2& Q, const Vec2& R, const Vec2& S)
			{
				const double D = (Q.X - P.X) * (S.Y - R.Y) - (Q.Y - P.Y) * (S.X - R.X);
				if (std::abs(D) < 1.0e-9)
				{
					return false;
				}
				const double T = ((R.X - P.X) * (S.Y - R.Y) - (R.Y - P.Y) * (S.X - R.X)) / D;
				const double U = ((R.X - P.X) * (Q.Y - P.Y) - (R.Y - P.Y) * (Q.X - P.X)) / D;
				return T > 0.0 && T < 1.0 && U > 0.0 && U < 1.0;
			};
			for (size_t i = 0; i < A.size(); ++i)
			{
				for (size_t j = 0; j < B.size(); ++j)
				{
					if (Cross(A[i], A[(i + 1) % A.size()], B[j], B[(j + 1) % B.size()]))
					{
						return true;
					}
				}
			}
			return false;
		}

		/** A polygon as convex pieces: its triangles joined while they stay convex. Clipping by half-planes is exact on a convex polygon;
		 *  on a concave one (a stepped crescent of a block) the pieces it leaves can lie over each other, and where they do a wall is
		 *  drawn twice on one plane and flickers */
		std::vector<std::vector<Vec2>> ConvexPieces(const std::vector<Vec2>& Poly)
		{
			std::vector<int32_t> Indices;
			geoground::Triangulate(Poly, Indices);
			std::vector<std::vector<int32_t>> Pieces;
			for (size_t i = 0; i + 2 < Indices.size(); i += 3)
			{
				Pieces.push_back({ Indices[i], Indices[i + 1], Indices[i + 2] });
			}
			if (Pieces.empty())
			{
				return { Poly };
			}
			auto Convex = [&Poly](const std::vector<int32_t>& P)
			{
				double Sign = 0.0;
				for (size_t i = 0; i < P.size(); ++i)
				{
					const Vec2& A = Poly[size_t(P[i])];
					const Vec2& B = Poly[size_t(P[(i + 1) % P.size()])];
					const Vec2& C = Poly[size_t(P[(i + 2) % P.size()])];
					const double Cross = (B.X - A.X) * (C.Y - B.Y) - (B.Y - A.Y) * (C.X - B.X);
					if (std::abs(Cross) < 1.0)
					{
						continue;	// (a straight run)
					}
					if (Sign == 0.0)
					{
						Sign = Cross;
					}
					else if (Sign * Cross < 0.0)
					{
						return false;
					}
				}
				return true;
			};
			bool bMerged = true;
			while (bMerged)
			{
				bMerged = false;
				for (size_t a = 0; a < Pieces.size() && !bMerged; ++a)
				{
					for (size_t b = a + 1; b < Pieces.size() && !bMerged; ++b)
					{
						const std::vector<int32_t>& A = Pieces[a];
						const std::vector<int32_t>& B = Pieces[b];
						for (size_t i = 0; i < A.size() && !bMerged; ++i)
					{
							for (size_t j = 0; j < B.size() && !bMerged; ++j)
							{
								// (A's side from A[i] to A[i + 1] is B's side from B[j] to B[j + 1], the other way round)
								if (A[i] != B[(j + 1) % B.size()] || A[(i + 1) % A.size()] != B[j])
								{
									continue;
								}
								std::vector<int32_t> Joined;
								for (size_t k = 1; k <= A.size(); ++k)
								{
									Joined.push_back(A[(i + k) % A.size()]);
								}
								for (size_t k = 2; k < B.size(); ++k)
								{
									Joined.push_back(B[(j + k) % B.size()]);
								}
								if (Convex(Joined))
								{
									Pieces[a] = std::move(Joined);
									Pieces.erase(Pieces.begin() + static_cast<std::ptrdiff_t>(b));
									bMerged = true;
								}
							}
						}
					}
				}
			}
			std::vector<std::vector<Vec2>> Out;
			for (const std::vector<int32_t>& P : Pieces)
			{
				std::vector<Vec2>& Piece = Out.emplace_back();
				for (const int32_t Index : P)
				{
					Piece.push_back(Poly[size_t(Index)]);
				}
			}
			return Out;
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
			float Shoulder = 0.0f;	// the wider of its two shoulders: the surface is HalfWidth + this each side
			bool bTunnel = false;	// a tunnel: nothing stands over it, and no passage is left for it
			int32_t Edge = -1;	// the road it belongs to (a tunnel's shell ignores its own)
			};
			std::unordered_map<uint64_t, std::vector<RoadPiece>> RoadCells;
		if (Roads)
		{
			for (const RoadEdge& Edge : Roads->Edges)
			{
				if (Edge.Heights.size() != Edge.Points.size())
				{
					continue;
				}
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					// (a tunnel is a road like any other, and a building over it is cut away: the road wins)
					RoadPiece Piece{ Edge.Points[i], Edge.Points[i + 1], Edge.Heights[i], Edge.Heights[i + 1], Edge.HalfWidth, std::max(Edge.ShoulderLeft, Edge.ShoulderRight), Edge.bTunnel, static_cast<int32_t>(&Edge - Roads->Edges.data()) };
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
		auto RoadsInside = [&RoadCells](const std::vector<Vec2>& Footprint, std::vector<float>& OutHeights, std::vector<RoadPiece>* OutPieces = nullptr)
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
						if (Length < 1.0 || Piece.bTunnel)
						{
							continue;
						}
						const Vec2 Side = Vec2(Piece.A.Y - Piece.B.Y, Piece.B.X - Piece.A.X) * (1.0 / Length);
						bool bHit = false;
						for (double S = 50.0; S < Length; S += 100.0)
						{
							const Vec2 Middle = Piece.A + (Piece.B - Piece.A) * (S / Length);
							for (const double Across : { 0.0, -0.8, 0.8 })
							{
								const Vec2 P = Middle + Side * (Across * Piece.HalfWidth);
								if (Box.IsInside(P) && InsidePolygon(Footprint, P))
								{
									OutHeights.push_back(geoground::LerpF(Piece.HeightA, Piece.HeightB, static_cast<float>(S / Length)));
									if (OutPieces && !bHit)
									{
										bHit = true;
										const bool bKnown = std::any_of(OutPieces->begin(), OutPieces->end(), [&Piece](const RoadPiece& Other)
										{
											return Other.A.X == Piece.A.X && Other.A.Y == Piece.A.Y && Other.B.X == Piece.B.X && Other.B.Y == Piece.B.Y;
										});
										if (!bKnown)
										{
											OutPieces->push_back(Piece);
										}
									}
								}
							}
						}
					}
				}
			}
		};

		// the road pieces within 3 m of a footprint's box (a building touching a road's edge has no sample of the road inside it)
		/** A convex region the buildings are cut away from, as half-planes (a point on each line and the direction that is inside): the
		 *  road's surface, and that with a clear strip round it */
		struct CutRegion
		{
			std::vector<std::pair<Vec2, Vec2>> Surface, Clear;
			float RoadTop = 0.0f;
			bool bTunnel = false;
		};
		auto RoadsNear = [&RoadCells](const std::vector<Vec2>& Footprint, std::vector<RoadPiece>& OutPieces)
		{
			geoground::Box2 Box;
			for (const Vec2& P : Footprint)
			{
				Box += P;
			}
			const int32_t MinX = geoground::FloorToInt((Box.Min.X - 300.0) / PassageCell), MinY = geoground::FloorToInt((Box.Min.Y - 300.0) / PassageCell);
			const int32_t MaxX = geoground::FloorToInt((Box.Max.X + 300.0) / PassageCell), MaxY = geoground::FloorToInt((Box.Max.Y + 300.0) / PassageCell);
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
						const bool bKnown = std::any_of(OutPieces.begin(), OutPieces.end(), [&Piece](const RoadPiece& Other)
						{
							return Other.A.X == Piece.A.X && Other.A.Y == Piece.A.Y && Other.B.X == Piece.B.X && Other.B.Y == Piece.B.Y;
						});
						if (!bKnown)
						{
							OutPieces.push_back(Piece);
						}
					}
				}
			}
		};

		// the junction areas by 50 m square (a building looks only at the ones near it: every building against every junction
		// was most of a big city's build)
		std::vector<geoground::Box2> JunctionBoxes;
		std::unordered_map<uint64_t, std::vector<int32_t>> JunctionCells;
		if (Options.Junctions)
		{
			for (int32_t Index = 0; Index < static_cast<int32_t>(Options.Junctions->size()); ++Index)
			{
				geoground::Box2& Box = JunctionBoxes.emplace_back();
				for (const Vec2& Corner : (*Options.Junctions)[Index].Corners)
				{
					Box += Corner;
				}
				if ((*Options.Junctions)[Index].Corners.size() < 3)
				{
					continue;
				}
				for (int32_t X = geoground::FloorToInt(Box.Min.X / PassageCell); X <= geoground::FloorToInt(Box.Max.X / PassageCell); ++X)
				{
					for (int32_t Y = geoground::FloorToInt(Box.Min.Y / PassageCell); Y <= geoground::FloorToInt(Box.Max.Y / PassageCell); ++Y)
					{
						JunctionCells[CellKey(X, Y)].push_back(Index);
					}
				}
			}
		}

		// a building's footprint, counter-clockwise (so walls face outwards and ear clipping works); false if it has none worth building
		auto MakeFootprint = [&Data](const georoads::OsmWay& Way, std::vector<Vec2>& Out)
		{
			Out.clear();
			for (const int64_t NodeId : Way.Nodes)
			{
				const auto Found = Data.Nodes.find(NodeId);
				if (Found != Data.Nodes.end())
				{
					const Vec2& Position = Found->second;
					if (Out.empty() || std::abs(Out.back().X - Position.X) > 1.0 || std::abs(Out.back().Y - Position.Y) > 1.0)
					{
						Out.push_back(Position);
					}
				}
			}
			if (Out.size() > 1 && std::abs(Out[0].X - Out.back().X) <= 1.0 && std::abs(Out[0].Y - Out.back().Y) <= 1.0)
			{
				Out.pop_back();
			}
			if (Out.size() < 3)
			{
				return false;
			}
			const double Area = geoground::SignedArea(Out);
			if (std::abs(Area) < 50.0)
			{
				return false;
			}
			if (Area < 0.0)
			{
				std::reverse(Out.begin(), Out.end());
			}
			return true;
		};

		// no two buildings share ground (two drawn over each other flicker, one then the other taking the front): each is cut
		// round the ones that beat it, the taller first, then the smaller (the detail inside a block), so what is left is
		// buildings side by side, never one in another
		struct Standing
		{
			std::vector<Vec2> Footprint;
			geoground::Box2 Box;
			double Area = 0.0;
			float Height = 0.0f, Min = 0.0f;
			std::vector<int32_t> Winners;
		};
		std::vector<Standing> Standings(Data.Ways.size());
		{
			constexpr double OverlapCell = 5000.0;
			std::unordered_map<uint64_t, std::vector<int32_t>> Cells;
			for (int32_t WayIndex = 0; WayIndex < static_cast<int32_t>(Data.Ways.size()); ++WayIndex)
			{
				const georoads::OsmWay& Way = Data.Ways[size_t(WayIndex)];
				Standing& S = Standings[size_t(WayIndex)];
				if (!Way.Tag("building") || Way.Nodes.size() < 4 || !MakeFootprint(Way, S.Footprint))
				{
					S.Footprint.clear();
					continue;
				}
				for (const Vec2& Point : S.Footprint)
				{
					S.Box += Point;
				}
				S.Area = std::abs(geoground::SignedArea(S.Footprint));
				S.Height = BuildingHeight(Way);
				S.Min = BuildingMinHeight(Way, S.Height);
				for (int32_t X = geoground::FloorToInt(S.Box.Min.X / OverlapCell); X <= geoground::FloorToInt(S.Box.Max.X / OverlapCell); ++X)
				{
					for (int32_t Y = geoground::FloorToInt(S.Box.Min.Y / OverlapCell); Y <= geoground::FloorToInt(S.Box.Max.Y / OverlapCell); ++Y)
					{
						Cells[CellKey(X, Y)].push_back(WayIndex);
					}
				}
			}
			auto Beats = [&Standings](int32_t Other, int32_t Own)
			{
				const Standing& O = Standings[size_t(Other)];
				const Standing& W = Standings[size_t(Own)];
				if (std::abs(O.Height - W.Height) > 100.0f)
				{
					return O.Height > W.Height;
				}
				if (std::abs(O.Area - W.Area) > 1.0e5)
				{
					return O.Area < W.Area;
				}
				return Other < Own;
			};
			std::vector<int32_t> Candidates;
			for (int32_t WayIndex = 0; WayIndex < static_cast<int32_t>(Standings.size()); ++WayIndex)
			{
				Standing& S = Standings[size_t(WayIndex)];
				if (S.Footprint.empty())
				{
					continue;
				}
				Candidates.clear();
				for (int32_t X = geoground::FloorToInt(S.Box.Min.X / OverlapCell); X <= geoground::FloorToInt(S.Box.Max.X / OverlapCell); ++X)
				{
					for (int32_t Y = geoground::FloorToInt(S.Box.Min.Y / OverlapCell); Y <= geoground::FloorToInt(S.Box.Max.Y / OverlapCell); ++Y)
					{
						const auto Found = Cells.find(CellKey(X, Y));
						if (Found != Cells.end())
						{
							Candidates.insert(Candidates.end(), Found->second.begin(), Found->second.end());
						}
					}
				}
				std::sort(Candidates.begin(), Candidates.end());
				Candidates.erase(std::unique(Candidates.begin(), Candidates.end()), Candidates.end());
				for (const int32_t Other : Candidates)
				{
					const Standing& O = Standings[size_t(Other)];
					if (Other == WayIndex || O.Box.Max.X < S.Box.Min.X || O.Box.Min.X > S.Box.Max.X || O.Box.Max.Y < S.Box.Min.Y || O.Box.Min.Y > S.Box.Max.Y)
					{
						continue;
					}
					// (one over the other, like a building across a street, is not sharing ground)
					if (std::min(O.Height, S.Height) - std::max(O.Min, S.Min) < 100.0f || !Beats(Other, WayIndex) || !PolygonsOverlap(S.Footprint, O.Footprint))
					{
						continue;
					}
					S.Winners.push_back(Other);
				}
			}
		}

		// the buildings in batches on the worker pool, each into its own mesh, joined in order after (the same mesh as one
		// at a time): ~35,000 of them in a city's piece took ~2 s on one thread
		struct BuildingBatch
		{
			MeshData Mesh;
			std::vector<BuildingOutline> Outlines;	// (its triangles tagged with these, numbered in the batch)
			int32_t NumBuildings = 0, NumRaised = 0, NumCut = 0, NumOnRoads = 0, NumShared = 0, NumCovered = 0;
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
			std::vector<RoadPiece> Cuts;	// the road pieces running through the footprint
			struct CarvedPart
			{
				std::vector<Vec2> Poly;
				float Base = 0.0f;
				bool bPassage = false;	// over a road: its underside is seen from the street
			};
			std::vector<CarvedPart> Parts;	// what is left of the building once the roads have their space (empty: the whole footprint)
			std::vector<int32_t> RoofIndices, WinnerIndices;
			std::vector<std::vector<Vec2>> Pieces;	// what is left of the footprint outside the buildings that beat it
			const int32_t FirstWay = static_cast<int32_t>(static_cast<int64_t>(NumWays) * BatchIndex / NumBatches);
			const int32_t EndWay = static_cast<int32_t>(static_cast<int64_t>(NumWays) * (BatchIndex + 1) / NumBatches);
			for (int32_t WayIndex = FirstWay; WayIndex < EndWay; ++WayIndex)
			{
				const georoads::OsmWay& Way = Data.Ways[size_t(WayIndex)];
				if (!Way.Tag("building") || Way.Nodes.size() < 4)
				{
					continue;
				}

				if (!MakeFootprint(Way, Footprint))
				{
					continue;
				}

				// the buildings that beat this one cut out of it (a triangle of theirs at a time: what is outside each stays)
				bool bCarved = false;
				Parts.clear();
				if (!Standings[size_t(WayIndex)].Winners.empty())
				{
					Pieces = ConvexPieces(Footprint);
					for (const int32_t Winner : Standings[size_t(WayIndex)].Winners)
					{
						const std::vector<Vec2>& Over = Standings[size_t(Winner)].Footprint;
						WinnerIndices.clear();
						geoground::Triangulate(Over, WinnerIndices);
						for (size_t t = 0; t + 2 < WinnerIndices.size() && !Pieces.empty(); t += 3)
						{
							const std::vector<std::pair<Vec2, Vec2>> Planes = TrianglePlanes(Over[size_t(WinnerIndices[t])], Over[size_t(WinnerIndices[t + 1])], Over[size_t(WinnerIndices[t + 2])]);
							if (Planes.empty())
							{
								continue;
							}
							std::vector<std::vector<Vec2>> Remaining;
							for (const std::vector<Vec2>& Piece : Pieces)
							{
								if (InsideOf(Piece, Planes).empty())
								{
									Remaining.push_back(Piece);	// (the triangle doesn't touch it: left whole)
									continue;
								}
								bCarved = true;
								std::vector<Vec2> Still = Piece;
								for (const std::pair<Vec2, Vec2>& Plane : Planes)
								{
									std::vector<Vec2> Beyond = Tidy(ClipHalfPlane(Still, Plane.first, Plane.second * -1.0));
									if (!Beyond.empty())
									{
										Remaining.push_back(std::move(Beyond));
									}
									Still = ClipHalfPlane(Still, Plane.first, Plane.second);
								}
							}
							Pieces = std::move(Remaining);
						}
						if (Pieces.empty())
						{
							break;
						}
					}
					if (Pieces.empty())
					{
						++Batch.NumCovered;	// (all of it inside taller ones: nothing of it shows)
						continue;
					}
					Batch.NumShared += bCarved ? 1 : 0;
				}

				// sit on the slope: floor near the median ground height, foundation down to the lowest point
				Ground.clear();
				for (const Vec2& Point : Footprint)
				{
					Ground.push_back(GroundHeight(Point));
				}
				{
					// and inside: a point every ~4 m across it (at most 64), so it stands on the lowest ground under it, not only
					// at its corners
					geoground::Box2 Box;
					for (const Vec2& Point : Footprint)
					{
						Box += Point;
					}
					const double Step = std::max(400.0, std::max(Box.Max.X - Box.Min.X, Box.Max.Y - Box.Min.Y) / 8.0);
					int32_t Taken = 0;
					for (double X = Box.Min.X + Step * 0.5; X < Box.Max.X && Taken < 64; X += Step)
					{
						for (double Y = Box.Min.Y + Step * 0.5; Y < Box.Max.Y && Taken < 64; Y += Step)
						{
							if (InsidePolygon(Footprint, Vec2(X, Y)))
							{
								Ground.push_back(GroundHeight(Vec2(X, Y)));
								++Taken;
							}
						}
					}
				}
				std::sort(Ground.begin(), Ground.end());
				if (Ground[Ground.size() / 2] < -50.0f)
				{
					continue;	// standing in the sea (a boat, a floating pontoon, a mapped outline over the water): no slab on the seabed
				}
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
					Cuts.clear();
					Parts.clear();
					RoadsInside(Footprint, RoadHeights);
					RoadsNear(Footprint, Cuts);
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
					if (Raised > 0.0f && RoadHeights.empty())
					{
						// raised off the ground (a part of a building, high up) with no road under it: nothing holds it up, so it
						// stands on the ground
						Base = Lowest - 50.0f;
						bRaised = false;
					}
					bool bTouched = false;
					if (AtGround >= 6 && Raised > 0.0f)
					{
						// (a part raised off the ground already, with a road at ground level through it: lifted clear of the road)
						Base = std::max(Base, std::max(Floor, GroundRoadTop) + 450.0f);
						bRaised = bTouched = true;
					}
					else if (Raised <= 0.0f)
					{
						// the road is the boss: where a road runs through the building at ground level the building is cut away as if by a
						// tunnel (truck height, 4.5 m, over the road's surface), and stays whole beside it and above it, as buildings round a
						// road through a gateway, in Monaco or in England, do. The road's space (its width and a half metre) is cut from the
						// footprint, one road piece after another: what's inside it becomes a part that starts 4.5 m up, what's outside stays
						// down to the ground
					// the regions to cut away: each road piece near it (a rectangle), and each junction area (its fan of triangles)
					std::vector<CutRegion> Regions;
					for (const RoadPiece& Cut : Cuts)
					{
						const Vec2 Along = (Cut.B - Cut.A).Normal();
						if (Along.SizeSquared() < 0.5)
						{
							continue;
						}
						const Vec2 Across(-Along.Y, Along.X);
						// the surface the road is drawn with (carriageway and shoulders), and beside it a clear strip (the kerb and pavement,
						// and room so a wall doesn't stand touching the road's edge); past each end too, so pieces at a bend leave no sliver
						const double SurfaceHalf = Cut.HalfWidth + Cut.Shoulder;
						const double ClearHalf = SurfaceHalf + 90.0;
						CutRegion Region;
						Region.RoadTop = std::max(Cut.HeightA, Cut.HeightB);
						Region.bTunnel = Cut.bTunnel;
						for (const double Half : { SurfaceHalf, ClearHalf })
						{
							std::vector<std::pair<Vec2, Vec2>>& Planes = Half == SurfaceHalf ? Region.Surface : Region.Clear;
							Planes = { { Cut.A - Along * ClearHalf, Along }, { Cut.B + Along * ClearHalf, Along * -1.0 }, { Cut.A - Across * Half, Across }, { Cut.A + Across * Half, Across * -1.0 } };
						}
						Regions.push_back(std::move(Region));
					}
					if (Options.Junctions)
					{
						geoground::Box2 FootBox;
						for (const Vec2& Point : Footprint)
						{
							FootBox += Point;
						}
						FootBox = FootBox.ExpandBy(300.0);
						std::vector<int32_t> NearJunctions;
						for (int32_t X = geoground::FloorToInt(FootBox.Min.X / PassageCell); X <= geoground::FloorToInt(FootBox.Max.X / PassageCell); ++X)
						{
							for (int32_t Y = geoground::FloorToInt(FootBox.Min.Y / PassageCell); Y <= geoground::FloorToInt(FootBox.Max.Y / PassageCell); ++Y)
							{
								const auto Found = JunctionCells.find(CellKey(X, Y));
								if (Found != JunctionCells.end())
								{
									NearJunctions.insert(NearJunctions.end(), Found->second.begin(), Found->second.end());
								}
							}
						}
						std::sort(NearJunctions.begin(), NearJunctions.end());
						NearJunctions.erase(std::unique(NearJunctions.begin(), NearJunctions.end()), NearJunctions.end());
						for (const int32_t Near : NearJunctions)
						{
							const JunctionArea& Area = (*Options.Junctions)[Near];
							const geoground::Box2& AreaBox = JunctionBoxes[Near];
							if (Area.Corners.size() < 3 || AreaBox.Max.X < FootBox.Min.X || AreaBox.Min.X > FootBox.Max.X || AreaBox.Max.Y < FootBox.Min.Y || AreaBox.Min.Y > FootBox.Max.Y)
							{
								continue;
							}
							for (size_t i = 0; i < Area.Corners.size(); ++i)
							{
								const Vec2& P1 = Area.Corners[i];
								const Vec2& P2 = Area.Corners[(i + 1) % Area.Corners.size()];
								// (the clear strip: the fan's rim pushed out from the middle by 90 cm)
								const Vec2 Out1 = Area.Centre + (P1 - Area.Centre) * (1.0 + 90.0 / std::max(1.0, georoads::Distance(Area.Centre, P1)));
								const Vec2 Out2 = Area.Centre + (P2 - Area.Centre) * (1.0 + 90.0 / std::max(1.0, georoads::Distance(Area.Centre, P2)));
								CutRegion Region;
								Region.RoadTop = Area.Height;
								Region.Surface = TrianglePlanes(Area.Centre, P1, P2);
								Region.Clear = TrianglePlanes(Area.Centre, Out1, Out2);
								if (Region.Surface.empty() || Region.Clear.empty())
								{
									continue;	// (a fan triangle with no width)
								}
								Regions.push_back(std::move(Region));
							}
						}
					}

					// (in convex pieces only if a road comes near it: most buildings are left whole)
					bool bNearRoad = false;
					for (const CutRegion& Region : Regions)
					{
						if ((Region.bTunnel || Region.RoadTop < Floor + 300.0f) && !InsideOf(Footprint, Region.Clear).empty())
						{
							bNearRoad = true;
							break;
						}
					}
					std::vector<std::vector<Vec2>> Solid = bCarved ? Pieces : bNearRoad ? ConvexPieces(Footprint) : std::vector<std::vector<Vec2>>{ Footprint };
					bool bRoadCut = false;
					for (const CutRegion& Region : Regions)
					{
						if (!Region.bTunnel && Region.RoadTop >= Floor + 300.0f)
						{
							continue;	// (a bridge over it, taken care of below)
						}
						const float PassageBase = std::max(Floor, Region.RoadTop) + 450.0f;
						std::vector<std::vector<Vec2>> Remaining;
						for (const std::vector<Vec2>& Piece : Solid)
						{
							if (InsideOf(Piece, Region.Clear).empty())
							{
								Remaining.push_back(Piece);	// (the region doesn't touch it: left whole)
								continue;
							}
							bRoadCut = true;
							// outside the clear strip: one piece beyond each side, and inside the sides before it
							std::vector<Vec2> Still = Piece;
							for (const std::pair<Vec2, Vec2>& Plane : Region.Clear)
							{
								std::vector<Vec2> Beyond = Tidy(ClipHalfPlane(Still, Plane.first, Plane.second * -1.0));
								if (!Beyond.empty())
								{
									Remaining.push_back(std::move(Beyond));
								}
								Still = ClipHalfPlane(Still, Plane.first, Plane.second);
							}
							// inside it: over the road itself, a passage (the whole strip, so no wall stands on the kerb); only beside the road,
							// the strip is simply cut away
							std::vector<Vec2> InClear = Tidy(Still);
							if (!InClear.empty() && !Region.bTunnel && !InsideOf(Piece, Region.Surface).empty() && Top - PassageBase >= 250.0f)
							{
								Parts.push_back({ std::move(InClear), PassageBase, true });
							}
						}
						Solid = std::move(Remaining);
					}
					if (!bRoadCut && !bCarved)
					{
						Solid.assign(1, Footprint);	// (no road touches it: the whole building, not its convex pieces)
					}
						if (!Parts.empty() || Solid.size() != 1 || bCarved)
						{
							for (std::vector<Vec2>& Left : Solid)
							{
								Parts.push_back({ std::move(Left), Base, false });
							}
							bTouched = true;
							if (Parts.empty())
							{
								++Batch.NumOnRoads;
								continue;
							}
						}
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
					if (Options.Log && Base - Lowest > 400.0f)
					{
						// (why a building stands high off the ground, for finding the floating ones: the first few, with their map id)
						static std::atomic<int32_t> Logged{ 0 };
						if (Logged++ < 80)
						{
							Options.Log(georoads::Format("[Buildings] way %lld stands %.1f m above its lowest ground (min_height %.1f m, height %.1f m; %d road samples inside: %d at ground level, %d above)",
								static_cast<long long>(Way.Id), (Base - Lowest) / 100.0f, Raised / 100.0f, Height / 100.0f, int(RoadHeights.size()), AtGround, Above));
						}
					}
				}
				Batch.NumRaised += bRaised ? 1 : 0;
				if (bCarved && Parts.empty())
				{
					// (cut round taller buildings but nothing of the roads': what is left of it stands where it was)
					for (std::vector<Vec2>& Left : Pieces)
					{
						Parts.push_back({ std::move(Left), Base, false });
					}
				}

				// (a check, for the log: a solid part of the building that still has a road's surface inside it, between its foot and its top)
				if (Options.Log && Roads)
				{
					static std::atomic<int32_t> Overlaps{ 0 };
					std::vector<float> Inside;
					auto Check = [&](const std::vector<Vec2>& Poly, float PartBase, bool bPassage)
					{
						if (bPassage)
						{
							return;
						}
						Inside.clear();
						RoadsInside(Poly, Inside);
						for (const float RoadH : Inside)
						{
							if (RoadH >= PartBase - 30.0f && RoadH <= Top - 30.0f)
							{
								if (Overlaps++ < 120)
								{
									Options.Log(georoads::Format("[BuildingOverlap] way %lld at x %.0f y %.0f: road surface %.1f m inside a solid part (foot %.1f m, top %.1f m, ground %.1f m)",
										static_cast<long long>(Way.Id), Poly[0].X, Poly[0].Y, RoadH / 100.0f, PartBase / 100.0f, Top / 100.0f, Floor / 100.0f));
								}
								return;
							}
						}
					};
					if (Parts.empty())
					{
						Check(Footprint, Base, Raised > 0.0f && bRaised);
					}
					for (const CarvedPart& Part : Parts)
					{
						Check(Part.Poly, Part.Base, Part.bPassage);
					}
				}

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

					// walls, roof (and the underside of what's over a road) of a polygon between two heights
					auto Emit = [&](const std::vector<Vec2>& Poly, float PBase, float PTop, bool bUnderside)
					{
						for (size_t i = 0; i < Poly.size(); ++i)
						{
							const Vec2& A = Poly[i];
							const Vec2& B = Poly[(i + 1) % Poly.size()];
							const Vec2 Edge = B - A;
							const Vec2 Outward = Vec2(Edge.Y, -Edge.X).Normal();
							const Vec3f Normal(static_cast<float>(Outward.X), static_cast<float>(Outward.Y), 0.0f);
					
							const Vec3f A0 = At(A, PBase), B0 = At(B, PBase), A1 = At(A, PTop), B1 = At(B, PTop);
							const float U = static_cast<float>(Edge.Size() / 300.0);
							const float VTop = (PTop - PBase) / 300.0f;
							Mesh.AddTriangle(A0, B0, B1, Normal, Vec2f{ 0, 0 }, Vec2f{ U, 0 }, Vec2f{ U, VTop });
							Mesh.AddTriangle(A0, B1, A1, Normal, Vec2f{ 0, 0 }, Vec2f{ U, VTop }, Vec2f{ 0, VTop });
						}
						RoofIndices.clear();
						geoground::Triangulate(Poly, RoofIndices);
						const Vec3f Up(0.0f, 0.0f, 1.0f);
						for (size_t i = 0; i + 2 < RoofIndices.size(); i += 3)
						{
							const Vec2& A = Poly[size_t(RoofIndices[i])];
							const Vec2& B = Poly[size_t(RoofIndices[i + 1])];
							const Vec2& C = Poly[size_t(RoofIndices[i + 2])];
							Mesh.AddTriangle(At(A, PTop), At(B, PTop), At(C, PTop), Up,
								Vec2f{ float(A.X / 300.0), float(A.Y / 300.0) }, Vec2f{ float(B.X / 300.0), float(B.Y / 300.0) }, Vec2f{ float(C.X / 300.0), float(C.Y / 300.0) });
							if (bUnderside)
							{
								// the underside, seen from the street below
								Mesh.AddTriangle(At(A, PBase), At(C, PBase), At(B, PBase), -Up,
									Vec2f{ float(A.X / 300.0), float(A.Y / 300.0) }, Vec2f{ float(C.X / 300.0), float(C.Y / 300.0) }, Vec2f{ float(B.X / 300.0), float(B.Y / 300.0) });
							}
						}
					};
					if (Parts.empty())
					{
						Emit(Footprint, Base, Top, Raised > 0.0f);
					}
					for (const CarvedPart& Part : Parts)
					{
						Emit(Part.Poly, Part.Base, Top, Part.bPassage);
					}

				Mesh.Colors.resize(Mesh.Positions.size());
				std::fill(Mesh.Colors.begin() + std::ptrdiff_t(FirstCorner), Mesh.Colors.end(), Look);
				++Batch.NumBuildings;
			}
		});
		int32_t Shared = 0, Covered = 0;
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
			Shared += Batch.NumShared;
			Covered += Batch.NumCovered;
		}

		if (Options.Log && Shared + Covered > 0)
		{
			Options.Log(georoads::Format("[Buildings] %d buildings overlapped taller ones and were cut round them, %d lay wholly inside others and were left out", Shared, Covered));
		}

		// (a check, for the log: faces on one plane, facing one way, that cover the same ground flicker as one then the other takes the
		// front. Found by plane, then by overlap in the plane; the worst few are told with the buildings' map ids)
		if (Options.Log)
		{
			struct Face { Vec3f A, B, C, N; float D; Vec3f Lo, Hi; int32_t Triangle; };
			std::map<std::tuple<int32_t, int32_t, int32_t, int32_t>, std::vector<Face>> Planes;
			const int32_t NumTriangles = static_cast<int32_t>(Mesh.Indices.size() / 3);
			for (int32_t T = 0; T < NumTriangles; ++T)
			{
				const Vec3f& A = Mesh.Positions[size_t(Mesh.Indices[size_t(T) * 3])];
				const Vec3f& B = Mesh.Positions[size_t(Mesh.Indices[size_t(T) * 3 + 1])];
				const Vec3f& C = Mesh.Positions[size_t(Mesh.Indices[size_t(T) * 3 + 2])];
				Vec3f N = geoground::Cross(B - A, C - A);
				const float Length = std::sqrt(N.X * N.X + N.Y * N.Y + N.Z * N.Z);
				if (Length < 1.0f)
				{
					continue;
				}
				N = Vec3f(N.X / Length, N.Y / Length, N.Z / Length);
				const float D = N.X * A.X + N.Y * A.Y + N.Z * A.Z;
				Face F{ A, B, C, N, D, Vec3f(std::min({ A.X, B.X, C.X }), std::min({ A.Y, B.Y, C.Y }), std::min({ A.Z, B.Z, C.Z })),
					Vec3f(std::max({ A.X, B.X, C.X }), std::max({ A.Y, B.Y, C.Y }), std::max({ A.Z, B.Z, C.Z })), T };
				Planes[{ static_cast<int32_t>(std::lround(N.X * 50.0f)), static_cast<int32_t>(std::lround(N.Y * 50.0f)), static_cast<int32_t>(std::lround(N.Z * 50.0f)), static_cast<int32_t>(std::lround(D / 3.0f)) }].push_back(F);
			}
			auto Overlap = [](const Face& P, const Face& Q)
			{
				const Vec3f* Tri[2][3] = { { &P.A, &P.B, &P.C }, { &Q.A, &Q.B, &Q.C } };
				for (int32_t Which = 0; Which < 2; ++Which)
				{
					for (int32_t E = 0; E < 3; ++E)
					{
						const Vec3f Edge = *Tri[Which][(E + 1) % 3] - *Tri[Which][E];
						Vec3f Axis = geoground::Cross(P.N, Edge);
						const float Size = std::sqrt(Axis.X * Axis.X + Axis.Y * Axis.Y + Axis.Z * Axis.Z);
						if (Size < 1.0e-3f)
						{
							continue;
						}
						Axis = Vec3f(Axis.X / Size, Axis.Y / Size, Axis.Z / Size);
						float Min[2] = { 1.0e30f, 1.0e30f }, Max[2] = { -1.0e30f, -1.0e30f };
						for (int32_t Tr = 0; Tr < 2; ++Tr)
						{
							for (int32_t V = 0; V < 3; ++V)
							{
								const float Along = Axis.X * Tri[Tr][V]->X + Axis.Y * Tri[Tr][V]->Y + Axis.Z * Tri[Tr][V]->Z;
								Min[Tr] = std::min(Min[Tr], Along);
								Max[Tr] = std::max(Max[Tr], Along);
							}
						}
						if (std::min(Max[0], Max[1]) - std::max(Min[0], Min[1]) < 5.0f)
						{
							return false;	// (apart, or only touching: under 5 cm of each other)
						}
					}
				}
				return true;
			};
			int32_t Pairs = 0;
			std::set<int64_t> Ways;
			for (auto& Entry : Planes)
			{
				std::vector<Face>& Faces = Entry.second;
				if (Faces.size() < 2)
				{
					continue;
				}
				std::sort(Faces.begin(), Faces.end(), [](const Face& L, const Face& R) { return L.Lo.X < R.Lo.X; });
				for (size_t i = 0; i < Faces.size(); ++i)
				{
					for (size_t j = i + 1; j < Faces.size() && Faces[j].Lo.X < Faces[i].Hi.X; ++j)
					{
						const Face& P = Faces[i];
						const Face& Q = Faces[j];
						if (P.Hi.Y < Q.Lo.Y || Q.Hi.Y < P.Lo.Y || P.Hi.Z < Q.Lo.Z || Q.Hi.Z < P.Lo.Z)
						{
							continue;
						}
						const int32_t TagP = size_t(P.Triangle) < Mesh.Tags.size() ? Mesh.Tags[size_t(P.Triangle)] : -1;
						const int32_t TagQ = size_t(Q.Triangle) < Mesh.Tags.size() ? Mesh.Tags[size_t(Q.Triangle)] : -1;
						if (TagP == TagQ && TagP >= 0 && std::abs(P.N.Z) > 0.9f)
						{
							continue;	// (one building's roof split in triangles that share a corner: not it)
						}
						if (Overlap(P, Q))
						{
							if (Pairs++ < 25)
							{
								const int64_t IdP = TagP >= 0 && size_t(TagP) < Out.Outlines.size() ? Out.Outlines[size_t(TagP)].Id : -1;
								const int64_t IdQ = TagQ >= 0 && size_t(TagQ) < Out.Outlines.size() ? Out.Outlines[size_t(TagQ)].Id : -1;
								Options.Log(georoads::Format("[BuildingFlicker] faces of way %lld and way %lld lie on one plane (%s, facing %.2f %.2f %.2f) at x %.0f y %.0f: z %.1f to %.1f m and %.1f to %.1f m, triangles %d and %d",
									static_cast<long long>(IdP), static_cast<long long>(IdQ), std::abs(P.N.Z) > 0.9f ? "a roof" : "a wall", P.N.X, P.N.Y, P.N.Z, P.Lo.X, P.Lo.Y,
									P.Lo.Z / 100.0f, P.Hi.Z / 100.0f, Q.Lo.Z / 100.0f, Q.Hi.Z / 100.0f, P.Triangle, Q.Triangle));
								if (Pairs <= 4)
								{
									Options.Log(georoads::Format("[BuildingFlicker]   corners: (%.0f %.0f %.0f) (%.0f %.0f %.0f) (%.0f %.0f %.0f)  and  (%.0f %.0f %.0f) (%.0f %.0f %.0f) (%.0f %.0f %.0f)",
										P.A.X, P.A.Y, P.A.Z, P.B.X, P.B.Y, P.B.Z, P.C.X, P.C.Y, P.C.Z, Q.A.X, Q.A.Y, Q.A.Z, Q.B.X, Q.B.Y, Q.B.Z, Q.C.X, Q.C.Y, Q.C.Z));
								}
							}
							Ways.insert(Mesh.Tags.size() > size_t(P.Triangle) ? Mesh.Tags[size_t(P.Triangle)] : -1);
						}
					}
				}
			}
			Options.Log(georoads::Format("[Buildings] %d pairs of faces share a plane and the same ground (%d buildings)", Pairs, static_cast<int32_t>(Ways.size())));
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

		// tunnels: a tube along the road (walls and a ceiling, seen from inside and out), open at its ends
		constexpr float TubeHeight = 650.0f, TubeMargin = 50.0f, StationStep = 1000.0f, MinTube = 450.0f;	// (top: the road over it, at its clearance)
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

			// the tunnel is a thin shell (walls and a roof) that forms like a building does, against what is round it: its roof is
			// 6.5 m over the road under a hill, and comes down to just under the ground where that is nearer, never lower than
			// 4.5 m (a lorry); where the ground is lower still it stands out as a low covered road, not a block. It has no end walls,
			// so a mouth is open; where it meets another tunnel road its wall isn't there, so they join; and the road under or over
			// it is kept 6.5 m clear by the clearance pass and the buildings are cut away from it, so nothing else conflicts
			const float Half = Edge.HalfWidth + TubeMargin;
			// the road is the boss: no wall or roof where another road runs (a junction, a side road, a tunnel beside this one: they
			// share the space). Is there a road of another edge over this spot, its surface within the shell's height?
			const int32_t OwnEdge = static_cast<int32_t>(&Edge - Roads->Edges.data());
			auto RoadThrough = [&](const Vec2& Where, float Low, float High)
			{
				const auto Found = RoadCells.find(CellKey(geoground::FloorToInt(Where.X / PassageCell), geoground::FloorToInt(Where.Y / PassageCell)));
				if (Found == RoadCells.end())
				{
					return false;
				}
				for (const RoadPiece& Other : Found->second)
				{
					if (Other.Edge == OwnEdge)
					{
						continue;
					}
					const Vec2 AB = Other.B - Other.A;
					const double Length2 = AB.SizeSquared();
					const double T = Length2 > 1.0 ? std::clamp(georoads::Dot(Where - Other.A, AB) / Length2, 0.0, 1.0) : 0.0;
					if (georoads::Distance(Other.A + AB * T, Where) > Other.HalfWidth + Other.Shoulder + TubeMargin)
					{
						continue;
					}
					const float Surface = geoground::LerpF(Other.HeightA, Other.HeightB, static_cast<float>(T));
					if (Surface >= Low && Surface <= High)
					{
						return true;
					}
				}
				return false;
			};
			std::vector<float> Roofs(Points.size());
			for (size_t i = 0; i < Points.size(); ++i)
			{
				const Vec2 Side(-Dirs[i].Y, Dirs[i].X);
				// (the lowest ground over the road and its two sides: the roof has to be under all of it)
				const float Ground = std::min(GroundHeight(Points[i]), std::min(GroundHeight(Points[i] + Side * Half), GroundHeight(Points[i] - Side * Half)));
				Roofs[i] = std::max(std::min(Heights[i] + TubeHeight, Ground - 30.0f), Heights[i] + MinTube);
			}
			for (size_t i = 0; i + 1 < Points.size(); ++i)
			{
				const Vec2 LA(-Dirs[i].Y, Dirs[i].X), LB(-Dirs[i + 1].Y, Dirs[i + 1].X);
				const float ZA = Heights[i], ZB = Heights[i + 1], TopA = Roofs[i], TopB = Roofs[i + 1];
				// (another level within a shell's height of this one counts too: stacked tunnels leave loose slivers of shell between them)
				const float Low = std::min(ZA, ZB) - TubeHeight, High = std::max(TopA, TopB) + TubeHeight;
				const Vec2 LeftA = Points[i] + LA * Half, LeftB = Points[i + 1] + LB * Half;
				const Vec2 RightA = Points[i] - LA * Half, RightB = Points[i + 1] - LB * Half;
				const Vec3f Side(float(LA.X), float(LA.Y), 0.0f);
				// a wall quad stays unless a road passes through it (not at its ends: the next piece of the same tunnel starts there)
				auto Blocked = [&](const Vec2& A, const Vec2& B) { return RoadThrough((A + B) * 0.5, Low, High) || RoadThrough(A * 0.75 + B * 0.25, Low, High) || RoadThrough(A * 0.25 + B * 0.75, Low, High); };
				if (!Blocked(LeftA, LeftB))
				{
					DoubleQuad(P(LeftA, ZA - 50.0f), P(LeftB, ZB - 50.0f), P(LeftB, TopB), P(LeftA, TopA), -Side);
				}
				if (!Blocked(RightA, RightB))
				{
					DoubleQuad(P(RightA, ZA - 50.0f), P(RightB, ZB - 50.0f), P(RightB, TopB), P(RightA, TopA), Side);
				}
				// the roof in three strips across: a road under only one of them takes only that one away
				for (int32_t Strip = 0; Strip < 3; ++Strip)
				{
					const float F0 = Strip / 3.0f, F1 = (Strip + 1) / 3.0f;
					const Vec2 A0 = LeftA + (RightA - LeftA) * F0, A1 = LeftA + (RightA - LeftA) * F1;
					const Vec2 B0 = LeftB + (RightB - LeftB) * F0, B1 = LeftB + (RightB - LeftB) * F1;
					if (!Blocked((A0 + A1) * 0.5, (B0 + B1) * 0.5))
					{
						DoubleQuad(P(A0, TopA), P(B0, TopB), P(B1, TopB), P(A1, TopA), Vec3f(0, 0, -1));
					}
				}
			}
			++Tunnels;
		}
		if (Tunnels > 0 && Options.Log)
		{
			Options.Log(georoads::Format("[Buildings] %d tunnels (a portal where one comes out of the ground)", Tunnels));
		}
		Mesh.Colors.resize(FirstWorks, Vec4f{ 1, 1, 1, 1 });
		Mesh.Colors.resize(Mesh.Positions.size(), Vec4f{ 0.0f, 0.5f, 0.6f, 1.0f });
	}
}
