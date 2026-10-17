// Geodrive cartest - AGPL-3.0 (see LICENSE)
//
// A test track in Core, no game engine: each car on its parts (CarMotion: the shared motion) on flat, dry tarmac,
// timed as a road test would: 0-100 and 0-200 km/h, its top speed, its stop from 100 km/h, and the most it corners at
// 80 km/h (the steering wound on slowly). Printed beside the real car's figures.

#include "GeoCar/CarBody.h"
#include "GeoCar/CarMotion.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace geocar;

namespace
{
	/** Flat, dry tarmac at z = 0 (CarSim's frame: x north, y east, z up) */
	class FlatWorld final : public CarWorld
	{
	public:
		bool SweepSphere(const Vec3& Start, const Vec3& End, double Radius, SweepHit& Hit) override
		{
			const double A = Start.Z - Radius, B = End.Z - Radius;
			if (A <= 0.0)
			{
				Hit.bStartPenetrating = true;
				Hit.Location = Start;
			}
			else if (B < 0.0)
			{
				const double T = A / (A - B);
				Hit.Location = Start + (End - Start) * T;
			}
			else
			{
				return false;
			}
			Hit.ImpactPoint = Vec3(Hit.Location.X, Hit.Location.Y, 0.0);
			Hit.ImpactNormal = Vec3(0.0, 0.0, 1.0);
			Hit.Grip = 1.0f;
			return true;
		}
		void AddForceAtPosition(const Vec3&, const Vec3&) override {}	// (CarMotion takes them)
	};

	struct Run
	{
		CarDesign Design;
		Vec3 Wheels[NumWheels];
		CarSim Sim;
		CarMotion Motion;
		FlatWorld World;
		Controls Input;
		double Time = 0.0;

		explicit Run(CarBody Body) : Design(designs::Build(Body, designs::DefaultTemplate()))
		{
			const CarFrame& F = Design.Frame;
			const Vec3 Origin((F.FrontX() + F.RearX()) * 0.5, 0.0, F.Ground());
			for (int i = 0; i < NumWheels; ++i)
			{
				Wheels[i] = (F.Wheels[i] - Origin) * 0.01;
			}
			Motion.Use(Design.Parts);
			BodyState S;
			S.Forward = Vec3(1.0, 0.0, 0.0);
			S.Right = Vec3(0.0, 1.0, 0.0);
			S.Up = Vec3(0.0, 0.0, 1.0);
			Motion.Sync(S);
			Input.bAutomatic = true;
			// settle on its springs
			for (int i = 0; i < 240; ++i)
			{
				Step();
			}
			Time = 0.0;
		}
		void Step()
		{
			Motion.Step(World, Sim, Design.Setup, Input, Wheels, 1.0f / 120.0f);
			Time += 1.0 / 120.0;
		}
		double Speed() const { const BodyState S = Motion.State(); return Dot(S.Velocity, S.Forward) * 3.6; }	// km/h
	};
}

