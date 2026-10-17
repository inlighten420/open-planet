// Geodrive Core - GPL (see Core/README.md)

#include "CarSound.h"

#include <algorithm>
#include <cmath>

namespace geocar
{
	namespace
	{
		constexpr float Pi = 3.14159265358979f;

		/** Speed of sound in hot exhaust gas (m/s): with the pipe length it sets the pipe's resonances */
		constexpr float ExhaustSoundSpeed = 480.0f;

		constexpr float MasterGain = 0.45f;

		/** Filter coefficients are refreshed this often (frames) */
		constexpr int32_t ControlRate = 64;

		/** Small fixed differences between cylinders: the uneven, gritty texture of a real engine */
		constexpr float CylinderLevel[12] = { 1.0f, 0.84f, 0.95f, 0.78f, 0.92f, 0.87f, 0.97f, 0.81f, 0.9f, 0.86f, 0.93f, 0.8f };

		/** One-pole coefficient for a cutoff frequency */
		float LowPassCoefficient(float Frequency, float SampleRate)
		{
			return 1.0f - std::exp(-2.0f * Pi * Frequency / SampleRate);
		}

		float Lerp(float A, float B, float T) { return A + (B - A) * T; }
	}

	SoundInput SoundFromState(const VehicleState& State, float Throttle)
	{
		SoundInput Out;
		const float Speed = std::fabs(State.SpeedMS);
		Out.EngineRPM = State.EngineRPM;
		Out.SpeedMS = Speed;

		// engine load: the pedal, cut while the gearbox shifts (ignition cut) and when traction control intervenes
		Out.Load = std::clamp(Throttle, 0.0f, 1.0f) * (State.bShifting ? 0.15f : State.bTCActive ? 0.6f : 1.0f);

		// tyres squeal past the peak of the slip curves on asphalt; loose surfaces rumble instead
		float Slip = 0.0f, Rough = 0.0f;
		int32_t Contacts = 0;
		for (const WheelState& Wheel : State.Wheels)
		{
			if (!Wheel.bContact)
			{
				continue;
			}
			++Contacts;
			if (Wheel.Grip < 0.9f)
			{
				Rough += 1.0f;
				continue;
			}
			const float Sliding = std::max((std::fabs(Wheel.SlipAngleDegrees) - 5.0f) / 7.0f, (std::fabs(Wheel.SlipRatio) - 0.1f) / 0.25f);
			Slip = std::max(Slip, std::clamp(Sliding, 0.0f, 1.0f) * std::clamp(Wheel.Load / 3000.0f, 0.0f, 1.0f));
		}
		Out.TyreSlip = Slip * std::clamp((Speed - 1.0f) / 3.0f, 0.0f, 1.0f);
		Out.Rough = Contacts > 0 ? Rough / Contacts * std::clamp(Speed / 15.0f, 0.0f, 1.0f) : 0.0f;
		return Out;
	}

	void SoundSynth::BandPass::Set(float Frequency, float Q, float SampleRate)
	{
		const float W = 2.0f * Pi * std::min(Frequency, SampleRate * 0.45f) / SampleRate;
		const float Alpha = std::sin(W) / (2.0f * Q);
		const float A0 = 1.0f + Alpha;
		B0 = Alpha / A0;
		B2 = -Alpha / A0;
		A1 = -2.0f * std::cos(W) / A0;
		A2 = (1.0f - Alpha) / A0;
	}

	float SoundSynth::BandPass::Process(float X)
	{
		const float Y = B0 * X + B2 * X2 - A1 * Y1 - A2 * Y2;
		X2 = X1;
		X1 = X;
		Y2 = Y1;
		Y1 = Y;
		return Y;
	}

	void SoundSynth::Pipe::SetLength(float Metres, float SampleRate)
	{
		Delay = std::clamp(static_cast<int32_t>(std::lround(2.0f * Metres / ExhaustSoundSpeed * SampleRate)), 8, MaxDelay - 1);
	}

	float SoundSynth::Pipe::Process(float X)
	{
		const float Back = Line[(Write - Delay + MaxDelay) % MaxDelay];
		const float Y = X - Reflection * EndLoss.Process(Back, 0.3f);
		Line[Write] = Y;
		Write = (Write + 1) % MaxDelay;
		return Y;
	}

	SoundSynth::SoundSynth(float InSampleRate)
		: SampleRate(std::max(InSampleRate, 8000.0f))
	{
		SquealNoise.Set(1150.0f, 5.0f, SampleRate);
		Intake.Set(700.0f, 1.2f, SampleRate);
		Rasp.Set(2400.0f, 0.9f, SampleRate);
		BlowOffBand.Set(3500.0f, 0.8f, SampleRate);
		SetEngine(EngineSound());
	}

	void SoundSynth::SetEngine(const EngineSound& InEngine)
	{
		Engine = InEngine;
		for (int32_t Bank = 0; Bank < 2; ++Bank)
		{
			Pipes[Bank].SetLength(Engine.PipeLength[Bank], SampleRate);
		}
	}

