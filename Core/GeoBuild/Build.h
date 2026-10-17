// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Building, as a game like Rust has it: pieces on a grid, each snapping to its place beside, on or over the others,
// that stand only while something holds them up. A structure is one grid (3 m squares, 3 m storeys) anchored on the
// ground: foundations on the land, walls on the squares' edges (plain, a doorway, a window, all glass; half walls and
// railings that hold nothing up), pillars at the squares' corners, floors and roofs over the walls and pillars, stairs
// inside. Each piece is made of a real material (wood, brick, concrete) at a real thickness, so it
// weighs what it would (GeoBody parts).
//
// Holding up (the support rule): a foundation stands on the ground; a wall on a foundation or floor beside it, or on
// the wall under it (a little less firmly each storey); a pillar on a floor by its corner, or on the pillar under it;
// a floor or roof on a wall under any of its edges or a pillar under any of its corners, or reaching out from a floor
// beside it (half as firmly each square out); stairs on the floor they stand on. A piece with too
// little holding it can't be placed, and taking one out lets what it held come down.
//
// Engine-free, as the rest of Core: metres, the structure's own frame (x along its rows, y along its columns, z up;
// right-handed), the host placing it in the world and drawing and colliding what it gives.

#pragma once

#include "GeoBody/Body.h"

#include <cstdint>
#include <string>
#include <vector>

namespace geobuild
{
	constexpr double Cell = 3.0;		// m: a square's side
	constexpr double Storey = 3.0;		// m: floor to floor
	constexpr double FoundationDepth = 3.0;	// m: a foundation's block, its top level 0 (on a slope, its footing reaches
	// down to the low side, as a house's stem wall does: a hillside, not a cliff)
	constexpr double MinSupport = 0.05;	// a piece needs at least this much holding it

	enum class Kind : uint8_t
	{
		Foundation,	// a block on the ground, its top the floor (level 0 only)
		Floor,		// a slab over a storey (level 1 up)
		Wall,		// on a square's edge, a storey high
		Doorway,	// a wall with a door's opening (1.0 x 2.1 m)
		Window,		// a wall with a window (1.2 x 1.2 m, its sill at 0.9 m)
		Stairs,		// a flight up a storey, inside a square
		Roof,		// a pitched panel over a square, rising 1.5 m across it
		Door,		// a hinged door in a doorway (its slot the doorway's), open or shut
		HalfWall,	// a wall 1 m high on an edge (a balcony's parapet, a garden wall): holds nothing up
		Railing,	// posts and rails 1 m high on an edge (wooden; steel, chosen in brick or concrete): holds nothing up
		GlassWall,	// a wall of glass in a slim frame, floor to ceiling (2.6 x 2.6 m of glass): holds up as a wall does
		Pillar,		// a post a storey high at a square's corner (its slot the square whose south-west corner it is)
		Count
	};
	const char* Name(Kind K);
	inline bool IsEdge(Kind K)	// (a wall's slot: one to an edge)
	{
		return K == Kind::Wall || K == Kind::Doorway || K == Kind::Window || K == Kind::HalfWall || K == Kind::Railing || K == Kind::GlassWall;
	}
	inline bool OnEdge(Kind K) { return IsEdge(K) || K == Kind::Door; }	// (snapped to an edge)
	inline bool Holds(Kind K) { return K == Kind::Wall || K == Kind::Doorway || K == Kind::Window || K == Kind::GlassWall; }	// (a storey's wall: what's over it can stand on it)

	/** A plain wall's size (flags, any together): half as high (1.5 m: holds nothing up), half as wide (1.5 m, on one half
	 *  of its edge: Slot::Part), half as thick (a partition) */
	enum SizeFlag : uint8_t { HalfHigh = 1, HalfWide = 2, Thin = 4 };
	inline bool Sizable(Kind K) { return K == Kind::Wall; }

	enum class Material : uint8_t
	{
		Wood,		// pine planks
		Brick,		// fired clay brick (South Africa's double brick walls)
		Concrete,	// cast concrete
		Count
	};
	const char* Name(Material M);
	const geobody::Material& Stuff(Material M);	// (its density)

	/** Where a piece goes: a square (X, Y) at a level (storeys up), and for an edge piece which edge (Side 0 the
	 *  square's west edge, along y; 1 its south edge, along x); for stairs and roofs, which way they rise (Turn 0-3:
	 *  +x, +y, -x, -y) */
	struct Slot
	{
		int32_t X = 0, Y = 0, Level = 0;
		int32_t Side = 0;	// (edge pieces)
		int32_t Turn = 0;	// (stairs, roofs)
		int32_t Part = 0;	// (a half-wide wall: which half of its edge, 0 its -x or -y end, 1 the other)
		bool operator==(const Slot& O) const { return X == O.X && Y == O.Y && Level == O.Level && Side == O.Side && Turn == O.Turn && Part == O.Part; }
	};

