// Geodrive Core - GPL (see Core/README.md)
//
// The elevation of an area: a raster stitched from Terrarium tiles (Web Mercator, 256 px; AWS Terrain Tiles: SRTM on
// land, ETOPO1 at sea), with better base heights where a lat/lon heights archive has them (Copernicus GLO-30: the
// lower of the two per pixel) and fine heights on top where a British-grid heights archive has them (England's
// LiDAR), blended in at their edge. Metres above sea level. The host fetches and decodes the tiles and maps the
// archives; this is the data and how it is read.

#pragma once

#include "BritishGrid.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geoground
{
	/** A heights archive in memory (mapped or read), and its file name for the log */
	struct Archive
	{
		const uint8_t* Data = nullptr;
		size_t Size = 0;
		std::string Name;
	};

	struct HeightRaster
	{
		static constexpr int32_t TileSize = 256;
		int32_t Zoom = 12;

		/** Tile range covered */
		int32_t MinTileX = 0, MinTileY = 0, TilesX = 0, TilesY = 0;

		/** Heights row-major, (TilesX*256) x (TilesY*256) */
		std::vector<float> Heights;

		bool IsValid() const { return !Heights.empty(); }

		/** The tiles covering a box, the heights all 0 until SetTile fills them */
		void SetBox(double South, double West, double North, double East, int32_t InZoom = 12);

		/** One decoded Terrarium tile (Index = row * TilesX + column), Channels bytes a pixel (3 RGB, 4 RGBA) */
		void SetTile(int32_t Index, const uint8_t* Pixels, int32_t Channels);

		/** Bilinear height at a latitude/longitude (metres); 0 outside the raster. The fine heights where there are any. */
		float Sample(double Lat, double Lon) const;

		/**
		 *  Finer heights where an archive has them, in the British National Grid at the archive's cell size, row 0 at the
		 *  north edge; NaN where there are none, and there the ~25 m heights are used. FineEdge is how many cells each one
		 *  is from the nearest without fine heights (up to FadeCells): the two blend over that, no step.
		 */
		std::vector<float> Fine;
		std::vector<uint8_t> FineEdge;
		double FineWestE = 0.0, FineNorthN = 0.0, FineCell = 0.0;
		geotile::FineGrid FineGridKind = geotile::FineGrid::British;	// (the grid its squares are in: Britain's, or UTM 32)
		int32_t FineWidth = 0, FineHeight = 0;
		static constexpr int32_t FadeCells = 50;
		bool HasFine() const { return !Fine.empty(); }

		/** The fine heights alone (the truth test's "real" surface): false where there are none */
		bool SampleFine(double Lat, double Lon, float& OutMetres) const;

		/** While set, Sample ignores the fine heights (the truth test solves the roads on SRTM alone, as without LiDAR) */
		mutable bool bIgnoreFine = false;

		static double LonToTileX(double Lon, int32_t Z);
		static double LatToTileY(double Lat, int32_t Z);

	private:
		float SampleCoarse(double Lat, double Lon) const;
	};

	/**
	 *  Better base heights where a lat/lon archive (zoom 14 ids: Copernicus from geotile copernicus) has them: each pixel
	 *  takes the lower of its own height and the archive's at its middle (both are surface models that read too high,
	 *  each in its own places). Returns how many pixels it changed.
	 */
	int64_t ApplyBase(HeightRaster& Raster, const std::vector<Archive>& Archives);

	/**
	 *  The fine heights for the box out of every British-grid archive that has any. Returns how many 1 km squares it
	 *  found (0: none, or not near Britain); OutLog says what was loaded.
	 */
	int32_t LoadFine(HeightRaster& Raster, const std::vector<Archive>& Archives, double South, double West, double North, double East, std::string& OutLog);
}
