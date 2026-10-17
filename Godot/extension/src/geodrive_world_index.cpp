// Geodrive for Godot - GPL (see Core/README.md)

#include "geodrive_world_index.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <algorithm>
#include <cmath>
#include <map>

using namespace godot;

void GeodriveWorldIndex::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("load", "folder"), &GeodriveWorldIndex::load);
	ClassDB::bind_method(D_METHOD("is_loaded"), &GeodriveWorldIndex::is_loaded);
	ClassDB::bind_method(D_METHOD("is_empty"), &GeodriveWorldIndex::is_empty);
	ClassDB::bind_method(D_METHOD("get_summary"), &GeodriveWorldIndex::get_summary);
	ClassDB::bind_method(D_METHOD("get_bounds"), &GeodriveWorldIndex::get_bounds);
	ClassDB::bind_method(D_METHOD("places_in", "south", "west", "north", "east", "max_rank", "limit"), &GeodriveWorldIndex::places_in);
	ClassDB::bind_method(D_METHOD("search", "text", "limit"), &GeodriveWorldIndex::search);
	ClassDB::bind_method(D_METHOD("place_name", "index"), &GeodriveWorldIndex::place_name);
	ClassDB::bind_method(D_METHOD("place_kind", "index"), &GeodriveWorldIndex::place_kind);
	ClassDB::bind_method(D_METHOD("place_rank", "index"), &GeodriveWorldIndex::place_rank);
	ClassDB::bind_method(D_METHOD("place_lat", "index"), &GeodriveWorldIndex::place_lat);
	ClassDB::bind_method(D_METHOD("place_lon", "index"), &GeodriveWorldIndex::place_lon);
	ClassDB::bind_method(D_METHOD("place_population", "index"), &GeodriveWorldIndex::place_population);
	ClassDB::bind_method(D_METHOD("draw_roads", "canvas_item", "centre_lat", "centre_lon", "pixels_per_degree", "size"), &GeodriveWorldIndex::draw_roads);
	ClassDB::bind_method(D_METHOD("road_lines", "class"), &GeodriveWorldIndex::road_lines);
}

