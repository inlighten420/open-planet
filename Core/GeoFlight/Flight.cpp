// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Flight.h"

#include <algorithm>

namespace geoflight
{
	namespace
	{
		constexpr double Gravity = 9.81;
		constexpr double SeaDensity = 1.225;
		constexpr double Pi = 3.14159265358979;
		constexpr double StepLength = 1.0 / 240.0;

		double Smooth(double A, double B, double X)
		{
			const double T = std::clamp((X - A) / (B - A), 0.0, 1.0);
			return T * T * (3.0 - 2.0 * T);
		}

		double Ease(double Value, double Target, double Seconds, double TimeConstant)
		{
			return Value + (Target - Value) * (1.0 - std::exp(-Seconds / std::max(TimeConstant, 1e-3)));
		}

		// (GeoBody's and our own: the same frame, the same numbers)
		geobody::Vec3 ToBody(const Vec3& V) { return geobody::Vec3(V.X, V.Y, V.Z); }
		Vec3 FromBody(const geobody::Vec3& V) { return Vec3(V.X, V.Y, V.Z); }
		geobody::Quat ToBody(const Quat& Q) { return geobody::Quat{ Q.W, Q.X, Q.Y, Q.Z }; }
		Quat FromBody(const geobody::Quat& Q) { return Quat{ Q.W, Q.X, Q.Y, Q.Z }; }
	}

	// --- rotations ---

	Vec3 Quat::Rotate(const Vec3& V) const
	{
		const Vec3 Q(X, Y, Z);
		const Vec3 T = Q.Cross(V) * 2.0;
		return V + T * W + Q.Cross(T);
	}

	Vec3 Quat::Unrotate(const Vec3& V) const
	{
		const Quat C{ W, -X, -Y, -Z };
		return C.Rotate(V);
	}

	Quat Quat::operator*(const Quat& B) const
	{
		return { W * B.W - X * B.X - Y * B.Y - Z * B.Z, W * B.X + X * B.W + Y * B.Z - Z * B.Y,
			W * B.Y - X * B.Z + Y * B.W + Z * B.X, W * B.Z + X * B.Y - Y * B.X + Z * B.W };
	}

	Quat Quat::Normal() const
	{
		const double L = std::sqrt(W * W + X * X + Y * Y + Z * Z);
		return L > 1e-12 ? Quat{ W / L, X / L, Y / L, Z / L } : Quat();
	}

	Quat Quat::FromAxisAngle(const Vec3& Axis, double Angle)
	{
		const Vec3 A = Axis.Normal();
		const double S = std::sin(Angle * 0.5);
		return { std::cos(Angle * 0.5), A.X * S, A.Y * S, A.Z * S };
	}

	Quat Quat::FromHeading(double Heading, double Pitch)
	{
		// the nose (x) from east round to the heading (anticlockwise about up is from east towards north), then up
		// (about the left wing, which turns the nose down for a positive angle)
		return (FromAxisAngle(Vec3(0.0, 0.0, 1.0), Pi * 0.5 - Heading) * FromAxisAngle(Vec3(0.0, 1.0, 0.0), -Pitch)).Normal();
	}

	void Atmosphere(double Height, double& OutDensity, double& OutSound)
	{
		const double H = std::clamp(Height, -500.0, 30000.0);
		if (H < 11000.0)
		{
			const double T = 288.15 - 0.0065 * H;
			OutDensity = SeaDensity * std::pow(T / 288.15, 4.2559);
			OutSound = 340.29 * std::sqrt(T / 288.15);
		}
		else
		{
			OutDensity = 0.3639 * std::exp(-(H - 11000.0) / 6341.6);
			OutSound = 295.07;
		}
	}

	// --- the aircraft ---

	Aircraft::Aircraft(const Design& InDesign) : D(InDesign)
	{
		Squash.assign(D.Wheels.size(), 0.0);
		GroundUnder.assign(D.Wheels.size(), Vec3());
		GroundNormal.assign(D.Wheels.size(), Vec3(0.0, 0.0, 1.0));
		GroundFound.assign(D.Wheels.size(), false);
		Frame.SetParts(D.Parts);
	}

	void Aircraft::Sync()
	{
		Pos = FromBody(Frame.Origin());
		Rot = FromBody(Frame.Rotation());
		Vel = FromBody(Frame.Velocity());
		Omega = FromBody(Frame.OwnSpin());
	}

	void Aircraft::Move(const Vec3& Offset)
	{
		Frame.Move(ToBody(Offset));
		Pos += Offset;
	}

	void Aircraft::Place(const Vec3& Position, const Quat& Rotation, double Speed)
	{
		// Position is where the wheels stand: the aircraft over it, its tyres just touching
		double Lowest = 0.0;
		for (const Wheel& W : D.Wheels)
		{
			Lowest = std::min(Lowest, W.At.Z);
		}
		Rot = Rotation.Normal();
		Pos = Position + Vec3(0.0, 0.0, -Lowest + 0.05);
		Vel = Rot.Rotate(Vec3(Speed, 0.0, 0.0));
		Omega = Vec3();
		Frame.Place(ToBody(Pos), ToBody(Rot));
		Frame.SetVelocity(ToBody(Vel));
		Spool = SpoolAB = 0.0;
		GearState = 1.0;
		FlapState = 0.0;
		FlapSetting = 0;
		AirborneTime = Speed > 0.0 ? 10.0 : 0.0;
		Out = Readout();
	}

