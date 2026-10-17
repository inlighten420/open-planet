// Geodrive Core - GPL (see Core/README.md)
//
// Where plants grow: a piece's vegetation map, 10 m cells over its box, saying how thick the trees, the shrubs and the
// grass are in each, and what the land is. From the satellite's land cover where the host has it (ESA WorldCover, 10 m,
// worldwide: tree cover, shrubland, grassland, cropland, built up, ...), else from the map's forests; the map's forests
// added either way; the roads (with a margin), the buildings and the water cut out; none on cliffs and rock faces. The
// clients place the plants themselves from it (a hash of each plant's spot, on the graphics card), so every plant
// is where it is every time without being stored. The ground's colour carries its trees and shrubs too (Terrain), so
// far-off land looks covered.

#pragma once

#include "Mesh.h"
#include "GeoRoads/RoadNetwork.h"
#include "GeoRoads/RoadGraph.h"

#include <cstdint>
#include <vector>

namespace geoground
{
	class Terrain;

	/** Land cover classes, as ESA WorldCover numbers them (0: not known) */
	enum LandClass : uint8_t
	{
		LandUnknown = 0, LandTrees = 10, LandShrubs = 20, LandGrass = 30, LandCrops = 40, LandBuilt = 50, LandBare = 60,
		LandSnow = 70, LandWater = 80, LandWetland = 90, LandMangrove = 95, LandMoss = 100
	};

	/** A land cover raster in latitude and longitude, as the host fetches it (WorldCover: 1/12000 degree cells) */
	struct LandCover
	{
		/** The box wanted (degrees): set by Core before asking the host */
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;

		/** Filled by the host: the raster's top-left corner, its cell size (degrees), and the classes row by row from the
		 *  north; empty if there is none (offline, or no data there) */
		double Top = 0.0, Left = 0.0, CellDegrees = 1.0 / 12000.0;
		int32_t Width = 0, Height = 0;
		std::vector<uint8_t> Classes;

		bool IsValid() const { return Width > 0 && Height > 0 && Classes.size() == size_t(Width) * size_t(Height); }
		uint8_t At(double Lat, double Lon) const;
	};

	/** A piece's vegetation map: per 10 m cell, four bytes: trees (0-255, how thick), shrubs, grass (how lush), and the
	 *  land class (LandClass). Measured from Min in the piece's own cm (as its Terrain's Bounds) */
	struct VegetationMap
	{
		static constexpr double Cell = 1000.0;	// cm

		Vec2 Min;
		int32_t Width = 0, Height = 0;
		std::vector<uint8_t> Texels;	// RGBA8, rows along x (north), columns along y (east): Texels[(X * Height + Y) * 4]
		/** Per cell (as Texels, one byte): the room from the cell's middle to the nearest road's edge or building wall,
		 *  in quarter metres plus 64 (so -16 m, well inside, to 47.75 m; 255 nothing near). Signed, so it blends between
		 *  cells as the true distance does: a client checks a plant's crown has room where it stands */
		std::vector<uint8_t> Room;
		static constexpr float RoomScale = 4.0f, RoomZero = 64.0f;
		bool bFromSatellite = false;

		bool IsValid() const { return Width > 0 && Height > 0; }
		/** The four values at a point (bilinear between cells; 0-1), and the class of the cell it's in */
		void Sample(const Vec2& Position, float& OutTrees, float& OutShrubs, float& OutGrass) const;
		uint8_t ClassAt(const Vec2& Position) const;
		int64_t Bytes() const { return int64_t(Texels.size() + Room.size()); }
	};

	struct VegetationOptions
	{
		georoads::ParallelForFn ParallelFor;
		georoads::LogFn Log;
	};

	/** Nothing grows along a line (the map's own cm: a runway, a taxiway) within Reach of it, nor (bArea) inside it as a
	 *  ring (an apron) and Reach round it: the clients clear airports this way (they come from their own data) */
	int64_t ClearPlants(VegetationMap& Map, const std::vector<Vec2>& Line, double Reach, bool bArea);	// (how many cells)

	/** Builds the map over the Terrain's box: from Cover (may be invalid: the map's forests only), the Terrain's forests,
	 *  water and slopes, with the roads (Roads, local cm) and the buildings (LandWays' building footprints) cut out */
	void BuildVegetation(const LandCover& Cover, const Terrain& Ground, const georoads::RoadGraph* Roads,
		const georoads::OsmData& LandWays, VegetationMap& Out, const VegetationOptions& Options);
}
