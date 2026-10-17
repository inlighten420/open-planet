// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// A car: its setup (tyres, axles, engine, gearbox, aero), the driver's controls, and what the simulation publishes.
// SI units (m, kg, N, s, rad/s) unless a field says otherwise. Wheels: 0 front left, 1 front right, 2 rear left,
// 3 rear right.

#pragma once

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace geocar
{
	constexpr int32_t NumWheels = 4;
	inline bool IsFront(int32_t Wheel) { return Wheel < 2; }
	inline bool IsLeft(int32_t Wheel) { return (Wheel % 2) == 0; }

	/** A point or direction in the world: metres, the game's world frame (x north, y east, z up) */
	struct Vec3
	{
		double X = 0.0, Y = 0.0, Z = 0.0;

		Vec3() = default;
		Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

		Vec3 operator+(const Vec3& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
		Vec3 operator-(const Vec3& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
		Vec3 operator-() const { return { -X, -Y, -Z }; }
		Vec3 operator*(double S) const { return { X * S, Y * S, Z * S }; }
		double Size() const { return std::sqrt(X * X + Y * Y + Z * Z); }
		Vec3 Normal() const
		{
			const double S = X * X + Y * Y + Z * Z;
			return S > 1.0e-8 ? *this * (1.0 / std::sqrt(S)) : Vec3();
		}
	};
	inline double Dot(const Vec3& A, const Vec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	inline Vec3 Cross(const Vec3& A, const Vec3& B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }

	/**
	 *  TMeasy tyre parameters (see Tire.cpp). Forces are given at the nominal load and at twice the nominal load; in
	 *  between the tyre loses grip efficiency, which is what makes weight transfer matter. Slips are dimensionless
	 *  (lateral slip ~ tan of the slip angle). Defaults: a GT3 slick.
	 */
	struct TireSetup
	{
		float NominalLoad = 4000.0f;		// N
		float PeakForceY = 7000.0f;			// peak lateral force at nominal / twice nominal load (N)
		float PeakForceY2 = 12800.0f;
		float StiffnessY = 120000.0f;		// cornering stiffness at nominal / twice nominal load (N per unit slip)
		float StiffnessY2 = 220000.0f;
		float PeakSlipY = 0.123f;			// lateral slip at peak force, and where the tyre is fully sliding
		float SlideSlipY = 0.5f;
		float LongitudinalPeakScale = 1.05f;	// longitudinal peak force relative to lateral
		float StiffnessX = 160000.0f;		// longitudinal stiffness at nominal / twice nominal load
		float StiffnessX2 = 280000.0f;
		float PeakSlipX = 0.10f;			// slip ratio at peak force, and where the tyre is fully sliding
		float SlideSlipX = 0.6f;
		float SlideRatio = 0.88f;			// sliding force as a fraction of peak force
		float TrailFactor = 0.18f;			// pneumatic trail at zero slip, as a fraction of contact length
		float TrailZeroSlip = 0.12f;		// lateral slip where the trail crosses zero, and where it has fully gone
		float TrailEndSlip = 0.35f;
		float VerticalStiffness = 250000.0f;	// for the contact patch length (N/m)
		float RelaxationLength = 0.35f;		// how far the tyre rolls before side force builds up (m)
	};

	/** Per-axle suspension, wheel and brake setup */
	struct AxleSetup
	{
		float WheelRadius = 0.34f;
		float WheelInertia = 1.2f;		// wheel + tyre + brake disc (kg m^2)
		float MaxSteerAngle = 20.0f;	// road-wheel steering angle (degrees), 0 = not steered
		float SpringRate = 91000.0f;	// wheel rate (N/m)
		float BumpDamping = 6300.0f;	// compressing / extending (N s/m)
		float ReboundDamping = 7700.0f;
		float AntiRollBar = 34000.0f;	// per metre of left/right compression difference (N/m)
		float BumpTravel = 0.06f;		// travel above and below ride height (m)
		float DroopTravel = 0.08f;
		float BumpStopRange = 0.02f;	// the bump stop engages this far before full bump, rising quadratically (N/m^2)
		float BumpStopRate = 2.0e7f;
		float MaxBrakeTorque = 3000.0f;	// per wheel at full pedal (N m)
		float MaxHandbrakeTorque = 0.0f;
		bool bDriven = false;
	};

	struct EngineSetup
	{
		std::vector<std::pair<double, double>> TorqueCurve = { { 1000.0, 250.0 }, { 3000.0, 400.0 }, { 5000.0, 500.0 }, { 6000.0, 520.0 },
			{ 7500.0, 494.0 }, { 8500.0, 430.0 }, { 9000.0, 400.0 } };	// full throttle: rpm, N m
		float IdleRPM = 1200.0f;
		float LimiterRPM = 9000.0f;
		float Inertia = 0.2f;			// crank + flywheel (kg m^2)
		float EngineBrakeTorque = 70.0f;	// off throttle at the limiter (scales with rpm)
		float ClutchTorque = 900.0f;	// capacity (N m)
		float LaunchRPM = 0.0f;			// where the clutch holds it pulling away flat out (a launch control's); 0: a driver's, by throttle
	};

	struct GearboxSetup
	{
		std::vector<float> GearRatios = { 2.80f, 2.10f, 1.70f, 1.42f, 1.22f, 1.07f };
		float ReverseRatio = 2.90f;
		float FinalDrive = 3.50f;
		float Efficiency = 0.92f;
		float UpshiftTime = 0.05f;		// upshifts cut ignition, downshifts blip (s)
		float DownshiftTime = 0.09f;
		float AutoUpshiftRPM = 8400.0f;	// automatic mode shift points
		float AutoDownshiftRPM = 4500.0f;
		float DiffPreload = 80.0f;		// clutch-type limited slip: locking torque = preload + ratio x input torque
		float DiffPowerRatio = 0.5f;
		float DiffCoastRatio = 0.3f;
		bool bHasDiffLock = false;		// the driver can lock the differential (a locker, as on bakkies)
	};

	struct AeroSetup
	{
		float DownforceArea = 1.45f;	// ClA (m^2)
		float DragArea = 0.95f;			// CdA (m^2)
		float FrontBalance = 0.42f;		// share of downforce on the front axle
		float AirDensity = 1.225f;
	};

	/** Everything that defines a car. Defaults are a generic GT3. */
	struct CarSetup
	{
		float Mass = 1280.0f;
		float FrontWeight = 0.47f;		// share of weight on the front axle
		float CoGHeight = 0.42f;		// above the ground (m)
		/** How hard it is to turn (kg m2) about its centre of mass: roll (about its length), pitch, yaw. Zero: the host
		 *  guesses from its box (as if its mass were even through it). A car built from parts (CarParts) has its own */
		Vec3 Inertia;
		float MechanicalTrail = 0.025f;	// caster trail, for steering feel (m)

		/** Steering axis inclination (degrees) and the wheel centre's offset from that axis (m): turning lifts the car
		 *  slightly, so its weight pushes the wheels back to straight */
		float KingpinInclination = 10.0f;
		float KingpinOffset = 0.05f;

		/** Share of the steering torque the power steering takes away when parking, its share at speed, and the speed
		 *  (m/s) by which it has eased down to it */
		float PowerSteeringAssist = 0.6f;
		float HighSpeedAssist = 0.3f;
		float AssistFadeSpeed = 30.0f;

		AxleSetup Front;
		AxleSetup Rear = DefaultRear();
		TireSetup Tire;
		EngineSetup Engine;
		GearboxSetup Gearbox;
		AeroSetup Aero;

		const AxleSetup& Axle(int32_t Wheel) const { return IsFront(Wheel) ? Front : Rear; }

		static AxleSetup DefaultRear()
		{
			AxleSetup Rear;
			Rear.WheelRadius = 0.355f;
			Rear.WheelInertia = 1.4f;
			Rear.MaxSteerAngle = 0.0f;
			Rear.SpringRate = 119000.0f;
			Rear.BumpDamping = 7800.0f;
			Rear.ReboundDamping = 9600.0f;
			Rear.AntiRollBar = 30000.0f;
			Rear.MaxBrakeTorque = 2200.0f;
			Rear.MaxHandbrakeTorque = 3000.0f;
			Rear.bDriven = true;
			return Rear;
		}
	};

	/** The driver's controls for one step */
	struct Controls
	{
		float SteerDegrees = 0.0f;		// road-wheel steering angle (+ = right)
		float Throttle = 0.0f;			// pedals in [0, 1]
		float Brake = 0.0f;
		float Handbrake = 0.0f;
		float Clutch = 0.0f;			// 0 = up / engaged, 1 = pressed, used when the driver has one (bClutchPedal)
		bool bClutchPedal = false;
		int32_t ShiftUpCount = 0;		// incremented once per shift request, so a step never misses or repeats one
		int32_t ShiftDownCount = 0;
		int32_t ResetCount = 0;			// incremented when the car is reset (teleported): clears wheel spin and tyre state
		bool bAutomatic = true;
		bool bBrakeReverses = true;		// automatic: holding the brake when stopped puts it in reverse (the player's way;
										// a robot driver holding still on its brakes mustn't back off down the road)
		bool bDiffLock = false;			// the rear diff locked (a locker: both driven wheels turn together)
		bool bEngineOff = false;		// the key turned off: no power, the engine spins down; back on, it starts (to idle)
		int32_t ABSLevel = 3;			// assist levels, 0 = off
		int32_t TCLevel = 3;
	};

	struct WheelState
	{
		bool bContact = false;
		float Compression = 0.0f;		// relative to ride height (m, + = compressed)
		float AngularVelocity = 0.0f;
		float SpinAngle = 0.0f;			// accumulated (rad), for visuals
		float SteerDegrees = 0.0f;
		float Load = 0.0f;				// tyre load and forces (N)
		float ForceX = 0.0f;
		float ForceY = 0.0f;
		float SlipRatio = 0.0f;
		float SlipAngleDegrees = 0.0f;
		float Trail = 0.0f;				// pneumatic trail (m), negative past the limit
		float Grip = 1.0f;				// surface grip under this tyre (1 = asphalt)
		bool bABSActive = false;
		float SpeedBlend = 0.0f;		// 0 = low-speed sticky tyre model, 1 = full model
	};

	struct VehicleState
	{
		WheelState Wheels[NumWheels];
		float SpeedMS = 0.0f;			// forward speed
		float EngineRPM = 0.0f;
		int32_t Gear = 1;				// -1 = reverse, 0 = neutral, 1.. = forward gears
		bool bShifting = false;
		bool bAutomatic = true;
		bool bTCActive = false;
		bool bStalled = false;			// the manual clutch let out too low: pressing the clutch in restarts the engine
		bool bEngineOff = false;		// switched off (Controls::bEngineOff)
		float Downforce = 0.0f;
		float KingpinTorque = 0.0f;		// about the front steering axis (N m, + = pulls the wheels to the right)
		float SpeedFade = 0.0f;			// 0 when parked, 1 above walking pace (force feedback)
	};
}
