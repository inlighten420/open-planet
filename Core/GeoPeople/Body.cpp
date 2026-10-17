// Geodrive Core - GPL (see Core/README.md)

#include "Body.h"

#include <algorithm>
#include <cmath>

namespace geopeople
{
	namespace
	{
		constexpr float Gravity = 981.0f;	// cm/s/s
		constexpr float Reference = 175.0f;	// cm: the person the numbers are for
	}

	BodyMeasures MeasureBody(float HeightCm)
	{
		BodyMeasures B;
		B.Height = std::clamp(HeightCm, 120.0f, 230.0f);
		const float Scale = B.Height / Reference;
		const float Swing = std::sqrt(Scale);	// (speeds go with the square root of the legs' length)
		B.EyeStanding = 0.936f * B.Height;
		B.EyeSeated = 0.45f * B.Height;
		B.Radius = 25.0f * Scale;
		B.StepLength = 0.41f * B.Height;
		B.StepUp = 0.2f * B.Height;
		// (real people's: gait studies put a comfortable walk at 1.3-1.4 m/s, ordinary adults run at 10-13 km/h, and a
		// fit man's flat-out sprint is about 25 km/h)
		B.WalkSpeed = 140.0f * Swing;	// 5 km/h
		B.RunSpeed = 330.0f * Swing;	// 11.9 km/h
		B.SprintSpeed = 680.0f * Swing;	// 24.5 km/h
		B.JumpSpeed = std::sqrt(2.0f * Gravity * 40.0f * Scale);
		return B;
	}

	Vec3 Walk(const BodyMeasures& Body, const WalkInput& Input, float Yaw, bool bOnGround, const Vec3& Velocity, float Seconds)
	{
		// where they want to go, in the world's frame, no faster than walking (or running)
		float Forward = std::clamp(Input.Forward, -1.0f, 1.0f), Right = std::clamp(Input.Right, -1.0f, 1.0f);
		const float Length = std::sqrt(Forward * Forward + Right * Right);
		if (Length > 1.0f)
		{
			Forward /= Length;
			Right /= Length;
		}
		const float Speed = Forward > 0.0f && Input.bSprint ? Body.SprintSpeed
			: Forward > 0.0f && Input.bRun ? Body.RunSpeed
			: Body.WalkSpeed * (Forward < 0.0f ? 0.7f : 1.0f);	// (backwards slower)
		const float C = std::cos(Yaw), S = std::sin(Yaw);
		// (forward is along Yaw; right a quarter turn further towards +y: x is north and y east, so from north, east)
		const float WantX = (Forward * C - Right * S) * Speed;
		const float WantY = (Forward * S + Right * C) * Speed;

		Vec3 Out = Velocity;
		// on the ground: reach it in about a third of a second (a step or two); in the air: a little steering only
		const float Rate = bOnGround ? 8.0f : 0.6f;
		const float Blend = 1.0f - std::exp(-Rate * Seconds);
		Out.X += (WantX - Out.X) * Blend;
		Out.Y += (WantY - Out.Y) * Blend;
		if (bOnGround)
		{
			Out.Z = Input.bJump ? Body.JumpSpeed : std::min(Out.Z, 0.0f);
		}
		else
		{
			Out.Z -= Gravity * Seconds;
		}
		return Out;
	}
}
