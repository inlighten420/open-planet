// Geodrive hunttest - AGPL-3.0 (see LICENSE)
//
// Checks Core/GeoHunt: the law (the Western Cape's 2025 hunting notice for the Mossel Bay district, and the bow-hunting
// guideline), the loads' flight against their published figures, where a shot lands on an animal, and half an hour of
// the animals' days on a small farm (they keep inside, graze, drink, look up; a lion hunts). Exits 1 if anything's off.
// The animals and weapons are the hunting pack's (Godot/packs/hunting), read as the game reads them; or a pack folder
// given: hunttest <pack folder>.

#include "GeoHunt/CreatureFile.h"
#include "GeoGame/Rules.h"
#include "GeoHunt/Hunt.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace geohunt;

namespace
{
	int Failed = 0;

	void Check(const char* What, bool bOk, const std::string& Detail = "")
	{
		Failed += bOk ? 0 : 1;
		std::printf("%s %s%s\n", bOk ? "  ok " : "  OFF", What, Detail.empty() ? "" : ("  (" + Detail + ")").c_str());
	}

	const Species& Kind(const char* Id) { return AllSpecies()[size_t(FindSpecies(Id))]; }

	std::string Num(double V, const char* Format = "%.1f")
	{
		char Buffer[64];
		std::snprintf(Buffer, sizeof(Buffer), Format, V);
		return Buffer;
	}

	std::string FileText(const std::filesystem::path& Path)
	{
		std::ifstream In(Path, std::ios::binary);
		std::stringstream Text;
		Text << In.rdbuf();
		return Text.str();
	}

	// A pack's folder of creature (or weapon) files, in name order as the game reads them: how many taken
	int ReadFolder(const std::filesystem::path& Folder, bool bCreatures, std::vector<std::string>& Errors)
	{
		std::vector<std::filesystem::path> Files;
		for (const auto& Entry : std::filesystem::directory_iterator(Folder))
		{
			if (Entry.path().extension() == ".json")
			{
				Files.push_back(Entry.path());
			}
		}
		std::sort(Files.begin(), Files.end());
		int Taken = 0;
		for (const auto& File : Files)
		{
			Taken += bCreatures ? ReadCreatures(FileText(File), Errors) : ReadLoads(FileText(File), Errors);
		}
		return Taken;
	}
}

