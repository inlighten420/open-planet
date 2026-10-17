// Geodrive tile tool - AGPL-3.0 (see LICENSE)
//
// Writes OSM's standard compact file format (.osm.pbf) - only for comparing sizes against our tiles.
// Spec: https://wiki.openstreetmap.org/wiki/PBF_Format

#pragma once

#include <cstdint>
#include <vector>

#include "OsmTypes.h"

namespace geotile
{
	/** Nodes as DenseNodes, ways with tags, zlib level 9, 8000 items per block (like osmium and osmconvert) */
	std::vector<uint8_t> WriteOsmPbf(const OsmSet& Set);
}
