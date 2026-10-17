// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "CarSim.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace geocar
{
	namespace CarSimImpl
	{
		/** Tyre and wheel substeps per physics step: the tyre/wheel coupling is the stiffest part of the model */
		constexpr int32_t SubSteps = 4;

		constexpr float Pi = 3.14159265358979323846f;
		constexpr float Gravity = 9.81f;
		constexpr float RadPerSecToRPM = 60.0f / (2.0f * Pi);
		constexpr float SmallNumber = 1.0e-4f;

		float Clamp(float V, float Lo, float Hi) { return std::min(std::max(V, Lo), Hi); }
		float Sign(float V) { return V > 0.0f ? 1.0f : (V < 0.0f ? -1.0f : 0.0f); }
		float Radians(float Degrees) { return Degrees * (Pi / 180.0f); }
		float Degrees(float Radians) { return Radians * (180.0f / Pi); }

		float SmoothStep(float A, float B, float X)
		{
			if (X < A)
			{
				return 0.0f;
			}
			if (X >= B)
			{
				return 1.0f;
			}
			const float T = (X - A) / (B - A);
			return T * T * (3.0f - 2.0f * T);
		}

		/** Assist thresholds as a multiple of the tyre's peak slip; higher levels intervene earlier */
		float AssistSlipThreshold(int32_t Level, float PeakSlip)
		{
			return PeakSlip * std::max(1.6f - 0.12f * static_cast<float>(Level), 0.9f);
		}
	}

	float CarSim::CurveTorque(const EngineSetup& Engine, float RPM)
	{
		const std::vector<std::pair<double, double>>& Curve = Engine.TorqueCurve;
		if (Curve.empty())
		{
			return 0.0f;
		}
		if (RPM <= Curve[0].first)
		{
			return static_cast<float>(Curve[0].second);
		}
		for (size_t i = 1; i < Curve.size(); ++i)
		{
			if (RPM <= Curve[i].first)
			{
				const double Alpha = (RPM - Curve[i - 1].first) / std::max(Curve[i].first - Curve[i - 1].first, 1.0);
				return static_cast<float>(Curve[i - 1].second + (Curve[i].second - Curve[i - 1].second) * Alpha);
			}
		}
		return static_cast<float>(Curve.back().second);
	}

	float CarSim::CurrentRatio(const GearboxSetup& Gearbox) const
	{
		if (ShiftTimer > 0.0f || Gear == 0)
		{
			return 0.0f;
		}
		if (Gear < 0)
		{
			return -Gearbox.ReverseRatio * Gearbox.FinalDrive;
		}
		const int32_t NumGears = static_cast<int32_t>(Gearbox.GearRatios.size());
		const int32_t Index = std::min(std::max(Gear - 1, 0), NumGears - 1);
		return Index >= 0 && Index < NumGears ? Gearbox.GearRatios[Index] * Gearbox.FinalDrive : 0.0f;
	}

	void CarSim::UpdateGearbox(const GearboxSetup& Gearbox, const Controls& Input, float SpeedMS, float Throttle, float Brake, float DeltaTime)
	{
		const int32_t TopGear = std::max(static_cast<int32_t>(Gearbox.GearRatios.size()), 1);

		auto RequestShift = [&](int32_t Direction)
		{
			const int32_t NewGear = std::min(std::max(TargetGear + Direction, -1), TopGear);
			if (NewGear == TargetGear)
			{
				return;
			}
			bUpshifting = NewGear > TargetGear && TargetGear >= 1;
			TargetGear = NewGear;
			ShiftTimer = bUpshifting ? Gearbox.UpshiftTime : Gearbox.DownshiftTime;
		};

		// paddle/keyboard requests (counters, so none are lost or repeated between frames)
		while (LastShiftUpCount < Input.ShiftUpCount)
		{
			++LastShiftUpCount;
			RequestShift(+1);
		}
		while (LastShiftDownCount < Input.ShiftDownCount)
		{
			++LastShiftDownCount;
			RequestShift(-1);
		}

		// automatic mode: shift on rpm, and use the brake pedal for reverse when stopped
		if (Input.bAutomatic && ShiftTimer <= 0.0f)
		{
			const float RPM = EngineOmega * CarSimImpl::RadPerSecToRPM;
			if (Gear >= 1)
			{
				if (RPM > Gearbox.AutoUpshiftRPM && InputRPMNow > Gearbox.AutoUpshiftRPM * 0.9f && Gear < TopGear)
				{
					RequestShift(+1);
				}
				// down on the engine's revs, or on the wheels' when the clutch is slipping (pulling away uphill in too
				// high a gear: the slipping clutch holds the revs up, and the car would sit there)
				else if ((RPM < Gearbox.AutoDownshiftRPM || InputRPMNow < Gearbox.AutoDownshiftRPM * 0.8f) && Gear > 1)
				{
					RequestShift(-1);
				}
			}

			const bool bStopped = std::fabs(SpeedMS) < 0.5f;
			ReverseTimer = (bStopped && ((Gear >= 1 && Input.bBrakeReverses && Brake > 0.3f && Throttle < 0.05f) || (Gear < 0 && Throttle > 0.3f))) ? ReverseTimer + DeltaTime : 0.0f;
			if (ReverseTimer > 1.0f)
			{
				ReverseTimer = 0.0f;
				TargetGear = Gear = (Gear >= 1) ? -1 : 1;
			}
			if (Gear == 0)
			{
				TargetGear = Gear = 1;
			}
		}

		if (ShiftTimer > 0.0f)
		{
			ShiftTimer -= DeltaTime;
			if (ShiftTimer <= 0.0f)
			{
				ShiftTimer = 0.0f;
				Gear = TargetGear;
			}
		}
	}

	void CarSim::Step(CarWorld& World, const BodyState& Body, const CarSetup& Setup, const Controls& Input,
		const Vec3 (&WheelPositions)[NumWheels], float DeltaTime)
	{
		using namespace CarSimImpl;

		if (DeltaTime <= SmallNumber)
		{
			return;
		}

		if (!bInitialised || LastResetCount != Input.ResetCount)
		{
			for (Wheel& W : Wheels)
			{
				W = Wheel();
			}
			EngineOmega = Setup.Engine.IdleRPM / RadPerSecToRPM;
			Gear = TargetGear = 1;
			ShiftTimer = 0.0f;
			LastShiftUpCount = Input.ShiftUpCount;
			LastShiftDownCount = Input.ShiftDownCount;
			LastResetCount = Input.ResetCount;
			bStalled = false;
			bInitialised = true;
		}

		// the key: off, the engine stops (as a stall, but it stays stopped); on again, the starter brings it to idle
		if (Input.bEngineOff)
		{
			bStalled = true;
		}
		else if (bWasOff)
		{
			bStalled = false;
			EngineOmega = std::max(EngineOmega, Setup.Engine.IdleRPM / RadPerSecToRPM);
		}
		bWasOff = Input.bEngineOff;
		// a stalled engine restarts as soon as the clutch is pushed in (or when there's no clutch pedal any more)
		if (bStalled && !Input.bEngineOff && (!Input.bClutchPedal || Input.Clutch > 0.85f))
		{
			bStalled = false;
			EngineOmega = std::max(EngineOmega, Setup.Engine.IdleRPM / RadPerSecToRPM);
		}

		// --- body state ---
		const Vec3& CenterOfMass = Body.CenterOfMass;
		const Vec3& Velocity = Body.Velocity;
		const Vec3& Up = Body.Up;
		const Vec3& Forward = Body.Forward;
		const Vec3& Right = Body.Right;

		auto PointVelocity = [&](const Vec3& Point) { return Velocity + Cross(Body.AngularVelocity, Point - CenterOfMass); };

		const float SpeedMS = static_cast<float>(Dot(Velocity, Forward));

		// --- pedals: in automatic reverse the pedals swap, like most driving games ---
		float Throttle = Clamp(Input.Throttle, 0.0f, 1.0f);
		float Brake = Clamp(Input.Brake, 0.0f, 1.0f);
		if (Input.bAutomatic && Gear < 0)
		{
			std::swap(Throttle, Brake);
		}

		UpdateGearbox(Setup.Gearbox, Input, SpeedMS, Input.Throttle, Input.Brake, DeltaTime);

		// --- steering with simple Ackermann: the inside wheel turns more ---
		float SteerAngles[NumWheels] = {};
		{
			const float Wheelbase = static_cast<float>(std::fabs(WheelPositions[0].X - WheelPositions[2].X));
			const float Track = static_cast<float>(std::fabs(WheelPositions[0].Y - WheelPositions[1].Y));
			const float Steer = Clamp(Input.SteerDegrees, -Setup.Front.MaxSteerAngle, Setup.Front.MaxSteerAngle);

			if (std::fabs(Steer) > 0.01f && Wheelbase > 0.5f)
			{
				const float TurnRadius = Wheelbase / std::tan(Radians(std::fabs(Steer)));
				const float Inner = Degrees(std::atan(Wheelbase / std::max(TurnRadius - Track * 0.5f, 0.1f)));
				const float Outer = Degrees(std::atan(Wheelbase / (TurnRadius + Track * 0.5f)));
				const float SteerSign = Sign(Steer);

				// turning right (+): the right wheel (index 1) is on the inside
				SteerAngles[0] = SteerSign * (Steer > 0.0f ? Outer : Inner);
				SteerAngles[1] = SteerSign * (Steer > 0.0f ? Inner : Outer);
			}
		}

		// --- suspension ---
		struct Contact
		{
			bool bHit = false;
			float Compression = 0.0f;
			float CompressionVelocity = 0.0f;
			Vec3 WheelCenter;
			Vec3 Normal;
			Vec3 ContactPoint;
			float SpringForce = 0.0f;
			float Load = 0.0f;
			float GripScale = 1.0f;
		};
		Contact Contacts[NumWheels];

		for (int32_t i = 0; i < NumWheels; ++i)
		{
			const AxleSetup& Axle = Setup.Axle(i);
			const Vec3 RideHeightCenter = Body.ToWorld(WheelPositions[i]);
			const Vec3 Start = RideHeightCenter + Up * Axle.BumpTravel;
			const Vec3 End = RideHeightCenter - Up * Axle.DroopTravel;

			Contact& C = Contacts[i];
			C.Normal = Up;
			SweepHit Hit;
			const bool bHit = World.SweepSphere(Start, End, Axle.WheelRadius, Hit);

			if (bHit && !Hit.bStartPenetrating)
			{
				const float Travelled = static_cast<float>(Dot(Start - Hit.Location, Up));
				C.bHit = true;
				C.Compression = Clamp(Axle.BumpTravel - Travelled, -Axle.DroopTravel, Axle.BumpTravel);
				C.WheelCenter = Hit.Location;
				C.Normal = Hit.ImpactNormal;
				C.ContactPoint = Hit.ImpactPoint;
				C.GripScale = Hit.Grip;
			}
			else if (bHit)
			{
				// sweep started inside the ground: treat as full bump
				C.bHit = true;
				C.Compression = Axle.BumpTravel;
				C.WheelCenter = Start;
				C.ContactPoint = Start - Up * Axle.WheelRadius;
			}
			else
			{
				C.Compression = -Axle.DroopTravel;
				C.WheelCenter = End;
			}

			// compression rate from the body's motion at the wheel (more stable than differencing the sweep)
			C.CompressionVelocity = -static_cast<float>(Dot(PointVelocity(RideHeightCenter), Up));
		}

		// spring + damper + bump stop + anti-roll bar. The spring is preloaded so the car rests exactly at ride height.
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			Contact& C = Contacts[i];
			if (!C.bHit)
			{
				continue;
			}

			const AxleSetup& Axle = Setup.Axle(i);
			const float AxleShare = IsFront(i) ? Setup.FrontWeight : 1.0f - Setup.FrontWeight;
			const float Preload = Setup.Mass * Gravity * AxleShare * 0.5f;

			const float X = C.Compression;
			const float XDot = C.CompressionVelocity;
			float Force = Preload + Axle.SpringRate * X + (XDot > 0.0f ? Axle.BumpDamping : Axle.ReboundDamping) * XDot;

			const float BumpStopStart = Axle.BumpTravel - Axle.BumpStopRange;
			if (X > BumpStopStart)
			{
				Force += Axle.BumpStopRate * (X - BumpStopStart) * (X - BumpStopStart);
			}

			const int32_t Opposite = IsLeft(i) ? i + 1 : i - 1;
			Force += Axle.AntiRollBar * (X - Contacts[Opposite].Compression);

			C.SpringForce = Clamp(Force, 0.0f, 60000.0f);
			C.Load = C.SpringForce;
		}

		// --- contact frames and patch velocities ---
		Vec3 WheelForward[NumWheels];
		Vec3 WheelRight[NumWheels];
		float PatchVx[NumWheels] = {};
		float PatchVy[NumWheels] = {};

		for (int32_t i = 0; i < NumWheels; ++i)
		{
			const float SteerRad = Radians(SteerAngles[i]);
			const Vec3 Heading = Forward * std::cos(SteerRad) + Right * std::sin(SteerRad);
			const Vec3& Normal = Contacts[i].Normal;

			WheelForward[i] = (Heading - Normal * Dot(Heading, Normal)).Normal();
			WheelRight[i] = Cross(Normal, WheelForward[i]);

			if (Contacts[i].bHit)
			{
				const Vec3 PatchVelocity = PointVelocity(Contacts[i].ContactPoint);
				PatchVx[i] = static_cast<float>(Dot(PatchVelocity, WheelForward[i]));
				PatchVy[i] = static_cast<float>(Dot(PatchVelocity, WheelRight[i]));
			}
		}

		// --- assists (from last step's slip) ---
		const bool bBraking = Brake > 0.05f && std::fabs(SpeedMS) > 3.0f;
		for (Wheel& W : Wheels)
		{
			const bool bLocking = Input.ABSLevel > 0 && bBraking && W.LastSlipX < -AssistSlipThreshold(Input.ABSLevel, Setup.Tire.PeakSlipX);
			W.ABSFactor = bLocking ? std::max(W.ABSFactor - 25.0f * DeltaTime, 0.15f) : std::min(W.ABSFactor + 8.0f * DeltaTime, 1.0f);
		}

		float DrivenSlip = 0.0f;
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			if (Setup.Axle(i).bDriven)
			{
				DrivenSlip = std::max(DrivenSlip, Wheels[i].LastSlipX * Sign(CurrentRatio(Setup.Gearbox) + SmallNumber));
			}
		}
		// traction control: holds the driven wheels' slip at its target (the tyre's best, a little past it at the lower
		// levels) by easing the throttle in proportion to the excess, and gives it back as fast as the slip allows; an on-off
		// cut had made the slip swing between nothing and too much, losing grip both ways
		const float TargetSlip = AssistSlipThreshold(Input.TCLevel, Setup.Tire.PeakSlipX);
		if (Input.TCLevel > 0 && Throttle > 0.05f)
		{
			const float Excess = (DrivenSlip - TargetSlip) / std::max(TargetSlip, 0.01f);
			TCFactor = Clamp(TCFactor + (Excess > 0.0f ? -12.0f * Excess : std::min(-3.0f * Excess, 3.0f)) * DeltaTime, 0.05f, 1.0f);
		}
		else
		{
			TCFactor = std::min(TCFactor + 4.0f * DeltaTime, 1.0f);
		}

		// --- drivetrain + tyres, substepped ---
		const EngineSetup& Engine = Setup.Engine;
		const GearboxSetup& Gearbox = Setup.Gearbox;
		const float Ratio = CurrentRatio(Gearbox);
		const float IdleOmega = Engine.IdleRPM / RadPerSecToRPM;
		const float LimiterOmega = Engine.LimiterRPM / RadPerSecToRPM;
		const float EngineInertia = std::max(Engine.Inertia, 0.01f);

		float SumForceX[NumWheels] = {};
		float SumForceY[NumWheels] = {};
		float SumTrail[NumWheels] = {};
		TireForces LastForces[NumWheels];

		const float H = DeltaTime / SubSteps;
		for (int32_t Sub = 0; Sub < SubSteps; ++Sub)
		{
			// engine torque: throttle (cut while upshifting and on the limiter) minus engine braking
			const float RPM = EngineOmega * RadPerSecToRPM;
			float EffectiveThrottle = Throttle * TCFactor;
			if ((ShiftTimer > 0.0f && bUpshifting) || EngineOmega > LimiterOmega || bStalled)
			{
				EffectiveThrottle = 0.0f;
			}
			float EngineTorque = CurveTorque(Engine, RPM) * EffectiveThrottle
				- Engine.EngineBrakeTorque * (RPM / std::max(Engine.LimiterRPM, 1.0f)) * (1.0f - EffectiveThrottle);

			// idle governor keeps the engine running (a stalled engine just spins down)
			if (EngineOmega < IdleOmega && !bStalled)
			{
				EngineTorque = std::max(EngineTorque, 0.0f) + (IdleOmega - EngineOmega) * 2.0f;
			}
			else if (bStalled)
			{
				EngineTorque = -Engine.EngineBrakeTorque * 0.3f;
			}

			// clutch: fully engaged when the wheels turn the engine above idle, otherwise a launch clutch that
			// bites with engine rpm (so the car can pull away and stop without stalling)
			float ClutchTorque = 0.0f;
			float Engagement = 0.0f;
			int32_t NumDriven = 0;
			float DrivenOmega = 0.0f, DrivenInertia = 0.0f;
			for (int32_t i = 0; i < NumWheels; ++i)
			{
				if (Setup.Axle(i).bDriven)
				{
					DrivenOmega += Wheels[i].AngularVelocity;
					DrivenInertia += Setup.Axle(i).WheelInertia;
					++NumDriven;
				}
			}

			if (NumDriven > 0 && std::fabs(Ratio) > SmallNumber)
			{
				DrivenOmega /= static_cast<float>(NumDriven);
				const float InputOmega = DrivenOmega * Ratio;
				const float InputRPM = std::fabs(InputOmega) * RadPerSecToRPM;
				InputRPMNow = InputRPM;

				// Auto-clutch: fully closed once the wheels keep the engine above idle. Pulling away, it lets the engine
				// rev towards a launch speed that rises with throttle before taking the full load, so it neither bogs nor stalls.
				Engagement = 1.0f;
				if (Input.bClutchPedal)
				{
					// the driver's clutch: nothing in the top quarter of the pedal's travel, biting through the middle, fully
					// closed in the bottom quarter (as the pedal comes up)
					Engagement = SmoothStep(0.25f, 0.75f, 1.0f - Clamp(Input.Clutch, 0.0f, 1.0f));
				}
				else
				{
					// it slips until the wheels have caught the engine up to its launch speed (as an automatic's torque
					// converter, a twin clutch's launch, or a driver feeding the clutch out does), the engine held there
					// pulling hard; closing at the first turn of the wheels had dragged it down into its weakest revs.
					// (Never above 60% of the rev range: a diesel would never get there and the clutch would never bite)
					const float DriverLaunch = std::min(Engine.IdleRPM + 500.0f + 3500.0f * Throttle, Engine.IdleRPM + 0.6f * (Engine.LimiterRPM - Engine.IdleRPM));
					const float LaunchRPM = Engine.LaunchRPM > 0.0f ? std::max(DriverLaunch, Engine.IdleRPM + (Engine.LaunchRPM - Engine.IdleRPM) * Throttle) : DriverLaunch;
					if (InputRPM < std::max(LaunchRPM, Engine.IdleRPM + 200.0f))
					{
						// (biting over the last 1200 rpm below it: the engine sits near its launch speed, not far under)
						const float From = std::max(Engine.IdleRPM, LaunchRPM - 1200.0f);
						const float Bite = Clamp((RPM - From) / std::max(LaunchRPM - From, 1.0f), 0.0f, 1.0f);
						Engagement = Throttle > 0.05f ? Bite * Bite : (InputRPM < Engine.IdleRPM + 200.0f ? 0.0f : 1.0f);
					}
				}
				const float Capacity = Engine.ClutchTorque * Engagement;

				// torque that would match engine and gearbox speeds within this substep, limited by the clutch capacity
				const float SyncTorque = (EngineOmega - InputOmega) / (H * (1.0f / EngineInertia + Ratio * Ratio / DrivenInertia));
				ClutchTorque = Clamp(SyncTorque, -Capacity, Capacity);
			}

			EngineOmega = Clamp(EngineOmega + H * (EngineTorque - ClutchTorque) / EngineInertia, 0.0f, LimiterOmega * 1.05f);

			// with the driver's clutch, letting it out with too few revs drags the engine down until it stalls
			if (Input.bClutchPedal && !bStalled && Engagement > 0.6f && EngineOmega < IdleOmega * 0.45f)
			{
				bStalled = true;
			}

			// limited slip differential between the two driven wheels
			const float DiffInput = ClutchTorque * Ratio * Gearbox.Efficiency;
			// (locked: no limit, the two wheels are made to turn together)
			const float LockTorque = Input.bDiffLock ? 1.0e6f
				: Gearbox.DiffPreload + (DiffInput >= 0.0f ? Gearbox.DiffPowerRatio : Gearbox.DiffCoastRatio) * std::fabs(DiffInput);
			const float HalfInertia = Setup.Rear.WheelInertia * 0.5f;
			const float EqualiseTorque = HalfInertia * (Wheels[2].AngularVelocity - Wheels[3].AngularVelocity) / H;
			const float DiffClutch = Clamp(EqualiseTorque, -LockTorque, LockTorque);

			float DriveTorque[NumWheels] = {};
			if (Setup.Rear.bDriven)
			{
				DriveTorque[2] = DiffInput * 0.5f - DiffClutch;
				DriveTorque[3] = DiffInput * 0.5f + DiffClutch;
			}
			if (Setup.Front.bDriven)
			{
				DriveTorque[0] = DriveTorque[1] = DiffInput * 0.5f;
			}

			// wheels: tyre force, then a linearised implicit spin update so stiff tyres stay stable at low speed
			for (int32_t i = 0; i < NumWheels; ++i)
			{
				const AxleSetup& Axle = Setup.Axle(i);
				Wheel& W = Wheels[i];
				const float Radius = Axle.WheelRadius;

				TireForces Forces;
				if (Contacts[i].bHit && Contacts[i].Load > 0.0f)
				{
					Forces = ComputeTire(Setup.Tire, Contacts[i].Load, PatchVx[i], PatchVy[i], W.AngularVelocity, Radius, H, Contacts[i].GripScale, W.Tire);
				}
				else
				{
					W.Tire = TireState();
				}

				// reflected drivetrain inertia when the clutch is closed
				float Inertia = Axle.WheelInertia;
				if (Axle.bDriven && std::fabs(Ratio) > SmallNumber)
				{
					Inertia += EngineInertia * Ratio * Ratio / static_cast<float>(std::max(NumDriven, 1));
				}

				const float D = Forces.DForceXDOmega;
				const float FreeOmega = (Inertia * W.AngularVelocity + H * (DriveTorque[i] - Radius * Forces.ForceX + Radius * D * W.AngularVelocity))
					/ (Inertia + H * Radius * D);

				// brakes can stop the wheel but never spin it backwards
				const float BrakeTorque = Brake * Axle.MaxBrakeTorque * W.ABSFactor + Input.Handbrake * Axle.MaxHandbrakeTorque;
				const float BrakeImpulse = BrakeTorque * H / Inertia;
				W.AngularVelocity = std::fabs(FreeOmega) <= BrakeImpulse ? 0.0f : FreeOmega - Sign(FreeOmega) * BrakeImpulse;

				SumForceX[i] += Forces.ForceX;
				SumForceY[i] += Forces.ForceY;
				SumTrail[i] += Forces.Trail;
				LastForces[i] = Forces;
			}
		}

		// --- apply forces ---
		float KingpinTorque = 0.0f;
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			Wheel& W = Wheels[i];
			const Contact& C = Contacts[i];
			W.SpinAngle = std::fmod(W.SpinAngle + W.AngularVelocity * DeltaTime, 2.0f * Pi);
			W.LastSlipX = LastForces[i].SlipX;

			if (!C.bHit)
			{
				continue;
			}

			const float ForceX = SumForceX[i] / SubSteps;
			const float ForceY = SumForceY[i] / SubSteps;

			// suspension along the body's up axis, tyre forces in the contact plane, both at the wheel centre
			// (applying tyre forces at the wheel centre rather than the ground keeps body roll realistic without roll-centre geometry)
			const Vec3 Force = Up * C.SpringForce + WheelForward[i] * ForceX + WheelRight[i] * ForceY;
			World.AddForceAtPosition(Force, C.WheelCenter);

			if (IsFront(i))
			{
				// side force to the right pushes the steering back to the left
				const float Trail = SumTrail[i] / SubSteps;
				KingpinTorque -= (Trail + Setup.MechanicalTrail * LastForces[i].SpeedBlend) * ForceY;

				// kingpin inclination: steering lifts the car, so the load on the wheel turns it back towards straight
				KingpinTorque -= C.Load * Setup.KingpinOffset * std::sin(Radians(Setup.KingpinInclination)) * std::sin(Radians(SteerAngles[i]));
			}
		}

		// --- aero ---
		const float AirSpeed = static_cast<float>(Velocity.Size());
		const float DynamicPressure = 0.5f * Setup.Aero.AirDensity * AirSpeed * AirSpeed;
		const float Downforce = DynamicPressure * Setup.Aero.DownforceArea;
		{
			const Vec3 FrontAxle = Body.ToWorld((WheelPositions[0] + WheelPositions[1]) * 0.5);
			const Vec3 RearAxle = Body.ToWorld((WheelPositions[2] + WheelPositions[3]) * 0.5);
			World.AddForceAtPosition(-Up * (Downforce * Setup.Aero.FrontBalance), FrontAxle);
			World.AddForceAtPosition(-Up * (Downforce * (1.0f - Setup.Aero.FrontBalance)), RearAxle);

			if (AirSpeed > 0.1f)
			{
				World.AddForceAtPosition(-Velocity.Normal() * (DynamicPressure * Setup.Aero.DragArea), CenterOfMass);
			}
		}

		// --- publish ---
		float SpeedFade = 0.0f;
		VehicleState Out;
		for (int32_t i = 0; i < NumWheels; ++i)
		{
			WheelState& WheelOut = Out.Wheels[i];
			WheelOut.bContact = Contacts[i].bHit;
			WheelOut.Compression = Contacts[i].Compression;
			WheelOut.AngularVelocity = Wheels[i].AngularVelocity;
			WheelOut.SpinAngle = Wheels[i].SpinAngle;
			WheelOut.SteerDegrees = SteerAngles[i];
			WheelOut.Load = Contacts[i].Load;
			WheelOut.ForceX = SumForceX[i] / SubSteps;
			WheelOut.ForceY = SumForceY[i] / SubSteps;
			WheelOut.SlipRatio = LastForces[i].SlipX;
			WheelOut.SlipAngleDegrees = Degrees(std::atan(-LastForces[i].SlipY));
			WheelOut.Trail = SumTrail[i] / SubSteps;
			WheelOut.bABSActive = Wheels[i].ABSFactor < 0.99f;
			WheelOut.Grip = Contacts[i].bHit ? Contacts[i].GripScale : 1.0f;
			WheelOut.SpeedBlend = LastForces[i].SpeedBlend;
			if (IsFront(i))
			{
				SpeedFade = std::max(SpeedFade, LastForces[i].SpeedBlend);
			}
		}
		Out.SpeedMS = SpeedMS;
		Out.EngineRPM = EngineOmega * RadPerSecToRPM;
		Out.Gear = Gear;
		Out.bShifting = ShiftTimer > 0.0f;
		Out.bAutomatic = Input.bAutomatic;
		Out.bTCActive = TCFactor < 0.99f;
		Out.bStalled = bStalled;
		Out.bEngineOff = Input.bEngineOff;
		Out.Downforce = Downforce;
		Out.SpeedFade = SpeedFade;

		// power steering takes part of the torque away before it reaches the driver: most when parking, less at speed,
		// like real speed-sensitive (electric) power steering, so the wheel firms up and has weight around the centre
		const float AssistBlend = SmoothStep(3.0f, std::max(Setup.AssistFadeSpeed, 4.0f), std::fabs(SpeedMS));
		const float Assist = Setup.PowerSteeringAssist + (Setup.HighSpeedAssist - Setup.PowerSteeringAssist) * AssistBlend;
		Out.KingpinTorque = KingpinTorque * (1.0f - Clamp(Assist, 0.0f, 0.95f));

		State = Out;
	}
}