	struct Piece
	{
		Kind What = Kind::Foundation;
		Material Of = Material::Wood;
		Slot At;
		double Support = 0.0;	// how firmly it's held (0-1; the ground 1)
		bool bOpen = false;		// (a door)
		uint8_t Size = 0;		// (a plain wall: SizeFlag)
	};

	/** What a face looks like, for the host's texture: plain (its colour only), planks, brick, concrete, roof tiles */
	enum class Finish : uint8_t { Plain, Planks, Brick, Concrete, Tiles, Glass };	// (glass: the host draws it see-through)

	/** A box (the structure's frame, m): where it is, its half sizes, turned about z (Yaw) and its +x end raised (Tilt) */
	struct Box
	{
		geobody::Vec3 Centre, Half;
		double Yaw = 0.0, Tilt = 0.0;	// rad
		float R = 1.0f, G = 1.0f, B = 1.0f;	// its colour (linear)
		bool bGlass = false;	// (a window's pane: glass, not the piece's material)
		bool bSteel = false;	// (a steel railing's: steel)
		Finish Look = Finish::Plain;	// (which texture the host lays on it)
		geobody::Quat Turn() const;
	};

	/** What the host draws: triangles, counter-clockwise seen from outside, a colour and a finish at each corner */
	struct Mesh
	{
		std::vector<geobody::Vec3> Positions, Normals;
		std::vector<float> Colours;	// r, g, b, finish (Finish's number) per corner
		std::vector<int32_t> Indices;
	};

	/** Why a piece can't go where it was asked */
	enum class Refusal : uint8_t { None, Taken, Unsupported, NotOnGround, WrongLevel, NeedsDoorway };
	const char* Why(Refusal R);

	class Structure
	{
	public:
		/** A piece put there: fine, or why not (Ground: for a foundation, the lowest and highest ground under its
		 *  square, as heights in this frame: it must stand on it, the ground no higher than its top and no lower
		 *  than its bottom) */
		Refusal CanPlace(Kind What, const Slot& At, double GroundLow = 0.0, double GroundHigh = 0.0, uint8_t Size = 0) const;
		Refusal Place(Kind What, Material Of, const Slot& At, double GroundLow = 0.0, double GroundHigh = 0.0, uint8_t Size = 0);
		/** How firmly a piece would be held there (0: not at all), as the support rule has it */
		double SupportIfPlaced(Kind What, const Slot& At, uint8_t Size = 0) const;

		/** The piece at a slot (-1 none; a half-wide wall at its half), and taking it out: what no longer stands comes down
		 *  with it (how many in all) */
		int32_t Find(Kind What, const Slot& At) const;
		int32_t Remove(int32_t Index);

		/** The slot nearest a point (this frame) for a kind of piece: the square under or beside it, the edge it's
		 *  nearest (and the half of it), the level it's at; Facing (x, y) the way the builder looks, for which way stairs
		 *  and roofs rise */
		Slot Snap(Kind What, const geobody::Vec3& Point, double FacingX, double FacingY) const;

		/** The piece nearest a point (within half a square), for taking out; -1 none */
		int32_t PieceAt(const geobody::Vec3& Point) const;

		const std::vector<Piece>& Pieces() const { return All; }
		/** A door opened or shut (whether it's open now; false if it isn't a door) */
		bool Toggle(int32_t Index);
		void SetPieces(const std::vector<Piece>& Pieces);	// (loaded: the support worked out again)

		/** What it's made of, and its weight (GeoBody: each piece's parts) */
		std::vector<geobody::Part> Parts() const;

		/** Its boxes (to collide with: stairs a ramp), and its look (stairs as steps) */
		std::vector<Box> Solids() const;
		Mesh Look() const;

		/** One piece's boxes and look on its own (the builder's preview) */
		static std::vector<Box> PieceSolids(const Piece& P, bool bForLook);
		static Mesh PieceLook(const Piece& P);

	private:
		void Settle();	// (the support worked out from the ground up)
		std::vector<Piece> All;
	};

	/** The boxes as triangles (counter-clockwise from outside) */
	void AddBoxes(Mesh& Out, const std::vector<Box>& Boxes);
}
