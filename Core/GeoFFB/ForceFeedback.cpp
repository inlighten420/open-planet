// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "ForceFeedback.h"
#include "WheelDevice.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace geoffb
{
	namespace ForceFeedbackImpl
	{
		constexpr double Pi = 3.14159265358979323846;

		/** Target loop period (500 Hz) */
		constexpr double Period = 0.002;

		/** Forces drop to zero if the game hasn't sent a command for this long */
		constexpr double CommandTimeout = 0.1;

		/** How often to try opening the wheel again when it isn't connected */
		constexpr double ReconnectInterval = 1.0;

		/** Smoothing time constant for force changes: removes frame-rate steps without adding noticeable lag */
		constexpr float SmoothingTau = 0.008f;

		/** Maximum force change per second, so a sudden drop to zero doesn't jolt the wheel */
		constexpr float MaxSlewRate = 8.0f;

		/** Output stays linear up to this level, then rolls off smoothly instead of hard clipping */
		constexpr float SoftClipKnee = 0.8f;

		/** Forces below this ramp the minimum force in, so tiny noise around zero can't buzz the wheel */
		constexpr float MinForceRamp = 0.02f;

		float Sign(float V) { return V > 0.0f ? 1.0f : (V < 0.0f ? -1.0f : 0.0f); }

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

		/** Linear below the knee, then a tanh roll-off that approaches but never exceeds 1 */
		float SoftClip(float Value)
		{
			const float Magnitude = std::fabs(Value);
			if (Magnitude <= SoftClipKnee)
			{
				return Value;
			}
			const float Headroom = 1.0f - SoftClipKnee;
			return Sign(Value) * (SoftClipKnee + Headroom * std::tanh((Magnitude - SoftClipKnee) / Headroom));
		}

		/** Lifts small forces above the wheel's own friction without changing the sign */
		float ApplyMinForce(float Value, float MinForce)
		{
			if (MinForce <= 0.0f)
			{
				return Value;
			}
			const float Magnitude = std::fabs(Value);
			const float Lift = MinForce * std::min(Magnitude / MinForceRamp, 1.0f);
			return Sign(Value) * (Lift + (1.0f - MinForce) * Magnitude);
		}

		/**
		 *  What the front tyres make you feel besides the steering torque, built at the wheel's own rate because it
		 *  shakes faster than the physics runs. All of it comes from the tyres' state, scaled by their load:
		 *  - lock-up: a tyre braked past its peak grip starts to stick and slip, a judder that grows as it locks;
		 *  - ABS: the brake pressure pulsing, ~12 times a second;
		 *  - scrub: a tyre sliding sideways past its peak slip angle (understeer) has a fine grain; loose surfaces rumble;
		 *  - road: a bump under one front wheel loads it more than the other, which tugs the steering for a moment.
		 */
		struct TyreEffects
		{
			double LockupPhase = 0.0, ABSPhase = 0.0;
			float Grain = 0.0f, Rumble = 0.0f;
			float SlowLoadDifference = 0.0f, Bump = 0.0f;
			bool bHasLoads = false;
			uint32_t Noise = 0x9E3779B9u;

			/** Strength of each effect at the steering wheel at full (N m), the ABS pulse rate, and the lever (m) a load
			 *  difference between the front wheels acts on the steering through */
			static constexpr float LockupPeakNm = 1.2f;
			static constexpr float ABSPeakNm = 0.7f;
			static constexpr float ABSPulseHz = 12.0f;
			static constexpr float ScrubPeakNm = 0.45f;
			static constexpr float RumblePeakNm = 0.5f;
			static constexpr float BumpLever = 0.012f;

			/** Cheap white noise in [-1, 1] */
			float NextNoise()
			{
				Noise ^= Noise << 13;
				Noise ^= Noise >> 17;
				Noise ^= Noise << 5;
				return static_cast<float>(Noise & 0xFFFF) / 32767.5f - 1.0f;
			}

			/** Steering-wheel torque from the effects (N m, + = to the right) */
			float Compute(const geocar::CarTelemetry& T, const Command& C, float DeltaTime)
			{
				constexpr auto Relaxed = std::memory_order_relaxed;
				const float Speed = T.SpeedMS.load(Relaxed);
				const float Moving = SmoothStep(1.0f, 4.0f, Speed);
				const float Nominal = std::max(T.NominalLoad.load(Relaxed), 100.0f);
				const float PeakRatio = std::max(T.PeakSlipRatio.load(Relaxed), 0.02f);
				const float PeakAngle = std::max(T.PeakSlipAngle.load(Relaxed), 1.0f);

				float Lock = 0.0f, Scrub = 0.0f, Loose = 0.0f, LoadShare = 0.0f;
				bool bABS = false;
				float Loads[2];
				for (int32_t Side = 0; Side < 2; ++Side)
				{
					Loads[Side] = T.FrontLoad[Side].load(Relaxed);
					const float Share = std::clamp(Loads[Side] / Nominal, 0.0f, 2.0f);
					LoadShare += 0.5f * Share;
					const float Braking = -T.FrontSlipRatio[Side].load(Relaxed);
					Lock = std::max(Lock, Share * SmoothStep(PeakRatio * 1.3f, 0.5f, Braking));
					const float Sideways = std::fabs(T.FrontSlipAngle[Side].load(Relaxed));
					Scrub = std::max(Scrub, Share * std::clamp((Sideways - PeakAngle) / (1.5f * PeakAngle), 0.0f, 1.0f));
					Loose = std::max(Loose, std::clamp((1.0f - T.FrontGrip[Side].load(Relaxed)) / 0.4f, 0.0f, 1.0f));
					bABS |= T.FrontABS[Side].load(Relaxed);
				}

				// lock-up judder: faster with road speed, a little uneven like a real stick-slip
				const float LockupHz = std::clamp(14.0f + 0.9f * Speed, 15.0f, 45.0f);
				LockupPhase = std::fmod(LockupPhase + 2.0 * Pi * LockupHz * DeltaTime, 2.0 * Pi);
				const float Judder = static_cast<float>(std::sin(LockupPhase) + 0.35 * std::sin(2.1 * LockupPhase)) + 0.2f * NextNoise();
				const float LockupNm = LockupPeakNm * Lock * Moving * C.LockupGain * Judder;

				// ABS: the pressure pulsing
				ABSPhase = bABS ? std::fmod(ABSPhase + 2.0 * Pi * ABSPulseHz * DeltaTime, 2.0 * Pi) : 0.0;
				const float Pulse = static_cast<float>(std::sin(ABSPhase) * std::fabs(std::sin(ABSPhase)));
				const float ABSNm = bABS ? ABSPeakNm * LoadShare * Moving * C.ABSGain * Pulse : 0.0f;

				// scrub grain and loose-surface rumble: filtered noise, the rumble coarser and growing with speed
				Grain += (NextNoise() - Grain) * (1.0f - std::exp(-DeltaTime * 2.0f * float(Pi) * 45.0f));
				Rumble += (NextNoise() - Rumble) * (1.0f - std::exp(-DeltaTime * 2.0f * float(Pi) * 20.0f));
				const float ScrubNm = C.ScrubGain * Moving * (ScrubPeakNm * Scrub * 2.0f * Grain
					+ RumblePeakNm * Loose * std::clamp(Speed / 15.0f, 0.0f, 1.0f) * 2.0f * Rumble);

				// road: the change in how the load is shared between the front wheels (slow changes, like cornering
				// weight, filtered out), through a short lever about the steering axis
				const float Difference = Loads[0] - Loads[1];
				if (!bHasLoads)
				{
					SlowLoadDifference = Difference;
					bHasLoads = true;
				}
				SlowLoadDifference += (Difference - SlowLoadDifference) * (1.0f - std::exp(-DeltaTime / 0.25f));
				Bump += ((Difference - SlowLoadDifference) - Bump) * (1.0f - std::exp(-DeltaTime * 2.0f * float(Pi) * 25.0f));
				const float RoadNm = -Bump * BumpLever * std::fabs(C.TelemetryScale) * Moving * C.RoadGain;

				return (LockupNm + ABSNm + ScrubNm + RoadNm) * Sign(C.TelemetryScale == 0.0f ? 1.0f : C.TelemetryScale);
			}
		};
	}

	ForceFeedback::~ForceFeedback()
	{
		Stop();
	}

	void ForceFeedback::Start(void* Window, LogFunction Log)
	{
		if (Thread.joinable())
		{
			return;
		}
		bStop = false;
		Thread = std::thread([this, Window, Log] { Run(Window, Log); });
	}

	void ForceFeedback::Stop()
	{
		if (Thread.joinable())
		{
			bStop = true;
			Thread.join();
		}
	}

	WheelInput ForceFeedback::GetInput() const
	{
		std::lock_guard<std::mutex> Guard(Lock);
		return Input;
	}

	void ForceFeedback::SetCommand(const Command& NewCommand, std::shared_ptr<const geocar::CarTelemetry> Telemetry)
	{
		std::lock_guard<std::mutex> Guard(Lock);
		Wanted = NewCommand;
		CarTelemetry = std::move(Telemetry);
	}

	Status ForceFeedback::GetStatus() const
	{
		std::lock_guard<std::mutex> Guard(Lock);
		return Loop;
	}

	void ForceFeedback::Run(void* Window, LogFunction Log)
	{
		using namespace ForceFeedbackImpl;
		BeginLoopTiming();
		WheelDevice Device;
		WheelInput Read;

		double LastTime = Now();
		double NextReconnect = 0.0;
		double RateWindowStart = LastTime;
		int32_t RateWindowLoops = 0;
		float LoopHz = 0.0f;

		float Force = 0.0f, Damper = 0.0f, Friction = 0.0f, Output = 0.0f;
		TyreEffects Effects;
		bool bClipping = false;

		while (!bStop)
		{
			const double Time = Now();
			const float DeltaTime = static_cast<float>(std::min(Time - LastTime, 0.05));
			LastTime = Time;

			// (re)connect
			if (!Device.IsOpen() && Time >= NextReconnect)
			{
				NextReconnect = Time + ReconnectInterval;
				Device.Open(Window, Log);
				Force = Damper = Friction = 0.0f;
			}

			// read the wheel
			if (Device.IsOpen() && !Device.Poll(Read))
			{
				if (Log)
				{
					Log("Wheel disconnected");
				}
				Device.Close();
			}
			if (!Device.IsOpen())
			{
				Read = WheelInput();
			}

			// swap with the game
			Command C;
			std::shared_ptr<const geocar::CarTelemetry> Telemetry;
			{
				std::lock_guard<std::mutex> Guard(Lock);
				Input = Read;
				C = Wanted;
				Telemetry = CarTelemetry;
				Loop.LoopHz = LoopHz;
				Loop.AppliedForce = Output;
				Loop.AppliedDamper = Damper;
				Loop.AppliedFriction = Friction;
				Loop.bClipping = bClipping;
			}

			// safety: no fresh command means no force
			const bool bActive = C.bEnabled && (Time - C.TimeSeconds) < CommandTimeout;

			// our own cars: the torque straight from the physics, so it isn't tied to the frame rate
			float TorqueNm = C.TorqueNm;
			if (C.bUseTelemetry && Telemetry)
			{
				TorqueNm = Telemetry->KingpinTorque.load(std::memory_order_relaxed) * C.TelemetryScale;
			}

			// car torque (N m) -> share of the wheel's range
			const float RequestedForce = TorqueNm * C.Gain / std::max(C.MaxForceNm, 1.0f);
			const float TargetForce = bActive ? RequestedForce : 0.0f;
			const float TargetDamper = bActive ? std::clamp(C.Damper, 0.0f, 1.0f) : 0.0f;
			const float TargetFriction = bActive ? std::clamp(C.Friction, 0.0f, 1.0f) : 0.0f;

			// smooth the game's per-frame steps, then limit the slew rate
			const float Alpha = 1.0f - std::exp(-DeltaTime / SmoothingTau);
			const float MaxStep = MaxSlewRate * DeltaTime;
			Force += std::clamp((TargetForce - Force) * Alpha, -MaxStep, MaxStep);
			Damper += std::clamp((TargetDamper - Damper) * Alpha, -MaxStep, MaxStep);
			Friction += std::clamp((TargetFriction - Friction) * Alpha, -MaxStep, MaxStep);

			// the tyres' shakes go on top, after the smoothing (which would take them out)
			float Shake = 0.0f;
			if (bActive && C.bUseTelemetry && Telemetry)
			{
				Shake = Effects.Compute(*Telemetry, C, DeltaTime) * C.Gain / std::max(C.MaxForceNm, 1.0f);
			}

			// shape the output: soft clip near the limit, then the minimum force
			const float Total = Force + Shake;
			bClipping = std::fabs(Total) > 1.0f;
			Output = ApplyMinForce(SoftClip(Total), bActive ? C.MinForce : 0.0f);

			if (Device.IsOpen())
			{
				// DirectInput's positive constant force on the steering axis turns the wheel to the left (anticlockwise),
				// while our torque is + = to the right. Confirmed on a Moza R9.
				Device.SetConstantForce(-Output);
				Device.SetDamper(Damper);
				Device.SetFriction(Friction);
			}

			// the loop rate, once a second
			++RateWindowLoops;
			if (Time - RateWindowStart >= 1.0)
			{
				LoopHz = static_cast<float>(RateWindowLoops / (Time - RateWindowStart));
				RateWindowLoops = 0;
				RateWindowStart = Time;
			}

			const double Remaining = Period - (Now() - Time);
			if (Remaining > 0.0)
			{
				std::this_thread::sleep_for(std::chrono::duration<double>(Remaining));
			}
		}

		// Close() zeroes every force before letting the device go
		Device.Close();
		EndLoopTiming();
		std::lock_guard<std::mutex> Guard(Lock);
		Input = WheelInput();
	}
}