	void Aircraft::Step(double Seconds, const Controls& Input, const RayFn& Ray)
	{
		if (Out.bCrashed || Seconds <= 0.0)
		{
			return;
		}
		const Vec3 Up(0.0, 0.0, 1.0);
		// where the ground is under each wheel (a plane through the point found), once a step
		for (size_t i = 0; i < D.Wheels.size(); ++i)
		{
			const Vec3 At = Pos + Rot.Rotate(D.Wheels[i].At);
			Vec3 Hit, Normal;
			GroundFound[i] = Ray && Ray(At + Up * 3.0, At - Up * 6.0, Hit, Normal);
			GroundUnder[i] = Hit;
			GroundNormal[i] = Normal.Z > 0.2 ? Normal.Normal() : Up;
		}
		const Vec3 Before = Pos;
		const int Steps = std::max(1, int(std::ceil(Seconds / StepLength)));
		for (int s = 0; s < Steps && !Out.bCrashed; ++s)
		{
			Substep(Seconds / Steps, Input, Ray, false);
		}
		if (!Ray || Out.bCrashed)
		{
			return;
		}
		// anything hit: along the way, and between the middle and the hard points (nose, tail, wingtips, engines; the
		// wheels too with the gear up)
		Vec3 Hit, Normal;
		auto Crash = [&](const char* What)
		{
			Out.bCrashed = true;
			Out.Crash = What;
			Vel = Vec3();
			Omega = Vec3();
			Frame.SetVelocity(geobody::Vec3());
			Frame.SetSpin(geobody::Vec3());
		};
		if ((Pos - Before).Length() > 0.5 && Ray(Before, Pos, Hit, Normal))
		{
			Crash("Flew into something");
			return;
		}
		std::vector<Vec3> Points = D.Hard;
		if (GearState < 0.9)
		{
			for (const Wheel& W : D.Wheels)
			{
				Points.push_back(W.At);
			}
		}
		for (const Vec3& P : Points)
		{
			if (Ray(Pos, Pos + Rot.Rotate(P), Hit, Normal))
			{
				Crash(GearState < 0.9 && Out.bOnGround ? "Landed with the gear up" : Out.Airspeed > 40.0 ? "Hit the ground" : "Scraped it");
				return;
			}
		}
		// how high: over what's below the wheels
		double Lowest = 0.0;
		for (const Wheel& W : D.Wheels)
		{
			Lowest = std::min(Lowest, W.At.Z);
		}
		Out.Height = Ray(Pos, Pos - Up * 3000.0, Hit, Normal) ? Pos.Z + Lowest - Hit.Z : -1.0;
	}

	void Aircraft::Substep(double Dt, const Controls& Input, const RayFn&, bool)
	{
		// its body moves it (GeoBody: gravity, then its pushes, then the motion), as every body
		geobody::Surroundings Round;
		Round.Gravity = geobody::Vec3(0.0, 0.0, -Gravity);
		Frame.Step(Dt, Round, [this, &Input](geobody::Body& On, double Step) { Push(On, Input, Step); });
		Sync();
	}

