// Geodrive Core - GPL (see Core/README.md)

#include "BritishGrid.h"

#include <cmath>

namespace geotile
{
	namespace
	{
		constexpr double Pi = 3.14159265358979323846;
		constexpr double Rad = Pi / 180.0;
		constexpr double ArcSecond = Rad / 3600.0;

		struct Ellipsoid
		{
			double A, B;
		};
		constexpr Ellipsoid Wgs84 = { 6378137.0, 6356752.314245 };
		constexpr Ellipsoid Airy1830 = { 6377563.396, 6356256.909 };

		void ToCartesian(const Ellipsoid& E, double Lat, double Lon, double& X, double& Y, double& Z)
		{
			const double E2 = 1.0 - (E.B * E.B) / (E.A * E.A);
			const double Nu = E.A / std::sqrt(1.0 - E2 * std::sin(Lat) * std::sin(Lat));
			X = Nu * std::cos(Lat) * std::cos(Lon);
			Y = Nu * std::cos(Lat) * std::sin(Lon);
			Z = Nu * (1.0 - E2) * std::sin(Lat);
		}

		void FromCartesian(const Ellipsoid& E, double X, double Y, double Z, double& Lat, double& Lon)
		{
			const double E2 = 1.0 - (E.B * E.B) / (E.A * E.A);
			const double P = std::sqrt(X * X + Y * Y);
			Lat = std::atan2(Z, P * (1.0 - E2));
			for (int i = 0; i < 10; ++i)
			{
				const double Nu = E.A / std::sqrt(1.0 - E2 * std::sin(Lat) * std::sin(Lat));
				Lat = std::atan2(Z + E2 * Nu * std::sin(Lat), P);
			}
			Lon = std::atan2(Y, X);
		}
	}

	void ToBritishGrid(double LatDegrees, double LonDegrees, double& OutEasting, double& OutNorthing)
	{
		// WGS84 to OSGB36: Ordnance Survey's Helmert parameters (translation m, scale ppm, rotation arc seconds)
		double X, Y, Z;
		ToCartesian(Wgs84, LatDegrees * Rad, LonDegrees * Rad, X, Y, Z);
		const double Tx = -446.448, Ty = 125.157, Tz = -542.060;
		const double S = 20.4894e-6;
		const double Rx = -0.1502 * ArcSecond, Ry = -0.2470 * ArcSecond, Rz = -0.8421 * ArcSecond;
		const double X2 = Tx + (1.0 + S) * X - Rz * Y + Ry * Z;
		const double Y2 = Ty + Rz * X + (1.0 + S) * Y - Rx * Z;
		const double Z2 = Tz - Ry * X + Rx * Y + (1.0 + S) * Z;
		double Lat, Lon;
		FromCartesian(Airy1830, X2, Y2, Z2, Lat, Lon);

		// Ordnance Survey's Transverse Mercator: true origin 49N 2W, false origin 400 km west and 100 km north of it
		const double A = Airy1830.A, B = Airy1830.B, F0 = 0.9996012717;
		const double Lat0 = 49.0 * Rad, Lon0 = -2.0 * Rad, N0 = -100000.0, E0 = 400000.0;
		const double E2 = 1.0 - (B * B) / (A * A);
		const double N = (A - B) / (A + B), N2 = N * N, N3 = N2 * N;
		const double SinLat = std::sin(Lat), CosLat = std::cos(Lat), TanLat = std::tan(Lat);
		const double Nu = A * F0 / std::sqrt(1.0 - E2 * SinLat * SinLat);
		const double Rho = A * F0 * (1.0 - E2) / std::pow(1.0 - E2 * SinLat * SinLat, 1.5);
		const double Eta2 = Nu / Rho - 1.0;
		const double DLat = Lat - Lat0, SLat = Lat + Lat0;
		const double M = B * F0 * ((1.0 + N + 1.25 * N2 + 1.25 * N3) * DLat
			- (3.0 * N + 3.0 * N2 + 2.625 * N3) * std::sin(DLat) * std::cos(SLat)
			+ (1.875 * N2 + 1.875 * N3) * std::sin(2.0 * DLat) * std::cos(2.0 * SLat)
			- (35.0 / 24.0) * N3 * std::sin(3.0 * DLat) * std::cos(3.0 * SLat));
		const double Cos3 = CosLat * CosLat * CosLat, Cos5 = Cos3 * CosLat * CosLat;
		const double Tan2 = TanLat * TanLat, Tan4 = Tan2 * Tan2;
		const double I = M + N0;
		const double II = Nu / 2.0 * SinLat * CosLat;
		const double III = Nu / 24.0 * SinLat * Cos3 * (5.0 - Tan2 + 9.0 * Eta2);
		const double IIIA = Nu / 720.0 * SinLat * Cos5 * (61.0 - 58.0 * Tan2 + Tan4);
		const double IV = Nu * CosLat;
		const double V = Nu / 6.0 * Cos3 * (Nu / Rho - Tan2);
		const double VI = Nu / 120.0 * Cos5 * (5.0 - 18.0 * Tan2 + Tan4 + 14.0 * Eta2 - 58.0 * Tan2 * Eta2);
		const double DLon = Lon - Lon0, DLon2 = DLon * DLon;
		OutNorthing = I + II * DLon2 + III * DLon2 * DLon2 + IIIA * DLon2 * DLon2 * DLon2;
		OutEasting = E0 + IV * DLon + V * DLon2 * DLon + VI * DLon2 * DLon2 * DLon;
	}

