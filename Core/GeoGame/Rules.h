// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// A game's rules: what happens when, as its file says (a pack's area's "rules"), no code. Each rule a when and what's
// done then; the game kept (its score, its time, its timer, what's been killed and how often someone's been caught)
// and its end (won or lost). Engine-free: the host tells it what happens (Event) and how time goes (Step), and shows
// what it says (TakeSaid).
//
//   "rules": [{"when": "start", "then": [{"do": "timer", "seconds": 300}]},
//             {"when": "kill", "of": "zombie", "then": [{"do": "score", "by": 1}]},
//             {"when": "score", "reaches": 25, "then": [{"do": "win", "text": "25 zombies: you've won"}]},
//             {"when": "time_up", "then": [{"do": "lose", "text": "Time's up"}]}]
//
// When: start; kill (of a kind, or any); caught; score (reaches N); kills (N, of a kind or any); caught_times (N);
// after (N s from the start); every (N s); time_up (the timer's run out); checkpoint (of N, or any), lap, finish (a
// course's: SetCourse). The ones that come true once (score, kills, caught_times, after, time_up, finish) are done once.
//
// A course: its checkpoints, reached in order (Event "reach", of its number: only the next one counts). The first is
// the start: the race's clock starts there. No laps: point to point, the last the finish. Laps: a circuit, each lap
// round to the first again (the start and finish line).
// Then: score (by N: added, or taken off); win, lose (text: said; the game's over); say (text); timer (seconds: a
// countdown from there); time (seconds: added to the timer).

#pragma once

#include "GeoHunt/Json.h"

#include <map>
#include <string>
#include <vector>

namespace geogame
{
	struct Step
	{
		std::string Do;		// score, win, lose, say, timer, time
		double Number = 0.0;	// (score: by; timer, time: seconds)
		std::string Text;
	};

	struct Rule
	{
		std::string When;	// start, kill, caught, score, kills, caught_times, after, every, time_up
		std::string Of;		// (kill, kills: the creature's id; "": any)
		double Number = 0.0;	// (score: reaches; kills, caught_times: how many; after, every: seconds)
		std::vector<Step> Then;
		bool bDone = false;	// (once: done)
		double Next = 0.0;	// (every: when next)
	};

	/** Something the game says: "say", "win" or "lose", and its words */
	struct Said
	{
		std::string Kind;
		std::string Text;
	};

	class Game
	{
	public:
		/** Its rules, from its file's "rules" (a list); false and Error (which, and why) if one isn't a rule */
		bool Load(const geojson::Value& Rules, std::string& Error);
		/** The rules as a file has them */
		geojson::Value Write() const;
		const std::vector<Rule>& Rules() const { return List; }

		/** Begun: all from nothing, then what's done at the start */
		void Start();
		/** Time going on (Dt s): every, after, the timer */
		void Step(double Dt);
		/** Something that happened: "kill" (Of: what), "caught" */
		void Event(const std::string& Kind, const std::string& Of = "");
		/** What it's said since last asked, oldest first */
		std::vector<Said> TakeSaid();
		/** A course of Count checkpoints, round Laps times (0: point to point); 0 checkpoints: none */
		void SetCourse(int Count, int Laps);

		// the race: the checkpoint next (its number), the lap it's on (from 1), its time since the start (-1: not
		// started), its time when it finished (-1: not yet), each checkpoint's time
		int Checkpoints = 0;
		int LapsWanted = 0;
		int NextCheckpoint = 0;
		int Lap = 0;
		double RaceStart = -1.0;
		double FinishTime = -1.0;
		std::vector<double> Splits;
		double RaceTime() const { return RaceStart < 0.0 ? -1.0 : (FinishTime >= 0.0 ? FinishTime : Elapsed - RaceStart); }

		double Score = 0.0;
		double Elapsed = 0.0;
		double TimeLeft = -1.0;		// (the timer: s; -1 none)
		int Caught = 0;
		int KillsAll = 0;
		std::map<std::string, int> Kills;
		bool bOver = false;
		std::string Result;		// ("won", "lost"; "" while it's on)
		bool bStarted = false;

	private:
		std::vector<Rule> List;
		std::vector<Said> Out;
		void Fire(Rule& R);
		void Do(const geogame::Step& S);
		void Thresholds();	// (the rules that come true by the game's numbers: score, kills, caught_times)
		void Reached(int Index);
		void Happened(const std::string& When, const std::string& Of);
	};
}
