// Geodrive for Godot - AGPL-3.0 (see LICENSE)
//
// GeodriveWorldIndex: what the world map shows, as the Unreal game reads it: every region's world index next to its
// map archive (Cache/regions/*.world.json, made by geotile world from a country .osm.pbf): its towns (biggest first,
// the same place twice kept once) and its main roads. The map draws the roads through here (only what the screen can
// show) and asks for the places in view or matching a search.

#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <atomic>
#include <string>
#include <vector>

namespace godot
{
	class GeodriveWorldIndex : public RefCounted
	{
		GDCLASS(GeodriveWorldIndex, RefCounted)

	public:
		/** Reads every *.world.json in the folder (may run on a worker; is_loaded() says when it's done) */
		void load(const String& Folder);
		bool is_loaded() const { return bLoaded; }
		bool is_empty() const { return Places.empty() && Roads.empty(); }
		String get_summary() const;

		/** Everything on it: south, west, north, east (degrees) */
		PackedFloat64Array get_bounds() const;

		/** The places (biggest first) in a box no smaller than MaxRank (0 city, 1 town, 2 village, 3 suburb, 4 any),
		 *  at most Limit of them; or, with a search, the places whose name has it */
		PackedInt32Array places_in(double South, double West, double North, double East, int MaxRank, int Limit) const;
		PackedInt32Array search(const String& Text, int Limit) const;

		/** One place: its name, kind, rank, lat, lon, population */
		String place_name(int Index) const;
		String place_kind(int Index) const;
		int place_rank(int Index) const;
		double place_lat(int Index) const;
		double place_lon(int Index) const;
		int64_t place_population(int Index) const;

		/** The main roads in view onto a canvas item, at this view (the map's middle, pixels per degree, size): the
		 *  bigger ones from far away, all of them close up; roads under 2 pixels skipped, points closer than 1.5 pixels
		 *  dropped */
		void draw_roads(const RID& CanvasItem, double CentreLat, double CentreLon, double PixelsPerDegree, const Vector2& Size) const;
		/** A class's main roads (0 the biggest, 3 the smallest) as line segments, two points each, in degrees: x the
		 *  longitude, y minus the latitude (made as it loads: the map makes one mesh of them and only moves it) */
		PackedVector2Array road_lines(int Class) const;

	protected:
		static void _bind_methods();

	private:
		struct Place
		{
			std::string Name, Kind;
			double Lat = 0.0, Lon = 0.0;
			int64_t Population = 0;
			int Rank = 4;
		};
		struct Road
		{
			int Class = 3;
			std::vector<double> Points;	// lat, lon, lat, lon...
			double South = 1e9, West = 1e9, North = -1e9, East = -1e9;
		};
		std::vector<Place> Places;
		std::vector<Road> Roads;
		PackedVector2Array Lines[4];
		double South = 0.0, West = 0.0, North = 0.0, East = 0.0;
		int Files = 0;
		double Seconds = 0.0;
		std::atomic<bool> bLoaded{ false };
	};
}
