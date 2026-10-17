// Geodrive Core - GPL (see Core/README.md)
//
// Hunting on a game farm: its animals (real species, their real sizes and speeds), what the law allows to be hunted
// there (the Western Cape's annual hunting notice for the farm's district; the bow-hunting guideline's minimum arrow
// weights and energies), the rifles' and the bow's real loads (their flight through the air: drop, speed and energy
// lost), where a shot lands on an animal and what it does, and the animals' days (walking to somewhere, grazing or
// browsing there, drinking when thirsty, looking up now and then; lions hunting).
//
// Engine-free, as the rest of Core. Frames: the farm's own, in metres (x east, y north, z up); an animal's own (x
// forward, y left, z up, its feet at 0).

#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace geohunt
{
	struct Vec2
	{
		double X = 0.0, Y = 0.0;
		Vec2() = default;
		Vec2(double InX, double InY) : X(InX), Y(InY) {}
		Vec2 operator+(const Vec2& O) const { return { X + O.X, Y + O.Y }; }
		Vec2 operator-(const Vec2& O) const { return { X - O.X, Y - O.Y }; }
		Vec2 operator*(double S) const { return { X * S, Y * S }; }
		double Dot(const Vec2& O) const { return X * O.X + Y * O.Y; }
		double Length() const;
		Vec2 Normal() const;
	};

	struct Vec3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		Vec3() = default;
		Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
		Vec3 operator+(const Vec3& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
		Vec3 operator-(const Vec3& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
		Vec3 operator*(double S) const { return { X * S, Y * S, Z * S }; }
		double Dot(const Vec3& O) const { return X * O.X + Y * O.Y + Z * O.Z; }
		double Length() const;
		Vec3 Normal() const;
	};

	// --- the animals ---

	enum class Diet : uint8_t { Grazer, Browser, Mixed, Carnivore };

	/** The bow-hunting guideline's classes: small under 20 kg, medium 20-100 kg, large 100 kg and over (adult, live) */
	enum class SizeClass : uint8_t { Small, Medium, Large };

	/** A coat's pattern, for the host to paint: plain, paler underneath, stripes (zebras), patches (giraffes), a face
	 *  mask and black side band (gemsbok), thin white stripes down its sides (kudu) */
	enum class Pattern : uint8_t { Plain, Countershade, Stripes, Patches, Mask, Pinstripes };

	/**
	 *  How a creature's body is built (Body: its parts' sizes and places), as a file can give it: everything about its
	 *  looks that its size doesn't say. A new creature is a new set of these, no code
	 */
	struct BodyShape
	{
		enum class Frame : uint8_t { FourLegs, Upright };	// (an antelope's; a person's: two legs, two arms, upright)
		enum class Horns : uint8_t { None, Pair, Nose };	// (a pair on the head, swept as Bend says; a rhino's on its nose)
		Frame Build = Frame::FourLegs;
		bool bArmsForward = false;	// (upright: its arms held out in front, not hanging)
		enum class Mane : uint8_t { None, Lion, Crest };	// (round the neck and head; a crest along the neck)
		double HeadUp = 1.08;		// how high it carries its head, looking: over its shoulder by this much of its shoulder height
		double TorsoDepth = -1.0;	// the body's half depth (of its shoulder height): -1, by its mass (0.17; 0.22 over 800 kg)
		double BackSlope = 0.0;		// its rump lower than its shoulders by this much of its shoulder height (a giraffe's)
		double Ears = 1.0;			// their size
		Horns HornKind = Horns::None;
		double HornLength = -1.0;	// m (-1: by its mass)
		double Bend[4] = { 0.35, 0.2, 0.5, 0.1 };	// each horn in two lengths: back from upright, out to the side (rad), then again
		Mane ManeKind = Mane::None;
		bool bLongTail = false;		// (a tuft at the end: a lion's, a zebra's, a giraffe's)
		Pattern Coat = Pattern::Countershade;
	};

	/**
	 *  How a creature spends its day, as a file can give it: building blocks, each with its numbers (times in s,
	 *  distances in m). By its diet unless a file says otherwise (ActsFor)
	 */
	struct Behaviour
	{
		// eating: at a bush (browsing) this share of its meals, else at the grass; or lying at rest instead (a lion)
		double Browsing = 0.0;
		bool bRests = false;
		double MealMin = 60.0, MealMax = 240.0;		// at a patch
		double ThirstMin = 400.0, ThirstMax = 800.0;	// thirsty again after (0: never drinks)
		// a shot: heard within, wary (heads up, looking that way) a while; its herd's run from it
		double Hears = 400.0;
		double WaryMin = 15.0, WaryMax = 40.0;
		double FleeMin = 8.0, FleeMax = 15.0;
		bool bPrey = true;	// what hunters hunt
		bool bFlees = true;	// runs from a shot (its herd with it)
		int WoundsToDrop = 0;	// this many wounds that don't kill (outside its vital zones) drop it, dead (0: never)
		// the zones a wound there kills from (bits: 1 << Zone; others only wound): all of them, unless a file says
		uint32_t Vital = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
		// going for people (Herds::SetPeople): one noticed within reach is come after at this much of its run, caught
		// within Reach (Herds::TakeCaught), given up on past GiveUp; a while after a catch before the next
		bool bGoesForPeople = false;
		double NoticeWithin = 60.0;
		double PursueSpeed = 1.0;
		double Reach = 1.0;
		double GiveUp = 150.0;
		// hunting (a lion's): every so often the nearest prey within reach and up to a mass; stalked at a walk, then
		// the rush from close (the prey bolts), faster than its run at first, tiring; the herd feeds on a kill
		bool bHunts = false;
		double HuntMin = 480.0, HuntMax = 900.0;
		double HuntWithin = 800.0;
		double PreyUpTo = 700.0;	// kg
		double RushFrom = 40.0;
		double RushFor = 20.0;		// (its first 8 s the fastest)
		double RushSpeed = 1.25;	// of its run
		double FeedMin = 500.0, FeedMax = 700.0;
	};

	struct Species
	{
		std::string Id;
		std::string Name;
		double Mass;		// kg: an adult's (the sexes' mean)
		double Shoulder;	// m: its height at the shoulder
		double Length;		// m: head and body
		double Walk, Run;	// m/s: walking, and running flat out
		int HerdMin, HerdMax;
		Diet Eats;
		float R, G, B;		// its coat (linear)
		bool bHorns;
		bool bProtected;	// not in the hunting notice at all (protected): never to be hunted
		// its open season in the farm's district (Mossel Bay; CapeNature's notice for 2025): months from-to (1-12, both
		// in); 0, 0: closed there (in the notice, but not for this district); and how many a day
		int SeasonFrom, SeasonTo;
		int BagPerDay;
		BodyShape Shape = {};
		Behaviour Acts = {};
	};

	/** How a creature of a diet behaves, given nothing else: a grazer at the grass, a browser at bushes, a mixed feeder
	 *  at both (40% bushes); a carnivore rests between hunts, hunts, and isn't prey */
	Behaviour ActsFor(Diet D);

	/** Every creature there is: none built in, each from a pack's file (CreatureFile.h) */
	const std::vector<Species>& AllSpecies();
	int FindSpecies(const std::string& Id);	// (-1: none)
	/** A creature added (from a pack's file), or the one with its id replaced: its index. Before any herds start (their
	 *  animals hold an index into these) */
	int AddSpecies(const Species& S);
	SizeClass SizeOf(const Species& S);

	// --- the law ---

	enum class Verdict : uint8_t { Legal, Protected, Closed, OutOfSeason, BagFull, ArrowTooLight, BowTooWeak, Automatic };
	const char* Why(Verdict V);

	/** Whether shooting this animal now is legal: Month 1-12; Taken: how many of the species already today; a bow's
	 *  arrow (grains) and its energy at the animal (ft-lb), mechanical broadheads needing 5% more */
	Verdict Judge(const Species& S, int Month, int Taken, bool bBow = false, double ArrowGrains = 0.0, double EnergyFtLb = 0.0, bool bMechanical = false,
		bool bAutomatic = false);

	// --- the guns and the bow ---

	struct Load
	{
		std::string Id;
		std::string Name;
		double Grains;		// the bullet's (or arrow's) weight
		double BC;			// a bullet's G1 ballistic coefficient (lb/in2); an arrow's: 0 (its drag's own)
		double Muzzle;		// m/s
		double ZeroAt;		// m: where the sights meet the bullet's path
		double SightHeight;	// m: the sight over the bore
		int Magazine;		// rounds a load (an arrow at a time)
		bool bArrow;
		// how it's worked and carried (the hands', not the flight's): bolt (a round a pull, the bolt worked after), semi
		// (a round a pull), auto (rounds as fast as Rate a minute while held); rounds carried, a reload's time; the
		// sight (scope, or plain sights); how far the muzzle jumps a shot (1: a hunting rifle's); the model in the hands
		std::string Action = "bolt";
		double Rate = 0.0;
		int Rounds = 20;
		double ReloadTime = 3.0;
		std::string Sight = "scope";
		double Kick = 1.0;
		std::string Model = "rifle";
		std::string Look;	// (how it looks, as its file has it: JSON text, the host's to read; Core keeps it as it is)
	};
	/** Every weapon there is: none built in, each from a pack's file (CreatureFile.h) */
	const std::vector<Load>& AllLoads();
	int FindLoad(const std::string& Id);
	/** A weapon added, or the one with its id replaced: its index */
	int AddLoad(const Load& L);

	/** Kinetic energy (ft-lb) of Grains at a speed (m/s), as hunters reckon it */
	double EnergyFtLb(double Grains, double Speed);

	struct ShotPoint
	{
		Vec3 At;		// the farm's frame (m)
		double Speed;	// m/s
		double Time;	// s since it left the muzzle
	};

	/** A shot's flight from the sights at Eye, looking along Aim: the bore under the sight and tilted up so the path
	 *  meets the line of sight at the load's zero; through still air at sea level, under gravity, every Step s until
	 *  MaxTime or the ground (GroundZ under a point: below it ends the flight) */
	template <typename GroundFn>
	std::vector<ShotPoint> Fly(const Load& L, const Vec3& Eye, const Vec3& Aim, double MaxTime, double Step, GroundFn&& GroundZ);
	/** The bore's tilt over the line of sight (rad) so the path meets it at the zero (a level shot) */
	double ZeroAngle(const Load& L);
	/** One step of a load's flight (velocity and position, Step s on) */
	void Advance(const Load& L, Vec3& Position, Vec3& Velocity, double Step);

	// --- an animal's body: its parts (drawn, and hit) ---

	enum class PartKind : uint8_t { Torso, Rump, Neck, Head, Muzzle, Ear, Leg, Shin, Hoof, Horn, Tail, Mane };	// (the torso: the chest; a leg: its upper part)

	enum class Zone : uint8_t { None, Brain, Spine, HeartLung, Gut, Neck, Leg };
	const char* Name(Zone Z);

	struct Pose
	{
		double HeadDown = 0.0;	// 0 head up (looking), 1 at the grass (-0.3: up at a bush)
		double Gait = 0.0;		// the legs' swing (rad)
		double Lying = 0.0;		// 0 standing, 1 down on its side (dead)
		double Pace = 0.0;		// 0 walking, 1 galloping (the legs' timing, the body's bob)
		double Stepping = 0.0;	// 0 standing still (its legs straight), 1 on the move
		double Rest = 0.0;		// 1 lying at rest on its belly (a lion), its legs folded
		double Turn = 0.0;		// rad: its head turned to its left (+) or right (-), looking round
		double Tail = 0.0;		// its tail swished to its left (+) or right (-)
		double Flick = 0.0;		// an ear flicked: its left (+) or right (-)
	};

	struct BodyPart
	{
		PartKind Kind;
		Vec3 Centre;	// the animal's frame (m)
		Vec3 Half;		// half sizes along the part's own axes (an ellipsoid's: every part's rounded; a horn's a cone up its z)
		double Pitch;	// rad: about y (its +x end up)
		double Roll;	// rad: about x (lying down)
		double Yaw = 0.0;	// rad: then about z (the head turned)
	};
	/** Its parts as they are in a pose: the chest and rump, the legs (upper, lower, hooves), the neck, head, muzzle and
	 *  ears, horns if it has them, a mane, the tail */
	std::vector<BodyPart> Body(const Species& S, const Pose& P);
	Pattern PatternOf(const Species& S);

	/** Where a shot through the animal's frame (a segment) first meets it, and the zone it's in; false: a miss */
	bool HitBody(const Species& S, const Pose& P, const Vec3& From, const Vec3& To, Vec3& Where, Zone& Struck);

	/** What a hit does: down at once (brain, spine, neck), or running on and dying after a while (heart and lungs:
	 *  seconds; gut: many minutes), or wounded and living (a leg; too little energy) */
	struct Effect
	{
		bool bDown = false;
		double DiesAfter = -1.0;	// s (-1: it lives)
		bool bWounded = false;
	};
	Effect Outcome(const Species& S, Zone Z, double EnergyFtLb, std::mt19937& Random);

	// --- the animals' days ---

	enum class Doing : uint8_t { Walk, Graze, Browse, Drink, Look, Flee, Stalk, Chase, Feed, Dead, Pursue };
	const char* Name(Doing D);

	struct Animal
	{
		int Species = 0;
		int Herd = 0;
		Vec2 At;				// the farm's frame
		double Heading = 0.0;	// rad, from east anticlockwise (towards north)
		double Speed = 0.0;
		Doing What = Doing::Walk;
		double Until = 0.0;		// when what it's doing ends (s)
		Vec2 Target;
		Pose Now;
		double Thirst = 0.0;	// 1: off to drink
		double NextLook = 0.0;	// (grazing: when it looks up next, and till when)
		double LookEnd = 0.0;
		bool bToDrink = false;	// (walking to water)
		int Prey = -1;			// (a lion's)
		int Person = -1;		// (going for people: whom)
		double PursueAt = 0.0;	// (and not before, after a catch)
		double BumpedAt = -10.0;	// (a car's last hit: once a hit, however long it's pushed)
		int Wounds = 0;			// (wounds that didn't kill it)
		double HuntAt = 0.0;	// (a lion: when it hunts next)
		double DiesAt = -1.0;	// (hit: when it falls)
		bool bWounded = false;
		bool bShot = false;		// (killed by the hunter, not a lion)
		// (alive between times: a few steps on while grazing; the tail swished, an ear flicked, the head turned)
		double NextStep = 0.0, StepUntil = 0.0;
		Vec2 StepTo;
		double NextSwish = 0.0, SwishFrom = -10.0, NextFlick = 0.0, FlickFrom = -10.0, FlickSide = 1.0, NextTurn = 0.0, TurnWanted = 0.0;
		// (wary: a shot heard or run from: heads up, looking that way, till then)
		double WaryUntil = 0.0;
		Vec2 Threat;
		bool bRegroup = false;	// (scattered: walking back to the others till it's among them)
	};

	/** A farm as the animals see it: its outline and its water (the farm's frame) */
	struct Range
	{
		std::vector<Vec2> Outline;
		std::vector<Vec2> Water;
		// what's in the way inside it (a town's buildings): a grid of Cell m squares from GridMin, Columns across, a
		// byte each (1: blocked); empty, nothing (a game farm)
		std::vector<uint8_t> Blocked;
		Vec2 GridMin;
		double Cell = 2.0;
		int Columns = 0, Rows = 0;
		/** Inside the outline, and not in anything in the way */
		bool Inside(const Vec2& P) const;
		/** Polygon (the farm's frame) marked blocked on the grid (made to cover the outline on the first) */
		void Block(const std::vector<Vec2>& Polygon);
	};

	/** A car among them (the farm's frame): where its middle is, which way it points, how fast it's going, its half
	 *  length and width (m), its mass (kg) */
	struct Car
	{
		Vec2 At;
		Vec2 Forward = Vec2(1.0, 0.0);
		Vec2 Velocity;
		double HalfLength = 2.6, HalfWidth = 0.95;
		double Mass = 1800.0;
	};

	/** A car's hit: on whom, how hard (the closing speed, m/s), which way (from the car to it), what came of it, and
	 *  the speed the car lost (m/s, along Way) */
	struct CarHit
	{
		int Index = -1;
		int CarIndex = 0;
		double Speed = 0.0;
		Vec2 Way;
		bool bKilled = false;
		bool bWounded = false;
		double CarLoses = 0.0;
	};

	/** Where an area's creatures are: spread over it (a game farm's), or a pool kept round the people in it (a town's
	 *  zombies): one farther than LeavePast from everyone, or dead for DeadFor s, is put back among them, Nearest to
	 *  Farthest m from one of them, on open ground */
	struct Spawning
	{
		bool bAroundPeople = false;
		double Nearest = 60.0, Farthest = 150.0;
		double LeavePast = 250.0;
		double DeadFor = 30.0;
	};

	/** How fast a car must hit a creature to kill it (m/s): ~20 km/h a duiker, an impala, a person; ~40 an eland;
	 *  ~90 a rhino. Half that or more wounds it */
	double LethalCarSpeed(const Species& S);

	class Herds
	{
	public:
		/** The farm stocked: Stock (species, how many) as herds of their sizes, scattered over it */
		void Start(const Range& Farm, const std::vector<std::pair<int, int>>& Stock, uint32_t Seed);
		/** Everyone's next Dt s */
		void Step(double Dt);
		/** A hunter's shot: Index hit (Hit), or a near miss at it; it and its herd run from From (and, the danger past,
		 *  come back together, wary); the other herds within earshot look up towards it. What the hit did (one wound too
		 *  many, past its WoundsToDrop: down) */
		Effect Shot(int Index, const Vec2& From, const Effect* Hit);
		/** A shot heard (nothing hit near them): herds within earshot look up towards it, wary a while */
		void Heard(const Vec2& From);
		/** Where the people are now (the farm's frame), for creatures that go for them; Catchable: each on foot (one in a
		 *  car is come for, not caught) */
		void SetPeople(const std::vector<Vec2>& At, const std::vector<bool>& Catchable = {}) { People = At; PeopleCatchable = Catchable; }
		/** Whom they've caught since last asked (indices into SetPeople's), oldest first */
		std::vector<int> TakeCaught();
		/** The pool's creatures within Within m of a spot put back farther off round the people (someone getting up
		 *  there, not to be set on at once) */
		void PutBack(const Vec2& Near, double Within);
		/** How they're kept (Spawning): spread, or a pool round the people */
		void SetSpawning(const Spawning& How) { Spawn = How; }
		/** The cars among them now (each step's: pushing them aside, hitting them; spooking them as they come) */
		void SetCars(const std::vector<Car>& Now) { Cars = Now; }
		/** The cars' hits since last asked, oldest first */
		std::vector<CarHit> TakeHits();

		/** Shared in a multiplayer game: the host's animals as they are now, ~21 bytes each (where, which way, how fast,
		 *  what doing, their pose), for the others to show (Unpack); false if it doesn't fit these herds */
		std::vector<uint8_t> Pack() const;
		bool Unpack(const std::vector<uint8_t>& Bytes);
		/** The others' copy between the host's: each carried on the way it's going, its legs swinging; nothing decided */
		void Follow(double Dt);

		const std::vector<Animal>& Animals() const { return All; }
		double Time() const { return Clock; }
		/** What happened (a lion's kill, an animal falling), oldest first; taken once read */
		std::vector<std::string> TakeNews();

	private:
		Range Farm;
		std::vector<Animal> All;
		std::vector<std::vector<int>> Members;	// herd -> its animals (the first leads)
		std::mt19937 Random;
		double Clock = 0.0;
		std::vector<std::string> News;
		std::vector<Vec2> People;
		std::vector<bool> PeopleCatchable;
		std::vector<Car> Cars;
		std::vector<CarHit> Hits;
		Spawning Spawn;
		double NextRecycle = 0.0;
		std::vector<int> Caught;

		double Uniform(double A, double B);
		Vec2 SomewhereInside(const Vec2& Near, double Within);
		bool ClearPath(const Vec2& A, const Vec2& B) const;
		void Decide(int Index);
		void Lead(int Herd, const Vec2& To, Doing Then);
		void Flee(int Index, const Vec2& From, double For);
		void Move(Animal& A, double Desired, const Vec2& Towards, double Dt);
		int Leader(int Herd) const;	// (its first still standing)
		Vec2 Middle(int Herd) const;	// (where its living animals are, on average)
		void Fidget(Animal& A, const Species& S, double HeadWanted, double Dt);
		void DriveCars();
		void Recycle();
		bool PutAmong(Animal& A);
	};

	// --- the template's body ---

	template <typename GroundFn>
	std::vector<ShotPoint> Fly(const Load& L, const Vec3& Eye, const Vec3& Aim, double MaxTime, double Step, GroundFn&& GroundZ)
	{
		std::vector<ShotPoint> Out;
		// the bore: the sight's height under the eye, tilted up by the zero angle in the aim's own vertical plane
		const Vec3 Forward = Aim.Normal();
		const Vec3 Flat = Vec3(Forward.X, Forward.Y, 0.0).Length() > 1e-6 ? Vec3(Forward.X, Forward.Y, 0.0).Normal() : Vec3(1.0, 0.0, 0.0);
		const Vec3 Up = (Vec3(0.0, 0.0, 1.0) - Forward * Forward.Z).Length() > 1e-6 ? (Vec3(0.0, 0.0, 1.0) - Forward * Forward.Z).Normal() : Flat * -1.0;
		const double Tilt = ZeroAngle(L);
		Vec3 Position = Eye - Up * L.SightHeight;
		Vec3 Velocity = (Forward * std::cos(Tilt) + Up * std::sin(Tilt)) * L.Muzzle;
		double Time = 0.0;
		Out.push_back({ Position, L.Muzzle, 0.0 });
		while (Time < MaxTime)
		{
			Advance(L, Position, Velocity, Step);
			Time += Step;
			Out.push_back({ Position, Velocity.Length(), Time });
			if (Position.Z < GroundZ(Position.X, Position.Y))
			{
				break;
			}
		}
		return Out;
	}
}
