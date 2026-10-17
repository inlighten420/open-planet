// Geodrive Core - GPL (see Core/README.md)
//
// Cars as parts (GeoBody): what each is made of and where, so its mass, the share of its weight on each axle, how
// high that weight sits and how hard it is to turn come from the car itself, not numbers typed in.

#pragma once

#include "GeoBody/Body.h"

#include <vector>

namespace geocar
{
	/** The Bakkie 3.0 TDi (a 2007 Isuzu KB300 TDi LX double cab 4x2) part by part, with its driver and a full tank.
	 *  Its frame: on the ground midway between the axles, x forward, y left, z up (m) */
	std::vector<geobody::Part> BakkieParts();
	constexpr double BakkieWheelbase = 3.05;	// m (the axles at +-half this)

	/** The RS 4.0 (a 992 GT3 RS), the Hatchback 1.6 (a Swift Sport) and the Formula car (today's grand prix car), each with
	 *  its driver and full of fuel, in the same frame */
	std::vector<geobody::Part> RSParts();
	constexpr double RSWheelbase = 2.457;
	std::vector<geobody::Part> HatchParts();
	constexpr double HatchWheelbase = 2.43;
	std::vector<geobody::Part> FormulaParts();
	constexpr double FormulaWheelbase = 3.6;
}
