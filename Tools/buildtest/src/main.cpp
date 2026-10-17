// Geodrive buildtest - AGPL-3.0 (see LICENSE)
//
// Checks Core/GeoBuild: builds a double-storey house piece by piece (2 x 2 squares: foundations, walls with a door and
// windows, a floor, stairs, walls again, a roof), checking what's held up and what isn't, what comes down when a piece
// is taken out, where pieces snap, and what the house weighs. Exits 1 if anything's off.

#include "GeoBuild/Build.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace geobuild;

namespace
{
	int Failed = 0;

	void Check(const char* What, bool bOk, const std::string& Detail = "")
	{
		Failed += bOk ? 0 : 1;
		std::printf("%s %s%s\n", bOk ? "  ok " : "  OFF", What, Detail.empty() ? "" : ("  (" + Detail + ")").c_str());
	}

	Slot At(int32_t X, int32_t Y, int32_t Level, int32_t Side = 0, int32_t Turn = 0)
	{
		Slot S;
		S.X = X;
		S.Y = Y;
		S.Level = Level;
		S.Side = Side;
		S.Turn = Turn;
		return S;
	}

	/** Walls round the 2 x 2 squares at a level (a door in the front, a window each side) */
	void Walls(Structure& House, int32_t Level, Material Of, bool bDoor)
	{
		for (int32_t i = 0; i < 2; ++i)
		{
			House.Place(i == 0 && bDoor ? Kind::Doorway : Kind::Wall, Of, At(i, 0, Level, 1));	// the south side
			House.Place(Kind::Wall, Of, At(i, 2, Level, 1));	// the north side
			House.Place(i == 1 ? Kind::Window : Kind::Wall, Of, At(0, i, Level, 0));	// the west side
			House.Place(i == 1 ? Kind::Window : Kind::Wall, Of, At(2, i, Level, 0));	// the east side
		}
	}
}