	void Aircraft::Push(geobody::Body& On, const Controls& Input, double Dt)
	{
		Sync();
		const Vec3 Up(0.0, 0.0, 1.0);
		const double Mass = D.Mass;

		// --- the engines, gear and flaps move ---
		// (in reverse the engines spool up as for thrust; the reversers send it forward. Only on the wheels, at idle)
		const bool bWheels = AirborneTime < 0.3;
		const bool bReversing = D.ReverseShare > 0.0 && bWheels && Input.Reverse > 0.05 && Input.Throttle < 0.05;
		const double Throttle = bReversing ? std::clamp(Input.Reverse, 0.0, 1.0) : std::clamp(Input.Throttle, 0.0, 1.0);
		Spool = Ease(Spool, Throttle, Dt, Throttle > Spool ? D.SpoolUp : D.SpoolDown);
		const bool bBurning = Input.bAfterburner && D.ThrustAfterburner > D.Thrust && Throttle > 0.95 && Spool > 0.9;
		SpoolAB = Ease(SpoolAB, bBurning ? 1.0 : 0.0, Dt, 1.0);
		if (!(Out.bOnGround && !Input.bGearDown))	// (it won't come up with the weight on it)
		{
			GearState = std::clamp(GearState + (Input.bGearDown ? 1.0 : -1.0) * Dt / D.GearTime, 0.0, 1.0);
		}
		FlapSetting = std::clamp(Input.Flaps, 0, int(D.FlapNames.size()) - 1);
		FlapState = FlapState < FlapSetting ? std::min(double(FlapSetting), FlapState + 0.25 * Dt) : std::max(double(FlapSetting), FlapState - 0.25 * Dt);
		const int FlapLow = std::clamp(int(FlapState), 0, int(D.FlapLift.size()) - 1);
		const int FlapHigh = std::min(FlapLow + 1, int(D.FlapLift.size()) - 1);
		const double FlapT = FlapState - FlapLow;
		const double FlapLift = D.FlapLift[FlapLow] + (D.FlapLift[FlapHigh] - D.FlapLift[FlapLow]) * FlapT;
		const double FlapDrag = D.FlapDrag[FlapLow] + (D.FlapDrag[FlapHigh] - D.FlapDrag[FlapLow]) * FlapT;

		// --- the air ---
		double Density = SeaDensity, Sound = 340.0;
		Atmosphere(Pos.Z, Density, Sound);
		const Vec3 Air = Rot.Unrotate(Vel);	// (no wind: the air is still)
		const double Speed = Air.Length();
		const double Dynamic = 0.5 * Density * Speed * Speed;
		double Alpha = 0.0, Beta = 0.0, CL = 0.0;
		Out.bSpoilers = false;
		Vec3 Force;	// the aircraft's frame: everything but gravity
		Vec3 Torque;
		if (Speed > 1.0)
		{
			Alpha = std::atan2(-Air.Z, Air.X);
			Beta = std::asin(std::clamp(Air.Y / Speed, -1.0, 1.0));
			const double Linear = D.CL0 + D.CLAlpha * Alpha + FlapLift;
			const double Stalled = Smooth(D.AlphaStall, D.AlphaStall + 0.12, std::fabs(Alpha));
			CL = Linear * (1.0 - Stalled) + (std::sin(2.0 * Alpha) + FlapLift * 0.5) * Stalled;
			const double Mach = Speed / Sound;
			const double Wave = Mach > D.MachCritical ? D.WaveDrag * (Mach - D.MachCritical) * (Mach - D.MachCritical) : 0.0;
			const double CD = D.CD0 + D.K * Linear * Linear * (1.0 - Stalled) + 1.2 * std::sin(Alpha) * std::sin(Alpha) * Stalled
				+ D.CDGear * GearState + FlapDrag + (Input.bSpeedBrake ? D.CDSpeedBrake : 0.0) + std::min(Wave, D.WaveMax);
			// the ground spoilers: on the wheels at idle (or the speed brake there), the wing's lift dumped onto the wheels
			// (so the brakes bite) and drag added, as an airliner's are on landing
			const bool bDump = (D.bGroundSpoilers || Input.bSpeedBrake) && bWheels && Input.Throttle < 0.1 && Speed > 10.0;
			Out.bSpoilers = bDump;
			if (bDump)
			{
				CL *= 0.25;
			}
			const double Spoiled = bDump ? 0.08 : 0.0;
			const Vec3 InPlane = Vec3(Air.X, 0.0, Air.Z).Normal();
			const Vec3 LiftWay = InPlane.Cross(Vec3(0.0, 1.0, 0.0)).Normal();
			const double QS = Dynamic * D.WingArea;
			Force += LiftWay * (QS * CL) - Air * (QS * (CD + Spoiled) / Speed) + Vec3(0.0, -QS * 1.0 * Beta, 0.0);
		}
		double Thrust = (D.Thrust * (D.Idle + (1.0 - D.Idle) * std::pow(Spool, 2.8)) + (D.ThrustAfterburner - D.Thrust) * SpoolAB) * std::pow(Density / SeaDensity, 0.7);
		if (bReversing)
		{
			Thrust = -Thrust * D.ReverseShare;
		}
		Out.bReverse = bReversing;
		Force += Vec3(Thrust, 0.0, 0.0);
		const Vec3 AirAndThrust = Force;	// (pushed at the centre of mass; the wheels push where they touch)

		// --- the wheels: springs and tyres on the ground ---
		bool bTouching = false;
		const double GroundSpeed = Vec3(Vel.X, Vel.Y, 0.0).Length();
		for (size_t i = 0; i < D.Wheels.size(); ++i)
		{
			Squash[i] = 0.0;
			const Wheel& W = D.Wheels[i];
			if (GearState < 0.99 || !GroundFound[i])
			{
				continue;
			}
			const Vec3 Arm = Rot.Rotate(W.At);
			const Vec3 At = Pos + Arm;
			const Vec3& N = GroundNormal[i];
			const double Depth = (GroundUnder[i] - At).Dot(N);
			if (Depth <= 0.0)
			{
				continue;
			}
			bTouching = true;
			Squash[i] = std::min(Depth, W.Travel);
			const double Weight = W.Load * Mass * Gravity;
			const double Spring = Weight / (0.5 * W.Travel);
			const double Damper = 2.0 * 0.6 * std::sqrt(Spring * W.Load * Mass);
			const Vec3 PointVel = FromBody(On.VelocityAt(ToBody(At)));
			const double Sinking = PointVel.Dot(N);
			if (Sinking < -6.0)
			{
				Out.bCrashed = true;
				Out.Crash = "Landed too hard";
				return;
			}
			// (bottomed out: much stiffer)
			const double Press = Spring * Depth + (Depth > W.Travel ? Spring * 20.0 * (Depth - W.Travel) : 0.0) - Damper * Sinking;
			const double Normal = std::max(0.0, Press);
			// which way it rolls: the nose wheel steers (less as it speeds up)
			double Steer = 0.0;
			if (W.bSteers)
			{
				Steer = -std::clamp(Input.Yaw, -1.0, 1.0) * D.NoseSteer * std::clamp(1.0 - GroundSpeed / 30.0, 0.1, 1.0);
			}
			const Vec3 Pointing = Rot.Rotate(Vec3(std::cos(Steer), std::sin(Steer), 0.0));
			const Vec3 Rolling = (Pointing - N * Pointing.Dot(N)).Normal();
			const Vec3 Across = N.Cross(Rolling);
			const double Along = PointVel.Dot(Rolling), Sideways = PointVel.Dot(Across);
			const double Brake = W.bSteers ? 0.0 : D.BrakeGrip * std::clamp(Input.Brakes, 0.0, 1.0);
			const Vec3 Push = N * Normal - Across * (std::clamp(Sideways / 0.3, -1.0, 1.0) * 0.8 * Normal)
				- Rolling * (std::clamp(Along / 0.3, -1.0, 1.0) * (0.015 + Brake) * Normal);
			Force += Rot.Unrotate(Push);	// (for the g meter)
			On.Push(ToBody(Push), ToBody(At));
		}
		AirborneTime = bTouching ? 0.0 : AirborneTime + Dt;
		const bool bFlying = AirborneTime > 0.5;

		// --- the flight computer: the controls turn it as the stick asks, within what the air allows ---
		const Vec3 Nose = Rot.Rotate(Vec3(1.0, 0.0, 0.0)), Left = Rot.Rotate(Vec3(0.0, 1.0, 0.0)), Top = Rot.Rotate(Vec3(0.0, 0.0, 1.0));
		const double Pitch = std::asin(std::clamp(Nose.Z, -1.0, 1.0));
		const double Bank = std::atan2(Left.Z, Top.Z);	// right wing down: positive
		const double RollRate = Omega.X, PitchRate = -Omega.Y, YawRate = -Omega.Z;	// right, nose up, nose right
		const Vec3 Reach(std::min(D.Authority.X * Dynamic / D.Pressure, D.AuthorityCap.X),
			std::min(D.Authority.Y * Dynamic / D.Pressure, D.AuthorityCap.Y), std::min(D.Authority.Z * Dynamic / D.Pressure, D.AuthorityCap.Z));
		const double Fast = std::max(Speed, 30.0);
		const double Response = D.Response;
		double WantRoll = 0.0, WantPitch = 0.0, WantYaw = 0.0;	// rad/s2
		Out.bStall = false;
		if (bFlying)
		{
			// pitch: the stick asks for a g (held level let go, turns included), as an angle of attack, never past the limit
			const double Level = std::cos(Pitch) / std::cos(std::clamp(Bank, -1.05, 1.05));
			const double Stick = std::clamp(Input.Pitch, -1.0, 1.0);
			const double GWanted = Stick >= 0.0 ? Level + Stick * (D.GMax - Level) : Level + Stick * (Level - D.GMin);
			const double LiftWanted = GWanted * Mass * Gravity / std::max(Dynamic * D.WingArea, 1.0);
			const double AlphaWanted = std::clamp((LiftWanted - D.CL0 - FlapLift) / D.CLAlpha, -0.2, D.AlphaLimit);
			Out.bStall = Alpha > D.AlphaLimit - 0.015;
			const double GGot = Dynamic * D.WingArea * (D.CL0 + FlapLift + D.CLAlpha * AlphaWanted) / (Mass * Gravity);
			const double Turning = Gravity * (GGot - std::cos(Pitch) * std::cos(Bank)) / Fast;
			WantPitch = Response * Response * (AlphaWanted - Alpha) + 1.6 * Response * (Turning - PitchRate);
			// roll: the stick asks for a roll rate; let go, the bank holds (an airliner's rolls back to 33 degrees past it,
			// and stops at its limit)
			double RollWanted = std::clamp(Input.Roll, -1.0, 1.0) * D.RollRate;
			if (D.BankLimit > 0.0)
			{
				const double Gentle = 33.0 * Pi / 180.0;
				if (std::fabs(Input.Roll) < 0.05 && std::fabs(Bank) > Gentle)
				{
					RollWanted = -std::copysign(std::min(0.1, (std::fabs(Bank) - Gentle) * 0.5), Bank);
				}
				if (std::fabs(Bank) > D.BankLimit && RollWanted * Bank > 0.0)
				{
					RollWanted = -std::copysign(0.05, Bank);
				}
			}
			WantRoll = 2.0 * Response * (RollWanted - RollRate);
			// yaw: turns kept tidy (no slipping), the pedals add a little
			const double YawWanted = Gravity * std::sin(Bank) * std::cos(Pitch) / Fast + std::clamp(Input.Yaw, -1.0, 1.0) * 0.15;
			WantYaw = 2.0 * (YawWanted - YawRate) - 3.0 * Response * Beta;
		}
		else
		{
			// on the wheels: the stick asks for a pitch rate (rotating for take-off, as pilots do, a few degrees a second;
			// let go and the nose stays), the rudder from the pedals; the wings held level by the gear
			WantPitch = 20.0 * (std::clamp(Input.Pitch, -1.0, 1.0) * D.RotateRate - PitchRate);	// (firm: the weight on the main wheels holds the nose down)
			WantYaw = std::clamp(Input.Yaw, -1.0, 1.0) * Reach.Z;
			WantRoll = -2.0 * RollRate;
		}
		WantRoll = std::clamp(WantRoll, -Reach.X, Reach.X);
		WantPitch = std::clamp(WantPitch, -Reach.Y, Reach.Y);
		WantYaw = std::clamp(WantYaw, -Reach.Z, Reach.Z);
		const Vec3& I = D.Inertia;
		Torque += Vec3(I.X * WantRoll, -I.Y * WantPitch, -I.Z * WantYaw);
		// (the air damps any turning a little, as the tail and wings do)
		Torque += Vec3(I.X * Omega.X, I.Y * Omega.Y, I.Z * Omega.Z) * (-0.3 * std::min(Dynamic / D.Pressure, 2.0));

		// --- push: the air and the engines at the centre of mass, the controls' turning (the body moves it) ---
		On.Push(ToBody(Rot.Rotate(AirAndThrust)), On.CentreOfMass());
		On.Twist(ToBody(Rot.Rotate(Torque)));

		// --- the instruments ---
		Out.TrueAirspeed = Speed;
		Out.Airspeed = Speed * std::sqrt(Density / SeaDensity);
		Out.GroundSpeed = Vec3(Vel.X, Vel.Y, 0.0).Length();
		Out.Mach = Speed / Sound;
		Out.Altitude = Pos.Z;
		Out.Climb = Vel.Z;
		Out.Heading = std::fmod(std::atan2(Nose.X, Nose.Y) * 180.0 / Pi + 360.0, 360.0);
		Out.Pitch = Pitch * 180.0 / Pi;
		Out.Bank = Bank * 180.0 / Pi;
		Out.Alpha = Alpha * 180.0 / Pi;
		Out.G = Force.Z / (Mass * Gravity);
		Out.Engine = Spool;
		Out.bAfterburner = SpoolAB > 0.5;
		Out.Gear = GearState;
		Out.bOnGround = !bFlying;
	}

