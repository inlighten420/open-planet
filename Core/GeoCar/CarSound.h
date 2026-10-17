// Geodrive Core - GPL (see Core/README.md)
//
// The car's sound, synthesised live (no recordings): each cylinder fires at its crank angle into its exhaust bank, whose
// pipe rings at its own resonances (EngineSound says which engine: flat six, V8, turbo diesel...), with the intake, a
// turbo's whistle and blow-off, pops on lift-off; tyre squeal from slip, road rumble and wind from speed. The host asks
// it for samples on its audio thread (Unreal's synth component, Godot's audio stream generator) and gives it the car's
// state every frame.

#pragma once

#include "CarBody.h"
#include "CarTypes.h"

#include <cstdint>

namespace geocar
{
	/** What the sound is made from, each frame */
	struct SoundInput
	{
		float EngineRPM = 1200.0f;
		float Load = 0.0f;		// 0 = off throttle (overrun), 1 = full throttle
		float SpeedMS = 0.0f;
		float TyreSlip = 0.0f;	// 0 = rolling, 1 = screeching
		float Rough = 0.0f;		// 0 = smooth asphalt, 1 = gravel or grass at speed
	};

	/** The player's tweaks (the menu's sound settings): 1 = as the car is meant to sound */
	struct SoundMix
	{
		float Volume = 1.0f;	// 0-2
		float EngineGain = 1.0f;	// engine against the tyres and wind
		float RaspGain = 1.0f;	// brightness: the sharp edge of each exhaust pulse
		float PipeGain = 1.0f;	// how much the exhaust rings
		float CrackleGain = 1.0f;	// pops on lift-off
	};

	/** The sound's input from the car's state: the throttle as the engine's load (cut while shifting and when traction
	 *  control cuts in), tyre squeal past the peak of the slip curves on asphalt, rumble on loose surfaces */
	SoundInput SoundFromState(const VehicleState& State, float Throttle);

	class SoundSynth
	{
	public:
		explicit SoundSynth(float SampleRate);

		/** The engine to sound like (a different car) */
		void SetEngine(const EngineSound& Engine);

		/** Frames of sound (the same on every channel), from where the car is now; changes are smoothed so they never
		 *  click */
		void Generate(const SoundInput& Input, const SoundMix& Mix, float* Out, int32_t Frames, int32_t Channels);

	private:
		static constexpr int32_t MaxDelay = 2048;	// longest pipe delay allowed for (samples, at up to 96 kHz)

		struct BandPass
		{
			float B0 = 0.0f, B2 = 0.0f, A1 = 0.0f, A2 = 0.0f;
			float X1 = 0.0f, X2 = 0.0f, Y1 = 0.0f, Y2 = 0.0f;
			void Set(float Frequency, float Q, float SampleRate);
			float Process(float X);
		};
		struct LowPass
		{
			float Y = 0.0f;
			float Process(float X, float Coefficient) { Y += Coefficient * (X - Y); return Y; }
		};

		/** An exhaust pipe as a digital waveguide: each pulse runs to the open end and back, partly reflected (inverted
		 *  and dulled at an open end), so the pipe rings at its own resonances */
		struct Pipe
		{
			float Line[MaxDelay] = {};
			int32_t Write = 0;
			int32_t Delay = 200;
			float Reflection = 0.3f;
			LowPass EndLoss;
			void SetLength(float Metres, float SampleRate);
			float Process(float X);
		};

		float Noise();

		float SampleRate;
		EngineSound Engine;
		float RPM = 1200.0f, Load = 0.0f, Speed = 0.0f, Slip = 0.0f, Rough = 0.0f;
		float CrankAngle = 0.0f, LastFiring = 0.0f;
		float Pulse[2] = { 0.0f, 0.0f }, Pop[2] = { 0.0f, 0.0f };
		float PulseDecay = 0.99f;
		float Boost = 0.0f, BlowOff = 0.0f, TurboPhase = 0.0f, HybridPhase = 0.0f;
		float SquealPhase = 0.0f, SquealWobble = 0.0f;
		float WindCoefficient = 0.0f, MufflerCoefficient = 0.1f, TurboCoefficient = 0.5f;
		int32_t FrameCount = 0;
		uint32_t NoiseState = 0x9E3779B9u;
		Pipe Pipes[2];
		BandPass Body, Body2, Rasp, Intake, SquealNoise, BlowOffBand;
		LowPass Muffler, Muffler2, RoadLow, WindLow, TurboSmooth[2];
	};
}
