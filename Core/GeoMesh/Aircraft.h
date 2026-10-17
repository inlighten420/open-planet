// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The aircraft (GeoFlight/Flight.h) to look at: simple solid shapes, as the plants are, sized from the real ones of
// their class: a fuselage lofted through rings, swept wings and tail as slabs, engines as tubes under the wings (the
// airliners), the cockpit's glass dark. The landing gear is a mesh of its own, for the game to fold away.
//
// cm, the aircraft's middle (its centre of gravity) at 0, x forward, y right, z up (as Core's meshes are). Colours
// are in the corners (linear).

#pragma once

#include "GeoGround/Mesh.h"

namespace geomesh
{
	enum class AircraftKind : int
	{
		FighterJet = 0,
		Airliner = 1,
		RegionalJet = 2,
		Count = 3
	};

	/** The aircraft (bGear false) or its landing gear (true) */
	void BuildAircraft(AircraftKind Kind, bool bGear, geoground::MeshData& Out);
}