	void ToUtm32(double Lat, double Lon, double& OutEasting, double& OutNorthing)
	{
		constexpr double A = 6378137.0, F = 1.0 / 298.257223563, K0 = 0.9996, Pi = 3.14159265358979323846;
		const double E2 = F * (2.0 - F), Ep2 = E2 / (1.0 - E2);
		const double Phi = Lat * Pi / 180.0, Lambda = Lon * Pi / 180.0, Lambda0 = 9.0 * Pi / 180.0;
		const double N = A / std::sqrt(1.0 - E2 * std::sin(Phi) * std::sin(Phi));
		const double T = std::tan(Phi) * std::tan(Phi), C = Ep2 * std::cos(Phi) * std::cos(Phi), Aa = std::cos(Phi) * (Lambda - Lambda0);
		const double M = A * ((1.0 - E2 / 4.0 - 3.0 * E2 * E2 / 64.0 - 5.0 * E2 * E2 * E2 / 256.0) * Phi
			- (3.0 * E2 / 8.0 + 3.0 * E2 * E2 / 32.0 + 45.0 * E2 * E2 * E2 / 1024.0) * std::sin(2.0 * Phi)
			+ (15.0 * E2 * E2 / 256.0 + 45.0 * E2 * E2 * E2 / 1024.0) * std::sin(4.0 * Phi) - (35.0 * E2 * E2 * E2 / 3072.0) * std::sin(6.0 * Phi));
		OutEasting = K0 * N * (Aa + (1.0 - T + C) * std::pow(Aa, 3) / 6.0 + (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * Ep2) * std::pow(Aa, 5) / 120.0) + 500000.0;
		OutNorthing = K0 * (M + N * std::tan(Phi) * (Aa * Aa / 2.0 + (5.0 - T + 9.0 * C + 4.0 * C * C) * std::pow(Aa, 4) / 24.0
			+ (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * Ep2) * std::pow(Aa, 6) / 720.0));
	}

	FineGrid FineGridAt(double Lat, double Lon)
	{
		if (NearBritishGrid(Lat, Lon))
		{
			return FineGrid::British;
		}
		if (Lat > 47.0 && Lat < 55.5 && Lon > 5.5 && Lon < 15.5)
		{
			return FineGrid::Utm32;	// (Germany, and its neighbours in the same zone)
		}
		return FineGrid::None;
	}

	void ToFineGrid(FineGrid Grid, double Lat, double Lon, double& OutEasting, double& OutNorthing)
	{
		if (Grid == FineGrid::Utm32)
		{
			ToUtm32(Lat, Lon, OutEasting, OutNorthing);
		}
		else
		{
			ToBritishGrid(Lat, Lon, OutEasting, OutNorthing);
		}
	}

	bool NearBritishGrid(double Lat, double Lon)
	{
		return Lat > 49.0 && Lat < 61.5 && Lon > -9.0 && Lon < 2.5;
	}
}
