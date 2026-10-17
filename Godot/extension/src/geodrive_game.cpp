// Geodrive Godot extension - GPL (see Core/README.md)

#include "geodrive_game.h"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

void GeodriveGame::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("load", "rules"), &GeodriveGame::load);
	ClassDB::bind_method(D_METHOD("get_rules"), &GeodriveGame::get_rules);
	ClassDB::bind_method(D_METHOD("start"), &GeodriveGame::start);
	ClassDB::bind_method(D_METHOD("step", "delta"), &GeodriveGame::step);
	ClassDB::bind_method(D_METHOD("event", "kind", "of"), &GeodriveGame::event, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("take_said"), &GeodriveGame::take_said);
	ClassDB::bind_method(D_METHOD("get_score"), &GeodriveGame::get_score);
	ClassDB::bind_method(D_METHOD("get_time_left"), &GeodriveGame::get_time_left);
	ClassDB::bind_method(D_METHOD("get_elapsed"), &GeodriveGame::get_elapsed);
	ClassDB::bind_method(D_METHOD("get_caught"), &GeodriveGame::get_caught);
	ClassDB::bind_method(D_METHOD("get_kills"), &GeodriveGame::get_kills);
	ClassDB::bind_method(D_METHOD("is_over"), &GeodriveGame::is_over);
	ClassDB::bind_method(D_METHOD("get_result"), &GeodriveGame::get_result);
	ClassDB::bind_method(D_METHOD("get_target"), &GeodriveGame::get_target);
	ClassDB::bind_method(D_METHOD("set_course", "count", "laps"), &GeodriveGame::set_course);
	ClassDB::bind_method(D_METHOD("get_next_checkpoint"), &GeodriveGame::get_next_checkpoint);
	ClassDB::bind_method(D_METHOD("get_checkpoints"), &GeodriveGame::get_checkpoints);
	ClassDB::bind_method(D_METHOD("get_lap"), &GeodriveGame::get_lap);
	ClassDB::bind_method(D_METHOD("get_laps"), &GeodriveGame::get_laps);
	ClassDB::bind_method(D_METHOD("get_race_time"), &GeodriveGame::get_race_time);
	ClassDB::bind_method(D_METHOD("get_finish_time"), &GeodriveGame::get_finish_time);
}

String GeodriveGame::load(const Array& Rules)
{
	// (Godot's to Core's by its text: full precision, read back the same)
	geojson::Value V;
	std::string Error;
	if (!geojson::Parse(JSON::stringify(Rules, "", false, true).utf8().get_data(), V, Error) || !Game.Load(V, Error))
	{
		return String::utf8(Error.c_str());
	}
	return String();
}

Array GeodriveGame::get_rules() const
{
	const Variant Parsed = JSON::parse_string(String::utf8(geojson::Write(Game.Write()).c_str()));
	return Parsed.get_type() == Variant::ARRAY ? Array(Parsed) : Array();
}

void GeodriveGame::start() { Game.Start(); }
void GeodriveGame::step(double Delta) { Game.Step(Delta); }
void GeodriveGame::event(const String& Kind, const String& Of) { Game.Event(Kind.utf8().get_data(), Of.utf8().get_data()); }

Array GeodriveGame::take_said()
{
	Array Out;
	for (const geogame::Said& S : Game.TakeSaid())
	{
		Dictionary D;
		D["kind"] = String::utf8(S.Kind.c_str());
		D["text"] = String::utf8(S.Text.c_str());
		Out.push_back(D);
	}
	return Out;
}

double GeodriveGame::get_target() const
{
	double Target = -1.0;
	for (const geogame::Rule& R : Game.Rules())
	{
		bool bWins = false;
		for (const geogame::Step& S : R.Then)
		{
			bWins = bWins || S.Do == "win";
		}
		if (R.When == "score" && bWins && (Target < 0.0 || R.Number < Target))
		{
			Target = R.Number;
		}
	}
	return Target;
}
