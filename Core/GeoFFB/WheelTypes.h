// Geodrive Core - GPL (see Core/README.md)
//
// A steering wheel (and its pedals): what it reports, what the game asks it to do, and how its loop is getting on.

#pragma once

#include <cstdint>
#include <string>

namespace geoffb
{
	/** DirectInput axes we read: X, Y, Z, RX, RY, RZ, Slider0, Slider1 */
	constexpr int32_t NumAxes = 8;
	constexpr int32_t NumButtons = 128;
	constexpr int32_t NumHats = 4;

	/** The axis range we ask the device for */
	constexpr int32_t AxisMin = -10000;
	constexpr int32_t AxisMax = 10000;

	/** Display names for the axes, in their order */
	const char* AxisName(int32_t Axis);

	/** The wheel's raw state */
	struct WheelInput
	{
		bool bConnected = false;	// a device is open and answering
		bool bHasFFB = false;		// it has force feedback
		std::string DeviceName;		// the product name its driver gives
		int32_t Axes[NumAxes] = {};	// raw, in [AxisMin, AxisMax]
		bool Buttons[NumButtons] = {};
		int32_t Hats[NumHats] = { -1, -1, -1, -1 };	// hundredths of a degree clockwise from up, -1 = centred
	};

	/** What the game asks of the wheel */
	struct Command
	{
		float TorqueNm = 0.0f;		// steering-wheel torque from the car (sign already corrected for the device)
		float MaxForceNm = 45.0f;	// car torque that maps to 100% wheel output
		float Gain = 0.7f;			// overall strength, [0, 1]
		float MinForce = 0.0f;		// minimum output so small forces aren't lost in the wheel's own friction, [0, 1]
		float Damper = 0.0f;		// device damper, [0, 1] (always opposes the wheel's motion)
		float Friction = 0.0f;		// device friction, [0, 1]
		bool bEnabled = false;		// false drops every force to zero (paused, in the background, no car)

		/** Read the torque from the car's telemetry at the loop's rate instead of TorqueNm (our own cars) */
		bool bUseTelemetry = false;
		float TelemetryScale = 0.0f;	// kingpin torque -> steering-wheel N m (sign and 1 / steering ratio)

		/** Strength of the effects made from the tyres' telemetry, 0-2 (1 = as the physics gives it): brake lock-up
		 *  shudder, ABS pulses, the grain of tyres scrubbing sideways (and loose surfaces), road bumps */
		float LockupGain = 1.0f;
		float ABSGain = 1.0f;
		float ScrubGain = 1.0f;
		float RoadGain = 1.0f;

		double TimeSeconds = 0.0;	// when the game last wrote it (geoffb::Now()): forces fade if it stops
	};

	/** How the wheel's loop is doing, for an overlay */
	struct Status
	{
		float LoopHz = 0.0f;
		float AppliedForce = 0.0f;	// after smoothing and the safety checks
		float AppliedDamper = 0.0f;
		float AppliedFriction = 0.0f;
		bool bClipping = false;		// the force asked for is more than the wheel can give (lower the gain or raise max N m)
	};

	/** Seconds on a steady clock, for Command::TimeSeconds */
	double Now();
}
