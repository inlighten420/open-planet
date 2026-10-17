// Geodrive Core - AGPL-3.0 (see LICENSE); the tyre equations follow Project Chrono's TMeasy (BSD-3, see Tire.cpp)
//
// The tyre: forces in the contact patch from load, slip and surface grip.

#pragma once

#include "CarTypes.h"

namespace geocar
{
	/** Per-tyre state carried between steps */
	struct TireState
	{
		float SlipY = 0.0f;					// relaxed lateral slip
		float StickX = 0.0f, StickY = 0.0f;	// contact patch displacement for the low-speed "sticky" model (m)
	};

	/** Tyre output for one step */
	struct TireForces
	{
		float ForceX = 0.0f;		// longitudinal and lateral force in the contact plane (N)
		float ForceY = 0.0f;
		float DForceXDOmega = 0.0f;	// d(ForceX)/d(wheel angular velocity), for the implicit wheel update (N s/rad)
		float Trail = 0.0f;			// pneumatic trail (m)
		float SlipX = 0.0f;			// the slip ratio and lateral slip used
		float SlipY = 0.0f;
		float SpeedBlend = 0.0f;	// 0 = low-speed sticky model, 1 = full tyre model
	};

	/**
	 *  Tyre forces. Load: vertical load (N). Vx/Vy: contact patch velocity along the wheel's forward/right axes (m/s).
	 *  Omega: wheel spin (rad/s). GripScale scales friction (surface grip).
	 */
	TireForces ComputeTire(const TireSetup& Setup, float Load, float Vx, float Vy, float Omega, float Radius,
		float DeltaTime, float GripScale, TireState& State);
}
