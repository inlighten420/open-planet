// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Local tangent-plane projection around an origin, using WGS84 radii of curvature at the origin. Error is ~0.2 m at
// 1 km from the origin, plenty for a driving area. World axes as the game uses them: X north, Y east, in cm.

#pragma once

#include "RoadGraph.h"

#include <algorithm>
#include <cmath>

namespace georoads
{
	struct Projection
	{
		Projection() = default;
		Projection(double InOriginLat, double InOriginLon)
			: OriginLat(InOriginLat), OriginLon(InOriginLon)
		{
			constexpr double SemiMajorAxis = 6378137.0;
			constexpr double EccentricitySquared = 0.00669437999014;
			const double SinLat = std::sin(Radians(OriginLat));
			const double Denominator = 1.0 - EccentricitySquared * SinLat * SinLat;
			MetresPerRadLat = SemiMajorAxis * (1.0 - EccentricitySquared) / std::pow(Denominator, 1.5);
			MetresPerRadLon = SemiMajorAxis / std::sqrt(Denominator) * std::cos(Radians(OriginLat));
		}

		/** Latitude/longitude (degrees) to world position (cm) */
		Vec2 ToWorld(double Lat, double Lon) const
		{
			return Vec2(Radians(Lat - OriginLat) * MetresPerRadLat * 100.0, Radians(Lon - OriginLon) * MetresPerRadLon * 100.0) - Offset;
		}

		/** World position (cm) back to latitude/longitude (degrees) */
		void ToLatLon(const Vec2& World, double& OutLat, double& OutLon) const
		{
			OutLat = OriginLat + Degrees((World.X + Offset.X) / 100.0 / MetresPerRadLat);
			OutLon = OriginLon + Degrees((World.Y + Offset.Y) / 100.0 / std::max(MetresPerRadLon, 1.0));
		}

		/** The same projection with positions measured from another point of it (cm): a piece of the world far from
		 *  the drive's origin is built round its own, so its single-precision mesh corners stay exact */
		Projection Shifted(const Vec2& By) const
		{
			Projection Out = *this;
			Out.Offset = Offset + By;
			return Out;
		}

		double OriginLat = 0.0;
		double OriginLon = 0.0;
		Vec2 Offset;	// positions are measured from this point of the projection (cm)

	private:

		static double Radians(double D) { return D * (3.1415926535897932 / 180.0); }
		static double Degrees(double R) { return R * (180.0 / 3.1415926535897932); }

		/** Metres per radian of latitude (meridian radius) and of longitude (prime vertical radius x cos lat) */
		double MetresPerRadLat = 0.0;
		double MetresPerRadLon = 0.0;
	};
}
