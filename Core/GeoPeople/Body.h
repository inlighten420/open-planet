// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// A person: their measures from their height, and how they walk. Everything is scaled from a 1.75 m person by human
// proportions: the eyes at 0.936 of the height standing and 0.45 above a seat; steps 0.41 of it long; speeds by the
// square root of it (legs swing as pendulums: longer ones swing slower but carry further). The speeds are real
// people's: walking 5 km/h, running 11.9, sprinting 24.5 at 1.75 m.
//
// Engine-free: the game keeps the person in its world, turns them with the mouse, and moves them with its own
// collision (ground, roads, buildings); Walk only says how their velocity changes. cm and s, x north, y east, z up (as
// the rest of Core).

#pragma once

namespace geopeople
{
	struct Vec3
	{
		float X = 0.0f, Y = 0.0f, Z = 0.0f;
	};

	/** A person's measures (cm, cm/s) from their height */
	struct BodyMeasures
	{
		float Height = 175.0f;
		float EyeStanding = 0.0f;	// the eyes over the feet
		float EyeSeated = 0.0f;		// the eyes over the seat (driving)
		float Radius = 0.0f;		// half the shoulders' width (their collision's)
		float StepLength = 0.0f;	// one step (the head bobs once a step)
		float StepUp = 0.0f;		// the highest kerb or step they walk up without jumping
		float WalkSpeed = 0.0f;
		float RunSpeed = 0.0f;		// a run they can keep up
		float SprintSpeed = 0.0f;	// flat out
		float JumpSpeed = 0.0f;		// up, leaving the ground (a ~40 cm jump at 1.75 m)
	};

	/** Measures for a height (cm, kept to 120-230) */
	BodyMeasures MeasureBody(float HeightCm);

	/** What the player asks for: Forward and Right -1 to 1 (relative to where they face), running, jumping */
	struct WalkInput
	{
		float Forward = 0.0f;
		float Right = 0.0f;
		bool bRun = false;
		bool bSprint = false;
		bool bJump = false;
	};

	/** The person's next velocity (cm/s, the world's frame) after Seconds: facing Yaw (radians from +x towards +y),
	 *  standing on the ground or not. On the ground they speed up and slow down quickly (a few steps), in the air they
	 *  keep what they had (a little steering) and fall */
	Vec3 Walk(const BodyMeasures& Body, const WalkInput& Input, float Yaw, bool bOnGround, const Vec3& Velocity, float Seconds);
}
