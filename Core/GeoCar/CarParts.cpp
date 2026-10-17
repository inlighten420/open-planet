// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "CarParts.h"

namespace geocar
{
	using namespace geobody;
	namespace M = geobody::Materials;

	std::vector<Part> BakkieParts()
	{
		// its published size: 3.05 m wheelbase, 4.90 m long (0.83 m ahead of the front axle, 1.02 m behind the rear),
		// 1.80 m wide, 1.735 m tall, tracks 1.515 m front and 1.52 m rear, 245/70 R16 tyres (0.375 m to the axle).
		// Weighed things (the engine, gearbox, seats, the driver) are lumps of their real mass; the rest is its steel,
		// glass, rubber, plastic and diesel at their sizes. Panels are a car's 0.8-0.9 mm steel, the frame's 4 mm.
		const double Front = 1.525, Rear = -1.525, Axle = 0.375;
		const Vec3 Along(1.0, 0.0, 0.0), Across(0.0, 1.0, 0.0);
		std::vector<Part> P;

		// --- the ladder frame: two box-section rails nose to tail, five cross members ---
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Box("frame rail", Vec3(-0.1, Side * 0.47, 0.56), Vec3(4.7, 0.065, 0.17), M::Steel, 0.0045));
		}
		for (const double X : { 2.1, 1.3, 0.2, -1.0, -2.3 })
		{
			P.push_back(Box("cross member", Vec3(X, 0.0, 0.56), Vec3(0.08, 0.88, 0.08), M::Steel, 0.004));
		}
		P.push_back(Lump("frame brackets (spring hangers, body and engine mounts)", Vec3(0.2, 0.0, 0.55), Vec3(4.4, 0.95, 0.12), 25.0));

		// --- the engine and drivetrain ---
		P.push_back(Lump("engine (4JH1-TC 3.0 turbo diesel, with its oil, turbo, alternator, pumps)", Vec3(1.30, 0.0, 0.86), Vec3(0.75, 0.62, 0.70), 255.0, M::CastIron));
		P.push_back(Lump("gearbox (5-speed manual, with its oil) and clutch", Vec3(0.55, 0.0, 0.66), Vec3(0.60, 0.30, 0.30), 48.0, M::Aluminium));
		P.push_back(Cylinder("prop shaft", Vec3(-0.50, 0.0, 0.52), 1.90, 0.075, Along, M::Steel, 0.002));
		P.push_back(Cylinder("rear axle housing", Vec3(Rear, 0.0, Axle), 1.40, 0.09, Across, M::Steel, 0.006));
		P.push_back(Lump("rear diff (crown wheel, pinion, the locker, oil)", Vec3(Rear, 0.0, Axle), Vec3(0.30, 0.30, 0.30), 32.0, M::CastIron));
		P.push_back(Cylinder("rear half shafts", Vec3(Rear, 0.0, Axle), 1.45, 0.032, Across, M::Steel));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Lump("front corner (wishbones, torsion bar, knuckle, hub, disc, calliper)", Vec3(Front, Side * 0.60, 0.42), Vec3(0.40, 0.40, 0.35), 38.0));
			P.push_back(Box("rear leaf spring", Vec3(Rear, Side * 0.47, 0.45), Vec3(1.30, 0.06, 0.05), M::Steel));
			P.push_back(Lump("rear drum brake", Vec3(Rear, Side * 0.66, Axle), Vec3(0.30, 0.10, 0.30), 11.0, M::CastIron));
		}
		P.push_back(Lump("steering box, column and linkage", Vec3(1.70, 0.30, 0.60), Vec3(0.60, 0.20, 0.20), 14.0));
		P.push_back(Cylinder("exhaust pipe", Vec3(-0.40, -0.30, 0.38), 3.80, 0.055, Along, M::Steel, 0.0015));
		P.push_back(Lump("silencer", Vec3(-0.30, -0.30, 0.38), Vec3(0.50, 0.20, 0.15), 10.0));

		// --- the wheels: 245/70 R16 all-terrain tyres on 16 x 7 steel rims, and the spare under the bed ---
		const Vec3 Wheels[5] = { Vec3(Front, 0.7575, Axle), Vec3(Front, -0.7575, Axle), Vec3(Rear, 0.76, Axle), Vec3(Rear, -0.76, Axle),
			Vec3(-2.05, 0.0, 0.36) };
		for (int i = 0; i < 5; ++i)
		{
			const Vec3 Axis = i < 4 ? Across : Vec3(0.0, 0.0, 1.0);	// (the spare lies flat)
			P.push_back(Cylinder("tyre", Wheels[i], 0.245, 0.750, Axis, M::Rubber, 0.012));
			Part Rim = Lump("steel rim", Wheels[i], Vec3(0.41, 0.18, 0.41), 12.0);
			if (i == 4)
			{
				Rim.Size = Vec3(0.41, 0.41, 0.18);
			}
			P.push_back(Rim);
		}

		// --- what keeps it going: a full 76 litre tank, the battery, the radiator ---
		P.push_back(Box("fuel tank", Vec3(-0.85, -0.10, 0.42), Vec3(0.90, 0.62, 0.18), M::Steel, 0.0012));
		P.push_back(Lump("diesel (76 litres, full)", Vec3(-0.85, -0.10, 0.42), Vec3(0.86, 0.58, 0.16), 76.0 * 0.001 * M::Diesel.Density, M::Diesel));
		P.push_back(Lump("battery", Vec3(1.75, 0.55, 0.95), Vec3(0.28, 0.17, 0.22), 19.0, M::Lead));
		P.push_back(Lump("radiator, intercooler and coolant", Vec3(2.10, 0.0, 0.80), Vec3(0.08, 0.70, 0.50), 16.0, M::Aluminium));

		// --- the body: the front end, the double cab, the load bed ---
		P.push_back(Box("bonnet", Vec3(1.65, 0.0, 1.12), Vec3(1.35, 1.60, 0.0009), M::Steel));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Box("front wing", Vec3(1.75, Side * 0.88, 0.90), Vec3(1.15, 0.0009, 0.50), M::Steel));
		}
		P.push_back(Box("front structure (radiator support, inner wings)", Vec3(1.65, 0.0, 0.85), Vec3(1.30, 1.60, 0.50), M::Steel, 0.0009));
		// (a car's body is double skinned: an outer panel, and an inner one behind it, the two welded at their edges)
		P.push_back(Box("cab's outer skin (floor, roof, firewall, back wall, sides)", Vec3(-0.05, 0.0, 1.27), Vec3(2.00, 1.72, 0.94), M::Steel, 0.0008));
		P.push_back(Box("cab's inner panels", Vec3(-0.05, 0.0, 1.27), Vec3(1.94, 1.66, 0.88), M::Steel, 0.0007));
		P.push_back(Box("cab floor's reinforcing", Vec3(-0.05, 0.0, 0.81), Vec3(1.90, 1.60, 0.0012), M::Steel));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Box("sill", Vec3(-0.05, Side * 0.80, 0.82), Vec3(2.00, 0.10, 0.12), M::Steel, 0.0015));
			for (const double X : { 0.80, -0.05, -0.95 })
			{
				P.push_back(Box("pillar", Vec3(X, Side * 0.82, 1.40), Vec3(0.08, 0.06, 0.70), M::Steel, 0.0012));
			}
			for (const double X : { 0.45, -0.55 })
			{
				P.push_back(Lump("door (skins, frame, lock, window winder, trim)", Vec3(X, Side * 0.83, 1.15), Vec3(0.90, 0.12, 0.70), X > 0.0 ? 24.0 : 20.0));
				P.push_back(Box("door window", Vec3(X, Side * 0.84, 1.52), Vec3(0.75, 0.004, 0.42), M::Glass));
			}
		}
		Part Windscreen = Box("windscreen (laminated)", Vec3(0.86, 0.0, 1.52), Vec3(0.005, 1.45, 0.85), M::Glass);
		Windscreen.Turn = Quat::FromAxisAngle(Across, -0.95);	// (raked back)
		P.push_back(Windscreen);
		P.push_back(Box("back window", Vec3(-1.03, 0.0, 1.55), Vec3(0.004, 1.30, 0.40), M::Glass));
		P.push_back(Lump("dashboard (its beam, heater and air conditioning)", Vec3(0.70, 0.0, 1.12), Vec3(0.40, 1.50, 0.40), 22.0, M::Polypropylene));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Lump("front seat", Vec3(0.10, Side * 0.38, 1.05), Vec3(0.55, 0.50, 0.70), 19.0, M::Foam));
		}
		P.push_back(Lump("rear bench", Vec3(-0.75, 0.0, 1.05), Vec3(0.50, 1.40, 0.70), 26.0, M::Foam));
		P.push_back(Lump("carpets, headlining and trim", Vec3(-0.05, 0.0, 1.20), Vec3(1.90, 1.60, 0.80), 24.0, M::Polypropylene));
		P.push_back(Lump("wiring, lights, wipers and the rest", Vec3(0.60, 0.0, 1.00), Vec3(3.00, 1.50, 0.60), 25.0, M::Polypropylene));
		// the load bed: floor, sides (double skinned), the bulkhead and the tailgate
		P.push_back(Box("bed floor", Vec3(-1.82, 0.0, 0.82), Vec3(1.45, 1.46, 0.0012), M::Steel));
		P.push_back(Lump("bed's under-frame", Vec3(-1.82, 0.0, 0.76), Vec3(1.45, 1.40, 0.08), 18.0));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Box("bed side", Vec3(-1.82, Side * 0.86, 1.00), Vec3(1.45, 0.05, 0.46), M::Steel, 0.0008));
		}
		P.push_back(Box("bed bulkhead", Vec3(-1.10, 0.0, 1.00), Vec3(0.05, 1.70, 0.46), M::Steel, 0.0008));
		P.push_back(Box("tailgate", Vec3(-2.54, 0.0, 1.00), Vec3(0.05, 1.60, 0.46), M::Steel, 0.0008));
		P.push_back(Lump("front bumper and grille (chrome)", Vec3(2.32, 0.0, 0.65), Vec3(0.20, 1.75, 0.35), 15.0));
		P.push_back(Lump("rear step bumper", Vec3(-2.60, 0.0, 0.55), Vec3(0.20, 1.70, 0.20), 20.0));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Lump("side step", Vec3(-0.05, Side * 0.86, 0.52), Vec3(1.90, 0.18, 0.05), 6.0, M::Aluminium));
		}

		// --- the small things that add up ---
		P.push_back(Lump("paint, sealer, sound deadening and underbody coating", Vec3(-0.2, 0.0, 1.00), Vec3(4.6, 1.75, 1.00), 28.0, M::Polypropylene));
		P.push_back(Lump("nuts, bolts, clips and small brackets", Vec3(0.2, 0.0, 0.90), Vec3(4.4, 1.60, 0.90), 15.0));
		for (const double Side : { -1.0, 1.0 })
		{
			P.push_back(Lump("door mirror", Vec3(0.85, Side * 0.98, 1.35), Vec3(0.12, 0.20, 0.18), 2.0, M::ABS));
		}
		P.push_back(Lump("air intake and filter", Vec3(1.60, 0.45, 1.00), Vec3(0.40, 0.25, 0.25), 5.0, M::Polypropylene));
		P.push_back(Lump("air conditioning (compressor, condenser, pipes)", Vec3(1.95, 0.0, 0.80), Vec3(0.30, 0.70, 0.40), 9.0, M::Aluminium));
		P.push_back(Cylinder("front anti-roll bar", Vec3(1.80, 0.0, 0.45), 1.20, 0.024, Across, M::Steel));
		P.push_back(Lump("steering wheel and airbag", Vec3(0.55, -0.38, 1.35), Vec3(0.15, 0.38, 0.38), 5.0, M::ABS));
		P.push_back(Lump("jack, wheel spanner and tools", Vec3(-0.75, 0.0, 0.90), Vec3(0.50, 0.40, 0.15), 6.0));
		P.push_back(Lump("washer bottle (full)", Vec3(1.90, -0.60, 0.90), Vec3(0.20, 0.15, 0.20), 4.0, M::Water));

		// --- the driver (on the right: South Africa drives on the left), the EU's standard 75 kg ---
		P.push_back(Lump("driver", Vec3(0.10, -0.38, 1.20), Vec3(0.45, 0.45, 0.90), 75.0, M::Water));
		return P;
	}

	namespace
	{
		/** A pair of the same lump, left and right (y: how far out) */
		void Pair(std::vector<Part>& P, const std::string& Name, const Vec3& At, const Vec3& Size, double Kg, const Material& Of = M::Steel)
		{
			P.push_back(Lump(Name, At, Size, Kg, Of));
			P.push_back(Lump(Name, Vec3(At.X, -At.Y, At.Z), Size, Kg, Of));
		}
	}

	std::vector<Part> RSParts()
	{
		// a 992 GT3 RS: 1450 kg ready to drive (DIN: fluids, 90% of the fuel), 2.457 m wheelbase. The engine hangs behind
		// the rear axle, the twin-clutch gearbox ahead of it; the fuel and the radiators are in the nose; carbon roof,
		// bonnet, doors and wings
		const double Front = 1.2285, Rear = -1.2285;
		std::vector<Part> P;
		P.push_back(Lump("engine (4.0 flat six, with its oil, intake, alternator)", Vec3(-1.62, 0.0, 0.42), Vec3(0.60, 0.85, 0.50), 240.0, M::Aluminium));
		P.push_back(Lump("gearbox (seven-speed twin clutch) and differential", Vec3(-0.95, 0.0, 0.36), Vec3(0.70, 0.45, 0.40), 145.0, M::Aluminium));
		P.push_back(Lump("body shell (aluminium and steel)", Vec3(-0.1, 0.0, 0.62), Vec3(4.2, 1.8, 0.9), 335.0, M::Aluminium));
		P.push_back(Lump("roof, bonnet, doors, wings (carbon)", Vec3(0.0, 0.0, 0.95), Vec3(3.0, 1.7, 0.4), 60.0, M::CarbonFibre));
		P.push_back(Lump("glass", Vec3(-0.2, 0.0, 1.05), Vec3(1.8, 1.4, 0.4), 25.0, M::Glass));
		P.push_back(Lump("rear wing and its swan necks", Vec3(-2.15, 0.0, 1.33), Vec3(0.5, 1.6, 0.2), 22.0, M::CarbonFibre));
		P.push_back(Lump("front bumper, splitter, canards", Vec3(2.1, 0.0, 0.30), Vec3(0.3, 1.8, 0.4), 18.0, M::CarbonFibre));
		P.push_back(Lump("rear bumper, diffuser, side skirts", Vec3(-2.2, 0.0, 0.30), Vec3(0.3, 1.8, 0.4), 17.0, M::CarbonFibre));
		Pair(P, "front suspension and brakes", Vec3(Front, 0.70, 0.35), Vec3(0.4, 0.3, 0.4), 40.0);
		Pair(P, "rear suspension, brakes and drive shaft", Vec3(Rear, 0.68, 0.37), Vec3(0.4, 0.3, 0.4), 45.0);
		Pair(P, "front wheel (275/35 R20 on a forged rim)", Vec3(Front, 0.79, 0.35), Vec3(0.70, 0.28, 0.70), 20.5, M::Rubber);
		Pair(P, "rear wheel (335/30 R21 on a forged rim)", Vec3(Rear, 0.78, 0.37), Vec3(0.73, 0.34, 0.73), 24.0, M::Rubber);
		P.push_back(Lump("fuel tank", Vec3(1.35, 0.0, 0.35), Vec3(0.6, 0.9, 0.3), 10.0));
		P.push_back(Lump("petrol (64 litres)", Vec3(1.35, 0.0, 0.35), Vec3(0.55, 0.85, 0.25), 64.0 * 0.001 * M::Petrol.Density, M::Petrol));
		P.push_back(Lump("radiators (three, in the nose) and coolant", Vec3(1.85, 0.0, 0.35), Vec3(0.3, 1.4, 0.4), 35.0, M::Aluminium));
		P.push_back(Lump("battery (lithium)", Vec3(1.55, 0.3, 0.45), Vec3(0.25, 0.18, 0.18), 13.0, M::Aluminium));
		P.push_back(Lump("front crash structure, luggage bay, brake booster, ABS, washer", Vec3(1.75, 0.0, 0.45), Vec3(0.6, 1.4, 0.4), 45.0, M::Aluminium));
		P.push_back(Lump("exhaust", Vec3(-2.0, 0.0, 0.30), Vec3(0.4, 1.0, 0.25), 20.0));
		P.push_back(Lump("bucket seats, dashboard, carpets", Vec3(-0.2, 0.0, 0.55), Vec3(1.4, 1.5, 0.7), 78.0, M::Polypropylene));
		P.push_back(Lump("wiring, electronics", Vec3(0.2, 0.0, 0.50), Vec3(3.0, 1.5, 0.5), 35.0, M::Copper));
		P.push_back(Lump("air conditioning, steering", Vec3(0.9, 0.0, 0.50), Vec3(0.8, 1.2, 0.4), 40.0, M::Aluminium));
		P.push_back(Lump("sound and heat insulation", Vec3(-0.6, 0.0, 0.55), Vec3(2.0, 1.5, 0.6), 5.0, M::Foam));
		P.push_back(Lump("driver", Vec3(-0.29, -0.37, 0.50), Vec3(0.45, 0.45, 0.80), 75.0, M::Water));
		return P;
	}

	std::vector<Part> HatchParts()
	{
		// a Swift Sport (ZC32S): 1045 kg at the kerb, 2.43 m wheelbase; the engine and gearbox across the front, driving
		// the front wheels; the tank under the rear seat
		const double Front = 1.215, Rear = -1.215;
		std::vector<Part> P;
		P.push_back(Lump("engine (M16A 1.6 four, with its oil)", Vec3(1.40, 0.05, 0.55), Vec3(0.55, 0.60, 0.60), 110.0, M::Aluminium));
		P.push_back(Lump("gearbox (six-speed manual) and differential", Vec3(1.30, 0.35, 0.40), Vec3(0.40, 0.40, 0.35), 45.0, M::Aluminium));
		P.push_back(Lump("body shell", Vec3(0.0, 0.0, 0.70), Vec3(3.6, 1.65, 1.2), 285.0));
		Pair(P, "front door", Vec3(0.35, 0.82, 0.75), Vec3(1.0, 0.12, 0.8), 18.0);
		Pair(P, "rear door", Vec3(-0.55, 0.82, 0.75), Vec3(0.8, 0.12, 0.8), 15.0);
		P.push_back(Lump("bonnet, tailgate, wings, bumpers", Vec3(0.3, 0.0, 0.60), Vec3(3.8, 1.7, 0.8), 55.0, M::Polypropylene));
		P.push_back(Lump("glass", Vec3(-0.1, 0.0, 1.15), Vec3(2.0, 1.4, 0.5), 32.0, M::Glass));
		Pair(P, "front suspension and brakes", Vec3(Front, 0.68, 0.32), Vec3(0.4, 0.3, 0.4), 27.0);
		P.push_back(Lump("rear torsion beam axle and brakes", Vec3(Rear, 0.0, 0.30), Vec3(0.4, 1.4, 0.3), 42.0));
		Pair(P, "front wheel (195/45 R17 on an alloy rim)", Vec3(Front, 0.745, 0.31), Vec3(0.62, 0.20, 0.62), 16.5, M::Rubber);
		Pair(P, "rear wheel (195/45 R17 on an alloy rim)", Vec3(Rear, 0.75, 0.31), Vec3(0.62, 0.20, 0.62), 16.5, M::Rubber);
		P.push_back(Lump("fuel tank", Vec3(-0.75, 0.0, 0.30), Vec3(0.6, 0.9, 0.25), 9.0, M::Polypropylene));
		P.push_back(Lump("petrol (42 litres)", Vec3(-0.75, 0.0, 0.30), Vec3(0.55, 0.85, 0.2), 42.0 * 0.001 * M::Petrol.Density, M::Petrol));
		P.push_back(Lump("radiator and coolant", Vec3(1.80, 0.0, 0.45), Vec3(0.1, 0.6, 0.4), 12.0, M::Aluminium));
		P.push_back(Lump("battery", Vec3(1.55, -0.45, 0.65), Vec3(0.25, 0.18, 0.2), 12.0, M::Lead));
		P.push_back(Lump("exhaust", Vec3(-0.4, -0.3, 0.25), Vec3(3.0, 0.2, 0.15), 18.0));
		P.push_back(Lump("seats", Vec3(-0.3, 0.0, 0.55), Vec3(1.4, 1.3, 0.6), 50.0, M::Foam));
		P.push_back(Lump("dashboard, carpets, trim", Vec3(0.3, 0.0, 0.70), Vec3(2.0, 1.5, 0.7), 55.0, M::Polypropylene));
		P.push_back(Lump("wiring, lights, wipers, air conditioning", Vec3(0.6, 0.0, 0.60), Vec3(3.0, 1.5, 0.6), 45.0, M::Copper));
		P.push_back(Lump("steering", Vec3(1.05, 0.0, 0.45), Vec3(0.3, 1.0, 0.3), 12.0));
		P.push_back(Lump("space-saver spare wheel, jack and tools", Vec3(-1.75, 0.0, 0.35), Vec3(0.6, 0.6, 0.2), 16.0));
		P.push_back(Lump("insulation, bumper beams, mirrors, door trims, the rest", Vec3(0.2, 0.0, 0.55), Vec3(3.6, 1.6, 0.8), 30.0, M::Polypropylene));
		P.push_back(Lump("driver", Vec3(-0.20, -0.36, 0.55), Vec3(0.45, 0.45, 0.80), 75.0, M::Water));
		return P;
	}

	std::vector<Part> FormulaParts()
	{
		// today's grand prix car: 798 kg with its driver (the rules' least, before fuel), 3.6 m wheelbase; a carbon tub, the
		// hybrid power unit (at least 150 kg) and its battery behind the driver, the gearbox to the rear axle, 18 inch
		// wheels; ballast low in the floor to the rules' weight; 110 kg of fuel at the start
		const double Front = 1.8, Rear = -1.8;
		std::vector<Part> P;
		P.push_back(Lump("survival cell (carbon tub)", Vec3(0.3, 0.0, 0.35), Vec3(2.2, 0.8, 0.6), 70.0, M::CarbonFibre));
		P.push_back(Lump("power unit (1.6 V6 turbo, its motors)", Vec3(-0.95, 0.0, 0.33), Vec3(0.7, 0.6, 0.55), 150.0, M::Aluminium));
		P.push_back(Lump("battery (the energy store)", Vec3(-0.35, 0.0, 0.18), Vec3(0.5, 0.5, 0.15), 22.0, M::Aluminium));
		P.push_back(Lump("gearbox (eight speeds)", Vec3(-1.55, 0.0, 0.30), Vec3(0.6, 0.35, 0.35), 40.0, M::Titanium));
		P.push_back(Lump("fuel cell (110 kg at the start)", Vec3(-0.35, 0.0, 0.32), Vec3(0.6, 0.7, 0.5), 110.0, M::Petrol));
		Pair(P, "sidepod, radiators and coolant", Vec3(-0.3, 0.5, 0.33), Vec3(1.4, 0.4, 0.4), 30.0, M::Aluminium);
		P.push_back(Lump("floor and plank", Vec3(-0.4, 0.0, 0.05), Vec3(3.4, 1.8, 0.05), 25.0, M::CarbonFibre));
		// (the rules' 798 kg is with the driver, without fuel: the rest is ballast, low and forward, where the teams put it to
		// set the balance)
		P.push_back(Lump("ballast (to the rules' weight, in the floor)", Vec3(1.1, 0.0, 0.06), Vec3(1.0, 0.5, 0.04), 84.0, M::Lead));
		P.push_back(Lump("front wing and nose", Vec3(2.45, 0.0, 0.15), Vec3(0.8, 1.8, 0.3), 12.0, M::CarbonFibre));
		P.push_back(Lump("rear wing", Vec3(-2.25, 0.0, 0.85), Vec3(0.5, 1.0, 0.3), 10.0, M::CarbonFibre));
		P.push_back(Lump("engine cover, halo, bodywork", Vec3(-0.4, 0.0, 0.65), Vec3(2.4, 0.9, 0.5), 35.0, M::CarbonFibre));
		Pair(P, "front suspension and brakes", Vec3(Front, 0.65, 0.33), Vec3(0.4, 0.5, 0.3), 20.0, M::CarbonFibre);
		Pair(P, "rear suspension and brakes", Vec3(Rear, 0.62, 0.33), Vec3(0.4, 0.5, 0.3), 22.0, M::CarbonFibre);
		Pair(P, "front wheel (18 inch)", Vec3(Front, 0.80, 0.36), Vec3(0.72, 0.30, 0.72), 20.0, M::Rubber);
		Pair(P, "rear wheel (18 inch)", Vec3(Rear, 0.78, 0.36), Vec3(0.72, 0.40, 0.72), 23.0, M::Rubber);
		P.push_back(Lump("wiring, electronics, hydraulics", Vec3(-0.3, 0.0, 0.35), Vec3(2.0, 0.6, 0.4), 40.0, M::Copper));
		P.push_back(Lump("driver (with his seat, 80 kg by the rules)", Vec3(0.55, 0.0, 0.30), Vec3(1.0, 0.45, 0.4), 80.0, M::Water));
		return P;
	}
}
