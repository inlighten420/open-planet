// Geodrive bodytest - AGPL-3.0 (see LICENSE)
//
// Checks Core/GeoBody against answers worked out by hand (the textbook's): what things weigh, how they turn, how they
// fall, spin and float. Prints each check and its result; exits 1 if any is off.

#include "GeoBody/Body.h"
#include "GeoCar/CarParts.h"
#include "GeoFlight/Flight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

using namespace geobody;

namespace
{
	int Failed = 0;

	void Check(const char* What, double Got, double Wanted, double Tolerance)
	{
		const bool bOk = std::abs(Got - Wanted) <= Tolerance;
		Failed += bOk ? 0 : 1;
		std::printf("%s %-58s %12.4f (wanted %.4f)\n", bOk ? "  ok " : "  OFF", What, Got, Wanted);
	}

	Surroundings Sea()
	{
		Surroundings S;
		S.Water = [](double, double, double& Level) { Level = 0.0; return true; };
		return S;
	}

	/** Lets a body settle in the water (from just touching it) and says how deep its centre of mass sits */
	double Settle(Body& B, double Seconds)
	{
		const Surroundings S = Sea();
		for (int i = 0; i < int(Seconds * 60.0); ++i)
		{
			B.Step(1.0 / 60.0, S);
		}
		return B.CentreOfMass().Z;
	}
}