void GeodriveWorldIndex::load(const String& Folder)
{
	const double Start = Time::get_singleton()->get_ticks_msec() / 1000.0;
	Places.clear();
	Roads.clear();
	for (PackedVector2Array& L : Lines)
	{
		L.clear();
	}
	Files = 0;
	double PlaceS = 1e9, PlaceW = 1e9, PlaceN = -1e9, PlaceE = -1e9;	// (only used without roads: far-off islands would stretch the map)
	South = West = 1e9;
	North = East = -1e9;
	static const std::map<std::string, int> Ranks = { { "city", 0 }, { "town", 1 }, { "village", 2 }, { "suburb", 3 } };
	for (const String& Name : DirAccess::get_files_at(Folder))
	{
		if (!Name.ends_with(".world.json"))
		{
			continue;
		}
		const Variant Parsed = JSON::parse_string(FileAccess::get_file_as_string(Folder.path_join(Name)));
		if (Parsed.get_type() != Variant::DICTIONARY)
		{
			continue;
		}
		++Files;
		const Dictionary Root = Parsed;
		const Array PlaceList = Root.get("places", Array());
		for (int64_t i = 0; i < PlaceList.size(); ++i)
		{
			const Array Fields = PlaceList[i];
			if (Fields.size() < 5)
			{
				continue;
			}
			Place P;
			P.Name = String(Fields[0]).utf8().get_data();
			P.Kind = String(Fields[1]).utf8().get_data();
			P.Lat = double(Fields[2]);
			P.Lon = double(Fields[3]);
			P.Population = int64_t(double(Fields[4]));
			const auto Found = Ranks.find(P.Kind);
			P.Rank = Found != Ranks.end() ? Found->second : 4;
			PlaceS = std::min(PlaceS, P.Lat);
			PlaceN = std::max(PlaceN, P.Lat);
			PlaceW = std::min(PlaceW, P.Lon);
			PlaceE = std::max(PlaceE, P.Lon);
			Places.push_back(std::move(P));
		}
		const Array RoadList = Root.get("roads", Array());
		for (int64_t i = 0; i < RoadList.size(); ++i)
		{
			const Dictionary Object = RoadList[i];
			const Array Points = Object.get("p", Array());
			if (Points.size() < 4)
			{
				continue;
			}
			Road R;
			R.Class = std::clamp(int(Object.get("c", 3)), 0, 3);
			R.Points.reserve(size_t(Points.size()));
			for (int64_t j = 0; j + 1 < Points.size(); j += 2)
			{
				const double Lat = Points[j], Lon = Points[j + 1];
				R.Points.push_back(Lat);
				R.Points.push_back(Lon);
				R.South = std::min(R.South, Lat);
				R.North = std::max(R.North, Lat);
				R.West = std::min(R.West, Lon);
				R.East = std::max(R.East, Lon);
			}
			South = std::min(South, R.South);
			North = std::max(North, R.North);
			West = std::min(West, R.West);
			East = std::max(East, R.East);
			Roads.push_back(std::move(R));
		}
	}
	// (each class's roads as segments for the map's meshes, made here on the loading worker)
	for (const Road& R : Roads)
	{
		PackedVector2Array& L = Lines[R.Class];
		for (size_t j = 2; j + 1 < R.Points.size(); j += 2)
		{
			L.push_back(Vector2(float(R.Points[j - 1]), float(-R.Points[j - 2])));
			L.push_back(Vector2(float(R.Points[j + 1]), float(-R.Points[j])));
		}
	}
	if (Roads.empty())
	{
		South = PlaceS;
		West = PlaceW;
		North = PlaceN;
		East = PlaceE;
	}

	// biggest first: cities, then towns by population; OSM sometimes has the same place twice (a node for the town and
	// another for its centre): the bigger one is kept
	std::stable_sort(Places.begin(), Places.end(), [](const Place& A, const Place& B)
	{
		return A.Rank != B.Rank ? A.Rank < B.Rank : A.Population > B.Population;
	});
	std::vector<Place> Unique;
	Unique.reserve(Places.size());
	std::map<std::string, std::vector<size_t>> ByName;
	for (Place& P : Places)
	{
		std::vector<size_t>& Same = ByName[P.Name];
		const bool bSeen = std::any_of(Same.begin(), Same.end(), [&](size_t i)
		{
			return std::abs(Unique[i].Lat - P.Lat) < 0.05 && std::abs(Unique[i].Lon - P.Lon) < 0.05;
		});
		if (bSeen)
		{
			continue;
		}
		Same.push_back(Unique.size());
		Unique.push_back(std::move(P));
	}
	Places = std::move(Unique);
	Seconds = Time::get_singleton()->get_ticks_msec() / 1000.0 - Start;
	bLoaded = true;
}

String GeodriveWorldIndex::get_summary() const
{
	return vformat("World map: %d places and %d main roads from %d region(s), %.2f s", int64_t(Places.size()), int64_t(Roads.size()), Files, Seconds);
}

PackedFloat64Array GeodriveWorldIndex::get_bounds() const
{
	PackedFloat64Array Out;
	Out.push_back(South);
	Out.push_back(West);
	Out.push_back(North);
	Out.push_back(East);
	return Out;
}

PackedInt32Array GeodriveWorldIndex::places_in(double S, double W, double N, double E, int MaxRank, int Limit) const
{
	PackedInt32Array Out;
	for (size_t i = 0; i < Places.size() && Out.size() < Limit; ++i)
	{
		const Place& P = Places[i];
		if (P.Rank <= MaxRank && P.Lat >= S && P.Lat <= N && P.Lon >= W && P.Lon <= E)
		{
			Out.push_back(int32_t(i));
		}
	}
	return Out;
}

