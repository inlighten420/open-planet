// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The ready-made areas to drive, each with named places to start at or teleport to (the car goes to the road nearest
// the place). Degrees.

#pragma once

#include <string>
#include <vector>

namespace geoplaces
{
	struct Place
	{
		std::string Name;
		double Lat = 0.0;
		double Lon = 0.0;
	};

	struct Area
	{
		std::string Id;		// for geo.Area and saved spots
		std::string Name;
		std::string Description;
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
		std::vector<Place> Places;

		bool Contains(double Lat, double Lon) const { return Lat >= South && Lat <= North && Lon >= West && Lon <= East; }
	};

	inline const std::vector<Area>& AllAreas()
	{
		static const std::vector<Area> Areas = {
			{ "gardenroute", "Garden Route", "The Crags to George along the N2: Plett, Knysna, Sedgefield, Wilderness",
				-34.12, 22.33, -33.90, 23.50, {
					{ "Plettenberg Bay", -34.0527, 23.3716 },
					{ "The Crags", -33.9530, 23.4650 },
					{ "Harkerville", -34.0480, 23.2400 },
					{ "Knysna", -34.0363, 23.0471 },
					{ "Sedgefield", -34.0186, 22.8000 },
					{ "Wilderness", -33.9935, 22.5780 },
					{ "George", -33.9630, 22.4617 } } },
			{ "plett", "Plettenberg Bay", "Plett, Harkerville to The Crags",
				-34.12, 23.20, -33.93, 23.50, {
					{ "Plettenberg Bay", -34.0527, 23.3716 },
					{ "Keurboomstrand", -34.0020, 23.4400 },
					{ "The Crags", -33.9530, 23.4650 },
					{ "Harkerville", -34.0480, 23.2400 } } },
			{ "fishhoek", "Fish Hoek & Chapman's Peak", "Fish Hoek, Noordhoek, Kommetjie side and Chapman's Peak Drive",
				-34.165, 18.33, -34.06, 18.47, {
					{ "Fish Hoek", -34.1364, 18.4316 },
					{ "Glencairn", -34.1600, 18.4280 },
					{ "Sun Valley", -34.1200, 18.3950 },
					{ "Noordhoek", -34.1050, 18.3800 },
					{ "Chapman's Peak Drive", -34.0870, 18.3600 },
					{ "Kommetjie", -34.1400, 18.3360 } } },
			{ "london", "Central London", "Westminster to Tower Bridge, King's Cross to Elephant & Castle",
				51.485, -0.185, 51.535, -0.065, {
					{ "Westminster Bridge", 51.5008, -0.1218 },
					{ "Trafalgar Square", 51.5076, -0.1278 },
					{ "Buckingham Palace", 51.5010, -0.1416 },
					{ "Hyde Park Corner", 51.5026, -0.1522 },
					{ "Tower Bridge", 51.5055, -0.0754 },
					{ "King's Cross", 51.5308, -0.1238 },
					{ "Elephant & Castle", 51.4946, -0.1005 } } },
		};
		return Areas;
	}
}