int main()
{
	std::printf("Masses and inertia\n");
	{
		const MassProperties M = Measure({ Box("steel cube", Vec3(), Vec3(1.0, 1.0, 1.0), Materials::Steel) });
		Check("a solid steel 1 m cube's mass (kg)", M.Mass, 7850.0, 1e-6);
		Check("its inertia about any axis (m a^2 / 6)", M.Inertia.M[0][0], 7850.0 / 6.0, 1e-6);
	}
	{
		// a steel tank: a 1 m cube with 5 mm walls
		const Part Tank = Box("tank", Vec3(), Vec3(1.0, 1.0, 1.0), Materials::Steel, 0.005);
		Check("a 5 mm steel shell 1 m cube's mass (kg)", Tank.GetMass(), 7850.0 * (1.0 - 0.99 * 0.99 * 0.99), 1e-6);
	}
	{
		// two 1 kg lumps 2 m apart: the centre between them, the inertia 2 x 1 x 1^2 about it (each a point, near enough)
		const MassProperties M = Measure({ Lump("a", Vec3(1.0, 0.0, 0.0), Vec3(0.01, 0.01, 0.01), 1.0), Lump("b", Vec3(-1.0, 0.0, 0.0), Vec3(0.01, 0.01, 0.01), 1.0) });
		Check("two lumps: centre of mass x", M.CentreOfMass.X, 0.0, 1e-9);
		Check("two lumps: inertia about z (parallel axes)", M.Inertia.M[2][2], 2.0, 1e-3);
		Check("two lumps: inertia about x (along them)", M.Inertia.M[0][0], 0.0, 1e-3);
	}
	{
		// an aluminium tube along y (2 m, 100 mm across, 2 mm wall): its mass, and its inertia about its length
		const Part Tube = Cylinder("tube", Vec3(), 2.0, 0.1, Vec3(0.0, 1.0, 0.0), Materials::Aluminium, 0.002);
		const double Pi = 3.14159265358979;
		const double Outer = Pi * 0.05 * 0.05 * 2.0, Inner = Pi * 0.048 * 0.048 * 1.996;
		Check("an aluminium tube's mass (kg)", Tube.GetMass(), 2700.0 * (Outer - Inner), 1e-6);
		const Mat3 I = Tube.GetInertia();
		Check("its inertia about its length (y)", I.M[1][1], 2700.0 * (Outer * 0.05 * 0.05 - Inner * 0.048 * 0.048) * 0.5, 1e-6);
	}

	std::printf("Motion\n");
	{
		Body B({ Box("block", Vec3(), Vec3(1.0, 1.0, 1.0), Materials::Steel) });
		B.Place(Vec3(0.0, 0.0, 100.0), Quat());
		Surroundings Dry;
		B.Step(1.0, Dry);
		Check("falling 1 s from rest (m: g/2, the steps' own error tiny)", 100.0 - B.Origin().Z, 9.80665 * 0.5, 0.03);
	}
	{
		// a box spinning about its longest axis keeps spinning (and its energy); about its middle axis it tumbles over
		Surroundings Space;
		Space.Gravity = Vec3();
		Body Stable({ Box("book", Vec3(), Vec3(0.3, 0.2, 0.05), Materials::Pine) });
		Stable.Place(Vec3(), Quat());
		Stable.SetSpin(Vec3(0.0, 0.0, 10.0) + Vec3(0.01, 0.0, 0.0));
		const double Before = Stable.Energy();
		Stable.Step(10.0, Space);
		Check("spinning about its least-easy axis: energy kept (share)", Stable.Energy() / Before, 1.0, 0.02);
		Check("...and still spinning that way (z's share of the spin)", std::abs(Stable.OwnSpin().Z) / Stable.OwnSpin().Length(), 1.0, 0.01);

		Body Tumbling({ Box("book", Vec3(), Vec3(0.3, 0.2, 0.05), Materials::Pine) });
		Tumbling.Place(Vec3(), Quat());
		Tumbling.SetSpin(Vec3(0.0, 10.0, 0.0) + Vec3(0.01, 0.0, 0.01));
		double Lowest = 1.0;
		for (int i = 0; i < 600; ++i)
		{
			Tumbling.Step(1.0 / 60.0, Space);
			Lowest = std::min(Lowest, Tumbling.OwnSpin().Y / 10.0);
		}
		Check("spinning about its middle axis it flips (spin turns round)", Lowest < -0.8 ? 1.0 : 0.0, 1.0, 0.0);
	}

	std::printf("Floating\n");
	{
		// a pine cube floats with 500 / 1025 of it under
		Body B({ Box("pine", Vec3(), Vec3(1.0, 1.0, 1.0), Materials::Pine) });
		B.Place(Vec3(0.0, 0.0, 0.5), Quat());
		const double Z = Settle(B, 60.0);
		Check("a pine 1 m cube's depth in the sea (m under)", 0.5 - Z, 500.0 / 1025.0, 0.02);
	}
	{
		// a sealed steel box (a pontoon: 4 x 2 x 1 m, 4 mm walls) floats, as deep as its weight over the water's
		const Part Pontoon = Box("pontoon", Vec3(), Vec3(4.0, 2.0, 1.0), Materials::Steel, 0.004);
		Part Sealed = Pontoon;
		Sealed.bSealed = true;
		Body B({ Sealed });
		B.Place(Vec3(0.0, 0.0, 0.5), Quat());
		const double Z = Settle(B, 60.0);
		Check("a sealed steel pontoon's draft (m)", 0.5 - Z, Sealed.GetMass() / (1025.0 * 8.0), 0.02);
		// an open one (not sealed) sinks: only its steel displaces water
		Body Open({ Pontoon });
		Open.Place(Vec3(0.0, 0.0, 0.5), Quat());
		Check("an open steel box sinks (m down after 20 s, past 2)", Settle(Open, 20.0) < -2.0 ? 1.0 : 0.0, 1.0, 0.0);
	}
	{
		// a pine log on its side rolls level: a long body floats lying down, not standing up
		Body B({ Cylinder("log", Vec3(), 4.0, 0.4, Vec3(0.0, 0.0, 1.0), Materials::Pine) });
		B.Place(Vec3(0.0, 0.0, 2.0), Quat::FromAxisAngle(Vec3(1.0, 0.0, 0.0), 0.05));
		Settle(B, 120.0);
		const Vec3 Along = B.Rotation().Rotate(Vec3(0.0, 0.0, 1.0));
		Check("a pine log let go upright ends lying down (its axis's tilt from up)", std::abs(Along.Z) < 0.2 ? 1.0 : 0.0, 1.0, 0.0);
	}

	std::printf("The Bakkie 3.0 TDi from its parts (against its published and measured numbers)\n");
	{
		const std::vector<Part> Parts = geocar::BakkieParts();
		const MassProperties M = Measure(Parts);
		// what weighs what, the heaviest first
		std::map<std::string, double> Kinds;
		for (const Part& P : Parts)
		{
			Kinds[P.Name] += P.GetMass();
		}
		std::vector<std::pair<double, std::string>> Sorted;
		for (const auto& K : Kinds)
		{
			Sorted.push_back({ K.second, K.first });
		}
		std::sort(Sorted.rbegin(), Sorted.rend());
		for (const auto& K : Sorted)
		{
			std::printf("        %7.1f kg  %s\n", K.first, K.second.c_str());
		}
		const double Half = geocar::BakkieWheelbase * 0.5;
		Check("its mass with the driver and a full tank (kg; 1,830 licensing + 75)", M.Mass, 1905.0, 1905.0 * 0.05);
		// (no measured split is published: a double cab pickup empty carries 52-60% on its front wheels; the 58% typed in
		// before was an estimate. A weighbridge, each axle on its own, would say)
		Check("its weight on the front wheels (share; a pickup's 52-60%)", (M.CentreOfMass.X + Half) / (2.0 * Half), 0.56, 0.04);
		Check("its centre of mass above the ground (m)", M.CentreOfMass.Z, 0.74, 0.06);
		std::printf("        inertia (kg m2): roll %.0f, pitch %.0f, yaw %.0f (a box's guess, now: %.0f, %.0f, %.0f)\n", M.Inertia.M[0][0], M.Inertia.M[1][1],
			M.Inertia.M[2][2], 1905.0 / 12.0 * (1.80 * 1.80 + 1.735 * 1.735), 1905.0 / 12.0 * (4.90 * 4.90 + 1.735 * 1.735), 1905.0 / 12.0 * (4.90 * 4.90 + 1.80 * 1.80));
	}

	std::printf("The other cars from their parts\n");
	{
		struct Car { const char* Name; std::vector<Part> Parts; double Wheelbase, Mass, Front; };
		// (published: the RS 1450 kg + 75 kg driver, about 40% in front; the Swift Sport 1045 + 75, about 61%; the
		// grand prix car 798 kg with its driver + 110 kg of fuel, 44-46% in front by the rules)
		const Car Cars[3] = { { "RS 4.0", geocar::RSParts(), geocar::RSWheelbase, 1525.0, 0.40 },
			{ "Hatchback 1.6", geocar::HatchParts(), geocar::HatchWheelbase, 1120.0, 0.61 },
			{ "Formula", geocar::FormulaParts(), geocar::FormulaWheelbase, 908.0, 0.45 } };
		for (const Car& C : Cars)
		{
			const MassProperties M = Measure(C.Parts);
			std::printf("        %s: %.0f kg, centre of mass %.2f m up; inertia roll %.0f, pitch %.0f, yaw %.0f kg m2\n", C.Name, M.Mass, M.CentreOfMass.Z,
				M.Inertia.M[0][0], M.Inertia.M[1][1], M.Inertia.M[2][2]);
			Check((std::string(C.Name) + ": its mass (kg)").c_str(), M.Mass, C.Mass, C.Mass * 0.03);
			Check((std::string(C.Name) + ": its weight on the front wheels (share)").c_str(), (M.CentreOfMass.X + C.Wheelbase * 0.5) / C.Wheelbase, C.Front, 0.04);
		}
	}

	std::printf("The aircraft from their parts\n");
	{
		// (the F-16's inertias are published: 12,875, 75,674 and 85,552 kg m2)
		const geoflight::Design Designs[3] = { geoflight::FighterJet(), geoflight::Airliner(), geoflight::RegionalJet() };
		const double Mass[3] = { 12000.0, 64000.0, 42000.0 };
		for (size_t i = 0; i < 3; ++i)
		{
			const geoflight::Design& D = Designs[i];
			std::printf("        %s: %.0f kg, centre of mass %.2f m along, %.2f up; inertia roll %.0f, pitch %.0f, yaw %.0f kg m2\n", D.Name.c_str(), D.Mass,
				D.CentreOfMass.X, D.CentreOfMass.Z, D.Inertia.X, D.Inertia.Y, D.Inertia.Z);
			Check((D.Name + ": its mass (kg, as flown)").c_str(), D.Mass, Mass[i], Mass[i] * 0.03);
			// its weight on the nose wheel: an airliner's 6-12%, a fighter's ~10-15% (ahead of the mains, or it sits on its tail)
			const geoflight::Vec3 Nose = D.Wheels[0].At, Main = D.Wheels[1].At;
			Check((D.Name + ": the nose wheel's share of the weight").c_str(), (D.CentreOfMass.X - Main.X) / (Nose.X - Main.X), 0.1, 0.06);
		}
		// NASA's F-16 (TP-1538): clean, 9,295 kg, inertias 12,875, 75,674 and 85,552 kg m2. Ours clean: no drop tanks,
		// missiles, rounds or pylons
		std::vector<Part> Clean;
		for (const Part& P : Designs[0].Parts)
		{
			if (P.Name.find("drop tank") == std::string::npos && P.Name.find("missile") == std::string::npos && P.Name.find("rounds") == std::string::npos)
			{
				Clean.push_back(P);
			}
		}
		const MassProperties F16 = Measure(Clean);
		std::printf("        the fighter clean: %.0f kg; inertia roll %.0f, pitch %.0f, yaw %.0f kg m2 (NASA's: 9,295 kg; 12,875, 75,674, 85,552)\n",
			F16.Mass, F16.Inertia.M[0][0], F16.Inertia.M[1][1], F16.Inertia.M[2][2]);
		Check("the fighter clean: roll inertia against NASA's (share)", F16.Inertia.M[0][0] / 12875.0, 1.0, 0.3);
		Check("the fighter clean: pitch inertia against NASA's (share)", F16.Inertia.M[1][1] / 75674.0, 1.0, 0.25);
		Check("the fighter clean: yaw inertia against NASA's (share)", F16.Inertia.M[2][2] / 85552.0, 1.0, 0.25);
	}

	std::printf(Failed == 0 ? "All right.\n" : "%d off.\n", Failed);
	return Failed == 0 ? 0 : 1;
}
