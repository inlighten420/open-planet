// Geodrive Godot extension - AGPL-3.0 (see LICENSE)
//
// A game's rules (Core's GeoGame) for Godot: loaded from its area's "rules", started, told what happens and how time
// goes; what it says taken; its score, time and end read.

#pragma once

#include "GeoGame/Rules.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{
	class GeodriveGame : public RefCounted
	{
		GDCLASS(GeodriveGame, RefCounted)

	public:
		/** Its rules (an area's "rules": a list): "" if they're all rules, else what's wrong */
		String load(const Array& Rules);
		/** The rules as a file has them */
		Array get_rules() const;
		void start();
		void step(double Delta);
		/** Something that happened: "kill" (Of: the creature's id), "caught" */
		void event(const String& Kind, const String& Of);
		/** What it's said since: [{kind: say / win / lose, text}] */
		Array take_said();
		double get_score() const { return Game.Score; }
		double get_time_left() const { return Game.TimeLeft; }
		double get_elapsed() const { return Game.Elapsed; }
		int get_caught() const { return Game.Caught; }
		int get_kills() const { return Game.KillsAll; }
		bool is_over() const { return Game.bOver; }
		String get_result() const { return String::utf8(Game.Result.c_str()); }
		/** A course: Count checkpoints, Laps times round (0: point to point) */
		void set_course(int Count, int Laps) { Game.SetCourse(Count, Laps); }
		int get_next_checkpoint() const { return Game.NextCheckpoint; }
		int get_checkpoints() const { return Game.Checkpoints; }
		int get_lap() const { return Game.Lap; }
		int get_laps() const { return Game.LapsWanted; }
		/** The race's time since its start (-1: not started), its time when it finished (-1: not yet) */
		double get_race_time() const { return Game.RaceTime(); }
		double get_finish_time() const { return Game.FinishTime; }
		/** The score a rule wins at (the lowest "score reaches ... then win"), or -1: for showing "7 / 25" */
		double get_target() const;

	protected:
		static void _bind_methods();

	private:
		geogame::Game Game;
	};
}
