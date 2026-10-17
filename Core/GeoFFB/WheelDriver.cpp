// Geodrive Core - GPL (see Core/README.md)

#include "WheelDriver.h"

#include <algorithm>
#include <cmath>

namespace geoffb
{
	float ReadPedal(const WheelSettings& Settings, const WheelInput& Input, int32_t Axis)
	{
		if (Axis < 0 || Axis >= NumAxes)
		{
			return 0.0f;
		}
		const float Rest = static_cast<float>(Settings.AxisRest[Axis]);
		const float Pressed = static_cast<float>(Settings.AxisPressed[Axis]);
		if (std::fabs(Rest - Pressed) <= 1.0e-8f)
		{
			return 0.0f;
		}
		return std::clamp((static_cast<float>(Input.Axes[Axis]) - Rest) / (Pressed - Rest), 0.0f, 1.0f);
	}

	float WheelPosition(const WheelSettings& Settings, const WheelInput& Input)
	{
		if (Settings.SteeringAxis < 0 || Settings.SteeringAxis >= NumAxes)
		{
			return 0.0f;
		}
		return std::clamp(static_cast<float>(Input.Axes[Settings.SteeringAxis]) / AxisMax, -1.0f, 1.0f) * (Settings.bInvertSteering ? -1.0f : 1.0f);
	}

	void Drive(const WheelSettings& Settings, const WheelInput& Input, geocar::Controls& Controls, DriverState& State)
	{
		auto Pressed = [&Input](int32_t Button) { return Button >= 0 && Button < NumButtons && Input.Buttons[Button]; };

		// steering: the physical wheel's angle through the steering ratio, straight to the road wheels
		if (Settings.SteeringAxis >= 0 && Settings.SteeringAxis < NumAxes)
		{
			State.WheelDegrees = WheelPosition(Settings, Input) * Settings.WheelRotationDegrees * 0.5f;
			State.RoadDegrees = State.WheelDegrees / std::max(Settings.SteeringRatio, 1.0f);
			Controls.SteerDegrees = State.RoadDegrees;
		}
		if (Settings.ThrottleAxis >= 0)
		{
			State.Throttle = ReadPedal(Settings, Input, Settings.ThrottleAxis);
			Controls.Throttle = State.Throttle;
		}
		if (Settings.BrakeAxis >= 0)
		{
			State.Brake = ReadPedal(Settings, Input, Settings.BrakeAxis);
			Controls.Brake = State.Brake;
		}

		// the clutch pedal, or the car's automatic clutch when there isn't one (or it's switched off)
		State.Clutch = Settings.UsesClutchPedal() ? ReadPedal(Settings, Input, Settings.ClutchAxis) : 0.0f;
		Controls.Clutch = State.Clutch;
		Controls.bClutchPedal = Settings.UsesClutchPedal();

		// paddles: one shift per pull, and shifting switches the car to manual
		const bool bShiftUp = Pressed(Settings.ShiftUpButton);
		const bool bShiftDown = Pressed(Settings.ShiftDownButton);
		if ((bShiftUp && !State.bShiftUpWasPressed) || (bShiftDown && !State.bShiftDownWasPressed))
		{
			Controls.bAutomatic = false;
		}
		if (bShiftUp && !State.bShiftUpWasPressed)
		{
			++Controls.ShiftUpCount;
		}
		if (bShiftDown && !State.bShiftDownWasPressed)
		{
			++Controls.ShiftDownCount;
		}
		State.bShiftUpWasPressed = bShiftUp;
		State.bShiftDownWasPressed = bShiftDown;
	}

	Command MakeCommand(const WheelSettings& Settings, float SpeedMS, float KingpinTorque, float SpeedFade, bool bActive)
	{
		Command C;
		C.TimeSeconds = Now();
		if (!bActive || !Settings.bEnabled)
		{
			return C;
		}
		const float Invert = Settings.bInvertForce ? -1.0f : 1.0f;
		const float Ratio = std::max(Settings.SteeringRatio, 1.0f);
		const float Speed = std::fabs(SpeedMS);

		// the steering torque comes from the car's telemetry at the wheel loop's own rate
		C.bUseTelemetry = true;
		C.TelemetryScale = Invert / Ratio;
		C.TorqueNm = KingpinTorque / Ratio * Invert;
		C.MaxForceNm = Settings.MaxForceNm;
		C.Gain = Settings.Gain;
		C.MinForce = Settings.MinForce;

		// parked: heavy damping and tyre scrub. Moving: the tyres resist being turned quickly, more the faster the car
		// goes (they have to sweep sideways across the road), and the steering rack keeps its own friction: together
		// they give the centre its weight
		const float SpeedShare = std::clamp(Speed / 35.0f, 0.0f, 1.0f);
		const float Fade = SpeedFade;
		C.Damper = Settings.Damper * (1.0f + ((0.5f + 0.7f * SpeedShare) - 1.0f) * Fade);
		C.Friction = Settings.Friction + (Settings.RackFriction - Settings.Friction) * Fade;
		C.LockupGain = Settings.LockupGain;
		C.ABSGain = Settings.ABSGain;
		C.ScrubGain = Settings.ScrubGain;
		C.RoadGain = Settings.RoadGain;
		C.bEnabled = true;
		return C;
	}
}