PackedInt32Array GeodriveWorldIndex::search(const String& Text, int Limit) const
{
	PackedInt32Array Out;
	const String Wanted = Text.to_lower();
	for (size_t i = 0; i < Places.size() && Out.size() < Limit; ++i)
	{
		if (String::utf8(Places[i].Name.c_str()).to_lower().contains(Wanted))
		{
			Out.push_back(int32_t(i));
		}
	}
	return Out;
}

static bool Valid(size_t Count, int Index) { return Index >= 0 && size_t(Index) < Count; }

String GeodriveWorldIndex::place_name(int I) const { return Valid(Places.size(), I) ? String::utf8(Places[I].Name.c_str()) : String(); }
String GeodriveWorldIndex::place_kind(int I) const { return Valid(Places.size(), I) ? String::utf8(Places[I].Kind.c_str()) : String(); }
int GeodriveWorldIndex::place_rank(int I) const { return Valid(Places.size(), I) ? Places[I].Rank : 4; }
double GeodriveWorldIndex::place_lat(int I) const { return Valid(Places.size(), I) ? Places[I].Lat : 0.0; }
double GeodriveWorldIndex::place_lon(int I) const { return Valid(Places.size(), I) ? Places[I].Lon : 0.0; }
int64_t GeodriveWorldIndex::place_population(int I) const { return Valid(Places.size(), I) ? Places[I].Population : 0; }

PackedVector2Array GeodriveWorldIndex::road_lines(int Class) const
{
	return Class >= 0 && Class < 4 ? Lines[Class] : PackedVector2Array();
}

void GeodriveWorldIndex::draw_roads(const RID& Item, double CentreLat, double CentreLon, double Ppd, const Vector2& Size) const
{
	RenderingServer* RS = RenderingServer::get_singleton();
	const double Squeeze = std::max(std::cos(CentreLat * 3.14159265358979 / 180.0), 0.1);
	auto ToScreen = [&](double Lat, double Lon)
	{
		return Vector2(float((Lon - CentreLon) * Ppd * Squeeze + Size.x * 0.5), float((CentreLat - Lat) * Ppd + Size.y * 0.5));
	};
	const double S = CentreLat - Size.y * 0.5 / Ppd, N = CentreLat + Size.y * 0.5 / Ppd;
	const double W = CentreLon - Size.x * 0.5 / (Ppd * Squeeze), E = CentreLon + Size.x * 0.5 / (Ppd * Squeeze);
	const int MaxClass = Ppd > 400.0 ? 3 : Ppd > 120.0 ? 2 : 1;
	static const Color Colours[] = { Color(0.95f, 0.55f, 0.15f), Color(0.85f, 0.7f, 0.25f), Color(0.7f, 0.7f, 0.65f), Color(0.45f, 0.47f, 0.47f) };
	static const float Widths[] = { 2.4f, 2.0f, 1.5f, 1.0f };
	// smaller roads first, so the big ones draw over them
	for (int Class = MaxClass; Class >= 0; --Class)
	{
		PackedColorArray Colour;
		Colour.push_back(Colours[Class]);
		for (const Road& R : Roads)
		{
			if (R.Class != Class || R.North < S || R.South > N || R.East < W || R.West > E)
			{
				continue;
			}
			if ((R.North - R.South) * Ppd < 2.0 && (R.East - R.West) * Ppd * Squeeze < 2.0)
			{
				continue;
			}
			PackedVector2Array Line;
			const size_t Count = R.Points.size() / 2;
			for (size_t i = 0; i < Count; ++i)
			{
				const Vector2 P = ToScreen(R.Points[i * 2], R.Points[i * 2 + 1]);
				if (Line.is_empty() || i + 1 == Count || P.distance_squared_to(Line[Line.size() - 1]) >= 2.25f)
				{
					Line.push_back(P);
				}
			}
			if (Line.size() >= 2)
			{
				RS->canvas_item_add_polyline(Item, Line, Colour, Widths[Class], true);
			}
		}
	}
}
