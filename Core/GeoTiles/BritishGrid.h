// Geodrive Core - GPL (see Core/README.md)
//
// The British National Grid (OSGB36 / EPSG:27700): what the Environment Agency's LiDAR heights are gridded in.
// WGS84 latitude/longitude (GPS, OpenStreetMap) to grid metres: a 7-parameter Helmert shift from WGS84 to the Airy
// 1830 ellipsoid, then Ordnance Survey's Transverse Mercator. Good to about 2-5 m across Great Britain; Ordnance
// Survey's OSTN15 grid shift would give centimetres.

#pragma once

namespace geotile
{
	/** WGS84 latitude/longitude (degrees) to British National Grid easting/northing (metres) */
	void ToBritishGrid(double Lat, double Lon, double& OutEasting, double& OutNorthing);

	/** Whether a point is roughly in Great Britain's grid (outside it the grid means little) */
	bool NearBritishGrid(double Lat, double Lon);

	/** WGS84 latitude/longitude to UTM zone 32 north (ETRS89 / EPSG:25832, as WGS84 to a few cm): what Germany's states
	 *  grid their 1 m heights in (Rheinland-Pfalz's DGM1: the Nuerburgring's) */
	void ToUtm32(double Lat, double Lon, double& OutEasting, double& OutNorthing);

	/** The grids fine (1 m) heights come in, each by where it is: Britain's, or UTM 32 across Germany */
	enum class FineGrid : int { None, British, Utm32 };
	FineGrid FineGridAt(double Lat, double Lon);
	void ToFineGrid(FineGrid Grid, double Lat, double Lon, double& OutEasting, double& OutNorthing);
}