int main()
{
	std::printf("A double-storey house, 2 x 2 squares (6 x 6 m)\n");
	Structure House;
	Check("a wall with nothing under it can't go", House.CanPlace(Kind::Wall, At(0, 0, 0, 1)) == Refusal::Unsupported);
	Check("a foundation in the air can't go (the ground 4 m under it)", House.CanPlace(Kind::Foundation, At(0, 0, 0), -4.0, -3.9) == Refusal::NotOnGround);
	Check("a foundation buried (the ground over its top) can't go", House.CanPlace(Kind::Foundation, At(0, 0, 0), 0.2, 0.5) == Refusal::NotOnGround);
	for (int32_t X = 0; X < 2; ++X)
	{
		for (int32_t Y = 0; Y < 2; ++Y)
		{
			House.Place(Kind::Foundation, Material::Concrete, At(X, Y, 0), -0.6, -0.1);
		}
	}
	Check("four foundations on a gentle slope", House.Pieces().size() == 4);
	Check("a second foundation in the same square can't go", House.CanPlace(Kind::Foundation, At(0, 0, 0), -0.5, -0.1) == Refusal::Taken);
	Check("a floor at level 0 can't go (that's the foundation's)", House.CanPlace(Kind::Floor, At(0, 0, 0)) == Refusal::WrongLevel);
	Check("a first floor with no walls under it can't go", House.CanPlace(Kind::Floor, At(0, 0, 1)) == Refusal::Unsupported);
	Walls(House, 0, Material::Brick, true);
	Check("the ground floor's 8 walls (a door, two windows)", House.Pieces().size() == 12);
	for (int32_t X = 0; X < 2; ++X)
	{
		for (int32_t Y = 0; Y < 2; ++Y)
		{
			if (!(X == 1 && Y == 1))
			{
				House.Place(Kind::Floor, Material::Concrete, At(X, Y, 1));
			}
		}
	}
	Check("the stairs on the foundation, up to the gap left in the floor", House.Place(Kind::Stairs, Material::Wood, At(1, 1, 0, 0, 1)) == Refusal::None);
	Check("the first floor (three squares: the fourth the stairwell)", House.Pieces().size() == 16);
	Walls(House, 1, Material::Brick, false);
	Check("the upper storey's walls, on those below", House.Pieces().size() == 24);
	const int32_t Upper = House.Find(Kind::Wall, At(0, 2, 1, 1));
	Check("an upper wall on the first floor is held as firmly as the floor", Upper >= 0 && std::abs(House.Pieces()[size_t(Upper)].Support - 1.0) < 1e-9,
		Upper >= 0 ? std::to_string(House.Pieces()[size_t(Upper)].Support) : "");
	const int32_t OverStairs = House.Find(Kind::Window, At(2, 1, 1, 0));
	Check("the upper window by the stairwell (no floor beside it) is held by the wall under it, a little less firmly",
		OverStairs >= 0 && std::abs(House.Pieces()[size_t(OverStairs)].Support - 0.9) < 1e-9, OverStairs >= 0 ? std::to_string(House.Pieces()[size_t(OverStairs)].Support) : "");
	// a gable roof: two squares rising north from the south walls, two rising south from the north walls
	for (int32_t X = 0; X < 2; ++X)
	{
		House.Place(Kind::Roof, Material::Brick, At(X, 0, 2, 0, 1));
		House.Place(Kind::Roof, Material::Brick, At(X, 1, 2, 0, 3));
	}
	Check("the roof, on the upper walls", House.Pieces().size() == 28);

	std::printf("Doors\n");
	Check("a door in a plain wall can't go", House.CanPlace(Kind::Door, At(0, 2, 0, 1)) == Refusal::NeedsDoorway);
	Check("a door in the front doorway", House.Place(Kind::Door, Material::Brick, At(0, 0, 0, 1)) == Refusal::None);
	const int32_t Door = House.Find(Kind::Door, At(0, 0, 0, 1));
	Check("it's wood, whatever was chosen, and held by its doorway", Door >= 0 && House.Pieces()[size_t(Door)].Of == Material::Wood && House.Pieces()[size_t(Door)].Support > 0.99);
	const geobody::Vec3 Shut = Structure::PieceSolids(House.Pieces()[size_t(Door)], false)[0].Centre;
	Check("it opens", House.Toggle(Door));
	const geobody::Vec3 Swung = Structure::PieceSolids(House.Pieces()[size_t(Door)], false)[0].Centre;
	Check("open, it has swung out of the doorway (its middle 0.69 m from shut)", std::abs((Swung - Shut).Length() - 0.693) < 0.01, std::to_string((Swung - Shut).Length()));
	Check("and shuts", !House.Toggle(Door));
	const int32_t Doorway = House.Find(Kind::Doorway, At(0, 0, 0, 1));
	const size_t Count = House.Pieces().size();
	House.Remove(Doorway);
	Check("taking out its doorway takes the door (and the floor over it stays: other walls hold it)", House.Find(Kind::Door, At(0, 0, 0, 1)) < 0,
		std::to_string(Count - House.Pieces().size()) + " came out");
	House.Place(Kind::Doorway, Material::Brick, At(0, 0, 0, 1));

	std::printf("Reaching out\n");
	Check("a balcony's first square, over the front wall, is held fully", std::abs(House.SupportIfPlaced(Kind::Floor, At(0, -1, 1)) - 1.0) < 1e-9);
	int32_t Out = 0;
	while (House.Place(Kind::Floor, Material::Wood, At(0, -1 - Out, 1)) == Refusal::None && Out < 20)
	{
		++Out;
	}
	Check("each square further out held half as firmly: five squares out at 1/16, a sixth can't go", Out == 5 && std::abs(House.Pieces().back().Support - 0.0625) < 1e-9,
		std::to_string(Out) + " squares, the last at " + std::to_string(House.Pieces().back().Support));

	std::printf("Taking out\n");
	const int32_t Balcony = House.Find(Kind::Floor, At(0, -1, 1));
	const int32_t Down = House.Remove(Balcony);
	Check("taking out the balcony's first square brings the rest of it down", Down == 5, std::to_string(Down) + " came down");
	const size_t Before = House.Pieces().size();
	const int32_t Corner = House.Find(Kind::Foundation, At(1, 1, 0));
	const int32_t Fell = House.Remove(Corner);
	Check("taking out a corner foundation brings down its two outer walls, the walls over them and the stairs (the roof holds from its neighbour)", Fell == 6,
		std::to_string(Fell) + " came down of " + std::to_string(Before));

	std::printf("Snapping\n");
	const Slot Wall = House.Snap(Kind::Wall, geobody::Vec3(0.2, 1.4, 0.0), 1.0, 0.0);
	Check("aiming at the floor by its west edge: the west edge's wall", Wall.X == 0 && Wall.Y == 0 && Wall.Side == 0 && Wall.Level == 0);
	const Slot East = House.Snap(Kind::Wall, geobody::Vec3(5.8, 1.4, 3.0), 1.0, 0.0);
	Check("aiming at the first floor by the east edge: the east edge's, upstairs", East.X == 2 && East.Y == 0 && East.Side == 0 && East.Level == 1);
	const Slot Floor = House.Snap(Kind::Floor, geobody::Vec3(1.5, 0.11, 2.4), 0.0, 1.0);
	Check("aiming high up a wall: the floor over that storey", Floor.Level == 1 && Floor.X == 0 && Floor.Y == 0);
	const Slot Stairs = House.Snap(Kind::Stairs, geobody::Vec3(4.5, 4.5, 0.0), 0.0, -1.0);
	Check("stairs rise the way the builder looks (south here)", Stairs.Turn == 3);

	std::printf("What it weighs\n");
	double Mass = 0.0;
	for (const geobody::Part& P : House.Parts())
	{
		Mass += P.GetMass();
	}
	std::printf("        %zu pieces, %.1f t (concrete foundations' slabs, double brick walls, concrete floor, tiled roof)\n", House.Pieces().size(), Mass / 1000.0);
	Check("a small brick house weighs tens of tonnes", Mass > 40000.0 && Mass < 150000.0);
	const Mesh Look = House.Look();
	Check("it has a look (triangles) and solids", !Look.Indices.empty() && !House.Solids().empty(), std::to_string(Look.Indices.size() / 3) + " triangles");

	std::printf("A veranda: pillars, a half wall, a railing, a glass wall\n");
	{
		Structure Veranda;
		for (int32_t X = 0; X < 2; ++X)
		{
			Veranda.Place(Kind::Foundation, Material::Concrete, At(X, 0, 0), -0.5, -0.1);
		}
		Check("a pillar in the air (no floor at its corner) can't go", Veranda.CanPlace(Kind::Pillar, At(5, 5, 0)) == Refusal::Unsupported);
		for (const int32_t X : { 0, 1, 2 })
		{
			for (const int32_t Y : { 0, 1 })
			{
				Veranda.Place(Kind::Pillar, Material::Wood, At(X, Y, 0));
			}
		}
		Check("six pillars at the foundations' corners", Veranda.Pieces().size() == 8);
		Check("a second pillar on the same corner can't go", Veranda.CanPlace(Kind::Pillar, At(1, 1, 0)) == Refusal::Taken);
		Check("the veranda's roof (two floors at level 1) on the pillars alone", Veranda.Place(Kind::Floor, Material::Wood, At(0, 0, 1)) == Refusal::None
			&& Veranda.Place(Kind::Floor, Material::Wood, At(1, 0, 1)) == Refusal::None);
		Check("a half wall and a railing on the foundations' edges", Veranda.Place(Kind::HalfWall, Material::Brick, At(0, 0, 0, 1)) == Refusal::None
			&& Veranda.Place(Kind::Railing, Material::Concrete, At(1, 0, 0, 1)) == Refusal::None);
		Check("a railing where the half wall is can't go (one to an edge)", Veranda.CanPlace(Kind::Railing, At(0, 0, 0, 1)) == Refusal::Taken);
		Check("a glass wall on the west edge", Veranda.Place(Kind::GlassWall, Material::Wood, At(0, 0, 0, 0)) == Refusal::None);
		bool bSteel = false;
		for (const Box& B : Structure::PieceSolids(Veranda.Pieces()[size_t(Veranda.Find(Kind::Railing, At(1, 0, 0, 1)))], true))
		{
			bSteel = bSteel || B.bSteel;
		}
		Check("a railing chosen in concrete is steel", bSteel);
		int32_t Panes = 0;
		for (const Box& B : Structure::PieceSolids(Veranda.Pieces()[size_t(Veranda.Find(Kind::GlassWall, At(0, 0, 0, 0)))], true))
		{
			Panes += B.Look == Finish::Glass ? 1 : 0;
		}
		Check("the glass wall's pane is glass to the host", Panes == 1);

		// what a half wall and a railing hold: nothing
		Structure Bare;
		Bare.Place(Kind::Foundation, Material::Concrete, At(0, 0, 0), -0.5, -0.1);
		Bare.Place(Kind::HalfWall, Material::Brick, At(0, 0, 0, 1));
		Bare.Place(Kind::Railing, Material::Wood, At(0, 0, 0, 0));
		Check("a half wall and a railing hold no floor over them", Bare.CanPlace(Kind::Floor, At(0, 0, 1)) == Refusal::Unsupported);
		Bare.Place(Kind::GlassWall, Material::Wood, At(1, 0, 0, 0));
		Check("a glass wall does", Bare.CanPlace(Kind::Floor, At(0, 0, 1)) == Refusal::None);

		// the pillars taken out one by one: the roof stays while any of its corners has one, then comes down (the glass
		// wall taken out first: it holds the roof too)
		Veranda.Remove(Veranda.Find(Kind::GlassWall, At(0, 0, 0, 0)));
		int32_t Down = 0;
		for (const int32_t X : { 0, 1, 2 })
		{
			for (const int32_t Y : { 0, 1 })
			{
				Down += Veranda.Remove(Veranda.Find(Kind::Pillar, At(X, Y, 0))) - 1;
			}
		}
		Check("taking out the pillars brings the roof down with the last that held it", Down == 2 && Veranda.Find(Kind::Floor, At(0, 0, 1)) < 0,
			std::to_string(Down) + " came down with them");
		const Slot PillarCorner = Veranda.Snap(Kind::Pillar, geobody::Vec3(2.8, 0.4, 0.5), 1.0, 0.0);
		Check("aiming near a corner: that corner's pillar", PillarCorner.X == 1 && PillarCorner.Y == 0 && PillarCorner.Level == 0);
	}

	std::printf("Wall sizes: half high, half wide, thin\n");
	{
		Structure Room;
		Room.Place(Kind::Foundation, Material::Concrete, At(0, 0, 0), -0.5, -0.1);
		Slot Left = At(0, 0, 0, 1), Right = At(0, 0, 0, 1);
		Right.Part = 1;
		Check("two half-wide walls share an edge, one brick, one wood", Room.Place(Kind::Wall, Material::Brick, Left, 0.0, 0.0, HalfWide) == Refusal::None
			&& Room.Place(Kind::Wall, Material::Wood, Right, 0.0, 0.0, HalfWide) == Refusal::None);
		Check("a third on that edge can't go (full or half)", Room.CanPlace(Kind::Wall, Left, 0.0, 0.0, HalfWide) == Refusal::Taken
			&& Room.CanPlace(Kind::Wall, Left) == Refusal::Taken);
		const std::vector<Box> Half = Structure::PieceSolids(Room.Pieces()[size_t(Room.Find(Kind::Wall, Right))], true);
		Check("a half-wide wall is half the edge long, on its half", Half.size() == 1 && std::abs(Half[0].Half.X * 2.0 - (3.0 + 0.15) * 0.5) < 1e-9
			&& Half[0].Centre.X > 1.5, std::to_string(Half[0].Half.X * 2.0) + " m long");
		Check("a floor over the two halves stands on them", Room.CanPlace(Kind::Floor, At(0, 0, 1)) == Refusal::None);

		Structure Low;
		Low.Place(Kind::Foundation, Material::Concrete, At(0, 0, 0), -0.5, -0.1);
		Check("a half-high wall goes on a foundation", Low.Place(Kind::Wall, Material::Brick, At(0, 0, 0, 1), 0.0, 0.0, HalfHigh) == Refusal::None);
		const std::vector<Box> Short = Structure::PieceSolids(Low.Pieces().back(), true);
		Check("it's 1.5 m high", Short.size() == 1 && std::abs(Short[0].Half.Z * 2.0 - 1.5) < 1e-9);
		Check("and holds no floor over it", Low.CanPlace(Kind::Floor, At(0, 0, 1)) == Refusal::Unsupported);
		Low.Place(Kind::Wall, Material::Brick, At(0, 0, 0, 0), 0.0, 0.0, Thin);
		const std::vector<Box> Partition = Structure::PieceSolids(Low.Pieces().back(), true);
		Check("a thin wall is half as thick, and holds a floor", Partition.size() == 1 && std::abs(Partition[0].Half.X * 2.0 - 0.11) < 1e-9
			&& Low.CanPlace(Kind::Floor, At(0, 0, 1)) == Refusal::None);
		Check("sizes are for plain walls only (a window stays whole)", Low.Place(Kind::Window, Material::Brick, At(1, 0, 0, 0), 0.0, 0.0, HalfHigh) == Refusal::None
			&& Low.Pieces().back().Size == 0);
		const Slot Aimed = Low.Snap(Kind::Wall, geobody::Vec3(2.4, 0.2, 1.0), 0.0, -1.0);
		Check("aiming at the south edge's east end: its second half", Aimed.Side == 1 && Aimed.Part == 1 && Aimed.X == 0);
	}

	std::printf(Failed == 0 ? "All right.\n" : "%d off.\n", Failed);
	return Failed == 0 ? 0 : 1;
}
