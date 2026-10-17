// Geodrive tile tool - GPL (see Core/README.md)
//
// Reading the game's cached Overpass responses, and whole-file helpers. Tool only; the game has its own JSON reader.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "OsmTypes.h"

namespace geotile
{
	/** Reads an Overpass JSON response (the game's osm2_*.json cache file). Throws on bad input. */
	OsmSet LoadOverpassJson(const std::string& Path, bool bKeepAllTags);

	/** Whole file into memory, and back. Throw on failure. */
	std::vector<uint8_t> ReadFile(const std::string& Path);
	void WriteFile(const std::string& Path, const std::vector<uint8_t>& Data);
}