	// --- the designs ---

	namespace
	{
		using geobody::Lump;
		namespace Mat = geobody::Materials;

		/** A pair of the same lump, either side (y: its distance out) */
		void Pair(std::vector<geobody::Part>& Parts, const std::string& Name, const geobody::Vec3& At, const geobody::Vec3& Size, double Kg,
			const geobody::Material& Of = Mat::Aluminium)
		{
			Parts.push_back(Lump(Name, At, Size, Kg, Of));
			Parts.push_back(Lump(Name, geobody::Vec3(At.X, -At.Y, At.Z), Size, Kg, Of));
		}

		/** Its mass, balance and inertia from what it's made of */
		void FromParts(Design& D)
		{
			const geobody::MassProperties M = geobody::Measure(D.Parts);
			D.Mass = M.Mass;
			D.Inertia = Vec3(M.Inertia.M[0][0], M.Inertia.M[1][1], M.Inertia.M[2][2]);
			D.CentreOfMass = Vec3(M.CentreOfMass.X, M.CentreOfMass.Y, M.CentreOfMass.Z);
		}
	}

	Design FighterJet()
	{
		// an F-16's numbers: 27.9 m2 of wing (9.96 m span), the F100's 79 kN dry
		// and 129 kN with afterburner, 9 g, 240 degrees a second of roll, 25 degrees of angle of attack at most
		Design D;
		D.Name = "Fighter jet";
		D.WingArea = 27.87;
		D.Span = 9.96;
		D.Chord = 3.45;
		D.CL0 = 0.08;
		D.CLAlpha = 4.3;	// (its strakes' vortices add lift at high angles)
		D.AlphaStall = 0.52;
		D.AlphaLimit = 0.436;
		D.CD0 = 0.021;
		D.K = 0.12;
		D.CDGear = 0.015;
		D.CDSpeedBrake = 0.06;
		D.MachCritical = 0.85;
		D.WaveDrag = 1.5;
		D.WaveMax = 0.035;	// (about 0.055 in all past Mach 1: it takes the afterburner to go supersonic low down)
		D.Thrust = 79000.0;
		D.ThrustAfterburner = 129000.0;
		D.Idle = 0.05;
		D.SpoolUp = 3.0;
		D.SpoolDown = 2.0;
		D.GMax = 9.0;
		D.GMin = -3.0;
		D.RollRate = 4.2;
		D.Pressure = 10000.0;
		D.Authority = Vec3(15.0, 5.0, 2.0);
		D.AuthorityCap = Vec3(20.0, 6.0, 3.0);
		D.Response = 4.0;
		D.RotateRate = 0.12;
		D.Wheels = { { Vec3(4.0, 0.0, -1.85), 0.25, 0.12, true }, { Vec3(-0.6, 1.18, -1.85), 0.25, 0.44, false }, { Vec3(-0.6, -1.18, -1.85), 0.25, 0.44, false } };
		D.BrakeGrip = 0.45;
		D.NoseSteer = 0.55;
		D.GearTime = 5.0;
		D.Hard = { Vec3(7.5, 0.0, -0.3), Vec3(-7.2, 0.0, -0.2), Vec3(-1.5, 4.98, -0.3), Vec3(-1.5, -4.98, -0.3), Vec3(-6.6, 0.0, 3.0) };
		D.Eye = Vec3(4.9, 0.0, 1.0);
		D.ChaseDistance = 18.0;
		// what it's made of (m from the wing's quarter chord; the group weights of an F-16C, ~8.4 t empty): the
		// structure, the F100 engine aft, the gear, the radar and cockpit in the nose; a 100 kg pilot on his seat, half
		// its internal fuel, two wingtip missiles, two half-full 370 gallon tanks under the wings, the gun's rounds
		using geobody::Vec3;
		std::vector<geobody::Part>& P = D.Parts;
		// (the fuselage's weight is mostly in its middle, round the wing's carry-through and the main bulkheads)
		P.push_back(Lump("centre fuselage (the wing's carry-through, bulkheads)", Vec3(0.0, 0.0, 0.1), Vec3(5.0, 1.2, 1.5), 1300.0, Mat::Aluminium));
		P.push_back(Lump("forward fuselage", Vec3(4.5, 0.0, 0.2), Vec3(5.0, 1.0, 1.3), 450.0, Mat::Aluminium));
		P.push_back(Lump("aft fuselage", Vec3(-4.5, 0.0, 0.1), Vec3(5.0, 1.2, 1.3), 550.0, Mat::Aluminium));
		P.push_back(Lump("wing", Vec3(-0.5, 0.0, 0.0), Vec3(3.4, 9.0, 0.25), 1000.0, Mat::Aluminium));
		P.push_back(Lump("tailplanes", Vec3(-5.8, 0.0, 0.0), Vec3(2.0, 5.6, 0.1), 200.0, Mat::Aluminium));
		P.push_back(Lump("fin", Vec3(-5.6, 0.0, 1.8), Vec3(3.0, 0.2, 2.6), 150.0, Mat::Aluminium));
		P.push_back(Lump("engine (F100-PW-229: its weight towards the front, the nozzle light)", Vec3(-2.2, 0.0, 0.0), Vec3(4.85, 1.18, 1.18), 1700.0, Mat::Titanium));
		P.push_back(Lump("nose gear", Vec3(4.0, 0.0, -1.1), Vec3(0.6, 0.3, 0.8), 120.0, Mat::Steel));
		Pair(P, "main gear", Vec3(-0.6, 1.18, -1.1), Vec3(0.8, 0.4, 0.8), 200.0, Mat::Steel);
		P.push_back(Lump("radar and nose", Vec3(5.6, 0.0, 0.0), Vec3(1.5, 0.8, 0.8), 400.0));
		P.push_back(Lump("cockpit and avionics bays", Vec3(2.6, 0.0, 0.3), Vec3(3.0, 1.0, 1.0), 700.0));
		P.push_back(Lump("gun, hydraulics, electrics, air conditioning, fuel system", Vec3(0.5, 0.0, 0.0), Vec3(6.0, 1.2, 1.2), 1400.0));
		P.push_back(Lump("pilot and ejection seat", Vec3(4.9, 0.0, 0.6), Vec3(0.8, 0.6, 1.2), 170.0, Mat::Water));
		P.push_back(Lump("fuel, fuselage tanks (half)", Vec3(0.0, 0.0, 0.1), Vec3(5.0, 1.0, 0.9), 1100.0, Mat::JetFuel));
		P.push_back(Lump("fuel, wing tanks (half)", Vec3(-0.6, 0.0, 0.0), Vec3(2.5, 6.0, 0.2), 490.0, Mat::JetFuel));
		Pair(P, "wingtip missile", Vec3(-1.5, 4.9, 0.0), Vec3(2.9, 0.13, 0.13), 86.0, Mat::Steel);
		Pair(P, "drop tank, half full", Vec3(-0.3, 2.4, -0.9), Vec3(4.6, 0.6, 0.6), 700.0, Mat::JetFuel);
		P.push_back(Lump("rounds, pylons", Vec3(0.5, 0.0, -0.3), Vec3(3.0, 1.0, 0.4), 300.0, Mat::Steel));
		FromParts(D);
		return D;
	}