int main(int ArgCount, char** Args)
{
	const std::filesystem::path PackFolder = ArgCount > 1 ? std::filesystem::path(Args[1])
		: std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." / "Godot" / "packs" / "hunting";
	std::printf("The hunting pack (%s)\n", PackFolder.lexically_normal().string().c_str());
	std::vector<std::string> Errors;
	const int Creatures = std::filesystem::exists(PackFolder / "creatures") ? ReadFolder(PackFolder / "creatures", true, Errors) : 0;
	const int WeaponFiles = std::filesystem::exists(PackFolder / "weapons") ? ReadFolder(PackFolder / "weapons", false, Errors) : 0;
	for (const std::string& E : Errors)
	{
		std::printf("        %s\n", E.c_str());
	}
	Check("its 19 creatures and 4 weapons read, every file all right", Creatures == 19 && WeaponFiles == 4 && Errors.empty(),
		std::to_string(Creatures) + " creatures, " + std::to_string(WeaponFiles) + " weapons");
	if (Creatures == 0 || WeaponFiles == 0)
	{
		std::printf("No pack: nothing more to check.\n");
		return 1;
	}
	std::printf("The animals\n");
	int Protected = 0;
	for (const Species& S : AllSpecies())
	{
		Protected += S.bProtected ? 1 : 0;
	}
	Check("19 species, 6 of them protected (lion, white rhino, buffalo, hippo, giraffe, Cape mountain zebra)", AllSpecies().size() == 19 && Protected == 6);
	Check("bow classes: duiker small, impala medium, eland large", SizeOf(Kind("grey_duiker")) == SizeClass::Small && SizeOf(Kind("impala")) == SizeClass::Medium
		&& SizeOf(Kind("eland")) == SizeClass::Large);

	std::printf("The law (Mossel Bay district, 2025)\n");
	Check("impala in October: legal", Judge(Kind("impala"), 10, 0) == Verdict::Legal);
	Check("the 11th impala of the day: the bag's full (10)", Judge(Kind("impala"), 10, 10) == Verdict::BagFull);
	Check("eland in October: out of season (July-August)", Judge(Kind("eland"), 10, 0) == Verdict::OutOfSeason);
	Check("eland in July: legal, one a day", Judge(Kind("eland"), 7, 0) == Verdict::Legal && Judge(Kind("eland"), 7, 1) == Verdict::BagFull);
	Check("grey duiker in June: legal (June-July here)", Judge(Kind("grey_duiker"), 6, 0) == Verdict::Legal && Judge(Kind("grey_duiker"), 8, 0) == Verdict::OutOfSeason);
	Check("kudu: closed in this district", Judge(Kind("kudu"), 6, 0) == Verdict::Closed);
	Check("an impala in season with an automatic: illegal (not a hunting arm)", Judge(Kind("impala"), 10, 0, false, 0.0, 0.0, false, true) == Verdict::Automatic);
	Check("lion, rhino, buffalo, giraffe: protected", Judge(Kind("lion"), 6, 0) == Verdict::Protected && Judge(Kind("white_rhino"), 6, 0) == Verdict::Protected
		&& Judge(Kind("buffalo"), 6, 0) == Verdict::Protected && Judge(Kind("giraffe"), 6, 0) == Verdict::Protected);
	Check("bow on impala: 425 gr at 70 ft-lb legal; a 350 gr arrow too light; 45 ft-lb too weak", Judge(Kind("impala"), 10, 0, true, 425.0, 70.0) == Verdict::Legal
		&& Judge(Kind("impala"), 10, 0, true, 350.0, 70.0) == Verdict::ArrowTooLight && Judge(Kind("impala"), 10, 0, true, 425.0, 45.0) == Verdict::BowTooWeak);
	Check("bow on zebra (large): 60 ft-lb; mechanical broadheads 63", Judge(Kind("zebra"), 10, 0, true, 425.0, 61.0) == Verdict::Legal
		&& Judge(Kind("zebra"), 10, 0, true, 425.0, 61.0, true) == Verdict::BowTooWeak);

	std::printf("The loads\n");
	auto Flat = [](double, double) { return -1000.0; };
	for (const Load& L : AllLoads())
	{
		const std::vector<ShotPoint> Path = Fly(L, Vec3(0.0, 0.0, 1.5), Vec3(1.0, 0.0, 0.0), L.bArrow ? 1.0 : 1.2, 0.0005, Flat);
		auto At = [&Path](double X)
		{
			for (size_t i = 1; i < Path.size(); ++i)
			{
				if (Path[i].At.X >= X)
				{
					const double T = (X - Path[i - 1].At.X) / (Path[i].At.X - Path[i - 1].At.X);
					return std::pair<double, double>(Path[i - 1].At.Z + (Path[i].At.Z - Path[i - 1].At.Z) * T - 1.5, Path[i - 1].Speed + (Path[i].Speed - Path[i - 1].Speed) * T);
				}
			}
			return std::pair<double, double>(-99.0, 0.0);
		};
		if (L.bArrow)
		{
			const auto A20 = At(20.0), A30 = At(30.0), A40 = At(40.0);
			std::printf("        %s: %.0f ft-lb, at 20 m %+.1f cm, 30 m %+.1f cm, 40 m %+.1f cm (%.0f fps)\n", L.Name.c_str(), EnergyFtLb(L.Grains, L.Muzzle),
				A20.first * 100.0, A30.first * 100.0, A40.first * 100.0, A40.second / 0.3048);
			// (an arrow takes ~0.47 s to 40 m: it falls ~1 m in that time, ~45 cm below a 20 m pin's line there)
			Check("the bow: ~77 ft-lb at the bow, on its 20 m pin, ~45 cm low at 40 m, still over 60 ft-lb there", std::abs(EnergyFtLb(L.Grains, L.Muzzle) - 76.7) < 1.0
				&& std::abs(A20.first) < 0.01 && A40.first < -0.38 && A40.first > -0.55 && EnergyFtLb(L.Grains, A40.second) > 60.0,
				Num(EnergyFtLb(L.Grains, A40.second), "%.0f ft-lb at 40 m"));
		}
		else
		{
			const auto A100 = At(100.0), A200 = At(200.0), A300 = At(300.0);
			std::printf("        %s: %.0f ft-lb at the muzzle; at 100 m %+.1f cm, %.0f m/s; 200 m %+.1f cm; 300 m %+.1f cm, %.0f ft-lb\n", L.Name.c_str(),
				EnergyFtLb(L.Grains, L.Muzzle), A100.first * 100.0, A100.second, A200.first * 100.0, A300.first * 100.0, EnergyFtLb(L.Grains, A300.second));
			if (L.Id == "308win")
			{
				// (published: 150 gr Nosler Partition, BC .387, 2,820 fps: ~2,560 fps at 100 yd, ~4" low at 200 yd with
				// a 100 yd zero, ~15" at 300 yd)
				Check(".308: 2,650 ft-lb at the muzzle; zeroed at 100 m; ~780 m/s there; 8-14 cm low at 200 m, 33-45 cm at 300", std::abs(EnergyFtLb(L.Grains, L.Muzzle) - 2650.0) < 30.0
					&& std::abs(A100.first) < 0.003 && A100.second > 765.0 && A100.second < 800.0 && A200.first < -0.08 && A200.first > -0.14 && A300.first < -0.33 && A300.first > -0.45);
			}
		}
	}

	std::printf("Where a shot lands (an impala broadside, a level shot from its left)\n");
	const Species& Impala = Kind("impala");
	Pose Standing;
	auto ShotAt = [&](double X, double Z, Zone& Struck)
	{
		Vec3 Where;
		return HitBody(Impala, Standing, Vec3(X, 20.0, Z), Vec3(X, -20.0, Z), Where, Struck);
	};
	Zone Z = Zone::None;
	bool bHit = ShotAt(0.2, 0.6, Z);
	Check("just behind the shoulder, low: heart and lungs", bHit && Z == Zone::HeartLung, Name(Z));
	bHit = ShotAt(0.0, 0.76, Z);
	Check("along the top of the back: spine", bHit && Z == Zone::Spine, Name(Z));
	bHit = ShotAt(-0.32, 0.6, Z);
	Check("the rear of the body: gut", bHit && Z == Zone::Gut, Name(Z));
	bHit = ShotAt(0.70, 0.98, Z);
	Check("the head: brain", bHit && Z == Zone::Brain, Name(Z));
	bHit = ShotAt(0.28, 0.2, Z);	// (its foreleg: 0.2 of its length ahead of the middle)
	Check("low on a leg: leg", bHit && Z == Zone::Leg, Name(Z));
	Check("a foot over its back: a miss", !ShotAt(0.0, 1.3, Z));
	{
		// (a grazing gemsbok's horns: up off its head, not into the ground)
		Pose Grazing;
		Grazing.HeadDown = 1.0;
		double HeadZ = 0.0, HornZ = -1.0;
		int Horns = 0;
		for (const BodyPart& B : Body(Kind("gemsbok"), Grazing))
		{
			HeadZ = B.Kind == PartKind::Head ? B.Centre.Z : HeadZ;
			if (B.Kind == PartKind::Horn)
			{
				HornZ = std::max(HornZ, B.Centre.Z);
				++Horns;
			}
		}
		Check("a grazing gemsbok's two horns (two lengths each) stand above its head", Horns == 4 && HornZ > HeadZ + 0.2, Num(HeadZ, "head %.2f m") + Num(HornZ, ", horns' middles %.2f m"));
	}
	std::mt19937 Random(7);
	const Effect Heart = Outcome(Impala, Zone::HeartLung, 2000.0, Random);
	const Effect Brain = Outcome(Impala, Zone::Brain, 2000.0, Random);
	const Effect Arrow = Outcome(Impala, Zone::HeartLung, 70.0, Random);
	const Effect Leg = Outcome(Impala, Zone::Leg, 2000.0, Random);
	Check("a .308 through the heart: runs a few seconds and falls; the brain: down at once; an arrow through the heart: a little longer; a leg: wounded, lives",
		!Heart.bDown && Heart.DiesAfter > 0.0 && Heart.DiesAfter <= 15.0 && Brain.bDown && Brain.DiesAfter == 0.0 && Arrow.DiesAfter >= 8.0 && Arrow.DiesAfter <= 30.0
		&& Leg.bWounded && Leg.DiesAfter < 0.0, Num(Heart.DiesAfter, "%.0f s") + ", " + Num(Arrow.DiesAfter, "%.0f s"));

	std::printf("Half an hour on a farm (1 km x 1 km, a dam in the middle): 30 impala, 10 zebra, 3 lions\n");
	Range Farm;
	Farm.Outline = { Vec2(0.0, 0.0), Vec2(1000.0, 0.0), Vec2(1000.0, 1000.0), Vec2(0.0, 1000.0) };
	Farm.Water = { Vec2(500.0, 500.0) };
	Herds Life;
	Life.Start(Farm, { { FindSpecies("impala"), 30 }, { FindSpecies("zebra"), 10 }, { FindSpecies("lion"), 3 } }, 42);
	std::map<Doing, int> Seen;
	int LookedUp = 0, Outside = 0, Hunts = 0, Kills = 0, GrazeSteps = 0, Swishing = 0, Turned = 0;
	double Moved = 0.0;
	std::vector<Vec2> Last;
	for (const Animal& A : Life.Animals())
	{
		Last.push_back(A.At);
	}
	for (int Step = 0; Step < 18000; ++Step)
	{
		Life.Step(0.1);
		for (size_t i = 0; i < Life.Animals().size(); ++i)
		{
			const Animal& A = Life.Animals()[i];
			++Seen[A.What];
			LookedUp += A.What == Doing::Graze && A.Now.HeadDown < 0.2 ? 1 : 0;
			GrazeSteps += A.What == Doing::Graze && A.Speed > 0.05 ? 1 : 0;
			Swishing += std::abs(A.Now.Tail) > 0.3 ? 1 : 0;
			Turned += std::abs(A.Now.Turn) > 0.3 ? 1 : 0;
			Outside += Farm.Inside(A.At) ? 0 : 1;
			Moved += (A.At - Last[i]).Length();
			Last[i] = A.At;
		}
		for (const std::string& N : Life.TakeNews())
		{
			Hunts += N.find("is hunting") != std::string::npos ? 1 : 0;
			Kills += N.find("killed") != std::string::npos ? 1 : 0;
		}
	}
	const double Count = double(Life.Animals().size());
	std::printf("        of their time: grazing %.0f%%, browsing %.0f%%, walking %.0f%%, drinking %.0f%%, looking up %.0f%%, running %.0f%%; %.1f km walked each\n",
		100.0 * Seen[Doing::Graze] / (18000.0 * Count), 100.0 * Seen[Doing::Browse] / (18000.0 * Count), 100.0 * Seen[Doing::Walk] / (18000.0 * Count),
		100.0 * Seen[Doing::Drink] / (18000.0 * Count), 100.0 * Seen[Doing::Look] / (18000.0 * Count), 100.0 * Seen[Doing::Flee] / (18000.0 * Count),
		Moved / Count / 1000.0);
	Check("nobody left the farm", Outside == 0, std::to_string(Outside));
	Check("they graze most of the time, and walk, drink and look up too", Seen[Doing::Graze] > Seen[Doing::Walk] && Seen[Doing::Drink] > 0 && Seen[Doing::Look] > 0 && LookedUp > 0,
		"looked up " + std::to_string(LookedUp) + " animal-steps while grazing, " + std::to_string(Seen[Doing::Look]) + " standing looking");
	Check("alive between times: grazing, they step on now and then; tails swish, heads turn", GrazeSteps > 0 && Swishing > 0 && Turned > 0,
		Num(100.0 * GrazeSteps / std::max(1, Seen[Doing::Graze]), "%.0f%% of grazing stepping on, ") + Num(100.0 * Swishing / (18000.0 * Count), "tails swishing %.0f%%, ")
		+ Num(100.0 * Turned / (18000.0 * Count), "heads turned %.0f%%"));
	Check("the lions hunted (every 8-15 minutes each: 3 lions, 30 minutes: at least 3 hunts)", Hunts >= 3, std::to_string(Hunts) + " hunts, " + std::to_string(Kills) + " kills");
	Herds Shooting;
	Shooting.Start(Farm, { { FindSpecies("impala"), 12 } }, 3);
	const Effect Hit = Outcome(Impala, Zone::HeartLung, 2000.0, Random);
	Shooting.Shot(0, Shooting.Animals()[0].At + Vec2(-150.0, 0.0), &Hit);
	int Running = 0, Herd = 0;
	for (const Animal& A : Shooting.Animals())
	{
		Running += A.What == Doing::Flee ? 1 : 0;
		Herd += A.Herd == Shooting.Animals()[0].Herd ? 1 : 0;
	}
	for (int Step = 0; Step < 300; ++Step)
	{
		Shooting.Step(0.1);
	}
	Check("a shot impala's herd runs (only its herd); the impala falls within 15 s", Running == Herd && Shooting.Animals()[0].What == Doing::Dead,
		std::to_string(Running) + " ran, of " + std::to_string(Shooting.Animals().size()));
	{
		// (three minutes on: the herd back together, somewhere else, wary)
		const Vec2 Before = Shooting.Animals()[1].At;
		for (int Step = 0; Step < 1800; ++Step)
		{
			Shooting.Step(0.1);
		}
		Vec2 Sum;
		int Alive = 0;
		for (const Animal& A : Shooting.Animals())
		{
			if (A.What != Doing::Dead && A.Herd == Shooting.Animals()[0].Herd)	// (the shot herd's: the farm's other impala another herd)
			{
				Sum = Sum + A.At;
				++Alive;
			}
		}
		const Vec2 Middle = Sum * (1.0 / std::max(Alive, 1));
		double Widest = 0.0;
		for (const Animal& A : Shooting.Animals())
		{
			Widest = A.What != Doing::Dead && A.Herd == Shooting.Animals()[0].Herd ? std::max(Widest, (A.At - Middle).Length()) : Widest;
		}
		Check("three minutes after, the herd's back together (all within 35 m of its middle)", Widest < 35.0 && (Middle - Before).Length() > 30.0,
			Num(Widest, "%.0f m across, ") + Num((Middle - Before).Length(), "%.0f m from where it was"));
	}
	{
		// a shot heard 200 m off: a herd there looks up towards it; one 1 km off doesn't notice
		Herds Near;
		Near.Start(Farm, { { FindSpecies("impala"), 10 } }, 11);
		const Vec2 At = Near.Animals()[0].At;
		Near.Heard(At + Vec2(200.0, 0.0));
		Near.Step(0.1);
		int Looking = 0;
		for (const Animal& A : Near.Animals())
		{
			Looking += A.What == Doing::Look || (A.At - At).Length() > 400.0 ? 1 : 0;	// (another herd 400 m+ off: not in earshot)
		}
		Check("a shot heard 200 m off: the game within earshot looks up towards it", Looking == int(Near.Animals().size()), std::to_string(Looking) + " of " + std::to_string(Near.Animals().size()));
	}
	{
		// behaviour as building blocks: a creature that isn't a carnivore, given only the hunting block (as a file can),
		// hunts; one that never drinks doesn't go to water; one that's deaf to shots doesn't look up
		Species Dog = Kind("warthog");
		Dog.Id = "test_hunting_grazer";
		Dog.Name = "Test hunter";
		Dog.Acts.bHunts = true;
		Dog.Acts.HuntMin = 60.0;
		Dog.Acts.HuntMax = 120.0;
		Dog.Acts.PreyUpTo = 60.0;
		Dog.Acts.ThirstMax = 0.0;
		Dog.Acts.Hears = 0.0;
		const int DogKind = AddSpecies(Dog);
		Herds Pack;
		Pack.Start(Farm, { { DogKind, 4 }, { FindSpecies("impala"), 20 } }, 7);
		int Hunting = 0, Drank = 0;
		for (int Step = 0; Step < 6000; ++Step)
		{
			Pack.Step(0.1);
			for (const std::string& N : Pack.TakeNews())
			{
				Hunting += N.find("A test hunter is hunting an impala") != std::string::npos ? 1 : 0;
			}
			for (const Animal& A : Pack.Animals())
			{
				Drank += A.Species == DogKind && A.What == Doing::Drink ? 1 : 0;
			}
		}
		Pack.Heard(Pack.Animals()[0].At + Vec2(50.0, 0.0));
		Pack.Step(0.1);
		const bool bDeaf = Pack.Animals()[0].What != Doing::Look;
		Check("a grazer given the hunting block hunts impala (in 10 minutes, hunting every 1-2); never drinks; deaf to a shot 50 m off",
			Hunting >= 4 && Drank == 0 && bDeaf, std::to_string(Hunting) + " hunts");
	}

	{
		// shared in a multiplayer game: the host's herds packed, the others' copy (the same farm and seed) unpacked
		Herds Host, Copy;
		Host.Start(Farm, { { FindSpecies("impala"), 30 }, { FindSpecies("zebra"), 10 } }, 21);
		Copy.Start(Farm, { { FindSpecies("impala"), 30 }, { FindSpecies("zebra"), 10 } }, 21);
		for (int Step = 0; Step < 600; ++Step)
		{
			Host.Step(0.1);
		}
		const std::vector<uint8_t> Bytes = Host.Pack();
		bool bFits = true;
		for (int k = 0; k < 5; ++k)
		{
			bFits = Copy.Unpack(Bytes) && bFits;	// (each update eases it half the way: five in a row, ~half a second)
		}
		double Off = 0.0;
		for (size_t i = 0; i < Host.Animals().size(); ++i)
		{
			Off = std::max(Off, (Host.Animals()[i].At - Copy.Animals()[i].At).Length());
		}
		Check("the host's herds packed and shown the same on the others' (40 animals; eased there over a few updates)", bFits && Off < 0.15 && Bytes.size() < 40 * 24,
			std::to_string(Bytes.size()) + " bytes" + Num(Off, ", %.3f m off"));
	}

	{
		// a car among them: driven at an impala slowly (bumped: alive, it runs), fast (killed, the car a little slower), at
		// an eland at 30 km/h (wounded, not killed: it's big); and a car coming fast spooks a herd 20 m off
		std::printf("Cars among them\n");
		Range Field;
		Field.Outline = { Vec2(0.0, 0.0), Vec2(400.0, 0.0), Vec2(400.0, 400.0), Vec2(0.0, 400.0) };
		// one creature, a car 4 m behind it coming straight at it at Speed: the first hit
		auto Drive = [&Field](const char* Id, double Speed, CarHit& Hit, Doing& After)
		{
			Herds Road;
			Road.Start(Field, { { FindSpecies(Id), 1 } }, 21);
			const Vec2 At = Road.Animals()[0].At;
			Car V;
			V.At = At - Vec2(4.0 + V.HalfLength, 0.0);
			V.Forward = Vec2(1.0, 0.0);
			V.Velocity = Vec2(Speed, 0.0);
			Hit = CarHit();
			for (int Step = 0; Step < 400 && Hit.Index < 0; ++Step)
			{
				Road.SetCars({ V });
				Road.Step(0.02);
				for (const CarHit& H : Road.TakeHits())
				{
					Hit = H;
				}
				V.At = V.At + V.Velocity * 0.02;
			}
			After = Road.Animals()[0].What;
		};
		CarHit Slow, Fast, Eland;
		Doing SlowAfter, FastAfter, ElandAfter;
		Drive("impala", 1.5, Slow, SlowAfter);
		Drive("impala", 16.7, Fast, FastAfter);
		Drive("eland", 8.3, Eland, ElandAfter);
		Check("an impala bumped at 5 km/h: alive, it runs; hit at 60 km/h: killed (the car ~0.5 m/s slower); an eland at 30 km/h: wounded, alive",
			Slow.Index == 0 && !Slow.bKilled && SlowAfter == Doing::Flee && Fast.bKilled && FastAfter == Doing::Dead
				&& Fast.CarLoses > 0.3 && Fast.CarLoses < 0.7 && Eland.Index == 0 && !Eland.bKilled && Eland.bWounded && ElandAfter != Doing::Dead,
			Num(Slow.Speed * 3.6, "%.0f km/h, ") + Num(Fast.Speed * 3.6, "%.0f km/h, ") + Num(Fast.CarLoses, "the car %.2f m/s slower, ") + Num(Eland.Speed * 3.6, "eland %.0f km/h"));
		// a herd, a car coming at it at 30 km/h: those near run, those farther look up
		Herds Herd;
		Herd.Start(Field, { { FindSpecies("impala"), 12 } }, 3);
		Vec2 Middle;
		for (const Animal& A : Herd.Animals())
		{
			Middle = Middle + A.At * (1.0 / 12.0);
		}
		Car Coming;
		Coming.At = Middle - Vec2(35.0, 0.0);	// (the herd from ~10 to ~60 m off)
		Coming.Velocity = Vec2(8.3, 0.0);
		// (those it's coming at: within 25 m, ahead of it; and 25-60 m)
		std::vector<int> Near, Farther;
		for (size_t i = 0; i < Herd.Animals().size(); ++i)
		{
			const Vec2 To = Herd.Animals()[i].At - Coming.At;
			if (Coming.Velocity.Dot(To.Normal()) > 2.0)
			{
				if (To.Length() < 24.0)
				{
					Near.push_back(int(i));
				}
				else if (To.Length() > 26.0 && To.Length() < 59.0)
				{
					Farther.push_back(int(i));
				}
			}
		}
		Herd.SetCars({ Coming });
		Herd.Step(0.1);
		int Ran = 0, Looked = 0;
		for (const int i : Near)
		{
			Ran += Herd.Animals()[size_t(i)].What == Doing::Flee ? 1 : 0;
		}
		for (const int i : Farther)
		{
			Looked += Herd.Animals()[size_t(i)].What == Doing::Look || Herd.Animals()[size_t(i)].What == Doing::Flee ? 1 : 0;
		}
		Check("a car coming at a herd at 30 km/h: those within 25 m of it run, those farther look up", !Near.empty() && Ran == int(Near.size())
			&& Looked == int(Farther.size()), std::to_string(Ran) + " of " + std::to_string(Near.size()) + " ran, " + std::to_string(Looked) + " of "
			+ std::to_string(Farther.size()) + " looked up");
	}
	{
		// the zombie pack's zombie (Godot/packs/zombies): an upright body, only the brain vital, doesn't run from shots,
		// goes for people
		std::printf("The zombie pack\n");
		std::vector<std::string> ZombieErrors;
		const int Read = ReadCreatures(FileText(PackFolder / ".." / "zombies" / "creatures" / "zombie.json"), ZombieErrors);
		Check("its zombie read", Read == 1 && ZombieErrors.empty() && FindSpecies("zombie") >= 0, ZombieErrors.empty() ? "" : ZombieErrors[0]);
		if (Read == 1)
		{
			const Species& Z = Kind("zombie");
			const std::vector<BodyPart> Parts = Body(Z, Pose{});
			double HeadZ = 0.0, Lowest = 10.0;
			int Limbs = 0;
			for (const BodyPart& B : Parts)
			{
				HeadZ = B.Kind == PartKind::Head ? B.Centre.Z : HeadZ;
				Lowest = std::min(Lowest, B.Centre.Z - B.Half.Z);
				Limbs += B.Kind == PartKind::Leg ? 1 : 0;
			}
			Check("upright: its head ~1.6 m up, its feet on the ground, two legs and two arms", HeadZ > 1.5 && HeadZ < 1.75 && Lowest < 0.05 && Limbs == 4,
				Num(HeadZ, "head %.2f m, ") + Num(Lowest, "lowest %.2f m, ") + std::to_string(Limbs) + " limbs");
			const double Tall = Z.Shoulder / 0.83;
			Vec3 Where;
			Zone Head = Zone::None, Chest = Zone::None;
			HitBody(Z, Pose{}, Vec3(12.0, 0.0, Tall * 0.95), Vec3(-12.0, 0.0, Tall * 0.95), Where, Head);	// (from in front: from the side, an ear first)
			HitBody(Z, Pose{}, Vec3(0.0, 12.0, Tall * 0.70), Vec3(0.0, -12.0, Tall * 0.70), Where, Chest);
			const Effect ChestShot = Outcome(Z, Chest, 2600.0, Random);
			const Effect HeadShot = Outcome(Z, Head, 2600.0, Random);
			Check("a .308 through its chest (heart and lungs) only wounds it; through its head (brain) it's down",
				Chest == Zone::HeartLung && !ChestShot.bDown && ChestShot.DiesAfter < 0.0 && Head == Zone::Brain && HeadShot.bDown,
				std::string(Name(Chest)) + ", " + Name(Head));
			// on a field: one standing 30 m off it is come for and caught; one 60 m off isn't noticed; a shot doesn't
			// make it run
			Range Field;
			Field.Outline = { Vec2(0.0, 0.0), Vec2(400.0, 0.0), Vec2(400.0, 400.0), Vec2(0.0, 400.0) };
			Herds Town;
			Town.Start(Field, { { FindSpecies("zombie"), 1 } }, 9);
			const Vec2 Start = Town.Animals()[0].At;
			const Vec2 Away = Start.X < 200.0 ? Vec2(1.0, 0.0) : Vec2(-1.0, 0.0);
			Town.Shot(0, Start + Away * 50.0, nullptr);	// (a near miss at it)
			Town.Heard(Start + Away * 50.0);
			Town.Step(0.1);
			const bool bStayed = Town.Animals()[0].What != Doing::Flee;
			Town.SetPeople({ Start + Away * 60.0 });
			Town.Step(0.1);
			const bool bNotFar = Town.Animals()[0].What != Doing::Pursue;
			Town.SetPeople({ Start + Away * 30.0 });
			double CaughtAfter = -1.0;
			for (int Step = 1; Step <= 600 && CaughtAfter < 0.0; ++Step)
			{
				Town.Step(0.1);
				CaughtAfter = Town.TakeCaught().empty() ? -1.0 : Step * 0.1;
			}
			Check("a shot doesn't make it run; one 60 m off isn't noticed; one 30 m off is come for and caught (2.2 m/s: ~14 s)",
				bStayed && bNotFar && CaughtAfter > 8.0 && CaughtAfter < 25.0, Num(CaughtAfter, "caught after %.1f s"));
			// shot in the body: wounded three times, down at the fourth (wounds_to_drop 4)
			{
				Herds Shooting;
				Shooting.Start(Field, { { FindSpecies("zombie"), 1 } }, 4);
				const Vec2 From = Shooting.Animals()[0].At + Vec2(20.0, 0.0);
				int DownAt = 0;
				for (int Shot = 1; Shot <= 6 && DownAt == 0; ++Shot)
				{
					const Effect Did = Shooting.Shot(0, From, &ChestShot);
					DownAt = Did.bDown ? Shot : 0;
				}
				Check("shot in the body: wounded, and down at the fourth", DownAt == 4 && Shooting.Animals()[0].What == Doing::Dead, "down at shot " + std::to_string(DownAt));
			}
			// a pool round someone (Spawning): 50 on a 2 km field gather round them (60-150 m, those within 250 left be); one killed is back among
			// them 30 s later; they follow them a kilometre on
			{
				Range Wide;
				Wide.Outline = { Vec2(0.0, 0.0), Vec2(2000.0, 0.0), Vec2(2000.0, 2000.0), Vec2(0.0, 2000.0) };
				Herds Pool;
				Pool.Start(Wide, { { FindSpecies("zombie"), 50 } }, 8);
				Spawning How;
				How.bAroundPeople = true;
				Pool.SetSpawning(How);
				Vec2 Person(500.0, 500.0);
				auto Round = [&Pool, &Person](double Within)
				{
					int N = 0;
					for (const Animal& A : Pool.Animals())
					{
						N += A.What != Doing::Dead && (A.At - Person).Length() < Within ? 1 : 0;
					}
					return N;
				};
				Pool.SetPeople({ Person }, { true });
				Pool.Step(0.1);
				const int Gathered = Round(255.0);	// (those already within 250 m are left where they are)
				Effect Brain;
				Brain.bDown = true;
				Brain.DiesAfter = 0.0;
				Pool.Shot(0, Person, &Brain);
				for (int Step = 0; Step < 320; ++Step)
				{
					Pool.SetPeople({ Person }, { false });	// (in a car: not caught, the pool kept whole)
					Pool.Step(0.1);
				}
				const bool bBack = Pool.Animals()[0].What != Doing::Dead && (Pool.Animals()[0].At - Person).Length() < 155.0;
				Person = Vec2(1500.0, 1500.0);
				for (int Step = 0; Step < 20; ++Step)
				{
					Pool.SetPeople({ Person }, { false });
					Pool.Step(0.1);
				}
				const int Followed = Round(255.0);
				Check("a pool of 50: all round someone at once; one killed back among them in 30 s; they follow them a km on",
					Gathered == 50 && bBack && Followed == 50, std::to_string(Gathered) + " round them, " + std::to_string(Followed) + " after they moved");
			}
			// someone in a car: come for, never caught
			Herds Road;
			Road.Start(Field, { { FindSpecies("zombie"), 1 } }, 9);
			const Vec2 InCar = Road.Animals()[0].At + Away * 30.0;
			bool bCame = false, bCaught = false;
			for (int Step = 0; Step < 600; ++Step)
			{
				Road.SetPeople({ InCar }, { false });
				Road.Step(0.1);
				bCame = bCame || Road.Animals()[0].What == Doing::Pursue;
				bCaught = bCaught || !Road.TakeCaught().empty();
			}
			Check("someone in a car: come for, never caught", bCame && !bCaught && (Road.Animals()[0].At - InCar).Length() < 3.0,
				Num((Road.Animals()[0].At - InCar).Length(), "it's %.1f m off them a minute later"));
			// a building on the grid: not somewhere to be
			Range Block = Field;
			Block.Block({ Vec2(100.0, 100.0), Vec2(120.0, 100.0), Vec2(120.0, 120.0), Vec2(100.0, 120.0) });
			Check("a building on the grid: inside it isn't somewhere to be; beside it is", !Block.Inside(Vec2(110.0, 110.0)) && Block.Inside(Vec2(125.0, 110.0))
				&& Block.Inside(Vec2(110.0, 95.0)));
		}
	}
	{
		// a game's rules (GeoGame): a zombie rush: 5 minutes; a zombie +1; 25 wins; caught 3 times loses; every 60 s ten
		// more seconds; time up loses
		std::printf("A game's rules\n");
		const char* Text = R"([
			{"when": "start", "then": [{"do": "timer", "seconds": 300}, {"do": "say", "text": "Go"}]},
			{"when": "kill", "of": "zombie", "then": [{"do": "score", "by": 1}]},
			{"when": "score", "reaches": 25, "then": [{"do": "win", "text": "25 zombies: you've won"}]},
			{"when": "caught_times", "count": 3, "then": [{"do": "lose", "text": "Caught three times"}]},
			{"when": "every", "seconds": 60, "then": [{"do": "time", "seconds": 10}]},
			{"when": "time_up", "then": [{"do": "lose", "text": "Time's up"}]}])";
		geojson::Value Rules;
		std::string Error;
		geojson::Parse(Text, Rules, Error);
		geogame::Game Rush;
		const bool bLoaded = Rush.Load(Rules, Error);
		// won: 25 zombies (a springbok scores nothing), half a minute a kill
		Rush.Start();
		const bool bSaidGo = !Rush.TakeSaid().empty();
		const double TimerAtStart = Rush.TimeLeft;
		Rush.Event("kill", "springbok");
		for (int k = 0; k < 25 && !Rush.bOver; ++k)
		{
			Rush.Step(9.0);	// (a kill every 9 s)
			Rush.Event("kill", "zombie");
		}
		const std::vector<geogame::Said> Won = Rush.TakeSaid();
		const bool bWon = Rush.Result == "won" && Rush.Score == 25.0 && !Won.empty() && Won.back().Kind == "win";
		Rush.Event("kill", "zombie");	// (over: nothing more)
		const bool bStill = Rush.Score == 25.0;
		// lost: caught three times
		Rush.Start();
		Rush.Event("caught");
		Rush.Event("caught");
		const bool bNotYet = !Rush.bOver;
		Rush.Event("caught");
		const bool bCaughtOut = Rush.Result == "lost";
		// lost: time up (300 s, ten more each minute: 300 + 50 = ~350 s)
		Rush.Start();
		double Ran = 0.0;
		while (!Rush.bOver && Ran < 1000.0)
		{
			Rush.Step(1.0);
			Ran += 1.0;
		}
		const bool bTimeUp = Rush.Result == "lost" && Ran > 340.0 && Ran < 380.0;
		// a rule that isn't one: turned away, with why
		geojson::Value Bad;
		geojson::Parse(R"([{"when": "sunrise", "then": [{"do": "win"}]}])", Bad, Error);
		geogame::Game Wrong;
		std::string Why;
		const bool bTurnedAway = !Wrong.Load(Bad, Why) && Why.find("rule 1") != std::string::npos;
		// written out and read back the same
		geogame::Game Again;
		std::string Ignored;
		Again.Load(Rush.Write(), Ignored);
		const bool bSame = geojson::Write(Again.Write()) == geojson::Write(Rush.Write());
		Check("a zombie rush: 25 kills win (a springbok none); over, nothing more; caught 3 times loses; time up loses (300 s and 10 more a minute)",
			bLoaded && bSaidGo && TimerAtStart == 300.0 && bWon && bStill && bNotYet && bCaughtOut && bTimeUp,
			Num(Ran, "time up after %.0f s"));
		Check("a rule that isn't one turned away (and why); the rules written out and read back the same", bTurnedAway && bSame, Why);
		// a race: 4 checkpoints, point to point; out of order doesn't count; the clock from the first; finished, won
		geojson::Value RaceRules;
		geojson::Parse(R"([{"when": "finish", "then": [{"do": "win", "text": "Finished"}]}])", RaceRules, Error);
		geogame::Game Race;
		Race.Load(RaceRules, Error);
		Race.SetCourse(4, 0);
		Race.Start();
		Race.Step(5.0);
		Race.Event("reach", "1");	// (before the start: no)
		const bool bNotStarted = Race.RaceTime() < 0.0;
		Race.Event("reach", "0");
		Race.Step(20.0);
		Race.Event("reach", "1");
		Race.Step(15.0);
		Race.Event("reach", "3");	// (missed 2: no)
		const bool bSkipped = Race.NextCheckpoint == 2 && !Race.bOver;
		Race.Event("reach", "2");
		Race.Step(12.5);
		Race.Event("reach", "3");
		const bool bFinished = Race.Result == "won" && std::abs(Race.FinishTime - 47.5) < 1e-6 && Race.Splits.size() == 3;
		// a circuit: 3 checkpoints, 2 laps: the line crossed again ends a lap, the second the finish
		geogame::Game Circuit;
		Circuit.Load(RaceRules, Error);
		Circuit.SetCourse(3, 2);
		Circuit.Start();
		int Reached = 0;
		for (const char* At : { "0", "1", "2", "0", "1", "2" })
		{
			Circuit.Step(10.0);
			Circuit.Event("reach", At);
			++Reached;
		}
		const bool bOnLap2 = Circuit.Lap == 2 && !Circuit.bOver;
		Circuit.Step(10.0);
		Circuit.Event("reach", "0");
		const bool bCircuitDone = Circuit.Result == "won" && std::abs(Circuit.FinishTime - 60.0) < 1e-6;
		Check("a race: out of order doesn't count; its clock from the start; finished in 47.5 s, won; a circuit of 2 laps finished on the line the second time (60 s)",
			bNotStarted && bSkipped && bFinished && bOnLap2 && bCircuitDone, Num(Race.FinishTime, "%.1f s, ") + Num(Circuit.FinishTime, "%.1f s"));
	}
	{
		// (last: these add copies to the creatures and weapons) every creature and weapon written out and read back,
		// as a copy, the same
		std::printf("The pack's files\n");
		int Same = 0, Tried = 0;
		const std::vector<Species> Kinds = AllSpecies();
		for (const Species& S : Kinds)
		{
			const std::string Text = geojson::Write(CreatureValue(S));
			geojson::Value Read;
			std::string Error;
			geojson::Parse(Text, Read, Error);
			Read.Set("id", "copy_of_" + S.Id);
			const int Index = ReadCreature(Read, Error);
			++Tried;
			if (Index >= 0)
			{
				geojson::Value Back = CreatureValue(AllSpecies()[size_t(Index)]);
				Back.Set("id", S.Id);
				Same += geojson::Write(Back) == Text ? 1 : 0;
			}
		}
		const std::vector<Load> Weapons = AllLoads();
		for (const Load& L : Weapons)
		{
			const std::string Text = geojson::Write(LoadValue(L));
			geojson::Value Read;
			std::string Error;
			geojson::Parse(Text, Read, Error);
			Read.Set("id", "copy_of_" + L.Id);
			const int Index = ReadLoad(Read, Error);
			++Tried;
			if (Index >= 0)
			{
				geojson::Value Back = LoadValue(AllLoads()[size_t(Index)]);
				Back.Set("id", L.Id);
				Same += geojson::Write(Back) == Text ? 1 : 0;
			}
		}
		Check("each creature and weapon written out and read back the same", Same == Tried, std::to_string(Same) + " of " + std::to_string(Tried));
		// a file with a mistake: told where; a creature with one: turned away, and why
		std::vector<std::string> Bad;
		ReadCreatures("[\n{ \"id\": \"a\", \"mass\": 50,\n  \"shoulder\" 0.8 }\n]", Bad);
		ReadCreatures("{ \"id\": \"test_slow\", \"like\": \"impala\", \"run\": 0.5 }", Bad);
		Check("a file's mistake told with its line; a creature that can't be, turned away with why", Bad.size() == 2
			&& Bad[0].find("line 3") != std::string::npos && Bad[1] == "test_slow: walk must be over 0, and run at least walk" && FindSpecies("test_slow") < 0,
			Bad.size() == 2 ? Bad[0] + "; " + Bad[1] : std::to_string(Bad.size()) + " messages");
	}

	std::printf(Failed == 0 ? "All right.\n" : "%d off.\n", Failed);
	return Failed == 0 ? 0 : 1;
}
