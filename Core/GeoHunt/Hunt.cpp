// Geodrive Core - GPL (see Core/README.md)

#include "Hunt.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace geohunt
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double Gravity = 9.80665;
		constexpr double AirDensity = 1.225;	// kg/m3 (sea level, 15 C)
		constexpr double SoundSpeed = 340.3;	// m/s
	}

	double Vec2::Length() const { return std::sqrt(X * X + Y * Y); }
	Vec2 Vec2::Normal() const { const double L = Length(); return L > 1e-12 ? Vec2(X / L, Y / L) : Vec2(); }
	double Vec3::Length() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	Vec3 Vec3::Normal() const { const double L = Length(); return L > 1e-12 ? Vec3(X / L, Y / L, Z / L) : Vec3(); }

	// --- the animals ---

	Behaviour ActsFor(Diet D)
	{
		Behaviour B;
		B.Browsing = D == Diet::Browser ? 1.0 : D == Diet::Mixed ? 0.4 : 0.0;
		B.bRests = D == Diet::Carnivore;
		B.bHunts = D == Diet::Carnivore;
		B.bPrey = D != Diet::Carnivore;
		return B;
	}

	namespace
	{
		// "A lion", "an impala": a creature's name as the news says it (bStart: a sentence's start)
		std::string AName(const Species& S, bool bStart = false)
		{
			std::string Name = S.Name;
			if (!Name.empty() && Name[0] >= 'A' && Name[0] <= 'Z' && !(Name.size() > 1 && Name[1] >= 'A' && Name[1] <= 'Z'))
			{
				Name[0] = char(Name[0] - 'A' + 'a');
			}
			const bool bVowel = !Name.empty() && std::strchr("aeiou", Name[0]) != nullptr;
			return std::string(bStart ? (bVowel ? "An " : "A ") : (bVowel ? "an " : "a ")) + Name;
		}
	}

	std::vector<Species>& Registry();	// (every creature added, from the packs)

	const std::vector<Species>& AllSpecies() { return Registry(); }

	int AddSpecies(const Species& S)
	{
		std::vector<Species>& List = Registry();
		for (size_t i = 0; i < List.size(); ++i)
		{
			if (List[i].Id == S.Id)
			{
				List[i] = S;
				return int(i);
			}
		}
		List.push_back(S);
		return int(List.size()) - 1;
	}

	std::vector<Species>& Registry()
	{
		// (none built in: every creature's from a pack's creatures/*.json, AddSpecies)
		static std::vector<Species> List;
		return List;
	}

	int FindSpecies(const std::string& Id)
	{
		const auto& List = AllSpecies();
		for (size_t i = 0; i < List.size(); ++i)
		{
			if (Id == List[i].Id)
			{
				return int(i);
			}
		}
		return -1;
	}

	SizeClass SizeOf(const Species& S)
	{
		return S.Mass < 20.0 ? SizeClass::Small : S.Mass < 100.0 ? SizeClass::Medium : SizeClass::Large;
	}

	// --- the law ---

	const char* Why(Verdict V)
	{
		switch (V)
		{
		case Verdict::Legal: return "";
		case Verdict::Automatic: return "Automatic fire isn't allowed for hunting";
		case Verdict::Protected: return "Protected: never to be hunted";
		case Verdict::Closed: return "No open season in this district";
		case Verdict::OutOfSeason: return "Out of season";
		case Verdict::BagFull: return "Today's bag is full for this species";
		case Verdict::ArrowTooLight: return "The arrow is too light for this animal";
		case Verdict::BowTooWeak: return "Too little arrow energy for this animal";
		default: return "?";
		}
	}

	Verdict Judge(const Species& S, int Month, int Taken, bool bBow, double ArrowGrains, double EnergyFtLb, bool bMechanical, bool bAutomatic)
	{
		// (a fully automatic weapon: not a hunting arm, anywhere in South Africa)
		if (bAutomatic && !S.bProtected)
		{
			return Verdict::Automatic;
		}
		if (S.bProtected)
		{
			return Verdict::Protected;
		}
		if (S.SeasonFrom == 0)
		{
			return Verdict::Closed;
		}
		const bool bInSeason = S.SeasonFrom <= S.SeasonTo ? (Month >= S.SeasonFrom && Month <= S.SeasonTo) : (Month >= S.SeasonFrom || Month <= S.SeasonTo);
		if (!bInSeason)
		{
			return Verdict::OutOfSeason;
		}
		if (Taken >= S.BagPerDay)
		{
			return Verdict::BagFull;
		}
		if (bBow)
		{
			// the bow-hunting guideline (CapeNature, December 2011): small 30 ft-lb and 300 gr, medium 50 and 380, large
			// 60 and 415; mechanical broadheads 5% more energy
			const SizeClass C = SizeOf(S);
			const double Energy = C == SizeClass::Small ? 30.0 : C == SizeClass::Medium ? 50.0 : 60.0;
			const double Grains = C == SizeClass::Small ? 300.0 : C == SizeClass::Medium ? 380.0 : 415.0;
			if (ArrowGrains < Grains)
			{
				return Verdict::ArrowTooLight;
			}
			if (EnergyFtLb < Energy * (bMechanical ? 1.05 : 1.0))
			{
				return Verdict::BowTooWeak;
			}
		}
		return Verdict::Legal;
	}

	// --- the guns and the bow ---

	namespace
	{
		// (none built in: every weapon's from a pack's weapons/*.json, AddLoad)
		std::vector<Load>& Loads()
		{
			static std::vector<Load> List;
			return List;
		}
	}

	const std::vector<Load>& AllLoads() { return Loads(); }

	int AddLoad(const Load& L)
	{
		std::vector<Load>& List = Loads();
		for (size_t i = 0; i < List.size(); ++i)
		{
			if (List[i].Id == L.Id)
			{
				List[i] = L;
				return int(i);
			}
		}
		List.push_back(L);
		return int(List.size()) - 1;
	}

	int FindLoad(const std::string& Id)
	{
		const auto& List = AllLoads();
		for (size_t i = 0; i < List.size(); ++i)
		{
			if (Id == List[i].Id)
			{
				return int(i);
			}
		}
		return -1;
	}

	double EnergyFtLb(double Grains, double Speed)
	{
		const double Fps = Speed / 0.3048;
		return Grains * Fps * Fps / 450240.0;
	}

	namespace
	{
		// the G1 standard projectile's drag coefficient by Mach number (Ingalls' table, as ballistics programs use it)
		double DragG1(double Mach)
		{
			static const double Table[][2] = {
				{ 0.00, 0.2629 }, { 0.50, 0.2032 }, { 0.60, 0.2034 }, { 0.70, 0.2165 }, { 0.80, 0.2547 }, { 0.90, 0.3415 },
				{ 0.95, 0.4084 }, { 1.00, 0.4805 }, { 1.05, 0.5427 }, { 1.10, 0.5883 }, { 1.20, 0.6203 }, { 1.30, 0.6276 },
				{ 1.40, 0.6230 }, { 1.50, 0.6146 }, { 1.60, 0.6034 }, { 1.80, 0.5803 }, { 2.00, 0.5543 }, { 2.20, 0.5320 },
				{ 2.50, 0.5097 }, { 3.00, 0.4757 }, { 3.50, 0.4504 }, { 4.00, 0.4247 }, { 5.00, 0.4000 } };
			const int N = int(std::size(Table));
			if (Mach <= Table[0][0])
			{
				return Table[0][1];
			}
			for (int i = 1; i < N; ++i)
			{
				if (Mach <= Table[i][0])
				{
					const double T = (Mach - Table[i - 1][0]) / (Table[i][0] - Table[i - 1][0]);
					return Table[i - 1][1] + (Table[i][1] - Table[i - 1][1]) * T;
				}
			}
			return Table[N - 1][1];
		}

		// drag deceleration (m/s2) at a speed: a bullet by its G1 coefficient (the reference projectile's drag, scaled
		// by its sectional density over form factor: BC in lb/in2, 1 lb/in2 = 703.07 kg/m2); an arrow, its own (a
		// carbon arrow with three vanes loses ~0.5% of its speed every 9 m)
		double Drag(const Load& L, double Speed)
		{
			if (L.bArrow)
			{
				return 5.6e-4 * Speed * Speed;
			}
			return DragG1(Speed / SoundSpeed) * AirDensity * Speed * Speed * Pi / (8.0 * L.BC * 703.07);
		}
	}

	void Advance(const Load& L, Vec3& Position, Vec3& Velocity, double Step)
	{
		// (midpoint: drag and gravity at the half step)
		auto Accel = [&L](const Vec3& V)
		{
			const double Speed = V.Length();
			return V.Normal() * -Drag(L, Speed) + Vec3(0.0, 0.0, -Gravity);
		};
		const Vec3 Half = Velocity + Accel(Velocity) * (Step * 0.5);
		Position = Position + Half * Step;
		Velocity = Velocity + Accel(Half) * Step;
	}

	double ZeroAngle(const Load& L)
	{
		// the tilt where a level shot's path crosses the line of sight (SightHeight over the bore) at the zero: by
		// bisection on its height there
		auto HeightAtZero = [&L](double Tilt)
		{
			Vec3 Position(0.0, 0.0, -L.SightHeight);
			Vec3 Velocity = Vec3(std::cos(Tilt), 0.0, std::sin(Tilt)) * L.Muzzle;
			for (int i = 0; i < 20000 && Position.X < L.ZeroAt; ++i)
			{
				Advance(L, Position, Velocity, 0.0005);
			}
			return Position.Z;
		};
		double Low = 0.0, High = 0.1;
		for (int i = 0; i < 60; ++i)
		{
			const double Mid = (Low + High) * 0.5;
			(HeightAtZero(Mid) < 0.0 ? Low : High) = Mid;
		}
		return (Low + High) * 0.5;
	}

	// --- an animal's body ---

	const char* Name(Zone Z)
	{
		switch (Z)
		{
		case Zone::Brain: return "brain";
		case Zone::Spine: return "spine";
		case Zone::HeartLung: return "heart and lungs";
		case Zone::Gut: return "gut";
		case Zone::Neck: return "neck";
		case Zone::Leg: return "leg";
		default: return "nothing";
		}
	}

	namespace
	{
		// how it carries its head (up, looking): over its shoulder by this much of its shoulder height, and how wide it is
		double HeadUp(const Species& S)
		{
			return S.Shape.HeadUp;
		}
		double Width(const Species& S) { return std::clamp(std::sqrt(S.Mass) / 90.0, 0.08, 0.6); }

		Vec3 RollAboutX(const Vec3& P, double R) { return { P.X, P.Y * std::cos(R) - P.Z * std::sin(R), P.Y * std::sin(R) + P.Z * std::cos(R) }; }

		// the torso's middle height and half height (its chest and rump round it)
		double TorsoZ(const Species& S) { return S.Shoulder * 0.72; }
		double TorsoHalf(const Species& S) { return S.Shoulder * (S.Shape.TorsoDepth > 0.0 ? S.Shape.TorsoDepth : S.Mass > 800.0 ? 0.22 : 0.17); }
		// how far its body's come down, lying at rest on its belly (a lion's day: ~20 hours of it)
		double Drop(const Species& S, const Pose& P) { return P.Rest * (TorsoZ(S) - TorsoHalf(S) * 1.08); }

		// a part along a line (its long axis its own z), from A to B, Thick wide (half sizes x, y)
		BodyPart Along(PartKind Kind, const Vec3& A, const Vec3& B, double HalfX, double HalfY)
		{
			const Vec3 D = B - A;
			const double Length = std::max(D.Length(), 1e-6);
			// its z turned to D: pitched (z to (-sin p, 0, cos p)), then rolled about x (to (-sin p, -cos p sin r, cos p cos r))
			const double Pitch = std::asin(std::clamp(-D.X / Length, -1.0, 1.0));
			const double Roll = std::atan2(-D.Y, D.Z);
			return { Kind, (A + B) * 0.5, Vec3(HalfX, HalfY, std::max(Length * 0.5, 0.01)), Pitch, Roll };
		}
	}

	Pattern PatternOf(const Species& S) { return S.Shape.Coat; }

	namespace
	{
		// An upright body (a person's build): Shoulder its shoulder height, its whole height ~1.2 times that; two legs
		// swinging by the gait, two arms (swinging the other way, or held out in front), the chest and the hips, the
		// neck and the head (bowed as it "grazes", turned as it looks round). Down: on its side
		std::vector<BodyPart> Upright(const Species& S, const Pose& P)
		{
			std::vector<BodyPart> Out;
			const double T = S.Shoulder / 0.83;	// (its whole height)
			const double K = T / 1.75;			// (a person's proportions, scaled)
			const double Bob = std::abs(std::sin(P.Gait)) * T * 0.012 * P.Stepping;
			const double Lean = 0.12 * P.Pace * P.Stepping;	// (forward, running)
			auto Leaned = [Lean](const Vec3& V) { return Vec3(V.X * std::cos(Lean) + V.Z * std::sin(Lean), V.Y, -V.X * std::sin(Lean) + V.Z * std::cos(Lean)); };
			const Vec3 Hips(0.0, 0.0, T * 0.53 + Bob);
			auto Above = [&](double X, double Y, double Z) { return Hips + Leaned(Vec3(X, Y, Z - T * 0.53)); };
			Out.push_back({ PartKind::Torso, Above(0.0, 0.0, T * 0.70), Vec3(0.11 * K, 0.19 * K, 0.17 * K), Lean, 0.0 });
			Out.push_back({ PartKind::Rump, Above(0.0, 0.0, T * 0.55), Vec3(0.10 * K, 0.17 * K, 0.11 * K), Lean, 0.0 });
			// the legs: hip to knee to foot, one after the other
			const double Swing = (0.35 + 0.35 * P.Pace) * P.Stepping;
			for (const double Side : { -1.0, 1.0 })
			{
				const double Phase = P.Gait + (Side > 0.0 ? 0.0 : Pi);
				const double Angle = std::sin(Phase) * Swing;
				const double Fold = std::max(0.0, -std::cos(Phase)) * (0.5 + 0.6 * P.Pace) * P.Stepping;
				const Vec3 Hip(0.0, Side * 0.09 * K, Hips.Z - 0.04 * K);
				const double Upper = (Hip.Z - Bob) * 0.5, Lower = (Hip.Z - Bob) * 0.5;
				const Vec3 Knee = Hip + Vec3(std::sin(Angle), 0.0, -std::cos(Angle)) * Upper;
				const Vec3 Foot = Knee + Vec3(std::sin(Angle - Fold), 0.0, -std::cos(Angle - Fold)) * Lower;
				Out.push_back(Along(PartKind::Leg, Hip, Knee, 0.075 * K, 0.075 * K));
				Out.push_back(Along(PartKind::Shin, Knee, Foot, 0.055 * K, 0.055 * K));
				Out.push_back({ PartKind::Hoof, Foot + Vec3(0.06 * K, 0.0, 0.03 * K), Vec3(0.12 * K, 0.05 * K, 0.035 * K), 0.0, 0.0 });
			}
			// the arms: from the shoulders, hanging and swinging against the legs, or held out in front
			for (const double Side : { -1.0, 1.0 })
			{
				const Vec3 Shoulder = Above(0.0, Side * 0.2 * K, T * 0.80);
				Vec3 Elbow, Hand;
				if (S.Shape.bArmsForward)
				{
					const double Sway = std::sin(P.Gait + (Side > 0.0 ? 0.0 : Pi)) * 0.08;
					Elbow = Shoulder + Vec3(0.28 * K, Side * 0.02 * K, -0.03 * K + Sway * 0.1);
					Hand = Elbow + Vec3(0.26 * K, -Side * 0.02 * K, Sway * 0.2);
				}
				else
				{
					const double Angle = -std::sin(P.Gait + (Side > 0.0 ? 0.0 : Pi)) * Swing * 0.8;
					Elbow = Shoulder + Vec3(std::sin(Angle), 0.0, -std::cos(Angle)) * (0.29 * K);
					Hand = Elbow + Vec3(std::sin(Angle + 0.25 + 0.6 * P.Pace), 0.0, -std::cos(Angle + 0.25 + 0.6 * P.Pace)) * (0.27 * K);
				}
				Out.push_back(Along(PartKind::Leg, Shoulder, Elbow, 0.05 * K, 0.05 * K));
				Out.push_back(Along(PartKind::Shin, Elbow, Hand, 0.04 * K, 0.04 * K));
			}
			// the neck and head: bowed as it "grazes" (stands about), turned as it looks round
			const double Bow = std::clamp(P.HeadDown, 0.0, 1.0) * 0.6;
			const double Turn = P.Lying > 0.0 ? 0.0 : std::clamp(P.Turn, -1.2, 1.2);
			const Vec3 NeckBase = Above(0.0, 0.0, T * 0.84);
			const Vec3 Head = NeckBase + Leaned(Vec3(std::sin(Bow) * 0.14 * K, 0.0, std::cos(Bow) * 0.14 * K));
			Out.push_back(Along(PartKind::Neck, NeckBase, Head, 0.055 * K, 0.055 * K));
			const size_t HeadFirst = Out.size();
			Out.push_back({ PartKind::Head, Head + Vec3(0.0, 0.0, 0.03 * K), Vec3(0.1 * K, 0.08 * K, 0.115 * K), Bow + Lean, 0.0 });
			for (const double Side : { -1.0, 1.0 })
			{
				const Vec3 Root = Head + Vec3(0.0, Side * 0.08 * K, 0.03 * K);
				Out.push_back(Along(PartKind::Ear, Root, Root + Vec3(0.0, Side * 0.02 * K * S.Shape.Ears, 0.04 * K * S.Shape.Ears), 0.015 * K, 0.02 * K));
			}
			for (size_t k = HeadFirst; k < Out.size(); ++k)
			{
				const Vec3 V = Out[k].Centre - Head;
				Out[k].Centre = Head + Vec3(V.X * std::cos(Turn) - V.Y * std::sin(Turn), V.X * std::sin(Turn) + V.Y * std::cos(Turn), V.Z);
				Out[k].Yaw = Turn;
			}
			// down: on its side
			if (P.Lying > 0.0)
			{
				const double R = P.Lying * Pi * 0.5;
				for (BodyPart& B : Out)
				{
					B.Centre = RollAboutX(B.Centre, R) + Vec3(0.0, 0.0, 0.17 * K * P.Lying);
					B.Roll += R;
				}
			}
			return Out;
		}
	}

	std::vector<BodyPart> Body(const Species& S, const Pose& P)
	{
		if (S.Shape.Build == BodyShape::Frame::Upright)
		{
			return Upright(S, P);
		}
		std::vector<BodyPart> Out;
		const BodyShape& Shape = S.Shape;
		const double Sh = S.Shoulder, L = S.Length, W = Width(S);
		const double TZ = TorsoZ(S), TH = TorsoHalf(S);
		const bool bSloped = Shape.BackSlope > 0.0;	// (a giraffe's back, down to its rump)
		// running: the body rises and falls with each bound; at rest, down on its belly
		const double Rest = std::clamp(P.Rest, 0.0, 1.0);
		const double Bob = std::sin(P.Gait * 2.0) * Sh * 0.025 * P.Pace * P.Stepping - Drop(S, P);
		// the chest (deep, in front), the barrel and the rump (behind): a giraffe's back slopes down to its rump
		const double Slope = Sh * Shape.BackSlope;
		const double Tilt = bSloped ? 0.25 * Shape.BackSlope / 0.12 : 0.0;	// (a giraffe's chest and barrel tipped back 0.25 rad)
		Out.push_back({ PartKind::Torso, Vec3(L * 0.12, 0.0, TZ + Sh * 0.02 + Bob), Vec3(L * 0.22, W, TH * 1.12), Tilt, 0.0 });
		Out.push_back({ PartKind::Torso, Vec3(-L * 0.02, 0.0, TZ - Slope * 0.5 + Bob), Vec3(L * 0.2, W * 0.97, TH * 1.04), Tilt, 0.0 });
		Out.push_back({ PartKind::Rump, Vec3(-L * 0.15, 0.0, TZ - Slope + Bob), Vec3(L * 0.2, W * 0.92, TH * 0.98), 0.0, 0.0 });
		// the legs: an upper part from the shoulder or hip to the knee, a lower from the knee to the hoof; swinging by
		// the gait (a walk: one after another, hind left, fore left, hind right, fore right; a gallop: the forelegs
		// together, then the hind), the lower part folding back as the leg comes forward
		const double JointZ = TZ - TH * 0.45 + Bob;
		const double Swing = (0.28 + 0.32 * P.Pace) * P.Stepping;
		for (int i = 0; i < 4; ++i)
		{
			const bool bFore = i < 2, bLeft = i % 2 == 0;
			const double Walk = (bFore ? (bLeft ? 0.25 : 0.75) : (bLeft ? 0.0 : 0.5));
			const double Gallop = (bFore ? (bLeft ? 0.0 : 0.1) : (bLeft ? 0.5 : 0.6));
			const double Phase = P.Gait + 2.0 * Pi * (Walk + (Gallop - Walk) * P.Pace);
			// (at rest: the forelegs stretched out in front, the hind folded under)
			const double Angle = std::sin(Phase) * Swing * (1.0 - Rest) + (bFore ? 1.4 : 1.25) * Rest;
			const double Fold = std::max(0.0, std::cos(Phase)) * (0.35 + 0.5 * P.Pace) * P.Stepping * (1.0 - Rest) + (bFore ? -0.15 : 2.7) * Rest;
			const double Z0 = !bFore ? JointZ - Slope : JointZ;
			const Vec3 Joint((bFore ? 1.0 : -1.0) * L * (bFore ? 0.2 : 0.22), (bLeft ? 1.0 : -1.0) * W * 0.6, Z0);
			const double Upper = (Z0 - Bob) * 0.5, Lower = (Z0 - Bob) * 0.5;
			const Vec3 Knee = Joint + Vec3(std::sin(Angle), 0.0, -std::cos(Angle)) * Upper;
			const Vec3 Foot = Knee + Vec3(std::sin(Angle - Fold), 0.0, -std::cos(Angle - Fold)) * Lower;
			const double Thick = std::max(0.03, W * 0.22);
			Out.push_back(Along(PartKind::Leg, Joint, Knee, Thick * 1.25, Thick * 1.15));
			Out.push_back(Along(PartKind::Shin, Knee, Foot, Thick * 0.7, Thick * 0.65));
			Out.push_back({ PartKind::Hoof, Foot + Vec3(0.0, 0.0, Thick * 0.5), Vec3(Thick * 0.9, Thick * 0.75, Thick * 0.6), 0.0, 0.0 });
		}
		// the neck, from the chest's front to the head: up (looking), down at the grass, or up at a bush
		const Vec3 Base(L * 0.28, 0.0, TZ + TH * 0.55 + Bob);
		const double Down = std::clamp(P.HeadDown, -0.3, 1.0);
		const double UpZ = Sh * HeadUp(S);
		const double HeadZ = Down >= 0.0 ? UpZ + (0.18 - UpZ + Drop(S, P)) * Down : UpZ - Down * Sh * 0.35;
		const double HeadX = L * (0.45 + 0.06 * std::max(Down, 0.0));
		// (looking round: the head swung about the neck's base; it and all on it turned with it)
		const double Turn = P.Lying > 0.0 ? 0.0 : std::clamp(P.Turn, -1.2, 1.2);
		auto Looking = [Turn](const Vec3& V) { return Vec3(V.X * std::cos(Turn) - V.Y * std::sin(Turn), V.X * std::sin(Turn) + V.Y * std::cos(Turn), V.Z); };
		const Vec3 Head = Base + Looking(Vec3(HeadX, 0.0, HeadZ + Bob) - Base);
		Out.push_back(Along(PartKind::Neck, Base, Head, W * 0.42, W * 0.34));
		const size_t HeadFirst = Out.size();
		// the head (pointing down its face: towards the ground grazing), the muzzle out in front, the ears at the back
		const double HeadLength = std::max(0.12, L * 0.10);
		const double HeadPitch = -0.6 - 0.8 * std::max(Down, 0.0);
		const Vec3 Face(std::cos(HeadPitch), 0.0, std::sin(HeadPitch));
		Out.push_back({ PartKind::Head, Head + Face * (HeadLength * 0.35), Vec3(HeadLength * 0.6, std::max(0.045, W * 0.4), std::max(0.05, Sh * 0.075)), HeadPitch, 0.0 });
		Out.push_back({ PartKind::Muzzle, Head + Face * (HeadLength * 1.05), Vec3(HeadLength * 0.45, std::max(0.03, W * 0.28), std::max(0.035, Sh * 0.055)), HeadPitch, 0.0 });
		// (the ears and horns turn with the head: as it looks up, their way; grazing, tipped forward with it)
		const double Nod = HeadPitch + 0.6;
		auto Turned = [Nod](const Vec3& V) { return Vec3(V.X * std::cos(Nod) - V.Z * std::sin(Nod), V.Y, V.X * std::sin(Nod) + V.Z * std::cos(Nod)); };
		const double Ear = Shape.Ears;
		for (const double Side : { -1.0, 1.0 })
		{
			const Vec3 Root = Head + Turned(Vec3(-HeadLength * 0.15, Side * W * 0.32, Sh * 0.06));
			// (a flick: the ear back and down a moment)
			const double Flick = Side * P.Flick > 0.0 ? std::abs(P.Flick) : 0.0;
			const Vec3 Tip = Turned(Vec3(-0.02 - Flick * Sh * 0.05, Side * Sh * 0.06 * Ear * (1.0 + Flick * 0.5), Sh * 0.07 * Ear * (1.0 - Flick * 0.8)));
			Out.push_back(Along(PartKind::Ear, Root, Root + Tip, Sh * 0.012 + 0.008, Sh * 0.03 * Ear));
		}
		// horns: from the top of the head, in two lengths each (a gemsbok's long and straight; a kudu's out and round in
		// a spiral; an impala's a lyre; a buffalo's and a wildebeest's out to the sides, then up; a giraffe's two short
		// knobs; a rhino's on its nose)
		if (Shape.HornKind != BodyShape::Horns::None)
		{
			const Vec3 Top = Head + Turned(Vec3(-HeadLength * 0.1, 0.0, Sh * 0.07));
			if (Shape.HornKind == BodyShape::Horns::Nose)
			{
				const Vec3 Nose = Head + Face * (HeadLength * 1.2) + Turned(Vec3(0.0, 0.0, Sh * 0.04));
				Out.push_back(Along(PartKind::Horn, Nose, Nose + Turned(Vec3(0.12, 0.0, 0.55)), 0.07, 0.07));
				Out.push_back(Along(PartKind::Horn, Nose - Face * 0.25, Nose - Face * 0.25 + Turned(Vec3(0.0, 0.0, 0.2)), 0.05, 0.05));
			}
			else
			{
				const double Length = Shape.HornLength > 0.0 ? Shape.HornLength : std::clamp(0.15 + S.Mass * 0.0015, 0.12, 0.4);
				// (each length's lean back from upright and out to the side, rad: the first, then the second)
				struct Bend { double Back1, Out1, Back2, Out2; };
				const Bend B{ Shape.Bend[0], Shape.Bend[1], Shape.Bend[2], Shape.Bend[3] };
				auto Way = [&Turned](double Back, double Side, double Out) { return Turned(Vec3(-std::sin(Back) * std::cos(Out), Side * std::sin(Out), std::cos(Back) * std::cos(Out))); };
				const double Thick = std::max(0.02, Length * 0.07);
				for (const double Side : { -1.0, 1.0 })
				{
					const Vec3 Root = Top + Vec3(0.0, Side * W * 0.18, 0.0);
					const Vec3 Mid = Root + Way(B.Back1, Side, B.Out1) * (Length * 0.5);
					const Vec3 Tip = Mid + Way(B.Back2, Side, B.Out2) * (Length * 0.5);
					Out.push_back(Along(PartKind::Horn, Root, Mid + (Mid - Root) * 0.25, Thick, Thick));	// (a little past the bend: no gap at it)
					Out.push_back(Along(PartKind::Horn, Mid, Tip, Thick * 0.62, Thick * 0.62));
				}
			}
		}
		// (the head's parts built facing ahead, turned with it round the head)
		for (size_t k = HeadFirst; k < Out.size(); ++k)
		{
			Out[k].Centre = Head + Looking(Out[k].Centre - Head);
			Out[k].Yaw = Turn;
		}
		// manes: a lion's round its neck and the back of its head; a zebra's and a wildebeest's a crest along the neck
		if (Shape.ManeKind == BodyShape::Mane::Lion)
		{
			Out.push_back(Along(PartKind::Mane, Base + (Head - Base) * 0.25, Head - Looking(Face) * (HeadLength * 0.1), Sh * 0.21, Sh * 0.22));
		}
		else if (Shape.ManeKind == BodyShape::Mane::Crest)
		{
			const Vec3 N = (Head - Base).Normal();
			const Vec3 Up = Vec3(-N.Z, 0.0, N.X);
			Out.push_back(Along(PartKind::Mane, Base + Up * (W * 0.38), Head + Up * (W * 0.3) - N * (HeadLength * 0.3), W * 0.09, W * 0.06));
		}
		// the tail: hanging from the rump (a lion's, a zebra's and a wildebeest's long, with a tuft)
		const bool bLong = Shape.bLongTail;
		const Vec3 Dock(-L * 0.35, 0.0, TZ + TH * 0.6 + Bob);
		const double TailLength = bLong ? std::min(Sh * 0.55, 0.9) : Sh * 0.18;
		// (swished: out to a side and up a little)
		const Vec3 TailEnd = Dock + Vec3(-0.08, P.Tail * TailLength * 0.45, -TailLength * (1.0 - 0.25 * std::abs(P.Tail)));
		Out.push_back(Along(PartKind::Tail, Dock, TailEnd, 0.025, 0.025));
		// down on its side: everything rolled over its feet's line and lifted onto its flank
		if (P.Lying > 0.0)
		{
			const double R = P.Lying * Pi * 0.5;
			for (BodyPart& B : Out)
			{
				B.Centre = RollAboutX(B.Centre, R) + Vec3(0.0, 0.0, W * P.Lying);
				B.Roll += R;
			}
		}
		return Out;
	}

	namespace
	{
		// a point into a part's own frame (its roll about x, then its pitch about y, undone)
		Vec3 IntoPart(const BodyPart& B, const Vec3& P)
		{
			Vec3 L = P - B.Centre;
			L = Vec3(L.X * std::cos(B.Yaw) + L.Y * std::sin(B.Yaw), -L.X * std::sin(B.Yaw) + L.Y * std::cos(B.Yaw), L.Z);
			L = RollAboutX(L, -B.Roll);
			const double C = std::cos(-B.Pitch), S = std::sin(-B.Pitch);
			return { L.X * C - L.Z * S, L.Y, L.X * S + L.Z * C };
		}

		// where a segment first enters an ellipsoid (its own frame, half sizes Half): 0-1 along it, or <0 not at all
		double SegmentEllipsoid(const Vec3& A, const Vec3& B, const Vec3& Half)
		{
			const Vec3 P(A.X / Half.X, A.Y / Half.Y, A.Z / Half.Z);
			const Vec3 D((B.X - A.X) / Half.X, (B.Y - A.Y) / Half.Y, (B.Z - A.Z) / Half.Z);
			const double a = D.Dot(D), b = 2.0 * P.Dot(D), c = P.Dot(P) - 1.0;
			if (c <= 0.0)
			{
				return 0.0;	// (starts inside)
			}
			const double Disc = b * b - 4.0 * a * c;
			if (a < 1e-12 || Disc < 0.0)
			{
				return -1.0;
			}
			const double T = (-b - std::sqrt(Disc)) / (2.0 * a);
			return T >= 0.0 && T <= 1.0 ? T : -1.0;
		}
	}

	bool HitBody(const Species& S, const Pose& P, const Vec3& From, const Vec3& To, Vec3& Where, Zone& Struck)
	{
		double Best = 2.0;
		const BodyPart* Hit = nullptr;
		const std::vector<BodyPart> Parts = Body(S, P);
		for (const BodyPart& B : Parts)
		{
			const double T = SegmentEllipsoid(IntoPart(B, From), IntoPart(B, To), B.Half);
			if (T >= 0.0 && T < Best)
			{
				Best = T;
				Hit = &B;
			}
		}
		if (!Hit)
		{
			return false;
		}
		Where = From + (To - From) * Best;
		switch (Hit->Kind)
		{
		case PartKind::Head:
			Struck = Zone::Brain;
			break;
		case PartKind::Neck:
		case PartKind::Mane:
			Struck = Zone::Neck;
			break;
		case PartKind::Leg:
		case PartKind::Shin:
		case PartKind::Hoof:
		case PartKind::Muzzle:	// (a jaw: a bad wound, not a kill)
			Struck = Zone::Leg;
			break;
		case PartKind::Torso:
		case PartKind::Rump:
		{
			// (upright: the chest the heart and lungs, the hips the gut)
			if (S.Shape.Build == BodyShape::Frame::Upright)
			{
				Struck = Hit->Kind == PartKind::Torso ? Zone::HeartLung : Zone::Gut;
				break;
			}
			// (carried on into the body a little: the vitals are behind the shoulder, low; the spine along the top; the rest
			// gut; standing, in the animal's own frame)
			Vec3 In = Where + (To - From).Normal() * 0.15;
			if (P.Lying > 0.0)
			{
				In = RollAboutX(In - Vec3(0.0, 0.0, Width(S) * P.Lying), -P.Lying * Pi * 0.5);
			}
			In.Z += Drop(S, P);
			const double TZ = TorsoZ(S), TH = TorsoHalf(S);
			if (In.Z > TZ + TH * 0.62)
			{
				Struck = Zone::Spine;
			}
			else if (In.X > -S.Length * 0.02 && In.X < S.Length * 0.3 && In.Z < TZ + TH * 0.45)
			{
				Struck = Zone::HeartLung;
			}
			else
			{
				Struck = Zone::Gut;
			}
			break;
		}
		default:
			Struck = Zone::None;	// (a horn, an ear, the tail: no harm)
			break;
		}
		return true;
	}

	Effect Outcome(const Species& S, Zone Z, double EnergyFtLb, std::mt19937& Random)
	{
		auto Uniform = [&Random](double A, double B) { return std::uniform_real_distribution<double>(A, B)(Random); };
		Effect Out;
		// the energy a bullet should bring for a clean kill (as hunters reckon it): ~300 ft-lb small, ~800 medium, ~1,500
		// large, ~2,000 over 300 kg, ~4,000 the very big; an arrow's the bow-hunting guideline's (it kills by cutting)
		const bool bArrow = EnergyFtLb < 150.0;
		const SizeClass C = SizeOf(S);
		const double Need = bArrow ? (C == SizeClass::Small ? 30.0 : C == SizeClass::Medium ? 50.0 : 60.0)
			: S.Mass > 800.0 ? 4000.0 : S.Mass > 300.0 ? 2000.0 : C == SizeClass::Large ? 1500.0 : C == SizeClass::Medium ? 800.0 : 300.0;
		const double Enough = EnergyFtLb / Need;
		switch (Z)
		{
		case Zone::Brain:
			if (Enough >= 0.3)
			{
				Out.bDown = true;
				Out.DiesAfter = 0.0;
			}
			else
			{
				Out.bWounded = true;
			}
			break;
		case Zone::Spine:
		case Zone::Neck:
			if (Enough >= 0.5)
			{
				Out.bDown = true;
				Out.DiesAfter = Uniform(2.0, 15.0);
			}
			else
			{
				Out.bWounded = true;
				Out.DiesAfter = Uniform(600.0, 1800.0);
			}
			break;
		case Zone::HeartLung:
			Out.bWounded = true;
			Out.DiesAfter = Enough >= 1.0 ? (bArrow ? Uniform(8.0, 30.0) : Uniform(4.0, 15.0)) : Enough >= 0.5 ? Uniform(20.0, 60.0) : Uniform(600.0, 1200.0);
			break;
		case Zone::Gut:
			Out.bWounded = true;
			Out.DiesAfter = Uniform(900.0, 2400.0);
			break;
		case Zone::Leg:
			Out.bWounded = true;
			break;
		default:
			break;
		}
		if (Z != Zone::None && !(S.Acts.Vital & (1u << uint32_t(Z))))
		{
			// (not a vital zone for this creature: hurt, no more)
			Out = Effect();
			Out.bWounded = true;
		}
		return Out;
	}

	// --- the animals' days ---

	const char* Name(Doing D)
	{
		switch (D)
		{
		case Doing::Walk: return "walking";
		case Doing::Graze: return "grazing";
		case Doing::Browse: return "browsing";
		case Doing::Drink: return "drinking";
		case Doing::Look: return "looking up";
		case Doing::Flee: return "running";
		case Doing::Stalk: return "stalking";
		case Doing::Chase: return "chasing";
		case Doing::Feed: return "feeding";
		case Doing::Dead: return "dead";
		case Doing::Pursue: return "coming for you";
		default: return "?";
		}
	}

	void Range::Block(const std::vector<Vec2>& Polygon)
	{
		if (Polygon.size() < 3 || Outline.empty())
		{
			return;
		}
		if (Blocked.empty())
		{
			Vec2 Min = Outline[0], Max = Outline[0];
			for (const Vec2& P : Outline)
			{
				Min = Vec2(std::min(Min.X, P.X), std::min(Min.Y, P.Y));
				Max = Vec2(std::max(Max.X, P.X), std::max(Max.Y, P.Y));
			}
			GridMin = Min;
			Columns = int(std::ceil((Max.X - Min.X) / Cell)) + 1;
			Rows = int(std::ceil((Max.Y - Min.Y) / Cell)) + 1;
			Blocked.assign(size_t(Columns) * size_t(Rows), 0);
		}
		Vec2 Low = Polygon[0], High = Polygon[0];
		for (const Vec2& P : Polygon)
		{
			Low = Vec2(std::min(Low.X, P.X), std::min(Low.Y, P.Y));
			High = Vec2(std::max(High.X, P.X), std::max(High.Y, P.Y));
		}
		// each square whose middle's inside it (the polygon's points in order round it)
		const int X0 = std::max(0, int(std::floor((Low.X - GridMin.X) / Cell))), X1 = std::min(Columns - 1, int(std::floor((High.X - GridMin.X) / Cell)));
		const int Y0 = std::max(0, int(std::floor((Low.Y - GridMin.Y) / Cell))), Y1 = std::min(Rows - 1, int(std::floor((High.Y - GridMin.Y) / Cell)));
		for (int Y = Y0; Y <= Y1; ++Y)
		{
			for (int X = X0; X <= X1; ++X)
			{
				const Vec2 Q = GridMin + Vec2((X + 0.5) * Cell, (Y + 0.5) * Cell);
				bool bIn = false;
				for (size_t i = 0, j = Polygon.size() - 1; i < Polygon.size(); j = i++)
				{
					const Vec2& A = Polygon[i];
					const Vec2& B = Polygon[j];
					if ((A.Y > Q.Y) != (B.Y > Q.Y) && Q.X < (B.X - A.X) * (Q.Y - A.Y) / (B.Y - A.Y) + A.X)
					{
						bIn = !bIn;
					}
				}
				if (bIn)
				{
					Blocked[size_t(Y) * size_t(Columns) + size_t(X)] = 1;
				}
			}
		}
		// (and every square its walls pass through: one only partly in it is still in it)
		for (size_t i = 0, j = Polygon.size() - 1; i < Polygon.size(); j = i++)
		{
			const Vec2 A = Polygon[j], B = Polygon[i];
			const int Steps = int(std::ceil((B - A).Length() / (Cell * 0.5))) + 1;
			for (int k = 0; k <= Steps; ++k)
			{
				const Vec2 P = A + (B - A) * (double(k) / Steps);
				const int X = int(std::floor((P.X - GridMin.X) / Cell)), Y = int(std::floor((P.Y - GridMin.Y) / Cell));
				if (X >= 0 && Y >= 0 && X < Columns && Y < Rows)
				{
					Blocked[size_t(Y) * size_t(Columns) + size_t(X)] = 1;
				}
			}
		}
	}

	bool Range::Inside(const Vec2& P) const
	{
		if (!Blocked.empty())
		{
			const int X = int(std::floor((P.X - GridMin.X) / Cell)), Y = int(std::floor((P.Y - GridMin.Y) / Cell));
			if (X >= 0 && Y >= 0 && X < Columns && Y < Rows && Blocked[size_t(Y) * size_t(Columns) + size_t(X)])
			{
				return false;
			}
		}
		bool bIn = false;
		for (size_t i = 0, j = Outline.size() - 1; i < Outline.size(); j = i++)
		{
			const Vec2& A = Outline[i];
			const Vec2& B = Outline[j];
			if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X)
			{
				bIn = !bIn;
			}
		}
		return bIn;
	}

	double Herds::Uniform(double A, double B) { return std::uniform_real_distribution<double>(A, B)(Random); }

	bool Herds::ClearPath(const Vec2& A, const Vec2& B) const
	{
		// (straight there without leaving the farm: the outline's concave in places)
		for (int i = 1; i <= 10; ++i)
		{
			if (!Farm.Inside(A + (B - A) * (i / 10.0)))
			{
				return false;
			}
		}
		return true;
	}

	Vec2 Herds::SomewhereInside(const Vec2& Near, double Within)
	{
		for (int Try = 0; Try < 60; ++Try)
		{
			const double Angle = Uniform(0.0, 2.0 * Pi), Distance = Within * std::sqrt(Uniform(0.0, 1.0));
			const Vec2 P = Near + Vec2(std::cos(Angle), std::sin(Angle)) * Distance;
			if (Farm.Inside(P) && ClearPath(Near, P))
			{
				return P;
			}
		}
		return Near;
	}

	void Herds::Start(const Range& InFarm, const std::vector<std::pair<int, int>>& Stock, uint32_t Seed)
	{
		Farm = InFarm;
		Random.seed(Seed);
		All.clear();
		Members.clear();
		Clock = 0.0;
		Vec2 Min = Farm.Outline.empty() ? Vec2() : Farm.Outline[0], Max = Min;
		for (const Vec2& P : Farm.Outline)
		{
			Min = Vec2(std::min(Min.X, P.X), std::min(Min.Y, P.Y));
			Max = Vec2(std::max(Max.X, P.X), std::max(Max.Y, P.Y));
		}
		const auto& Kinds = AllSpecies();
		for (const auto& [Kind, Count] : Stock)
		{
			const Species& S = Kinds[size_t(Kind)];
			int Left = Count;
			while (Left > 0)
			{
				const int Size = std::min(Left, int(std::round(Uniform(S.HerdMin, S.HerdMax))));
				Left -= std::max(Size, 1);
				// the herd's place: anywhere inside; its animals round it
				Vec2 Centre;
				for (int Try = 0; Try < 200; ++Try)
				{
					Centre = Vec2(Uniform(Min.X, Max.X), Uniform(Min.Y, Max.Y));
					if (Farm.Inside(Centre))
					{
						break;
					}
				}
				const int Herd = int(Members.size());
				Members.emplace_back();
				for (int i = 0; i < std::max(Size, 1); ++i)
				{
					Animal A;
					A.Species = Kind;
					A.Herd = Herd;
					A.At = SomewhereInside(Centre, 25.0);
					A.Heading = Uniform(0.0, 2.0 * Pi);
					A.Target = A.At;
					A.What = S.Acts.Browsing >= 1.0 ? Doing::Browse : Doing::Graze;
					A.Until = Uniform(1.0, 30.0);
					A.Now.HeadDown = A.What == Doing::Graze ? 1.0 : -0.3;
					A.NextLook = Uniform(5.0, 20.0);
					A.Thirst = Uniform(0.0, 0.8);
					A.HuntAt = Uniform(S.Acts.HuntMin, S.Acts.HuntMax);	// (a hunter's first hunt: a lion's in 8 to 15 minutes)
					Members.back().push_back(int(All.size()));
					All.push_back(A);
				}
			}
		}
	}

	int Herds::Leader(int Herd) const
	{
		for (const int k : Members[size_t(Herd)])
		{
			if (All[size_t(k)].What != Doing::Dead && All[size_t(k)].DiesAt < 0.0)
			{
				return k;
			}
		}
		return -1;
	}

	Vec2 Herds::Middle(int Herd) const
	{
		Vec2 Sum;
		int N = 0;
		for (const int k : Members[size_t(Herd)])
		{
			if (All[size_t(k)].What != Doing::Dead)
			{
				Sum = Sum + All[size_t(k)].At;
				++N;
			}
		}
		return N > 0 ? Sum * (1.0 / N) : Vec2();
	}

	// A herd off somewhere: its leader there, the others round it, after a moment each; then what they'll do there
	void Herds::Lead(int Herd, const Vec2& To, Doing Then)
	{
		for (size_t k = 0; k < Members[size_t(Herd)].size(); ++k)
		{
			Animal& A = All[size_t(Members[size_t(Herd)][k])];
			if (A.What == Doing::Dead || A.What == Doing::Flee || A.DiesAt >= 0.0)
			{
				continue;
			}
			A.Target = k == 0 ? To : SomewhereInside(To, 18.0);
			A.What = Doing::Walk;
			A.Until = Clock + (k == 0 ? 0.0 : Uniform(0.5, 6.0));	// (the others set off a moment later)
			A.bToDrink = Then == Doing::Drink;
		}
	}

	// What an animal does next, once what it was doing is over
	void Herds::Decide(int Index)
	{
		Animal& A = All[size_t(Index)];
		const Species& S = AllSpecies()[size_t(A.Species)];
		const bool bLeader = Leader(A.Herd) == Index;
		switch (A.What)
		{
		case Doing::Walk:
			// arrived: drink if that's why it came, else eat (a grazer at the grass, a browser at a bush) or look round
			if (A.bToDrink)
			{
				A.What = Doing::Drink;
				A.Until = Clock + Uniform(20.0, 45.0);
				A.Thirst = 0.0;
				A.bToDrink = false;
			}
			else if (Clock < A.WaryUntil || Uniform(0.0, 1.0) < 0.15)
			{
				// (back with the herd after a scare: stood looking that way a while)
				A.What = Doing::Look;
				A.Until = Clock + std::max(Uniform(2.0, 5.0), A.WaryUntil - Clock);
			}
			else
			{
				const bool bBrowse = S.Acts.Browsing >= 1.0 || (S.Acts.Browsing > 0.0 && Uniform(0.0, 1.0) < S.Acts.Browsing);
				A.What = bBrowse ? Doing::Browse : Doing::Graze;
				A.Until = Clock + Uniform(S.Acts.MealMin, S.Acts.MealMax);	// (minutes at a patch: grazers spend most of the day eating)
				A.NextLook = Clock + Uniform(6.0, 20.0);
			}
			break;
		default:
			// done eating, drinking or looking: the leader takes the herd on (to water if they're thirsty); the others
			// carry on till it does
			if (bLeader)
			{
				if (A.Thirst >= 1.0 && !Farm.Water.empty())
				{
					Vec2 Nearest = Farm.Water[0];
					for (const Vec2& W : Farm.Water)
					{
						Nearest = (W - A.At).Length() < (Nearest - A.At).Length() ? W : Nearest;
					}
					Lead(A.Herd, SomewhereInside(Nearest, 6.0), Doing::Drink);
				}
				else
				{
					Lead(A.Herd, SomewhereInside(A.At, Uniform(15.0, 90.0)), Doing::Graze);	// (on to the next patch nearby)
				}
			}
			else
			{
				A.What = S.Acts.Browsing >= 1.0 ? Doing::Browse : Doing::Graze;
				A.Until = Clock + Uniform(20.0, 60.0);
			}
			break;
		}
	}

	void Herds::Flee(int Index, const Vec2& From, double For)
	{
		Animal& A = All[size_t(Index)];
		if (A.What == Doing::Dead)
		{
			return;
		}
		Vec2 Away = (A.At - From).Normal();
		if (Away.Length() < 0.5)
		{
			Away = Vec2(std::cos(A.Heading), std::sin(A.Heading));
		}
		A.What = Doing::Flee;
		A.Target = A.At + Away * 400.0;
		A.Until = Clock + For;
		A.bToDrink = false;
	}

	Effect Herds::Shot(int Index, const Vec2& From, const Effect* Given)
	{
		if (Index < 0 || Index >= int(All.size()) || All[size_t(Index)].What == Doing::Dead)
		{
			return Given ? *Given : Effect();
		}
		Animal& A = All[size_t(Index)];
		// (a wound that doesn't kill it: one more; enough of them and it's down)
		Effect Applied = Given ? *Given : Effect();
		const Effect* Hit = Given ? &Applied : nullptr;
		const int ToDrop = AllSpecies()[size_t(A.Species)].Acts.WoundsToDrop;
		if (Given && !Applied.bDown && Applied.DiesAfter < 0.0 && Applied.bWounded && ToDrop > 0 && ++A.Wounds >= ToDrop)
		{
			Applied.bDown = true;
			Applied.DiesAfter = 0.0;
		}
		if (Hit)
		{
			A.bWounded = A.bWounded || Hit->bWounded || Hit->bDown;
			A.bShot = true;
			if (Hit->bDown)
			{
				A.What = Doing::Dead;
				A.Speed = 0.0;
				A.DiesAt = Clock + std::max(Hit->DiesAfter, 0.0);
				News.push_back(std::string(AllSpecies()[size_t(A.Species)].Name) + " down");
			}
			else if (Hit->DiesAfter >= 0.0)
			{
				A.DiesAt = A.DiesAt >= 0.0 ? std::min(A.DiesAt, Clock + Hit->DiesAfter) : Clock + Hit->DiesAfter;
			}
		}
		// the others round about hear it; it and its herd run from the shot
		Heard(From);
		for (const int Other : Members[size_t(A.Herd)])
		{
			if (All[size_t(Other)].What != Doing::Dead)
			{
				All[size_t(Other)].Threat = From;
				const Behaviour& Acts = AllSpecies()[size_t(All[size_t(Other)].Species)].Acts;
				if (Acts.bFlees)
				{
					Flee(Other, From, Uniform(Acts.FleeMin, Acts.FleeMax) + (Other == Index && A.DiesAt >= 0.0 ? 60.0 : 0.0));
				}
			}
		}
		return Applied;
	}

	// The pool round the people: one far from them all, or dead a while, put back among them (a ring round one of them,
	// on open ground), as if new
	void Herds::Recycle()
	{
		for (Animal& A : All)
		{
			double Nearest = 1e18;
			for (const Vec2& P : People)
			{
				Nearest = std::min(Nearest, (P - A.At).Length());
			}
			const bool bGone = A.What == Doing::Dead ? Clock - std::max(A.DiesAt, 0.0) > Spawn.DeadFor : Nearest > Spawn.LeavePast;
			if (bGone)
			{
				PutAmong(A);
			}
		}
	}

	void Herds::PutBack(const Vec2& Near, double Within)
	{
		if (!Spawn.bAroundPeople || People.empty())
		{
			return;
		}
		for (Animal& A : All)
		{
			if (A.What != Doing::Dead && (A.At - Near).Length() < Within)
			{
				PutAmong(A);
			}
		}
	}

	// One put back among the people (a ring round one of them, on open ground), as if new: false if nowhere was found
	bool Herds::PutAmong(Animal& A)
	{
		for (int Try = 0; Try < 30; ++Try)
		{
			const Vec2& Who = People[size_t(std::uniform_int_distribution<size_t>(0, People.size() - 1)(Random))];
			const double Angle = Uniform(0.0, 2.0 * Pi), Distance = Uniform(Spawn.Nearest, Spawn.Farthest);
			const Vec2 At = Who + Vec2(std::cos(Angle), std::sin(Angle)) * Distance;
			bool bClear = Farm.Inside(At);
			for (const Vec2& P : People)
			{
				bClear = bClear && (P - At).Length() >= Spawn.Nearest;
			}
			if (!bClear)
			{
				continue;
			}
			// (as new: where it is, standing about, unhurt)
			Animal Fresh;
			Fresh.Species = A.Species;
			Fresh.Herd = A.Herd;
			Fresh.At = At;
			Fresh.Target = At;
			Fresh.Heading = Uniform(0.0, 2.0 * Pi);
			Fresh.What = Doing::Graze;
			Fresh.Until = Clock + Uniform(5.0, 30.0);
			Fresh.NextLook = Clock + Uniform(5.0, 20.0);
			Fresh.HuntAt = A.HuntAt;
			Fresh.PursueAt = Clock;
			A = Fresh;
			return true;
		}
		return false;
	}

	namespace
	{
		// (a byte for -1..1, or 0..1)
		uint8_t Byte(double V, double Low, double High) { return uint8_t(std::clamp((V - Low) / (High - Low), 0.0, 1.0) * 255.0 + 0.5); }
		double Unbyte(uint8_t B, double Low, double High) { return Low + (High - Low) * B / 255.0; }
		template <typename T> void Put(std::vector<uint8_t>& Out, T V)
		{
			const uint8_t* P = reinterpret_cast<const uint8_t*>(&V);
			Out.insert(Out.end(), P, P + sizeof(T));
		}
		template <typename T> T Get(const uint8_t*& P)
		{
			T V;
			std::memcpy(&V, P, sizeof(T));
			P += sizeof(T);
			return V;
		}
		constexpr size_t PackedSize = 4 + 4 + 2 + 1 + 1 + 9 + 1;
	}

	std::vector<uint8_t> Herds::Pack() const
	{
		std::vector<uint8_t> Out;
		Out.reserve(4 + All.size() * PackedSize);
		Put<uint32_t>(Out, uint32_t(All.size()));
		for (const Animal& A : All)
		{
			Put<float>(Out, float(A.At.X));
			Put<float>(Out, float(A.At.Y));
			Put<uint16_t>(Out, uint16_t(std::remainder(A.Heading, 2.0 * Pi) / (2.0 * Pi) * 65535.0 + 32768.0));
			Out.push_back(Byte(A.Speed, 0.0, 30.0));
			Out.push_back(uint8_t(A.What));
			const Pose& P = A.Now;
			for (const uint8_t B : { Byte(P.HeadDown, -0.3, 1.0), Byte(P.Lying, 0.0, 1.0), Byte(P.Pace, 0.0, 1.0), Byte(P.Stepping, 0.0, 1.0), Byte(P.Rest, 0.0, 1.0),
				Byte(P.Turn, -1.2, 1.2), Byte(P.Tail, -1.0, 1.0), Byte(P.Flick, -1.0, 1.0), Byte(std::fmod(P.Gait, 2.0 * Pi), 0.0, 2.0 * Pi) })
			{
				Out.push_back(B);
			}
			Out.push_back(uint8_t((A.bShot ? 1 : 0) | (A.bWounded ? 2 : 0) | (A.DiesAt >= 0.0 ? 4 : 0)));
		}
		return Out;
	}

	bool Herds::Unpack(const std::vector<uint8_t>& Bytes)
	{
		if (Bytes.size() < 4)
		{
			return false;
		}
		const uint8_t* P = Bytes.data();
		const uint32_t Count = Get<uint32_t>(P);
		if (Count != All.size() || Bytes.size() != 4 + Count * PackedSize)
		{
			return false;
		}
		for (Animal& A : All)
		{
			const double X = Get<float>(P), Y = Get<float>(P);
			// (near where it was carried to: eased there, not jumped; far off (just joined, a jump): put there)
			const Vec2 To(X, Y);
			A.At = (To - A.At).Length() > 5.0 ? To : A.At + (To - A.At) * 0.5;
			A.Heading = (double(Get<uint16_t>(P)) - 32768.0) / 65535.0 * 2.0 * Pi;
			A.Speed = Unbyte(*P++, 0.0, 30.0);
			A.What = Doing(*P++);
			Pose& Now = A.Now;
			Now.HeadDown = Unbyte(*P++, -0.3, 1.0);
			Now.Lying = Unbyte(*P++, 0.0, 1.0);
			Now.Pace = Unbyte(*P++, 0.0, 1.0);
			Now.Stepping = Unbyte(*P++, 0.0, 1.0);
			Now.Rest = Unbyte(*P++, 0.0, 1.0);
			Now.Turn = Unbyte(*P++, -1.2, 1.2);
			Now.Tail = Unbyte(*P++, -1.0, 1.0);
			Now.Flick = Unbyte(*P++, -1.0, 1.0);
			const double Gait = Unbyte(*P++, 0.0, 2.0 * Pi);
			// (the legs kept swinging smoothly: only pulled into step if they've drifted far)
			if (std::abs(std::remainder(Gait - Now.Gait, 2.0 * Pi)) > 1.0)
			{
				Now.Gait = Gait;
			}
			const uint8_t Flags = *P++;
			A.bShot = (Flags & 1) != 0;
			A.bWounded = (Flags & 2) != 0;
			A.DiesAt = (Flags & 4) != 0 ? std::max(A.DiesAt, 0.0) : -1.0;
		}
		return true;
	}

	void Herds::Follow(double Dt)
	{
		Clock += Dt;
		const auto& Kinds = AllSpecies();
		for (Animal& A : All)
		{
			if (A.What == Doing::Dead)
			{
				continue;
			}
			const Vec2 Step = Vec2(std::cos(A.Heading), std::sin(A.Heading)) * (A.Speed * Dt);
			if (Farm.Inside(A.At + Step))
			{
				A.At = A.At + Step;
			}
			const Species& S = Kinds[size_t(A.Species)];
			A.Now.Gait += A.Speed * Dt / (S.Shoulder * (1.6 + 1.4 * A.Now.Pace)) * 2.0 * Pi;
		}
	}

	void Herds::Heard(const Vec2& From)
	{
		// (a rifle's heard for kilometres; game within ~400 m takes notice: heads up, looking that way, a while)
		const auto& Kinds = AllSpecies();
		for (Animal& A : All)
		{
			const Behaviour& Acts = Kinds[size_t(A.Species)].Acts;
			if (A.What == Doing::Dead || A.What == Doing::Flee || A.What == Doing::Stalk || A.What == Doing::Chase || (A.At - From).Length() > Acts.Hears)
			{
				continue;
			}
			A.Threat = From;
			A.WaryUntil = Clock + Uniform(Acts.WaryMin, Acts.WaryMax);
			if (A.What != Doing::Feed)
			{
				A.What = Doing::Look;
				A.Until = A.WaryUntil;
			}
		}
	}

	// Alive between times: the tail swished at flies every few seconds, an ear flicked, the head turned to look round
	// (wide with its head up, a little while grazing; towards a danger when wary); all easing, none in step with the next
	void Herds::Fidget(Animal& A, const Species& S, double HeadWanted, double Dt)
	{
		if (Clock >= A.NextSwish)
		{
			A.SwishFrom = Clock;
			A.NextSwish = Clock + Uniform(2.0, 9.0);
		}
		const double Swish = Clock - A.SwishFrom;
		A.Now.Tail = Swish < 1.2 ? std::sin(Swish / 1.2 * 3.0 * Pi) * std::sin(Swish / 1.2 * Pi) : 0.0;
		if (Clock >= A.NextFlick)
		{
			A.FlickFrom = Clock;
			A.FlickSide = Uniform(0.0, 1.0) < 0.5 ? -1.0 : 1.0;
			A.NextFlick = Clock + Uniform(2.0, 12.0);
		}
		const double Flick = Clock - A.FlickFrom;
		A.Now.Flick = Flick < 0.3 ? A.FlickSide * std::sin(Flick / 0.3 * Pi) : 0.0;
		const bool bUp = HeadWanted < 0.5;
		if (Clock >= A.NextTurn)
		{
			A.TurnWanted = bUp ? Uniform(-0.8, 0.8) : Uniform(-0.25, 0.25);
			A.NextTurn = Clock + (bUp ? Uniform(1.2, 4.0) : Uniform(3.0, 8.0));
		}
		double Want = A.Speed > S.Walk * 1.5 ? 0.0 : A.TurnWanted;
		if (Clock < A.WaryUntil && bUp)
		{
			const Vec2 To = A.Threat - A.At;
			Want = std::clamp(std::remainder(std::atan2(To.Y, To.X) - A.Heading, 2.0 * Pi), -1.0, 1.0);
		}
		A.Now.Turn += std::clamp(Want - A.Now.Turn, -Dt * 2.0, Dt * 2.0);
	}

	void Herds::Move(Animal& A, double Desired, const Vec2& Towards, double Dt)
	{
		const Species& S = AllSpecies()[size_t(A.Species)];
		const Vec2 To = Towards - A.At;
		if (To.Length() > 0.3)
		{
			// turning towards it (faster running), keeping inside the fence: a step out, it turns along it
			double Want = std::atan2(To.Y, To.X);
			double Turn = std::remainder(Want - A.Heading, 2.0 * Pi);
			const double Rate = (Desired > S.Walk * 1.5 ? 3.0 : 1.6) * Dt;
			A.Heading += std::clamp(Turn, -Rate, Rate);
		}
		const double Accel = (Desired > A.Speed ? 4.0 : 6.0) * Dt;
		A.Speed += std::clamp(Desired - A.Speed, -Accel, Accel);
		Vec2 Step = Vec2(std::cos(A.Heading), std::sin(A.Heading)) * (A.Speed * Dt);
		if (!Farm.Inside(A.At + Step * 4.0))
		{
			for (const double Try : { 0.5, -0.5, 1.0, -1.0, 1.6, -1.6, 2.4, -2.4, Pi })
			{
				const Vec2 Side = Vec2(std::cos(A.Heading + Try), std::sin(A.Heading + Try)) * (A.Speed * Dt);
				if (Farm.Inside(A.At + Side * 4.0))
				{
					A.Heading += Try * std::min(1.0, Dt * 4.0);
					Step = Side;
					break;
				}
			}
		}
		if (Farm.Inside(A.At + Step))
		{
			A.At = A.At + Step;
		}
		else
		{
			A.Speed = 0.0;
		}
		// the legs: a walk's stride ~1.6 shoulder heights, a gallop's ~3; the pace eases from one to the other
		const double Pace = std::clamp((A.Speed - S.Walk * 1.3) / (S.Run * 0.6 - S.Walk * 1.3), 0.0, 1.0);
		A.Now.Pace += std::clamp(Pace - A.Now.Pace, -Dt * 2.0, Dt * 2.0);
		const double Stride = S.Shoulder * (1.6 + 1.4 * A.Now.Pace);
		A.Now.Gait += A.Speed * Dt / Stride * 2.0 * Pi;
	}

	void Herds::Step(double Dt)
	{
		Clock += Dt;
		const auto& Kinds = AllSpecies();
		for (size_t i = 0; i < All.size(); ++i)
		{
			Animal& A = All[i];
			const Species& S = Kinds[size_t(A.Species)];
			// down: lying still (falling over once it dies)
			if (A.What == Doing::Dead)
			{
				A.Speed = 0.0;
				if (A.DiesAt < 0.0 || Clock >= A.DiesAt)
				{
					A.Now.Lying = std::min(1.0, A.Now.Lying + Dt * 1.5);
				}
				continue;
			}
			// hit: falls where it is when its time comes
			if (A.DiesAt >= 0.0 && Clock >= A.DiesAt)
			{
				A.What = Doing::Dead;
				News.push_back(std::string(S.Name) + (A.bShot ? " fell" : " died"));
				continue;
			}
			if (S.Acts.ThirstMax > 0.0)
			{
				A.Thirst += Dt / Uniform(S.Acts.ThirstMin, S.Acts.ThirstMax);	// (thirsty again every ~10 minutes)
			}
			double Desired = 0.0;
			Vec2 Towards = A.Target;
			double HeadWanted = 0.0, RestWanted = 0.0;
			switch (A.What)
			{
			case Doing::Walk:
				if (Clock < A.Until)
				{
					break;	// (setting off a moment after the leader)
				}
				Desired = S.Walk;
				if ((A.Target - A.At).Length() < 1.5)
				{
					// (coming back together after a scare: on to where the others are now, till it's among them)
					const Vec2 Mid = Middle(A.Herd);
					if (A.bRegroup && (Mid - A.At).Length() > 6.0 + 2.5 * std::sqrt(double(Members[size_t(A.Herd)].size())))
					{
						A.Target = SomewhereInside(Mid, 6.0);
						break;
					}
					A.bRegroup = false;
					Decide(int(i));
				}
				break;
			case Doing::Graze:
			case Doing::Drink:
				HeadWanted = 1.0;
				// (a lion "grazing": lying at rest, its head up)
				if (S.Acts.bRests && A.What == Doing::Graze)
				{
					HeadWanted = 0.0;
					RestWanted = 1.0;
					break;
				}
				// grazing on: a few slow steps every few seconds, head down, to the next mouthful (back towards the
				// herd if it's strayed from it)
				if (A.What == Doing::Graze)
				{
					if (Clock >= A.NextStep)
					{
						A.StepUntil = Clock + Uniform(1.0, 3.5);
						A.NextStep = A.StepUntil + Uniform(3.0, 12.0);
						const Vec2 Mid = Middle(A.Herd);
						const double Spread = 6.0 + 2.5 * std::sqrt(double(Members[size_t(A.Herd)].size()));
						const double Way = (Mid - A.At).Length() > Spread ? std::atan2(Mid.Y - A.At.Y, Mid.X - A.At.X) : A.Heading + Uniform(-0.9, 0.9);
						A.StepTo = A.At + Vec2(std::cos(Way), std::sin(Way)) * Uniform(1.0, 3.0);
					}
					if (Clock < A.StepUntil && Farm.Inside(A.StepTo))
					{
						Desired = S.Walk * 0.35;
						Towards = A.StepTo;
					}
				}
				// now and then the head comes up to look round (2-4 s), then down again
				if (A.What == Doing::Graze && Clock >= A.NextLook)
				{
					if (A.LookEnd < A.NextLook)
					{
						A.LookEnd = Clock + Uniform(2.0, 4.0);
					}
					HeadWanted = 0.0;
					if (Clock >= A.LookEnd)
					{
						A.NextLook = Clock + Uniform(6.0, 20.0);
					}
				}
				if (Clock >= A.Until)
				{
					Decide(int(i));
				}
				break;
			case Doing::Browse:
				HeadWanted = -0.3;	// (up at a bush)
				// (on to the next branch now and then)
				if (Clock >= A.NextStep)
				{
					A.StepUntil = Clock + Uniform(1.0, 2.5);
					A.NextStep = A.StepUntil + Uniform(6.0, 20.0);
					const double Way = A.Heading + Uniform(-1.2, 1.2);
					A.StepTo = A.At + Vec2(std::cos(Way), std::sin(Way)) * Uniform(1.0, 2.5);
				}
				if (Clock < A.StepUntil && Farm.Inside(A.StepTo))
				{
					Desired = S.Walk * 0.35;
					Towards = A.StepTo;
				}
				if (Clock >= A.Until)
				{
					Decide(int(i));
				}
				break;
			case Doing::Look:
				HeadWanted = 0.0;
				// (wary: turned to face the danger, if it's well round to a side)
				if (Clock < A.WaryUntil)
				{
					const Vec2 To = A.Threat - A.At;
					const double Off = std::remainder(std::atan2(To.Y, To.X) - A.Heading, 2.0 * Pi);
					if (std::abs(Off) > 0.9)
					{
						A.Heading += std::clamp(Off, -Dt * 1.2, Dt * 1.2);
					}
				}
				if (Clock >= A.Until)
				{
					Decide(int(i));
				}
				break;
			case Doing::Flee:
				Desired = A.DiesAt >= 0.0 ? S.Run * 0.8 : S.Run;
				if ((A.Target - A.At).Length() < 5.0 || Clock >= A.Until)
				{
					// the danger past: back together (to where the herd's got to), wary a while
					A.What = Doing::Walk;
					A.Target = SomewhereInside(Middle(A.Herd), 8.0);
					A.Until = Clock + Uniform(0.0, 2.0);
					A.Prey = -1;
					A.WaryUntil = std::max(A.WaryUntil, Clock + Uniform(25.0, 50.0));
					A.bRegroup = true;
				}
				break;
			case Doing::Stalk:
			case Doing::Chase:
			{
				// a lion after its prey: closing at a walk, low; then the rush (the prey bolts once it's within 40 m)
				const Behaviour& Acts = S.Acts;
				Animal* Prey = A.Prey >= 0 && A.Prey < int(All.size()) ? &All[size_t(A.Prey)] : nullptr;
				if (!Prey || Prey->What == Doing::Dead)
				{
					A.What = Doing::Walk;
					A.Target = SomewhereInside(A.At, 80.0);
					A.HuntAt = Clock + Uniform(Acts.HuntMin, Acts.HuntMax);
					break;
				}
				const double Gap = (Prey->At - A.At).Length();
				Towards = Prey->At;
				if (A.What == Doing::Stalk)
				{
					Desired = S.Walk * 0.8;
					HeadWanted = 0.4;
					if (Gap < Acts.RushFrom)
					{
						A.What = Doing::Chase;
						A.Until = Clock + Acts.RushFor;	// (a lion's rush lasts ~20 s at most)
						for (const int Other : Members[size_t(Prey->Herd)])
						{
							Flee(Other, A.At, Uniform(10.0, 18.0));
						}
					}
				}
				else
				{
					// faster than its prey for the first few seconds, then tiring
					Desired = S.Run * (Clock < A.Until - (Acts.RushFor - 8.0) ? Acts.RushSpeed : 0.9);
					if (Gap < 1.5)
					{
						Prey->What = Doing::Dead;
						Prey->DiesAt = Clock;
						News.push_back(AName(S, true) + " killed " + AName(Kinds[size_t(Prey->Species)]));
						for (const int Other : Members[size_t(A.Herd)])
						{
							All[size_t(Other)].What = Doing::Feed;
							All[size_t(Other)].Target = Prey->At;
							All[size_t(Other)].Until = Clock + Uniform(Acts.FeedMin, Acts.FeedMax);
						}
					}
					else if (Clock >= A.Until)
					{
						A.What = Doing::Walk;
						A.Target = SomewhereInside(A.At, 50.0);
						A.HuntAt = Clock + Uniform(Acts.HuntMin, Acts.HuntMax);
						News.push_back(AName(S, true) + "'s chase failed");
					}
				}
				break;
			}
			case Doing::Pursue:
			{
				// going for someone: straight at them (round what's in the way, as along a fence) at its pace; caught
				// within reach; given up on once they're far off (or gone)
				const bool bThere = A.Person >= 0 && A.Person < int(People.size());
				const double Gap = bThere ? (People[size_t(A.Person)] - A.At).Length() : 1e9;
				if (Gap > S.Acts.GiveUp)
				{
					A.What = Doing::Walk;
					A.Target = SomewhereInside(A.At, 30.0);
					A.Until = Clock;
					A.Person = -1;
					break;
				}
				Towards = People[size_t(A.Person)];
				Desired = S.Run * S.Acts.PursueSpeed;
				HeadWanted = 0.0;
				const bool bCatchable = size_t(A.Person) >= PeopleCatchable.size() || PeopleCatchable[size_t(A.Person)];
				if (Gap < S.Acts.Reach && bCatchable)
				{
					Caught.push_back(A.Person);
					News.push_back(AName(S, true) + " caught you");
					A.What = Doing::Look;
					A.Until = Clock + 8.0;
					A.PursueAt = Clock + 8.0;
					A.Person = -1;
				}
				break;
			}
			case Doing::Feed:
				Desired = (A.Target - A.At).Length() > 2.5 ? S.Walk : 0.0;
				HeadWanted = 1.0;
				if (Clock >= A.Until)
				{
					A.What = Doing::Look;
					A.Until = Clock + 5.0;
					A.HuntAt = Clock + Uniform(S.Acts.HuntMin, S.Acts.HuntMax);
				}
				break;
			default:
				break;
			}
			// going for people: the nearest one it notices
			if (S.Acts.bGoesForPeople && !People.empty() && Clock >= A.PursueAt && A.What != Doing::Pursue && A.What != Doing::Flee
				&& A.What != Doing::Stalk && A.What != Doing::Chase)
			{
				double Nearest = S.Acts.NoticeWithin;
				for (size_t k = 0; k < People.size(); ++k)
				{
					const double D = (People[k] - A.At).Length();
					if (D < Nearest)
					{
						Nearest = D;
						A.Person = int(k);
					}
				}
				if (Nearest < S.Acts.NoticeWithin)
				{
					A.What = Doing::Pursue;
				}
			}
			// a hunter (a lion: every 8-15 minutes it hunts the nearest antelope or zebra, not the rhino, hippo, buffalo
			// or giraffe: too big)
			if (S.Acts.bHunts && Clock >= A.HuntAt && A.What != Doing::Stalk && A.What != Doing::Chase && A.What != Doing::Feed)
			{
				int Best = -1;
				double Nearest = S.Acts.HuntWithin;
				for (size_t j = 0; j < All.size(); ++j)
				{
					const Species& Q = Kinds[size_t(All[j].Species)];
					if (All[j].What == Doing::Dead || !Q.Acts.bPrey || Q.Mass > S.Acts.PreyUpTo || All[j].Herd == A.Herd)
					{
						continue;
					}
					const double D = (All[j].At - A.At).Length();
					if (D < Nearest)
					{
						Nearest = D;
						Best = int(j);
					}
				}
				A.HuntAt = Clock + Uniform(S.Acts.HuntMin, S.Acts.HuntMax);
				if (Best >= 0)
				{
					A.What = Doing::Stalk;
					A.Prey = Best;
					News.push_back(AName(S, true) + " is hunting " + AName(Kinds[size_t(All[size_t(Best)].Species)]));
				}
			}
			// the head eases to where it's wanted
			A.Now.HeadDown += std::clamp(HeadWanted - A.Now.HeadDown, -Dt * 1.5, Dt * 1.5);
			A.Now.Rest += std::clamp(RestWanted - A.Now.Rest, -Dt * 0.8, Dt * 0.8);	// (down, or up, in a second or so)
			if (Desired > 0.0 || A.Speed > 0.01)
			{
				Move(A, Desired, Towards, Dt);
			}
			// its legs come to rest under it when it stops
			A.Now.Stepping += std::clamp((A.Speed > 0.05 ? 1.0 : 0.0) - A.Now.Stepping, -Dt * 3.0, Dt * 3.0);
			Fidget(A, S, HeadWanted, Dt);
		}
		if (!Cars.empty())
		{
			DriveCars();
		}
		// (a pool round the people: twice a second)
		if (Spawn.bAroundPeople && !People.empty() && Clock >= NextRecycle)
		{
			NextRecycle = Clock + 0.5;
			Recycle();
		}
	}

	double LethalCarSpeed(const Species& S) { return std::clamp(5.0 + S.Mass * 0.01, 5.0, 25.0); }

	std::vector<CarHit> Herds::TakeHits()
	{
		std::vector<CarHit> Out;
		Out.swap(Hits);
		return Out;
	}

	// The cars among them: one coming fast spooks those near its way (they run, if they're the running kind; look up
	// if it's farther); one touching a creature pushes it out of its way, and hits it (bumped, wounded or killed by how
	// fast; the car slowed by its share of their mass)
	void Herds::DriveCars()
	{
		const auto& Kinds = AllSpecies();
		for (size_t c = 0; c < Cars.size(); ++c)
		{
			const Car& V = Cars[c];
			const Vec2 F = V.Forward.Normal();
			const Vec2 R(F.Y, -F.X);
			const double Speed = V.Velocity.Length();
			for (size_t i = 0; i < All.size(); ++i)
			{
				Animal& A = All[i];
				const Species& S = Kinds[size_t(A.Species)];
				const Vec2 To = A.At - V.At;
				const double Gap = To.Length();
				if (Gap > 80.0)
				{
					continue;
				}
				// (spooked: a car coming at it, faster than a walk)
				const double Closing = Speed > 0.01 ? V.Velocity.Dot(To.Normal()) : 0.0;
				if (A.What != Doing::Dead && S.Acts.bFlees && A.What != Doing::Flee && A.What != Doing::Pursue && A.What != Doing::Stalk
					&& A.What != Doing::Chase && Closing > 2.0)
				{
					if (Gap < 25.0)
					{
						Flee(int(i), V.At, Uniform(S.Acts.FleeMin, S.Acts.FleeMax));
					}
					else if (Gap < 60.0 && A.What != Doing::Look && Clock >= A.WaryUntil)
					{
						A.Threat = V.At;
						A.WaryUntil = Clock + Uniform(S.Acts.WaryMin, S.Acts.WaryMax);
						if (A.What != Doing::Feed)
						{
							A.What = Doing::Look;
							A.Until = A.WaryUntil;
						}
					}
				}
				// touching it: its body's a circle round it (half its length, or its width standing upright)
				const double Radius = S.Shape.Build == BodyShape::Frame::Upright ? 0.25 : std::max(0.2, S.Length * 0.35);
				const double Along = To.Dot(F), Across = To.Dot(R);
				const double OverAlong = V.HalfLength + Radius - std::abs(Along), OverAcross = V.HalfWidth + Radius - std::abs(Across);
				if (OverAlong <= 0.0 || OverAcross <= 0.0)
				{
					continue;
				}
				// pushed out the nearer way (out of the front or back, or a side)
				const Vec2 Out = OverAlong < OverAcross ? F * (Along >= 0.0 ? 1.0 : -1.0) : R * (Across >= 0.0 ? 1.0 : -1.0);
				const double By = std::min(OverAlong, OverAcross) + 0.05;
				const Vec2 Moved = A.At + Out * By;
				if (Farm.Inside(Moved))
				{
					A.At = Moved;
				}
				// hit: how fast they met (the car's speed its way, less the creature's own)
				const Vec2 Own = Vec2(std::cos(A.Heading), std::sin(A.Heading)) * A.Speed;
				const double Hard = (V.Velocity - Own).Dot(Out);
				if (A.What == Doing::Dead || Hard < 0.5 || Clock - A.BumpedAt < 0.6)
				{
					continue;
				}
				A.BumpedAt = Clock;
				CarHit H;
				H.Index = int(i);
				H.CarIndex = int(c);
				H.Speed = Hard;
				H.Way = Out;
				H.CarLoses = Hard * S.Mass / (S.Mass + V.Mass);
				const double Lethal = LethalCarSpeed(S);
				if (Hard >= Lethal)
				{
					H.bKilled = true;
					A.What = Doing::Dead;
					A.Speed = 0.0;
					A.DiesAt = Clock;
					A.bWounded = true;
					News.push_back(AName(S, true) + " hit by a car, at " + std::to_string(int(std::lround(Hard * 3.6))) + " km/h: killed");
				}
				else
				{
					H.bWounded = Hard >= Lethal * 0.5;
					A.bWounded = A.bWounded || H.bWounded;
					A.Speed = Hard;
					A.Heading = std::atan2(Out.Y, Out.X);
					if (S.Acts.bFlees)
					{
						Flee(int(i), V.At, Uniform(S.Acts.FleeMin, S.Acts.FleeMax) + (H.bWounded ? 20.0 : 0.0));
					}
					News.push_back(AName(S, true) + (H.bWounded ? " hit by a car, wounded" : " bumped by a car"));
				}
				Hits.push_back(H);
			}
		}
	}

	std::vector<int> Herds::TakeCaught()
	{
		std::vector<int> Out;
		Out.swap(Caught);
		return Out;
	}

	std::vector<std::string> Herds::TakeNews()
	{
		std::vector<std::string> Out;
		Out.swap(News);
		return Out;
	}
}
