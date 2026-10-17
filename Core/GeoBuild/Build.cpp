// Geodrive Core - GPL (see Core/README.md)

#include "Build.h"

#include <algorithm>
#include <cmath>

namespace geobuild
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double RoofRise = 1.5;	// m across a square
		constexpr int32_t Steps = 12;		// a flight's (25 cm each, up and along)
		constexpr double StairWidth = 1.2;	// m

		using geobody::Vec3;

		// how thick each piece is in each material (m): walls, floors, roofs; a foundation's slab (its weight: the block
		// below it is the ground's, drawn so it meets a slope)
		double WallThickness(Material M) { return M == Material::Wood ? 0.15 : M == Material::Brick ? 0.22 : 0.20; }
		double FloorThickness(Material M) { return M == Material::Wood ? 0.12 : 0.15; }
		double RoofThickness(Material M) { return M == Material::Wood ? 0.10 : 0.12; }
		double SlabThickness(Material M) { return M == Material::Wood ? 0.15 : 0.30; }

		struct Colour { float R, G, B; };
		Colour ColourOf(Material M, Kind K)
		{
			// (linear: pine planks, red brick, grey concrete; roofs: dark weathered boards, terracotta tiles, concrete)
			if (K == Kind::Roof)
			{
				return M == Material::Wood ? Colour{ 0.12f, 0.07f, 0.04f } : M == Material::Brick ? Colour{ 0.42f, 0.10f, 0.04f } : Colour{ 0.22f, 0.22f, 0.21f };
			}
			const Colour C = M == Material::Wood ? Colour{ 0.38f, 0.20f, 0.08f } : M == Material::Brick ? Colour{ 0.32f, 0.07f, 0.035f } : Colour{ 0.34f, 0.34f, 0.32f };
			const float Shade = K == Kind::Foundation ? 0.75f : 1.0f;
			return { C.R * Shade, C.G * Shade, C.B * Shade };
		}

		Finish FinishOf(Material M, Kind K)
		{
			if (K == Kind::Roof)
			{
				return M == Material::Wood ? Finish::Planks : M == Material::Brick ? Finish::Tiles : Finish::Concrete;
			}
			return M == Material::Wood ? Finish::Planks : M == Material::Brick ? Finish::Brick : Finish::Concrete;
		}

		Box MakeBox(const Vec3& Centre, const Vec3& Half, Colour C, double Yaw = 0.0, double Tilt = 0.0)
		{
			Box B;
			B.Centre = Centre;
			B.Half = Half;
			B.Yaw = Yaw;
			B.Tilt = Tilt;
			B.R = C.R;
			B.G = C.G;
			B.B = C.B;
			return B;
		}

		/** The way Turn points (x, y) */
		void Direction(int32_t Turn, double& DX, double& DY)
		{
			const int32_t T = ((Turn % 4) + 4) % 4;
			DX = T == 0 ? 1.0 : T == 2 ? -1.0 : 0.0;
			DY = T == 1 ? 1.0 : T == 3 ? -1.0 : 0.0;
		}

		/** A point's distance from a box (0 inside) */
		double Distance(const Box& B, const Vec3& P)
		{
			const Vec3 L = B.Turn().Unrotate(P - B.Centre);
			const Vec3 Out(std::max(std::abs(L.X) - B.Half.X, 0.0), std::max(std::abs(L.Y) - B.Half.Y, 0.0), std::max(std::abs(L.Z) - B.Half.Z, 0.0));
			return Out.Length();
		}
	}

	geobody::Quat Box::Turn() const
	{
		return geobody::Quat::FromAxisAngle(Vec3(0.0, 0.0, 1.0), Yaw) * geobody::Quat::FromAxisAngle(Vec3(0.0, 1.0, 0.0), -Tilt);
	}

	const char* Name(Kind K)
	{
		switch (K)
		{
		case Kind::Foundation: return "Foundation";
		case Kind::Floor: return "Floor";
		case Kind::Wall: return "Wall";
		case Kind::Doorway: return "Doorway";
		case Kind::Window: return "Window";
		case Kind::Stairs: return "Stairs";
		case Kind::Roof: return "Roof";
		case Kind::Door: return "Door";
		case Kind::HalfWall: return "Half wall";
		case Kind::Railing: return "Railing";
		case Kind::GlassWall: return "Glass wall";
		case Kind::Pillar: return "Pillar";
		default: return "?";
		}
	}

	const char* Name(Material M)
	{
		switch (M)
		{
		case Material::Wood: return "Wood";
		case Material::Brick: return "Brick";
		case Material::Concrete: return "Concrete";
		default: return "?";
		}
	}

	const geobody::Material& Stuff(Material M)
	{
		return M == Material::Wood ? geobody::Materials::Pine : M == Material::Brick ? geobody::Materials::Brick : geobody::Materials::Concrete;
	}

	const char* Why(Refusal R)
	{
		switch (R)
		{
		case Refusal::None: return "";
		case Refusal::Taken: return "Something's already there";
		case Refusal::Unsupported: return "Nothing would hold it up";
		case Refusal::NotOnGround: return "A foundation must stand on the ground";
		case Refusal::WrongLevel: return "That goes at another level";
		case Refusal::NeedsDoorway: return "A door goes in a doorway";
		default: return "?";
		}
	}

	// --- a piece's boxes ---

	std::vector<Box> Structure::PieceSolids(const Piece& P, bool bForLook)
	{
		std::vector<Box> Out;
		const Slot& S = P.At;
		const double Z0 = S.Level * Storey;
		const double CX = (S.X + 0.5) * Cell, CY = (S.Y + 0.5) * Cell;
		const Colour C = ColourOf(P.Of, P.What);
		const size_t First = Out.size();
		switch (P.What)
		{
		case Kind::Foundation:
			// (drawn to its full depth, to meet the ground on a slope; to collide with, the same)
			Out.push_back(MakeBox(Vec3(CX, CY, -FoundationDepth * 0.5), Vec3(Cell * 0.5, Cell * 0.5, FoundationDepth * 0.5), C));
			break;
		case Kind::Floor:
		{
			const double T = FloorThickness(P.Of);
			Out.push_back(MakeBox(Vec3(CX, CY, Z0 - T * 0.5), Vec3(Cell * 0.5, Cell * 0.5, T * 0.5), C));
			break;
		}
		case Kind::Wall:
		case Kind::Doorway:
		case Kind::Window:
		case Kind::HalfWall:
		case Kind::GlassWall:
		{
			// along the edge: Side 0 the square's west edge (along y), 1 its south edge (along x); a little longer than
			// the square so walls meet at the corners (a thin wall half as thick)
			const double T = WallThickness(P.Of) * (P.What == Kind::Wall && (P.Size & Thin) ? 0.5 : 1.0);
			const bool bAlongY = S.Side == 0;
			const double EX = bAlongY ? S.X * Cell : CX, EY = bAlongY ? CY : S.Y * Cell;
			const double Length = Cell + T;
			// a span of the wall from A to B along it (from its middle) and from Low to High up it
			auto Span = [&](double A, double B, double Low, double High, bool bGlass = false)
			{
				const double Mid = (A + B) * 0.5, Half = (B - A) * 0.5;
				const Vec3 Centre(EX + (bAlongY ? 0.0 : Mid), EY + (bAlongY ? Mid : 0.0), Z0 + (Low + High) * 0.5);
				const Vec3 Size = bAlongY ? Vec3(bGlass ? 0.02 : T * 0.5, Half, (High - Low) * 0.5) : Vec3(Half, bGlass ? 0.02 : T * 0.5, (High - Low) * 0.5);
				Box B2 = MakeBox(Centre, Size, bGlass ? Colour{ 0.18f, 0.26f, 0.30f } : C);
				B2.bGlass = bGlass;
				Out.push_back(B2);
			};
			if (P.What == Kind::Wall)
			{
				// (half wide: from the edge's middle to one end, the halves meeting there; half high: 1.5 m)
				const bool bHalfWide = (P.Size & HalfWide) != 0;
				Span(bHalfWide && S.Part == 1 ? 0.0 : -Length * 0.5, bHalfWide && S.Part == 0 ? 0.0 : Length * 0.5, 0.0, (P.Size & HalfHigh) ? Storey * 0.5 : Storey);
			}
			else if (P.What == Kind::Doorway)
			{
				const double W = 1.0, H = 2.1;	// (a door's opening)
				Span(-Length * 0.5, -W * 0.5, 0.0, Storey);
				Span(W * 0.5, Length * 0.5, 0.0, Storey);
				Span(-W * 0.5, W * 0.5, H, Storey);
			}
			else if (P.What == Kind::Window)
			{
				const double W = 1.2, Sill = 0.9, Top = 2.1;	// (a window's)
				Span(-Length * 0.5, -W * 0.5, 0.0, Storey);
				Span(W * 0.5, Length * 0.5, 0.0, Storey);
				Span(-W * 0.5, W * 0.5, 0.0, Sill);
				Span(-W * 0.5, W * 0.5, Top, Storey);
				Span(-W * 0.5, W * 0.5, Sill, Top, true);	// the pane
			}
			else if (P.What == Kind::HalfWall)
			{
				Span(-Length * 0.5, Length * 0.5, 0.0, 1.0);
			}
			else
			{
				// (all glass: a slim frame round it, the posts at its ends where walls meet it, a kerb and a head)
				const double W = 2.6, Kerb = 0.1, Head = 2.7;
				Span(-Length * 0.5, -W * 0.5, 0.0, Storey);
				Span(W * 0.5, Length * 0.5, 0.0, Storey);
				Span(-W * 0.5, W * 0.5, 0.0, Kerb);
				Span(-W * 0.5, W * 0.5, Head, Storey);
				Span(-W * 0.5, W * 0.5, Kerb, Head, true);	// the pane
			}
			break;
		}
		case Kind::Railing:
		{
			// posts at its ends and middle, a handrail on them at 1 m and a rail half way; wooden, or painted steel
			// (chosen in brick or concrete)
			const bool bSteel = P.Of != Material::Wood;
			const double Post = bSteel ? 0.05 : 0.08, Rail = bSteel ? 0.04 : 0.06, High = 1.0;
			const bool bAlongY = S.Side == 0;
			const Vec3 Along = bAlongY ? Vec3(0.0, 1.0, 0.0) : Vec3(1.0, 0.0, 0.0);
			const Vec3 Mid(bAlongY ? S.X * Cell : CX, bAlongY ? CY : S.Y * Cell, Z0);
			const Colour Paint = bSteel ? Colour{ 0.05f, 0.05f, 0.05f } : C;
			auto Sized = [&](double L, double Thick, double Tall) { return bAlongY ? Vec3(Thick * 0.5, L * 0.5, Tall * 0.5) : Vec3(L * 0.5, Thick * 0.5, Tall * 0.5); };
			for (const double At : { -Cell * 0.5 + Post * 0.5, 0.0, Cell * 0.5 - Post * 0.5 })
			{
				Out.push_back(MakeBox(Mid + Along * At + Vec3(0.0, 0.0, High * 0.5), Sized(Post, Post, High), Paint));
			}
			for (const double Z : { High - Rail * 0.5, High * 0.5 })
			{
				Out.push_back(MakeBox(Mid + Vec3(0.0, 0.0, Z), Sized(Cell, Rail, Rail), Paint));
			}
			for (size_t i = First; i < Out.size(); ++i)
			{
				Out[i].bSteel = bSteel;
			}
			break;
		}
		case Kind::Pillar:
		{
			// at the square's south-west corner, a storey high
			const double Side = P.Of == Material::Wood ? 0.2 : 0.3;
			Out.push_back(MakeBox(Vec3(S.X * Cell, S.Y * Cell, Z0 + Storey * 0.5), Vec3(Side * 0.5, Side * 0.5, Storey * 0.5), C));
			break;
		}
		case Kind::Door:
		{
			// a 45 mm wooden door in the doorway's 1.0 x 2.1 m opening, hinged at its -end; open, swung a quarter turn
			// out to the +side (a little less wide and tall than the opening: it shuts in it), a brass handle on each face
			const double W = 0.98, H = 2.08, T = 0.045;
			const bool bAlongY = S.Side == 0;
			const Vec3 Along = bAlongY ? Vec3(0.0, 1.0, 0.0) : Vec3(1.0, 0.0, 0.0);
			const Vec3 Swing = bAlongY ? Vec3(1.0, 0.0, 0.0) : Vec3(0.0, 1.0, 0.0);
			const Vec3 Edge(bAlongY ? S.X * Cell : CX, bAlongY ? CY : S.Y * Cell, Z0 + H * 0.5 + 0.01);
			const Vec3 Hinge = Edge - Along * (W * 0.5);
			const Vec3 Long = P.bOpen ? Swing : Along;
			const Vec3 Face = P.bOpen ? Along : Swing;
			auto Sized = [](const Vec3& Axis, double L, double Thick, double Tall)
			{
				return std::abs(Axis.X) > 0.5 ? Vec3(L * 0.5, Thick * 0.5, Tall * 0.5) : Vec3(Thick * 0.5, L * 0.5, Tall * 0.5);
			};
			const Colour Panel = ColourOf(Material::Wood, Kind::Door);
			Out.push_back(MakeBox(Hinge + Long * (W * 0.5), Sized(Long, W, T, H), { Panel.R * 0.7f, Panel.G * 0.7f, Panel.B * 0.7f }));
			for (const double Side : { -1.0, 1.0 })
			{
				const Vec3 Handle = Hinge + Long * (W - 0.08) + Face * (Side * (T * 0.5 + 0.03)) - Vec3(0.0, 0.0, H * 0.5 - 1.0);
				Out.push_back(MakeBox(Handle, Sized(Long, 0.12, 0.02, 0.02), { 0.55f, 0.4f, 0.12f }));
			}
			break;
		}
		case Kind::Stairs:
		{
			// a straight flight up the storey across the square, its steps 25 cm up and along, 1.2 m wide (to walk up:
			// each step a box, as a body steps up them)
			double DX = 0.0, DY = 0.0;
			Direction(S.Turn, DX, DY);
			const double Rise = Storey / Steps, Run = Cell / Steps;
			for (int32_t k = 0; k < Steps; ++k)
			{
				const double Along = -Cell * 0.5 + (k + 0.5) * Run;
				const double Top = (k + 1) * Rise;
				const Vec3 Centre(CX + DX * Along, CY + DY * Along, Z0 + Top * 0.5);
				const Vec3 Half = DX != 0.0 ? Vec3(Run * 0.5, StairWidth * 0.5, Top * 0.5) : Vec3(StairWidth * 0.5, Run * 0.5, Top * 0.5);
				Out.push_back(MakeBox(Centre, Half, C));
			}
			break;
		}
		case Kind::Roof:
		{
			// a panel rising RoofRise across the square the way Turn points, on the walls' tops
			const double T = RoofThickness(P.Of);
			const double Slope = std::atan2(RoofRise, Cell), Length = std::sqrt(Cell * Cell + RoofRise * RoofRise) + 0.3;	// (a little eave)
			const Vec3 Centre(CX, CY, Z0 + RoofRise * 0.5 + T * 0.5);
			Out.push_back(MakeBox(Centre, Vec3(Length * 0.5, Cell * 0.5 + 0.1, T * 0.5), C, S.Turn * Pi * 0.5, Slope));
			break;
		}
		default:
			break;
		}
		(void)bForLook;	// (the look and the solids are the same boxes, for now)
		// each box's finish: its material's (the panes glass; handles and steel plain; a door's planks)
		for (size_t i = First; i < Out.size(); ++i)
		{
			const bool bHandle = P.What == Kind::Door && i > First;
			Out[i].Look = Out[i].bGlass ? Finish::Glass : bHandle || Out[i].bSteel ? Finish::Plain : FinishOf(P.What == Kind::Door ? Material::Wood : P.Of, P.What);
		}
		return Out;
	}

	void AddBoxes(Mesh& Out, const std::vector<Box>& Boxes)
	{
		for (const Box& B : Boxes)
		{
			const geobody::Quat Q = B.Turn();
			// each face: its outward normal (box frame) and its two in-plane axes
			const Vec3 Axes[3] = { Vec3(1.0, 0.0, 0.0), Vec3(0.0, 1.0, 0.0), Vec3(0.0, 0.0, 1.0) };
			const double Half[3] = { B.Half.X, B.Half.Y, B.Half.Z };
			for (int32_t a = 0; a < 3; ++a)
			{
				for (const double Sign : { -1.0, 1.0 })
				{
					const int32_t U = (a + 1) % 3, V = (a + 2) % 3;
					const Vec3 N = Axes[a] * Sign;
					const Vec3 Middle = N * Half[a];
					const Vec3 DU = Axes[U] * Half[U], DV = Axes[V] * Half[V];
					Vec3 Corners[4] = { Middle - DU - DV, Middle + DU - DV, Middle + DU + DV, Middle - DU + DV };
					// counter-clockwise seen from outside: (C1 - C0) x (C2 - C0) along the normal
					if ((Corners[1] - Corners[0]).Cross(Corners[2] - Corners[0]).Dot(N) < 0.0)
					{
						std::swap(Corners[1], Corners[3]);
					}
					const int32_t Base = int32_t(Out.Positions.size());
					const Vec3 WorldN = Q.Rotate(N);
					for (const Vec3& C : Corners)
					{
						Out.Positions.push_back(B.Centre + Q.Rotate(C));
						Out.Normals.push_back(WorldN);
						Out.Colours.insert(Out.Colours.end(), { B.R, B.G, B.B, float(B.Look) });
					}
					Out.Indices.insert(Out.Indices.end(), { Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
				}
			}
		}
	}

	Mesh Structure::PieceLook(const Piece& P)
	{
		Mesh Out;
		AddBoxes(Out, PieceSolids(P, true));
		return Out;
	}

	// --- holding up ---

	int32_t Structure::Find(Kind What, const Slot& At) const
	{
		for (size_t i = 0; i < All.size(); ++i)
		{
			const Piece& P = All[i];
			if (P.What != What || P.At.X != At.X || P.At.Y != At.Y || P.At.Level != At.Level)
			{
				continue;
			}
			if (OnEdge(What) && P.At.Side != At.Side)
			{
				continue;
			}
			if ((P.Size & HalfWide) && P.At.Part != At.Part)
			{
				continue;	// (the edge's other half)
			}
			return int32_t(i);
		}
		return -1;
	}

	namespace
	{
		// the best of a set of supports (0 none)
		struct Best
		{
			double Value = 0.0;
			void Offer(double V) { Value = std::max(Value, V); }
		};
	}

	double Structure::SupportIfPlaced(Kind What, const Slot& At, uint8_t Size) const
	{
		(void)Size;	// (a piece's size doesn't change what holds it)
		// the most firmly held piece of a kind there (on an edge: either half's; holding: only a storey-high one)
		auto SupportOf = [this](Kind K, int32_t X, int32_t Y, int32_t Level, int32_t Side = 0, bool bHolding = false) -> double
		{
			double Most = 0.0;
			for (const Piece& P : All)
			{
				if (P.What == K && P.At.X == X && P.At.Y == Y && P.At.Level == Level && (!OnEdge(K) || P.At.Side == Side)
					&& !(bHolding && (P.Size & HalfHigh)))
				{
					Most = std::max(Most, P.Support);
				}
			}
			return Most;
		};
		// a floor-like piece (what stands on it can): the foundation at level 0, a floor above
		auto Base = [&](int32_t X, int32_t Y, int32_t Level) { return Level == 0 ? SupportOf(Kind::Foundation, X, Y, 0) : SupportOf(Kind::Floor, X, Y, Level); };
		// any wall on an edge that holds up what's over it
		auto EdgeAt = [&](int32_t X, int32_t Y, int32_t Level, int32_t Side)
		{
			return std::max({ SupportOf(Kind::Wall, X, Y, Level, Side, true), SupportOf(Kind::Doorway, X, Y, Level, Side), SupportOf(Kind::Window, X, Y, Level, Side),
				SupportOf(Kind::GlassWall, X, Y, Level, Side) });
		};
		Best Out;
		switch (What)
		{
		case Kind::Foundation:
			Out.Offer(1.0);	// (on the ground: the host checks it's there)
			break;
		case Kind::Wall:
		case Kind::Doorway:
		case Kind::Window:
		case Kind::HalfWall:
		case Kind::Railing:
		case Kind::GlassWall:
		{
			// on the floor (or foundation) of either square beside its edge, or on the wall under it, a little less firmly
			const int32_t NX = At.Side == 0 ? At.X - 1 : At.X, NY = At.Side == 0 ? At.Y : At.Y - 1;
			Out.Offer(Base(At.X, At.Y, At.Level));
			Out.Offer(Base(NX, NY, At.Level));
			if (At.Level > 0)
			{
				Out.Offer(EdgeAt(At.X, At.Y, At.Level - 1, At.Side) * 0.9);
			}
			break;
		}
		case Kind::Floor:
		case Kind::Roof:
		{
			// on a wall under any of its edges, or a pillar under any of its corners; or reaching out from a floor (or
			// roof) beside it, half as firmly
			Out.Offer(EdgeAt(At.X, At.Y, At.Level - 1, 0));
			Out.Offer(EdgeAt(At.X + 1, At.Y, At.Level - 1, 0));
			Out.Offer(EdgeAt(At.X, At.Y, At.Level - 1, 1));
			Out.Offer(EdgeAt(At.X, At.Y + 1, At.Level - 1, 1));
			for (const int32_t CX : { At.X, At.X + 1 })
			{
				for (const int32_t CY : { At.Y, At.Y + 1 })
				{
					Out.Offer(SupportOf(Kind::Pillar, CX, CY, At.Level - 1));
				}
			}
			const int32_t Near[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
			for (const auto& N : Near)
			{
				Out.Offer(std::max(SupportOf(Kind::Floor, At.X + N[0], At.Y + N[1], At.Level), SupportOf(Kind::Roof, At.X + N[0], At.Y + N[1], At.Level)) * 0.5);
			}
			break;
		}
		case Kind::Stairs:
			Out.Offer(Base(At.X, At.Y, At.Level));
			break;
		case Kind::Pillar:
			// on the floor (or foundation) of any of the four squares at its corner, or on the pillar under it
			for (const int32_t SX : { At.X - 1, At.X })
			{
				for (const int32_t SY : { At.Y - 1, At.Y })
				{
					Out.Offer(Base(SX, SY, At.Level));
				}
			}
			if (At.Level > 0)
			{
				Out.Offer(SupportOf(Kind::Pillar, At.X, At.Y, At.Level - 1) * 0.9);
			}
			break;
		case Kind::Door:
			Out.Offer(SupportOf(Kind::Doorway, At.X, At.Y, At.Level, At.Side));	// (hung in its doorway)
			break;
		default:
			break;
		}
		return Out.Value;
	}

	Refusal Structure::CanPlace(Kind What, const Slot& At, double GroundLow, double GroundHigh, uint8_t Size) const
	{
		const bool bOver = What == Kind::Floor || What == Kind::Roof;
		if ((What == Kind::Foundation && At.Level != 0) || (bOver && At.Level < 1) || At.Level < 0)
		{
			return Refusal::WrongLevel;
		}
		// one floor, roof or foundation to a square at a level, one flight of stairs, one wall to an edge
		for (const Piece& P : All)
		{
			if (P.At.X != At.X || P.At.Y != At.Y || P.At.Level != At.Level)
			{
				continue;
			}
			// (an edge's two halves can each have a half-wide wall)
			const bool bOtherHalf = (Size & HalfWide) && (P.Size & HalfWide) && P.At.Part != At.Part;
			const bool bSameEdge = IsEdge(What) && IsEdge(P.What) && P.At.Side == At.Side && !bOtherHalf;
			const bool bBothOver = (What == Kind::Foundation || bOver) && (P.What == Kind::Foundation || P.What == Kind::Floor || P.What == Kind::Roof);
			const bool bBothStairs = What == Kind::Stairs && P.What == Kind::Stairs;
			const bool bBothDoors = What == Kind::Door && P.What == Kind::Door && P.At.Side == At.Side;
			const bool bBothPillars = What == Kind::Pillar && P.What == Kind::Pillar;
			if (bSameEdge || bBothOver || bBothStairs || bBothDoors || bBothPillars)
			{
				return Refusal::Taken;
			}
		}
		if (What == Kind::Door && Find(Kind::Doorway, At) < 0)
		{
			return Refusal::NeedsDoorway;
		}
		if (What == Kind::Foundation && (GroundHigh > 0.05 || GroundLow < -FoundationDepth))
		{
			return Refusal::NotOnGround;	// (the ground over its top, or below its bottom: it would float)
		}
		return SupportIfPlaced(What, At, Size) >= MinSupport ? Refusal::None : Refusal::Unsupported;
	}

	Refusal Structure::Place(Kind What, Material Of, const Slot& At, double GroundLow, double GroundHigh, uint8_t Size)
	{
		Size = Sizable(What) ? Size : 0;
		const Refusal R = CanPlace(What, At, GroundLow, GroundHigh, Size);
		if (R != Refusal::None)
		{
			return R;
		}
		Piece P;
		P.What = What;
		P.Of = What == Kind::Door ? Material::Wood : Of;	// (a door's wood, whatever the walls are)
		P.At = At;
		P.Size = Size;
		if (!(Size & HalfWide))
		{
			P.At.Part = 0;
		}
		if (!OnEdge(What))
		{
			P.At.Side = 0;
		}
		if (What != Kind::Stairs && What != Kind::Roof)
		{
			P.At.Turn = 0;
		}
		All.push_back(P);
		Settle();
		return Refusal::None;
	}

	void Structure::Settle()
	{
		// from the ground up: everything from nothing, then raised to what holds it, until nothing changes (the support
		// only ever rises in this, so it settles)
		for (Piece& P : All)
		{
			P.Support = 0.0;
		}
		for (int32_t Round = 0; Round < 200; ++Round)
		{
			bool bChanged = false;
			for (Piece& P : All)
			{
				const double S = SupportIfPlaced(P.What, P.At, P.Size);
				if (S > P.Support + 1e-9)
				{
					P.Support = S;
					bChanged = true;
				}
			}
			if (!bChanged)
			{
				break;
			}
		}
	}

	int32_t Structure::Remove(int32_t Index)
	{
		if (Index < 0 || Index >= int32_t(All.size()))
		{
			return 0;
		}
		All.erase(All.begin() + Index);
		int32_t Down = 1;
		// what no longer stands comes down too (and what that held, in turn)
		for (;;)
		{
			Settle();
			const size_t Before = All.size();
			All.erase(std::remove_if(All.begin(), All.end(), [](const Piece& P) { return P.Support < MinSupport; }), All.end());
			Down += int32_t(Before - All.size());
			if (All.size() == Before)
			{
				break;
			}
		}
		return Down;
	}

	bool Structure::Toggle(int32_t Index)
	{
		if (Index < 0 || Index >= int32_t(All.size()) || All[size_t(Index)].What != Kind::Door)
		{
			return false;
		}
		All[size_t(Index)].bOpen = !All[size_t(Index)].bOpen;
		return All[size_t(Index)].bOpen;
	}

	void Structure::SetPieces(const std::vector<Piece>& Pieces)
	{
		All = Pieces;
		Settle();
		All.erase(std::remove_if(All.begin(), All.end(), [](const Piece& P) { return P.Support < MinSupport; }), All.end());
		Settle();
	}

	// --- where a piece would go ---

	Slot Structure::Snap(Kind What, const Vec3& Point, double FacingX, double FacingY) const
	{
		Slot S;
		// (a touch further the way they look: a point on a wall's face counts as the square beyond it)
		const double Len = std::max(std::sqrt(FacingX * FacingX + FacingY * FacingY), 1e-9);
		const double PX = Point.X + FacingX / Len * 0.05, PY = Point.Y + FacingY / Len * 0.05;
		S.X = int32_t(std::floor(PX / Cell));
		S.Y = int32_t(std::floor(PY / Cell));
		switch (What)
		{
		case Kind::Foundation:
			S.Level = 0;
			break;
		case Kind::Floor:
		case Kind::Roof:
			// (over the storey they look into: on a floor, or up a wall)
			S.Level = std::max(1, int32_t(std::floor(Point.Z / Storey + 0.67)));
			break;
		default:
			S.Level = std::max(0, int32_t(std::floor(Point.Z / Storey + 0.1)));
			break;
		}
		if (OnEdge(What))
		{
			// the square's nearest edge (west and south its own; east and north the next square's)
			const double FX = PX - S.X * Cell, FY = PY - S.Y * Cell;
			const double West = FX, East = Cell - FX, South = FY, North = Cell - FY;
			const double Least0 = std::min({ West, East, South, North });
			S.Part = (Least0 == West || Least0 == East) ? (FY > Cell * 0.5 ? 1 : 0) : (FX > Cell * 0.5 ? 1 : 0);	// (along it: which half)
			const double Least = std::min({ West, East, South, North });
			if (Least == West)
			{
				S.Side = 0;
			}
			else if (Least == East)
			{
				S.Side = 0;
				S.X += 1;
			}
			else if (Least == South)
			{
				S.Side = 1;
			}
			else
			{
				S.Side = 1;
				S.Y += 1;
			}
		}
		if (What == Kind::Pillar)
		{
			// the nearest corner (named by the square north-east of it)
			S.X = int32_t(std::floor(PX / Cell + 0.5));
			S.Y = int32_t(std::floor(PY / Cell + 0.5));
		}
		if (What == Kind::Stairs || What == Kind::Roof)
		{
			// rising away from the builder
			S.Turn = std::abs(FacingX) >= std::abs(FacingY) ? (FacingX >= 0.0 ? 0 : 2) : (FacingY >= 0.0 ? 1 : 3);
		}
		return S;
	}

	int32_t Structure::PieceAt(const Vec3& Point) const
	{
		int32_t Nearest = -1;
		double Least = 0.5;
		for (size_t i = 0; i < All.size(); ++i)
		{
			for (const Box& B : PieceSolids(All[i], false))
			{
				const double D = Distance(B, Point);
				if (D < Least)
				{
					Least = D;
					Nearest = int32_t(i);
				}
			}
		}
		return Nearest;
	}

	// --- what it's made of ---

	std::vector<geobody::Part> Structure::Parts() const
	{
		std::vector<geobody::Part> Out;
		for (const Piece& P : All)
		{
			const geobody::Material& Of = Stuff(P.Of);
			const std::string Label = std::string(Name(P.Of)) + " " + Name(P.What);
			if (P.What == Kind::Foundation)
			{
				// (its slab; the block under it is drawn to meet the ground, which carries it)
				const double T = SlabThickness(P.Of);
				Out.push_back(geobody::Box(Label, Vec3((P.At.X + 0.5) * Cell, (P.At.Y + 0.5) * Cell, -T * 0.5), Vec3(Cell, Cell, T), Of));
				continue;
			}
			if (P.What == Kind::Stairs)
			{
				// (a flight: its treads and stringers, as a slab along its slope)
				double DX = 0.0, DY = 0.0;
				Direction(P.At.Turn, DX, DY);
				geobody::Part Flight = geobody::Box(Label, Vec3((P.At.X + 0.5) * Cell, (P.At.Y + 0.5) * Cell, P.At.Level * Storey + Storey * 0.5),
					Vec3(std::sqrt(Cell * Cell + Storey * Storey), StairWidth, 0.15), Of);
				Flight.Turn = geobody::Quat::FromAxisAngle(Vec3(0.0, 0.0, 1.0), P.At.Turn * Pi * 0.5) * geobody::Quat::FromAxisAngle(Vec3(0.0, 1.0, 0.0), -std::atan2(Storey, Cell));
				Out.push_back(Flight);
				continue;
			}
			for (const Box& B : PieceSolids(P, false))
			{
				geobody::Part Part = geobody::Box(Label, B.Centre, B.Half * 2.0, B.bGlass ? geobody::Materials::Glass : B.bSteel ? geobody::Materials::Steel : Of);
				Part.Turn = B.Turn();
				Out.push_back(Part);
			}
		}
		return Out;
	}

	std::vector<Box> Structure::Solids() const
	{
		std::vector<Box> Out;
		for (const Piece& P : All)
		{
			const std::vector<Box> Boxes = PieceSolids(P, false);
			Out.insert(Out.end(), Boxes.begin(), Boxes.end());
		}
		return Out;
	}

	namespace
	{
		/** A triangular prism: the triangle A, B, C (counter-clockwise seen from the +N side), Thick deep along N */
		void AddPrism(Mesh& Out, const Vec3& A, const Vec3& B, const Vec3& C, const Vec3& N, double Thick, float R, float G, float Bl, Finish F)
		{
			const Vec3 H = N * (Thick * 0.5);
			auto Face = [&](const std::vector<Vec3>& Corners, const Vec3& Normal)
			{
				std::vector<Vec3> P = Corners;
				if ((P[1] - P[0]).Cross(P[2] - P[0]).Dot(Normal) < 0.0)
				{
					std::reverse(P.begin(), P.end());
				}
				const int32_t Base = int32_t(Out.Positions.size());
				for (const Vec3& V : P)
				{
					Out.Positions.push_back(V);
					Out.Normals.push_back(Normal);
					Out.Colours.insert(Out.Colours.end(), { R, G, Bl, float(F) });
				}
				for (size_t i = 1; i + 1 < P.size(); ++i)
				{
					Out.Indices.insert(Out.Indices.end(), { Base, Base + int32_t(i), Base + int32_t(i) + 1 });
				}
			};
			Face({ A + H, B + H, C + H }, N);
			Face({ A - H, B - H, C - H }, -N);
			const Vec3 Mid = (A + B + C) * (1.0 / 3.0);
			const Vec3 Edges[3][2] = { { A, B }, { B, C }, { C, A } };
			for (const auto& E : Edges)
			{
				const Vec3 Along = E[1] - E[0];
				Vec3 Out2 = Along.Cross(N).Normal();
				if (Out2.Dot((E[0] + E[1]) * 0.5 - Mid) < 0.0)
				{
					Out2 = -Out2;
				}
				Face({ E[0] - H, E[1] - H, E[1] + H, E[0] + H }, Out2);
			}
		}
	}

	Mesh Structure::Look() const
	{
		Mesh Out;
		for (const Piece& P : All)
		{
			AddBoxes(Out, PieceSolids(P, true));
		}
		// the gables: where a roof's sloped end is over a wall, the triangle under it filled in that wall's material
		for (const Piece& R : All)
		{
			if (R.What != Kind::Roof)
			{
				continue;
			}
			double DX = 0.0, DY = 0.0;
			Direction(R.At.Turn, DX, DY);
			const double Z0 = R.At.Level * Storey;
			// its two sloped ends: the edges along the way it rises
			for (int32_t End = 0; End < 2; ++End)
			{
				Slot E;
				E.Level = R.At.Level - 1;
				E.X = R.At.X + (DX == 0.0 ? End : 0);
				E.Y = R.At.Y + (DY == 0.0 ? End : 0);
				E.Side = DX == 0.0 ? 0 : 1;	// (rising along y: its ends are the west and east edges, side 0)
				int32_t Under = -1;
				for (const Kind K : { Kind::Wall, Kind::Doorway, Kind::Window, Kind::GlassWall })
				{
					Under = Under >= 0 ? Under : Find(K, E);
				}
				if (Under < 0)
				{
					continue;
				}
				const Piece& W = All[size_t(Under)];
				const Colour C = ColourOf(W.Of, W.What);
				// the triangle: along the edge from the roof's low side to its high side, up to the roof's underside
				const Vec3 Start = E.Side == 0 ? Vec3(E.X * Cell, R.At.Y * Cell, Z0) : Vec3(R.At.X * Cell, E.Y * Cell, Z0);
				const Vec3 Along = E.Side == 0 ? Vec3(0.0, Cell, 0.0) : Vec3(Cell, 0.0, 0.0);
				const bool bRisesAlong = (DX + DY) > 0.0;	// (towards +x or +y: high at the edge's far end)
				const Vec3 Low = bRisesAlong ? Start : Start + Along, High = bRisesAlong ? Start + Along : Start;
				const Vec3 N = E.Side == 0 ? Vec3(1.0, 0.0, 0.0) : Vec3(0.0, 1.0, 0.0);
				AddPrism(Out, Low, High, High + Vec3(0.0, 0.0, RoofRise), N, WallThickness(W.Of), C.R, C.G, C.B, FinishOf(W.Of, W.What));
			}
		}
		return Out;
	}
}
