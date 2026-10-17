// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// What the steering wheel needs from the car, written by the physics each step and read by the force feedback loop
// at its own rate (lock-free: each value on its own).

#pragma once

#include "CarTypes.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>

namespace geocar
{
	struct CarTelemetry
	{
		std::atomic<float> KingpinTorque { 0.0f };	// about the steering axis, after power steering (N m, + = to the right)
		std::atomic<float> SpeedFade { 0.0f };		// 0 parked, 1 above walking pace

		/** The front tyres (0 left, 1 right): load, slip, the surface's grip, ABS working */
		std::atomic<float> FrontLoad[2] = { 0.0f, 0.0f };
		std::atomic<float> FrontSlipRatio[2] = { 0.0f, 0.0f };
		std::atomic<float> FrontSlipAngle[2] = { 0.0f, 0.0f };
		std::atomic<float> FrontGrip[2] = { 1.0f, 1.0f };
		std::atomic<bool> FrontABS[2] = { false, false };
		std::atomic<float> SpeedMS { 0.0f };

		/** The tyre's limits, to judge the slips by */
		std::atomic<float> PeakSlipRatio { 0.1f };
		std::atomic<float> PeakSlipAngle { 7.0f };	// degrees
		std::atomic<float> NominalLoad { 4000.0f };
	};

	/** Fills the telemetry from a step's state */
	inline void Publish(const VehicleState& State, const TireSetup& Tire, CarTelemetry& Out)
	{
		constexpr auto Relaxed = std::memory_order_relaxed;
		Out.KingpinTorque.store(State.KingpinTorque, Relaxed);
		Out.SpeedFade.store(State.SpeedFade, Relaxed);
		for (int32_t Side = 0; Side < 2; ++Side)
		{
			const WheelState& Front = State.Wheels[Side];
			Out.FrontLoad[Side].store(Front.bContact ? Front.Load : 0.0f, Relaxed);
			Out.FrontSlipRatio[Side].store(Front.SlipRatio, Relaxed);
			Out.FrontSlipAngle[Side].store(Front.SlipAngleDegrees, Relaxed);
			Out.FrontGrip[Side].store(Front.Grip, Relaxed);
			Out.FrontABS[Side].store(Front.bABSActive, Relaxed);
		}
		Out.SpeedMS.store(std::fabs(State.SpeedMS), Relaxed);
		Out.PeakSlipRatio.store(Tire.PeakSlipX, Relaxed);
		Out.PeakSlipAngle.store(std::atan(Tire.PeakSlipY) * 57.29577951f, Relaxed);
		Out.NominalLoad.store(Tire.NominalLoad, Relaxed);
	}
}