	Design Airliner()
	{
		// an A320's numbers: 122.6 m2 of wing (34.1 m span), two 120 kN engines, flaps 1, 2, 3
		// and full, 2.5 g, 15 degrees a second of roll, the bank stopped at 67 degrees, the angle of attack held under 12
		Design D;
		D.Name = "Airliner";
		D.WingArea = 122.6;
		D.Span = 34.1;
		D.Chord = 4.29;
		D.CL0 = 0.3;
		D.CLAlpha = 5.2;
		D.AlphaStall = 0.26;
		D.AlphaLimit = 0.21;
		D.CD0 = 0.022;
		D.K = 0.042;
		D.CDGear = 0.02;
		D.CDSpeedBrake = 0.04;
		D.MachCritical = 0.78;
		D.WaveDrag = 8.0;
		D.FlapNames = { "Up", "1", "2", "3", "Full" };
		D.FlapLift = { 0.0, 0.35, 0.6, 0.85, 1.15 };
		D.FlapDrag = { 0.0, 0.01, 0.025, 0.045, 0.075 };
		D.Thrust = 240000.0;
		D.Idle = 0.05;
		D.SpoolUp = 5.0;
		D.SpoolDown = 3.0;
		D.ReverseShare = 0.35;
		D.bGroundSpoilers = true;
		D.GMax = 2.5;
		D.GMin = -1.0;
		D.RollRate = 0.26;
		D.BankLimit = 67.0 * Pi / 180.0;
		D.Pressure = 5000.0;
		D.Authority = Vec3(0.6, 0.45, 0.25);
		D.AuthorityCap = Vec3(1.0, 0.6, 0.4);
		D.Response = 1.5;
		D.RotateRate = 0.055;
		D.Wheels = { { Vec3(11.5, 0.0, -3.4), 0.45, 0.08, true }, { Vec3(-1.2, 3.8, -3.4), 0.45, 0.46, false }, { Vec3(-1.2, -3.8, -3.4), 0.45, 0.46, false } };
		D.BrakeGrip = 0.4;
		D.NoseSteer = 1.3;
		D.GearTime = 8.0;
		D.Hard = { Vec3(17.5, 0.0, -1.2), Vec3(-18.0, 0.0, 0.0), Vec3(-3.0, 17.0, -0.4), Vec3(-3.0, -17.0, -0.4),
			Vec3(2.0, 5.75, -2.7), Vec3(2.0, -5.75, -2.7), Vec3(-17.0, 0.0, 8.5) };
		D.Eye = Vec3(15.5, 0.5, 0.6);
		D.ChaseDistance = 55.0;
		// what it's made of (m from the wing's quarter chord; an A320's 42.6 t empty, in a narrowbody's group weights):
		// the fuselage, the wing, the tail, two CFM56-5Bs (2.38 t each, with nacelle and pylon 3.6 t), the gear, the
		// systems and furnishings; 150 passengers with their bags (95 kg each), and 7.2 t of fuel in the wings
		using geobody::Vec3;
		std::vector<geobody::Part>& P = D.Parts;
		P.push_back(Lump("fuselage structure", Vec3(-0.3, 0.0, 0.0), Vec3(36.0, 3.95, 4.1), 9500.0, Mat::Aluminium));
		P.push_back(Lump("wing", Vec3(-0.6, 0.0, -1.5), Vec3(4.3, 34.0, 0.5), 8800.0, Mat::Aluminium));
		P.push_back(Lump("tailplane", Vec3(-15.5, 0.0, 1.0), Vec3(3.0, 12.5, 0.3), 700.0, Mat::Aluminium));
		P.push_back(Lump("fin", Vec3(-15.0, 0.0, 4.5), Vec3(4.5, 0.3, 5.5), 500.0, Mat::Aluminium));
		Pair(P, "engine (CFM56-5B), nacelle and pylon", Vec3(2.0, 5.75, -2.3), Vec3(4.0, 2.0, 2.0), 3600.0, Mat::Titanium);
		P.push_back(Lump("nose gear", Vec3(11.5, 0.0, -2.4), Vec3(1.0, 0.5, 1.2), 300.0, Mat::Steel));
		Pair(P, "main gear", Vec3(-1.2, 3.8, -2.4), Vec3(1.2, 0.8, 1.6), 1000.0, Mat::Steel);
		P.push_back(Lump("systems, furnishings and crew", Vec3(1.0, 0.0, 0.0), Vec3(30.0, 3.7, 3.0), 13300.0));
		P.push_back(Lump("150 passengers and their bags", Vec3(-0.5, 0.0, 0.2), Vec3(27.0, 3.5, 2.5), 14250.0, Mat::Water));
		P.push_back(Lump("fuel, wing tanks", Vec3(-0.3, 0.0, -1.5), Vec3(3.0, 24.0, 0.4), 7150.0, Mat::JetFuel));
		FromParts(D);
		return D;
	}

