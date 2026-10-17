// Geodrive Core - GPL (see Core/README.md)
//
// One car's physics: suspension, tyres, wheels, drivetrain (engine, clutch, gearbox, differential), aero and the
// assists (ABS, traction control). The game engine owns the rigid body and the collision world; each physics step it
// hands the car the body's state and a CarWorld to sweep the wheels against and push the forces into.
//
// Units: metres, kg, N, s. Directions are in the world frame the host chooses, as long as it's the same one throughout
// and Right = the car's right-hand side (in a left-handed frame like the game's, x north, y east, z up, the usual
// cross product formula gives Cross(Up, Forward) = Right; a right-handed host converts at its edge).

#pragma once

#include "CarTypes.h"
#include "Tire.h"

namespace geocar
{
	/** The rigid body's state at the start of the step */
	struct BodyState
	{
		Vec3 Position;			// the body's origin
		Vec3 Forward, Right, Up;	// its axes
		Vec3 CenterOfMass;		// world position
		Vec3 Velocity;			// at the centre of mass (m/s)
		Vec3 AngularVelocity;	// rad/s, in the frame's own handedness (Velocity at P = V + Cross(W, P - CoM))

		/** A point in the body's frame (x forward, y right, z up) to the world */
		Vec3 ToWorld(const Vec3& Local) const { return Position + Forward * Local.X + Right * Local.Y + Up * Local.Z; }
	};

	/** Where a wheel's sweep hit the ground */
	struct SweepHit
	{
		Vec3 Location;			// the sphere's centre at the hit
		Vec3 ImpactPoint;		// the contact point
		Vec3 ImpactNormal;		// the surface normal there
		bool bStartPenetrating = false;	// the sweep started inside something
		float Grip = 1.0f;		// surface grip (1 = asphalt)
	};

	/** What the car needs from the game engine for one step */
	class CarWorld
	{
	public:
		virtual ~CarWorld() = default;

		/** Sweeps a sphere from Start to End against the ground (not the car itself); false if it hit nothing */
		virtual bool SweepSphere(const Vec3& Start, const Vec3& End, double Radius, SweepHit& Hit) = 0;

		/** Pushes the body with Force (N) at a world position */
		virtual void AddForceAtPosition(const Vec3& Force, const Vec3& Position) = 0;
	};

	class CarSim
	{
	public:
		/**
		 *  Advances the car by one physics step and applies the forces through World. WheelPositions: the wheel centres at
		 *  ride height in the body's frame (m; x forward, y right, z up).
		 */
		void Step(CarWorld& World, const BodyState& Body, const CarSetup& Setup, const Controls& Input,
			const Vec3 (&WheelPositions)[NumWheels], float DeltaTime);

		/** The state after the last step */
		const VehicleState& GetState() const { return State; }

	private:
		struct Wheel
		{
			TireState Tire;
			float AngularVelocity = 0.0f;
			float SpinAngle = 0.0f;
			float ABSFactor = 1.0f;
			float LastSlipX = 0.0f;
		};

		Wheel Wheels[NumWheels];

		float EngineOmega = 0.0f;
		bool bStalled = false;
		bool bWasOff = false;	// (the key off last step: on again, the engine's started)
		int32_t Gear = 1;
		int32_t TargetGear = 1;
		float ShiftTimer = 0.0f;
		bool bUpshifting = false;
		float TCFactor = 1.0f;
		float ReverseTimer = 0.0f;

		/** The gearbox input speed the wheels give (rpm): the automatic shifts up on this, not on the revs of a slipping clutch */
		float InputRPMNow = 0.0f;

		int32_t LastShiftUpCount = 0;
		int32_t LastShiftDownCount = 0;
		int32_t LastResetCount = 0;
		bool bInitialised = false;

		VehicleState State;

		/** Applies shift requests, automatic shifting and the shift timer */
		void UpdateGearbox(const GearboxSetup& Gearbox, const Controls& Input, float SpeedMS, float Throttle, float Brake, float DeltaTime);

		/** Total ratio from engine to wheels for the current gear (0 in neutral or mid-shift) */
		float CurrentRatio(const GearboxSetup& Gearbox) const;

		/** Full-throttle torque at an engine speed */
		static float CurveTorque(const EngineSetup& Engine, float RPM);
	};
}