int main(int Count, char** Args)
{
	// (cartest steer <1-4>: at 60 km/h, the steering wound on; the steering torque the driver would feel)
	if (Count > 2 && std::string(Args[1]) == "steer")
	{
		Run R(CarBody(std::atoi(Args[2])));
		R.Input.Throttle = 1.0f;
		while (R.Speed() < 60.0 && R.Time < 30.0) R.Step();
		const double Start = R.Time;
		while (R.Time - Start < 4.0)
		{
			R.Input.Throttle = float(std::clamp((60.0 - R.Speed()) * 0.2 + 0.2, 0.0, 1.0));
			R.Input.SteerDegrees = float(std::min((R.Time - Start) / 2.0, 1.0)) * 3.0f;
			R.Step();
			if (int((R.Time - Start) * 120.0 + 0.5) % 60 == 0)
			{
				const VehicleState& V = R.Sim.GetState();
				std::printf("%4.1f s  steer %.1f deg  kingpin torque %6.1f Nm  front side force %6.0f N  speed %.0f\n", R.Time - Start, R.Input.SteerDegrees,
					V.KingpinTorque, V.Wheels[0].ForceY + V.Wheels[1].ForceY, R.Speed());
			}
		}
		return 0;
	}
	// (cartest fast <1-4> [kmh] [degrees]: held at speed, a gentle steer wound on over 1 s; is it stable?)
	if (Count > 2 && std::string(Args[1]) == "fast")
	{
		Run R(CarBody(std::atoi(Args[2])));
		const double Target = Count > 3 ? std::atof(Args[3]) : 250.0;
		const float Steer = Count > 4 ? float(std::atof(Args[4])) : 1.0f;
		R.Input.Throttle = 1.0f;
		while (R.Speed() < Target && R.Time < 90.0) R.Step();
		const double Start = R.Time;
		while (R.Time - Start < 5.0)
		{
			R.Input.Throttle = float(std::clamp((Target - R.Speed()) * 0.1 + 0.5, 0.0, 1.0));
			R.Input.SteerDegrees = float(std::min((R.Time - Start), 1.0)) * Steer;
			R.Step();
			if (int((R.Time - Start) * 120.0 + 0.5) % 30 == 0)
			{
				const VehicleState& V = R.Sim.GetState();
				const BodyState S = R.Motion.State();
				const double Yaw = Dot(S.AngularVelocity, S.Up), Fwd = Dot(S.Velocity, S.Forward), Side = Dot(S.Velocity, S.Right);
				const double Roll = std::asin(std::clamp(Dot(S.Right, Vec3(0, 0, 1)), -1.0, 1.0)) * 57.3;
				std::printf("%4.2f s %4.0f km/h  lat %.2f g  slip angle %5.1f deg  roll %4.1f  loads F %5.0f %5.0f R %5.0f %5.0f  slipY F %5.2f R %5.2f  downforce %5.0f N\n",
					R.Time - Start, R.Speed(), Yaw * Fwd / 9.81, std::atan2(Side, Fwd) * 57.3, Roll, V.Wheels[0].Load, V.Wheels[1].Load, V.Wheels[2].Load, V.Wheels[3].Load,
					V.Wheels[0].SlipAngleDegrees, V.Wheels[2].SlipAngleDegrees, V.Downforce);
			}
		}
		return 0;
	}
	// (cartest trace <1-4>: a launch, every quarter second)
	if (Count > 2 && std::string(Args[1]) == "trace")
	{
		Run R(CarBody(std::atoi(Args[2])));
		R.Input.Throttle = 1.0f;
		for (int a = 3; a < Count; ++a)	// (experiments: tc0, inertia<x>, grip<x>)
		{
			const std::string A = Args[a];
			if (A == "tc0") R.Input.TCLevel = 0;
			if (A.rfind("inertia", 0) == 0) R.Design.Setup.Engine.Inertia = float(std::atof(A.c_str() + 7));
			if (A.rfind("grip", 0) == 0) { const float G = float(std::atof(A.c_str() + 4)); R.Design.Setup.Tire.PeakForceY *= G; R.Design.Setup.Tire.PeakForceY2 *= G; }
		}
		while (R.Time < 8.0)
		{
			R.Step();
			if (int(R.Time * 120.0 + 0.5) % 30 == 0)
			{
				const VehicleState& V = R.Sim.GetState();
				std::printf("%5.2f s  %5.1f km/h  gear %d  %5.0f rpm  %s%s  slip rear %.2f %.2f front %.2f\n", R.Time, R.Speed(), V.Gear, V.EngineRPM,
					V.bTCActive ? "TC " : "", V.bShifting ? "shifting" : "", V.Wheels[2].SlipRatio, V.Wheels[3].SlipRatio, V.Wheels[0].SlipRatio);
			}
		}
		return 0;
	}
	struct Car { CarBody Body; const char* Real; };
	const Car Cars[] = {
		{ CarBody::GT, "992 GT3 RS: 0-100 3.2 s, 0-200 10.6 s, 296 km/h, ~31 m from 100, ~1.4 g" },
		{ CarBody::Formula, "grand prix car: 0-100 ~2.6 s, 0-200 ~4.8 s, ~350 km/h, ~17 m from 100, 2.5+ g with downforce" },
		{ CarBody::Hatch, "Swift Sport (ZC32S): 0-100 8.7 s, 195 km/h, ~37 m from 100, ~0.9 g" },
		{ CarBody::Bakkie, "KB300 TDi double cab: 0-100 ~12.5 s, ~165 km/h, ~44 m from 100, ~0.75 g" } };
	for (const Car& C : Cars)
	{
		// the launch: flat out from rest
		double To100 = -1.0, To200 = -1.0, Top = 0.0, Steady = 0.0;
		{
			Run R(C.Body);
			R.Input.Throttle = 1.0f;
			while (R.Time < 150.0)
			{
				R.Step();
				const double V = R.Speed();
				if (To100 < 0.0 && V >= 100.0) To100 = R.Time;
				if (To200 < 0.0 && V >= 200.0) To200 = R.Time;
				if (V > Top + 0.05) { Top = V; Steady = R.Time; }
				if (R.Time - Steady > 10.0) break;	// (no faster for 10 s: its top speed)
			}
		}
		// the stop: up to 100, then the brakes hard
		double Stop = -1.0;
		{
			Run R(C.Body);
			R.Input.Throttle = 1.0f;
			while (R.Speed() < 100.0 && R.Time < 60.0) R.Step();
			R.Input.Throttle = 0.0f;
			R.Input.Brake = 1.0f;
			const Vec3 From = R.Motion.State().Position;
			while (R.Speed() > 0.5 && R.Time < 120.0) R.Step();
			const Vec3 To = R.Motion.State().Position;
			Stop = std::sqrt((To.X - From.X) * (To.X - From.X) + (To.Y - From.Y) * (To.Y - From.Y));
		}
		// the corner: held at 80 km/h, the steering wound on over 12 s; the most sideways g it holds
		double Grip = 0.0;
		{
			Run R(C.Body);
			R.Input.Throttle = 1.0f;
			while (R.Speed() < 80.0 && R.Time < 60.0) R.Step();
			const double Start = R.Time;
			while (R.Time - Start < 12.0)
			{
				const double V = R.Speed();
				R.Input.Throttle = float(std::clamp((80.0 - V) * 0.2 + 0.3, 0.0, 1.0));
				R.Input.SteerDegrees = float((R.Time - Start) / 12.0) * R.Design.Setup.Front.MaxSteerAngle * 0.5f;
				R.Step();
				const BodyState S = R.Motion.State();
				const double Yaw = Dot(S.AngularVelocity, S.Up);
				Grip = std::max(Grip, std::fabs(Yaw * Dot(S.Velocity, S.Forward)) / 9.81);
				if (R.Speed() < 40.0) break;	// (spun or ploughed on: done)
			}
		}
		std::printf("%-16s 0-100 %5.1f s   0-200 %5.1f s   top %4.0f km/h   100-0 %4.1f m   corners %4.2f g\n                 (real %s)\n",
			designs::Name(C.Body), To100, To200, Top, Stop, Grip, C.Real);
	}
	return 0;
}