	Design RegionalJet()
	{
		// an Embraer E195's numbers (George to Cape Town's jet): 92.5 m2 of wing (28.7 m span),
		// two 89 kN engines, flaps 1 to 5 and full, 2.5 g; its fly-by-wire guards the stall but not the bank
		Design D;
		D.Name = "Regional jet";
		D.WingArea = 92.5;
		D.Span = 28.72;
		D.Chord = 3.5;
		D.CL0 = 0.3;
		D.CLAlpha = 5.0;
		D.AlphaStall = 0.26;
		D.AlphaLimit = 0.21;
		D.CD0 = 0.023;
		D.K = 0.045;
		D.CDGear = 0.02;
		D.CDSpeedBrake = 0.04;
		D.MachCritical = 0.78;
		D.WaveDrag = 8.0;
		D.FlapNames = { "Up", "1", "2", "3", "4", "5", "Full" };
		D.FlapLift = { 0.0, 0.3, 0.45, 0.6, 0.75, 0.95, 1.15 };
		D.FlapDrag = { 0.0, 0.008, 0.015, 0.025, 0.035, 0.055, 0.075 };
		D.Thrust = 178000.0;
		D.Idle = 0.05;
		D.SpoolUp = 5.0;
		D.SpoolDown = 3.0;
		D.ReverseShare = 0.35;
		D.bGroundSpoilers = true;
		D.GMax = 2.5;
		D.GMin = -1.0;
		D.RollRate = 0.35;
		D.Pressure = 5000.0;
		D.Authority = Vec3(0.7, 0.5, 0.3);
		D.AuthorityCap = Vec3(1.2, 0.7, 0.45);
		D.Response = 1.7;
		D.RotateRate = 0.055;
		D.Wheels = { { Vec3(13.5, 0.0, -2.9), 0.4, 0.08, true }, { Vec3(-1.2, 2.97, -2.9), 0.4, 0.46, false }, { Vec3(-1.2, -2.97, -2.9), 0.4, 0.46, false } };
		D.BrakeGrip = 0.4;
		D.NoseSteer = 1.2;
		D.GearTime = 7.0;
		D.Hard = { Vec3(19.0, 0.0, -1.0), Vec3(-19.2, 0.0, 0.6), Vec3(-3.0, 14.36, -0.6), Vec3(-3.0, -14.36, -0.6),
			Vec3(1.5, 5.2, -2.3), Vec3(1.5, -5.2, -2.3), Vec3(-17.0, 0.0, 7.6) };
		D.Eye = Vec3(16.5, 0.5, 0.4);
		D.ChaseDistance = 50.0;
		// what it's made of (m from the wing's quarter chord; an E195's 29 t empty, in a narrowbody's group weights):
		// the fuselage, the wing, the tail, two CF34-10Es (1.7 t each, with nacelle and pylon 2.45 t), the gear, the
		// systems and furnishings; 100 passengers with their bags, and 3.5 t of fuel (George to Cape Town)
		using geobody::Vec3;
		std::vector<geobody::Part>& P = D.Parts;
		P.push_back(Lump("fuselage structure", Vec3(-0.2, 0.0, 0.0), Vec3(37.5, 3.0, 3.35), 7000.0, Mat::Aluminium));
		P.push_back(Lump("wing", Vec3(-0.5, 0.0, -1.3), Vec3(3.5, 28.7, 0.45), 5600.0, Mat::Aluminium));
		P.push_back(Lump("tailplane", Vec3(-16.5, 0.0, 1.5), Vec3(2.6, 10.0, 0.25), 500.0, Mat::Aluminium));
		P.push_back(Lump("fin", Vec3(-16.0, 0.0, 4.2), Vec3(4.0, 0.3, 5.0), 400.0, Mat::Aluminium));
		Pair(P, "engine (CF34-10E), nacelle and pylon", Vec3(1.5, 5.2, -2.1), Vec3(3.7, 1.6, 1.6), 2450.0, Mat::Titanium);
		P.push_back(Lump("nose gear", Vec3(13.5, 0.0, -2.0), Vec3(0.9, 0.5, 1.0), 200.0, Mat::Steel));
		Pair(P, "main gear", Vec3(-1.2, 2.97, -2.0), Vec3(1.0, 0.7, 1.4), 700.0, Mat::Steel);
		P.push_back(Lump("systems, furnishings and crew", Vec3(1.0, 0.0, 0.0), Vec3(31.0, 2.8, 2.5), 8970.0));
		P.push_back(Lump("100 passengers and their bags", Vec3(-0.5, 0.0, 0.2), Vec3(28.0, 2.7, 2.0), 9500.0, Mat::Water));
		P.push_back(Lump("fuel, wing tanks", Vec3(-0.3, 0.0, -1.3), Vec3(2.6, 18.0, 0.35), 3530.0, Mat::JetFuel));
		FromParts(D);
		return D;
	}
}