	float SoundSynth::Noise()
	{
		NoiseState ^= NoiseState << 13;
		NoiseState ^= NoiseState >> 17;
		NoiseState ^= NoiseState << 5;
		return static_cast<float>(NoiseState) / 2147483648.0f - 1.0f;
	}

	void SoundSynth::Generate(const SoundInput& Input, const SoundMix& Mix, float* Out, int32_t Frames, int32_t Channels)
	{
		const float TargetRPM = Input.EngineRPM;
		const float TargetLoad = std::clamp(Input.Load, 0.0f, 1.0f);
		const float TargetSpeed = std::fabs(Input.SpeedMS);
		const float TargetSlip = std::clamp(Input.TyreSlip, 0.0f, 1.0f);
		const float TargetRough = std::clamp(Input.Rough, 0.0f, 1.0f);
		const float Volume = std::clamp(Mix.Volume, 0.0f, 2.0f);
		const float EngineGain = std::clamp(Mix.EngineGain, 0.0f, 3.0f);
		const float RaspGain = std::clamp(Mix.RaspGain, 0.0f, 3.0f);
		const float CrackleGain = std::clamp(Mix.CrackleGain, 0.0f, 3.0f);
		const float PipeGain = std::clamp(Mix.PipeGain, 0.0f, 3.0f);
		for (Pipe& P : Pipes)
		{
			P.Reflection = std::clamp(0.3f * PipeGain, 0.0f, 0.8f);
		}

		// smoothing so control changes (once per game frame) never click
		const float FastSmooth = LowPassCoefficient(40.0f, SampleRate);
		const float SlowSmooth = LowPassCoefficient(8.0f, SampleRate);
		const float BoostSmooth = LowPassCoefficient(1.5f, SampleRate);
		const int32_t Cylinders = std::min(static_cast<int32_t>(Engine.FiringAngles.size()), 12);
		const float Openness = std::clamp(Engine.Openness * RaspGain, 0.0f, 1.5f);

		for (int32_t Frame = 0; Frame < Frames; ++Frame, ++FrameCount)
		{
			RPM += FastSmooth * (TargetRPM - RPM);
			const float PreviousLoad = Load;
			Load += FastSmooth * (TargetLoad - Load);
			Speed += SlowSmooth * (TargetSpeed - Speed);
			Slip += SlowSmooth * (TargetSlip - Slip);
			Rough += SlowSmooth * (TargetRough - Rough);

			const float RevFraction = std::clamp(RPM / std::max(Engine.RedlineRPM, 1000.0f), 0.0f, 1.2f);
			const float FiringFrequency = std::max(RPM, 300.0f) / 60.0f * Cylinders * 0.5f;

			// turbo boost builds with load and revs and lags behind; lifting off with boost up blows it off
			const float BoostTarget = Engine.Turbo > 0.0f ? Load * std::clamp(RevFraction * 1.4f - 0.2f, 0.0f, 1.0f) : 0.0f;
			Boost += BoostSmooth * (BoostTarget - Boost);
			if (Engine.Turbo > 0.0f && PreviousLoad > 0.6f && Load < 0.3f && Boost > 0.4f && BlowOff < 0.05f)
			{
				BlowOff = Boost * Engine.Turbo;
			}
			BlowOff *= 1.0f - 4.0f / SampleRate;

			if (FrameCount % ControlRate == 0)
			{
				Body.Set(FiringFrequency, 1.2f, SampleRate);
				Body2.Set(FiringFrequency * 2.0f, 2.2f, SampleRate);
				MufflerCoefficient = LowPassCoefficient(500.0f + 3500.0f * Openness * (0.5f + RevFraction), SampleRate);
				TurboCoefficient = LowPassCoefficient(Lerp(8000.0f, 1500.0f, Engine.Turbo), SampleRate);
				WindCoefficient = LowPassCoefficient(150.0f + Speed * 18.0f, SampleRate);
				PulseDecay = std::exp(-1.0f / ((0.0006f + 0.0012f * (1.0f - std::min(Openness, 1.0f))) * SampleRate));
			}

			// --- engine: each cylinder fires at its crank angle into its bank's pipe ---
			const float PreviousAngle = CrankAngle;
			CrankAngle += std::max(RPM, 300.0f) / 60.0f * 360.0f / SampleRate;
			const bool bWrapped = CrankAngle >= 720.0f;
			if (bWrapped)
			{
				CrankAngle -= 720.0f;
			}
			const bool bOverrun = Load < 0.08f && RevFraction > 0.45f;
			for (int32_t c = 0; c < Cylinders; ++c)
			{
				const float Angle = Engine.FiringAngles[c];
				const bool bFires = bWrapped ? (Angle >= PreviousAngle || Angle < CrankAngle) : (Angle >= PreviousAngle && Angle < CrankAngle);
				if (!bFires)
				{
					continue;
				}
				const int32_t Bank = c < static_cast<int32_t>(Engine.Banks.size()) ? std::clamp(Engine.Banks[c], 0, 1) : c % 2;
				Pulse[Bank] += CylinderLevel[c] * (1.0f + 0.12f * Noise()) * (0.25f + 0.75f * Load);
				LastFiring = 0.0f;

				// unburnt fuel lighting in the exhaust when the throttle shuts at high revs
				if (bOverrun && Engine.Crackle > 0.0f && std::fabs(Noise()) < Engine.Crackle * CrackleGain * 0.12f * std::min(1.0f, 100.0f / FiringFrequency))
				{
					Pop[Bank] = 1.2f + 0.8f * std::fabs(Noise());
				}
			}
			LastFiring += 1.0f / SampleRate;

			float Exhaust = 0.0f, Excitation = 0.0f;
			for (int32_t Bank = 0; Bank < 2; ++Bank)
			{
				// a sharp pressure pulse with gritty noise on it, smoothed by a turbo if there is one
				const float Edge = Pulse[Bank] * (0.65f + 0.35f * Noise());
				const float Smoothed = Lerp(Edge, TurboSmooth[Bank].Process(Edge, TurboCoefficient), Engine.Turbo);
				const float PopNoise = Pop[Bank] * Noise();
				Excitation += Smoothed;
				// mostly the pulse itself, with the pipe's ring under it
				Exhaust += 0.5f * (Smoothed + PopNoise) + 0.5f * Pipes[Bank].Process(Smoothed + PopNoise);
				Pulse[Bank] *= PulseDecay;
				Pop[Bank] *= 1.0f - 900.0f / SampleRate;
			}

			// the muffler takes the edge off; a little of the raw rasp gets through, more on open pipes and high revs
			float EngineOut = Muffler2.Process(Muffler.Process(Exhaust, MufflerCoefficient), MufflerCoefficient) * 1.6f
				+ Rasp.Process(Exhaust) * Openness * (0.15f + 0.6f * Load) * (0.3f + RevFraction)
				+ Body.Process(Excitation) * 1.1f + Body2.Process(Excitation) * 0.35f;

			// the intake breathing on throttle, the turbo whistle, and the blow-off valve's hiss
			EngineOut += Intake.Process(Noise()) * Load * (0.05f + 0.25f * RevFraction) * std::exp(-LastFiring * 300.0f);
			if (Engine.Turbo > 0.0f)
			{
				TurboPhase += (2500.0f + 9000.0f * Boost) / SampleRate;
				TurboPhase -= std::floor(TurboPhase);
				EngineOut += std::sin(2.0f * Pi * TurboPhase) * Boost * Boost * Engine.Turbo * 0.06f;
				EngineOut += BlowOffBand.Process(Noise()) * BlowOff * 0.5f;
			}

			// a hybrid's electric motor: a whine geared to the road speed, on drive and on regen
			if (Engine.Hybrid > 0.0f)
			{
				HybridPhase += Speed * 48.0f / SampleRate;
				HybridPhase -= std::floor(HybridPhase);
				const float Whine = std::sin(2.0f * Pi * HybridPhase) + 0.4f * std::sin(6.0f * Pi * HybridPhase);
				EngineOut += Whine * Engine.Hybrid * 0.035f * std::clamp(Speed / 20.0f, 0.0f, 1.0f) * (0.4f + 0.6f * Load);
			}

			EngineOut *= (0.55f + 0.45f * Load) * EngineGain;
			EngineOut = std::tanh(EngineOut * 2.0f) * 0.8f;

			// --- tyres: a wavering screech when sliding, gravel/grass rumble on rough ground ---
			SquealPhase += 640.0f * (1.0f + 0.04f * std::sin(SquealWobble)) / SampleRate;
			SquealPhase -= std::floor(SquealPhase);
			SquealWobble += 2.0f * Pi * 7.0f / SampleRate;
			if (SquealWobble > 2.0f * Pi)
			{
				SquealWobble -= 2.0f * Pi;
			}
			const float SquealLevel = Slip * Slip * 0.35f;
			const float Squeal = SquealLevel * (std::sin(2.0f * Pi * SquealPhase) * 0.5f + SquealNoise.Process(Noise()) * 2.0f);

			const float SpeedFraction = std::clamp(Speed / 40.0f, 0.0f, 1.0f);
			const float RoadNoise = RoadLow.Process(Noise(), LowPassCoefficient(160.0f, SampleRate)) * (0.06f + 0.4f * Rough) * SpeedFraction;

			// --- wind: rises with the square of speed and gets brighter ---
			const float WindLevel = std::min((Speed / 60.0f) * (Speed / 60.0f) * 0.3f, 0.4f);
			const float Wind = WindLow.Process(Noise(), WindCoefficient) * WindLevel;

			const float Output = std::tanh((EngineOut + Squeal + RoadNoise + Wind) * MasterGain * Volume);
			for (int32_t Channel = 0; Channel < Channels; ++Channel)
			{
				Out[Frame * Channels + Channel] = Output;
			}
		}
	}
}
