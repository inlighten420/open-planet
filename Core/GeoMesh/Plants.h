// Geodrive Core - GPL (see Core/README.md)
//
// Plants made in code: a few shapes of tree and bush, each a trunk and a crown of solid lumpy blobs (broadleaf, gum,
// bush) or cones (conifer), the same every time for the same seed. Solid, not cards of leaves with holes: far cheaper
// to draw and to shadow, in the same simple look as the ground and the far woods' blanket. Clients draw thousands of
// copies of each (placed by the vegetation map, on the graphics card), so one mesh per kind is all that's made.
//
// In cm, the plant's foot at 0, z up (as the rest of Core). Each corner's colour says what it is, for a client's
// shader: R 0 bark, 1 leaves; G how far up the plant (0-1: how much it sways in the wind); B a number of its own (0-1);
// A 1. The crowns' normals lean out from the crown's middle, so a crown shades as one round thing.

#pragma once

#include "GeoGround/Mesh.h"

#include <cstdint>

namespace geomesh
{
	enum class PlantKind : uint8_t
	{
		Broadleaf = 0,	// oak, plane, beech: a round crown on a short trunk
		Conifer = 1,	// pine, cypress: a cone of tiers
		Eucalyptus = 2,	// a tall pale trunk, a big loose crown from 7 m up (the Cape's plantations, gum trees)
		Bush = 3,	// a low dome of leaves (fynbos, scrub, hedges)
		Count = 4
	};

	/** One plant: Detail 1 full (near), 0 light (the middle distance: fewer, coarser blobs, no branches), -1 far (one
	 *  blob or two cones on a three-sided trunk) */
	void BuildPlant(PlantKind Kind, uint32_t Seed, int32_t Detail, geoground::MeshData& Out);
}
