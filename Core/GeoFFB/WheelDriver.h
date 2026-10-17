// Geodrive Core - GPL (see Core/README.md)
//
// Driving one of our cars with a steering wheel: how the wheel and pedals are set up (the settings a game saves),
// turning their positions into the car's controls, and the car's state into the force command.

#pragma once

#include "WheelTypes.h"
#include "GeoCar/CarTypes.h"

#include <map>
#include <string>

namespace geoffb
{
	/** The wheel's setup and the force feedback's tuning. Defaults: a Moza R9 with its pedals in the base. */
	struct WheelSettings
	{
		int32_t SteeringAxis = 0;	// X
		int32_t ThrottleAxis = 2;	// Z
		int32_t BrakeAxis = 5;		// RZ
		int32_t ClutchAxis = -1;	// -1 = not set up
		bool bUseClutchPedal = true;

		/** Per axis: the raw value at rest and fully pressed (uncalibrated: one end of the range to the other) */
		int32_t AxisRest[NumAxes] = { AxisMin, AxisMin, AxisMin, AxisMin, AxisMin, AxisMin, AxisMin, AxisMin };
		int32_t AxisPressed[NumAxes] = { AxisMax, AxisMax, AxisMax, AxisMax, AxisMax, AxisMax, AxisMax, AxisMax };

		bool bInvertSteering = false;
		float WheelRotationDegrees = 900.0f;	// lock to lock; must match the wheel's own software
		float SteeringRatio = 12.0f;			// steering-wheel degrees per road-wheel degree

		float Gain = 0.6f;			// overall strength, [0, 1]
		float Damper = 0.15f;		// when parked, [0, 1]
		float Friction = 0.1f;		// tyre scrub when parked, [0, 1]
		float RackFriction = 0.04f;	// the steering rack's own friction while driving: weight round the centre
		float LockupGain = 1.0f;	// the tyres' effects, 0-2
		float ABSGain = 1.0f;
		float ScrubGain = 1.0f;
		float RoadGain = 1.0f;
		float MaxForceNm = 15.0f;	// our cars' torque (power steering included, ~15 N m at the grip limit) at full force
		float MinForce = 0.03f;		// [0, 1]
		bool bInvertForce = false;
		bool bEnabled = true;

		int32_t ShiftUpButton = 13;	// paddles (Moza R9: 13 up, 12 down), -1 = none
		int32_t ShiftDownButton = 12;

		/** The wheel's own controls for the camera and seat (see WheelSetup.h): name -> a button (0-127) or a hat
		 *  direction (1000 + hat * 4 + 0 up, 1 right, 2 down, 3 left) */
		std::map<std::string, int32_t> Controls;

		bool UsesClutchPedal() const { return bUseClutchPedal && ClutchAxis >= 0; }
	};

	/** A pedal's position in [0, 1] from its calibration */
	float ReadPedal(const WheelSettings& Settings, const WheelInput& Input, int32_t Axis);

	/** The physical wheel's position: [-1, 1] across its rotation (+ = right) */
	float WheelPosition(const WheelSettings& Settings, const WheelInput& Input);

	/** What the wheel last did to the car, and the paddles' last state (to shift once per pull) */
	struct DriverState
	{
		bool bShiftUpWasPressed = false;
		bool bShiftDownWasPressed = false;
		float WheelDegrees = 0.0f;	// the steering wheel's angle
		float RoadDegrees = 0.0f;	// the front wheels' angle it asks for
		float Throttle = 0.0f, Brake = 0.0f, Clutch = 0.0f;
	};

	/** Drives the car's controls from the wheel: steering through the ratio straight to the road wheels, pedals, the
	 *  clutch pedal (or the car's automatic clutch), paddles (a pull shifts once and switches to manual) */
	void Drive(const WheelSettings& Settings, const WheelInput& Input, geocar::Controls& Controls, DriverState& State);

	/** The force command for one of our cars: the steering torque from its telemetry, the tyres' effects, damping and
	 *  friction by speed (SpeedMS, KingpinTorque and SpeedFade from its state: see geocar::VehicleState). bActive false (paused, in the background, no car): no force. */
	Command MakeCommand(const WheelSettings& Settings, float SpeedMS, float KingpinTorque, float SpeedFade, bool bActive);
}
