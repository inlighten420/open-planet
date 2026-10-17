// Geodrive Core - GPL (see Core/README.md)
//
// Aircraft: a rigid body in the air and on its wheels. Lift grows with the wing's angle to the air until it stalls,
// flaps add lift and drag, drag grows towards the speed of sound, the engines spool up and lose thrust as the air
// thins (the standard atmosphere), and the landing gear is springs and tyres with brakes and a steering nose wheel.
//
// Flown as real fighters and airliners are, by wire: the stick asks for a g (pitch) and a roll rate, and the flight
// computer moves the controls to get them, within what the air lets it at that speed. Let go and it holds the flight
// path and the bank; it trims itself; it won't let the wing stall (the angle of attack is limited) and the airliner
// won't bank past 67 degrees. On the ground the stick moves the elevator directly, to rotate for take-off.
//
// Engine-free: the game says where the ground is (Ray) and draws the aircraft where it is. Metres, seconds and
// kilograms here (unlike the rest of Core: aircraft numbers are all metric), in a right-handed frame: the world x east,
// y north, z up; the aircraft x forward, y left, z up.

#pragma once

#include "GeoBody/Body.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace geoflight
{
	struct Vec3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;
		Vec3() = default;
		Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}
		Vec3 operator+(const Vec3& B) const { return { X + B.X, Y + B.Y, Z + B.Z }; }
		Vec3 operator-(const Vec3& B) const { return { X - B.X, Y - B.Y, Z - B.Z }; }
		Vec3 operator*(double S) const { return { X * S, Y * S, Z * S }; }
		Vec3 operator-() const { return { -X, -Y, -Z }; }
		Vec3& operator+=(const Vec3& B) { X += B.X; Y += B.Y; Z += B.Z; return *this; }
		double Dot(const Vec3& B) const { return X * B.X + Y * B.Y + Z * B.Z; }
		Vec3 Cross(const Vec3& B) const { return { Y * B.Z - Z * B.Y, Z * B.X - X * B.Z, X * B.Y - Y * B.X }; }
		double Length() const { return std::sqrt(Dot(*this)); }
		Vec3 Normal() const { const double L = Length(); return L > 1e-9 ? *this * (1.0 / L) : Vec3(); }
	};

	/** A rotation (the aircraft's to the world's) */
	struct Quat
	{
		double W = 1.0, X = 0.0, Y = 0.0, Z = 0.0;
		Vec3 Rotate(const Vec3& V) const;
		Vec3 Unrotate(const Vec3& V) const;
		Quat operator*(const Quat& B) const;
		Quat Normal() const;
		static Quat FromAxisAngle(const Vec3& Axis, double Angle);
		/** Facing a heading (radians clockwise from north), pitched up by Pitch, level */
		static Quat FromHeading(double Heading, double Pitch = 0.0);
	};

	/** A wheel: where the tyre touches the ground with the strut extended (the aircraft's frame, m), how far it gives */
	struct Wheel
	{
		Vec3 At;
		double Travel = 0.4;	// m
		double Load = 0.45;		// its share of the weight standing (sets the spring)
		bool bSteers = false;
	};

	/** An aircraft's numbers, from the real ones of its class (see Designs) */
	struct Design
	{
		std::string Name;
		double Mass = 10000.0;			// kg
		double WingArea = 30.0;			// m2
		double Span = 10.0, Chord = 3.0;	// m
		Vec3 Inertia = Vec3(1e4, 7e4, 8e4);	// kg m2: roll, pitch, yaw

		// lift: CL = CL0 + CLAlpha * alpha (+ flaps) up to the stall, then falling away
		double CL0 = 0.1, CLAlpha = 4.0;	// per radian
		double AlphaStall = 0.3;			// rad: the wing stalls past this
		double AlphaLimit = 0.25;			// rad: the flight computer keeps it below this (the protection)
		// drag: CD = CD0 + K CL^2 (+ gear, flaps, speed brake, and towards the speed of sound)
		double CD0 = 0.02, K = 0.1;
		double CDGear = 0.015, CDSpeedBrake = 0.05;
		double MachCritical = 0.8, WaveDrag = 5.0;	// CD += WaveDrag (M - MachCritical)^2 past it...
		double WaveMax = 0.05;						// ...up to this (it levels off past Mach 1)
		// flaps: their settings' names, extra lift and drag
		std::vector<std::string> FlapNames = { "Up" };
		std::vector<double> FlapLift = { 0.0 }, FlapDrag = { 0.0 };

		// engines (all together): thrust at sea level, the afterburner's (0: none), idle's share, how quickly they spool
		double Thrust = 50000.0;		// N
		double ThrustAfterburner = 0.0;	// N
		double Idle = 0.06;				// (the throttle sets the engine's speed, as N1: thrust grows with it to the power 2.8, as a fan's does)
		double SpoolUp = 4.0, SpoolDown = 2.5;	// s (time constants)
		double ReverseShare = 0.0;		// reverse thrust: this share of the thrust, backwards (0: none)
		bool bGroundSpoilers = false;	// on the wheels at idle, panels up: the wing's lift dumped, drag added

		// the flight computer: g it allows, the fastest roll it asks for, the bank it stops at (0: none), and how hard the
		// controls can turn the aircraft (rad/s2 at Pressure Pa of dynamic pressure, no more than the cap)
		double GMax = 9.0, GMin = -3.0;
		double RollRate = 4.0;			// rad/s
		double BankLimit = 0.0;			// rad (an airliner: 67 degrees; it rolls back to 33 if let go past that)
		double Pressure = 10000.0;		// Pa
		Vec3 Authority = Vec3(15.0, 3.0, 2.0);	// rad/s2: roll, pitch, yaw
		Vec3 AuthorityCap = Vec3(20.0, 6.0, 3.0);
		double Response = 4.0;			// rad/s: how briskly it follows the stick in pitch (the short period)
		double RotateRate = 0.1;		// rad/s: on the wheels, full stick pitches it this fast (rotating for take-off)

		// the gear: wheels (nose first), the brakes' grip, the nose wheel's steering, how fast it goes up and down
		std::vector<Wheel> Wheels;
		double BrakeGrip = 0.5;
		double NoseSteer = 1.0;			// rad, at walking pace (less with speed)
		double GearTime = 6.0;			// s
		/** Points that mustn't touch anything (the nose, tail, wingtips, engines): touching is a crash */
		std::vector<Vec3> Hard;

		/** What it's made of (GeoBody, the aircraft's frame): its Mass, Inertia and CentreOfMass come from these */
		std::vector<geobody::Part> Parts;
		Vec3 CentreOfMass;			// (the aircraft's frame, m: where its weight sits)

		Vec3 Eye;					// the pilot's eyes (the aircraft's frame, m)
		double ChaseDistance = 20.0;	// m: the chase camera's arm
	};

	/** The ones to fly: a fighter jet (an F-16's numbers), an airliner (an A320's) and a regional jet (an Embraer E195's) */
	Design FighterJet();
	Design Airliner();
	Design RegionalJet();

	/** What the pilot asks for */
	struct Controls
	{
		double Pitch = 0.0;		// -1 push to 1 pull
		double Roll = 0.0;		// -1 left to 1 right
		double Yaw = 0.0;		// -1 left to 1 right (pedals; the nose wheel on the ground)
		double Throttle = 0.0;	// 0 to 1
		bool bAfterburner = false;
		int Flaps = 0;			// the setting (0 up)
		bool bGearDown = true;
		double Brakes = 0.0;	// 0 to 1
		bool bSpeedBrake = false;
		double Reverse = 0.0;	// 0 to 1: reverse thrust (on the ground, at idle)
	};

	/** What the instruments show */
	struct Readout
	{
		double Airspeed = 0.0;		// m/s indicated (equivalent)
		double TrueAirspeed = 0.0;	// m/s
		double GroundSpeed = 0.0;	// m/s
		double Mach = 0.0;
		double Altitude = 0.0;		// m above the sea
		double Height = -1.0;		// m above what's below (-1: too high to tell)
		double Climb = 0.0;			// m/s up
		double Heading = 0.0;		// degrees clockwise from north
		double Pitch = 0.0, Bank = 0.0, Alpha = 0.0;	// degrees
		double G = 1.0;
		double Engine = 0.0;		// 0-1 spooled
		bool bAfterburner = false;
		bool bReverse = false;		// the engines' thrust reversed
		bool bSpoilers = false;		// the ground spoilers up
		double Gear = 1.0;			// 0 up, 1 down
		bool bOnGround = true;
		bool bStall = false;		// at the angle of attack limit
		bool bCrashed = false;
		std::string Crash;			// what happened
	};

	/** The game's world: the first thing a straight line from From to To hits (a ray), its point and which way it faces */
	using RayFn = std::function<bool(const Vec3& From, const Vec3& To, Vec3& OutHit, Vec3& OutNormal)>;

	class Aircraft
	{
	public:
		explicit Aircraft(const Design& InDesign);

		/** Put it somewhere, still (its wheels on the ground there, if Ground) */
		void Place(const Vec3& Position, const Quat& Rotation, double Speed = 0.0);

		/** Moved as it is (the game's origin moved) */
		void Move(const Vec3& Offset);

		/** One step (any length: it's cut into 1/240 s steps) */
		void Step(double Seconds, const Controls& Input, const RayFn& Ray);

		const Design& GetDesign() const { return D; }
		const Vec3& Position() const { return Pos; }
		const Quat& Rotation() const { return Rot; }
		const Vec3& Velocity() const { return Vel; }
		const Readout& Read() const { return Out; }
		/** How far each wheel's strut is pushed in (m), and the gear's state (0 up, 1 down), for drawing */
		const std::vector<double>& Compression() const { return Squash; }

	private:
		void Substep(double Dt, const Controls& Input, const RayFn& Ray, bool bProbe);
		/** Its pushes this step (the air, the engines, the wheels, the flight computer's turning), on its body */
		void Push(geobody::Body& On, const Controls& Input, double Dt);
		/** Where it is and how it's moving, from its body */
		void Sync();

		/** Its body (its parts): the one motion function moves it, as everything else */
		geobody::Body Frame;

		Design D;
		Vec3 Pos, Vel, Omega;	// (from Frame) Pos: its own origin (where its parts and wheels are measured from); Vel: its centre
		// of mass's; Omega: the aircraft's frame (rad/s about x forward, y left, z up)
		Quat Rot;
		double Spool = 0.0, SpoolAB = 0.0;
		double GearState = 1.0;
		int FlapSetting = 0;
		double FlapState = 0.0;	// eases between settings
		double AirborneTime = 0.0;
		std::vector<double> Squash;
		std::vector<Vec3> GroundUnder;		// each wheel's ground point and normal, found once a step
		std::vector<Vec3> GroundNormal;
		std::vector<bool> GroundFound;
		Readout Out;
	};

	/** The standard atmosphere at a height (m): air density (kg/m3) and the speed of sound (m/s) */
	void Atmosphere(double Height, double& OutDensity, double& OutSound);
}
