// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The far horizon: plain ground out to the view distance, beyond the pieces of the world that are built (a piece is some
// 8-16 km across, so without this the land ends a few km away). Heights only, from a coarse height raster (Terrarium
// zoom 10: ~120 m a pixel), no roads or buildings: what can be told apart at that distance anyway. Coloured as the
// near ground is (grass, sand along the shore, rock on steep slopes, the seabed), the sea left to the water.

#pragma once

#include "HeightRaster.h"
#include "Mesh.h"
#include "GeoRoads/Projection.h"

#include <vector>

namespace geoground
{
	struct HorizonOptions
	{
		/** Its middle (the drive's frame, cm): the mesh is measured from here */
		Vec2 Centre;
		double Radius = 3000000.0;	// cm
		double Spacing = 25000.0;	// cm between grid points

		/** Squares (the drive's frame) where the built ground is: left out, so the two don't fight */
		std::vector<Box2> Holes;

		/** How far below the raster's heights it sits (cm): where it meets the built ground, that one is on top */
		float Sink = 1500.0f;
	};

	/** The horizon round a point, from a raster that covers it (Projection: the drive's) */
	void BuildHorizon(const HeightRaster& Raster, const georoads::Projection& Projection, const HorizonOptions& Options, MeshData& Out);
}
