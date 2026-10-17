// Geodrive Godot client - GPL (see Core/README.md)

#pragma once

#include "GeoGround/Vegetation.h"

#include <string>

/** Fills a land cover raster over Cover's box from ESA WorldCover 2021 (10 m, worldwide, CC BY 4.0): only the parts the
 *  box needs, by HTTP byte ranges of its 3 x 3 degree files' 1024-pixel tiles, kept in CacheDir/landcover. Blocking
 *  (a piece's worker). False, Cover left empty, if none could be had (offline and not kept, or open sea). */
bool FetchWorldCover(geoground::LandCover& Cover, const std::string& CacheDir, std::string& OutLog, int32_t Level = 0);
// (Level: the files' own coarser copies, each half the last across: 4 is ~150 m a pixel, a whole file in 9 tiles; for
// the land far off)
