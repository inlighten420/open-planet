// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Rules.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace geogame
{
	using geojson::Value;

	namespace
	{
		const std::set<std::string> Whens = { "start", "kill", "caught", "score", "kills", "caught_times", "after", "every", "time_up", "checkpoint", "lap",
			"finish" };
		const std::set<std::string> Dos = { "score", "win", "lose", "say", "timer", "time" };
		// (the ones that come true once)
		bool Once(const std::string& When) { return When == "score" || When == "kills" || When == "caught_times" || When == "after" || When == "time_up" || When == "finish"; }

		// a race's time as it's said: "1:02.3"
		std::string Clock(double Seconds)
		{
			char Out[32];
			const int Minutes = int(Seconds / 60.0);
			std::snprintf(Out, sizeof(Out), "%d:%04.1f", Minutes, Seconds - Minutes * 60.0);
			return Out;
		}

		double NumberOf(const Value& V, const char* Key, double Default)
		{
			const Value* N = V.Find(Key);
			return N && N->IsNumber() ? N->Number : Default;
		}
		std::string TextOf(const Value& V, const char* Key)
		{
			const Value* T = V.Find(Key);
			return T && T->IsString() ? T->Text : std::string();
		}
	}

	bool Game::Load(const Value& Rules, std::string& Error)
	{
		List.clear();
		if (Rules.IsNull())
		{
			return true;
		}
		if (!Rules.IsList())
		{
			Error = "rules is a list [ ] of rules";
			return false;
		}
		for (size_t i = 0; i < Rules.Items.size(); ++i)
		{
			const Value& V = Rules.Items[i];
			const std::string Which = "rule " + std::to_string(i + 1) + ": ";
			Rule R;
			R.When = TextOf(V, "when");
			if (!Whens.count(R.When))
			{
				Error = Which + "when is start, kill, caught, score, kills, caught_times, after, every, time_up, checkpoint, lap or finish";
				return false;
			}
			R.Of = TextOf(V, "of");
			R.Number = NumberOf(V, R.When == "score" ? "reaches" : (R.When == "after" || R.When == "every") ? "seconds" : "count", 0.0);
			if ((R.When == "every" && R.Number <= 0.0) || ((R.When == "kills" || R.When == "caught_times") && R.Number < 1.0))
			{
				Error = Which + "every needs its seconds (over 0); kills and caught_times their count (1 or more)";
				return false;
			}
			const Value* Then = V.Find("then");
			if (!Then || !Then->IsList() || Then->Items.empty())
			{
				Error = Which + "then is a list [ ] of what's done";
				return false;
			}
			for (const Value& T : Then->Items)
			{
				geogame::Step S;
				S.Do = TextOf(T, "do");
				if (!Dos.count(S.Do))
				{
					Error = Which + "do is score, win, lose, say, timer or time";
					return false;
				}
				S.Number = NumberOf(T, S.Do == "score" ? "by" : "seconds", S.Do == "score" ? 1.0 : 0.0);
				S.Text = TextOf(T, "text");
				R.Then.push_back(S);
			}
			List.push_back(R);
		}
		return true;
	}

	Value Game::Write() const
	{
		Value Written = Value::List();
		for (const Rule& R : List)
		{
			Value V = Value::Object();
			V.Set("when", R.When);
			if (!R.Of.empty())
			{
				V.Set("of", R.Of);
			}
			if (R.When == "score")
			{
				V.Set("reaches", R.Number);
			}
			else if (R.When == "after" || R.When == "every")
			{
				V.Set("seconds", R.Number);
			}
			else if (R.When == "kills" || R.When == "caught_times")
			{
				V.Set("count", R.Number);
			}
			Value Then = Value::List();
			for (const geogame::Step& S : R.Then)
			{
				Value T = Value::Object();
				T.Set("do", S.Do);
				if (S.Do == "score")
				{
					T.Set("by", S.Number);
				}
				else if (S.Do == "timer" || S.Do == "time")
				{
					T.Set("seconds", S.Number);
				}
				if (!S.Text.empty())
				{
					T.Set("text", S.Text);
				}
				Then.Items.push_back(T);
			}
			V.Set("then", Then);
			Written.Items.push_back(V);
		}
		return Written;
	}

	void Game::Start()
	{
		Score = 0.0;
		Elapsed = 0.0;
		TimeLeft = -1.0;
		Caught = 0;
		KillsAll = 0;
		Kills.clear();
		bOver = false;
		Result.clear();
		bStarted = true;
		Out.clear();
		NextCheckpoint = 0;
		Lap = 0;
		RaceStart = -1.0;
		FinishTime = -1.0;
		Splits.clear();
		for (Rule& R : List)
		{
			R.bDone = false;
			R.Next = R.Number;
		}
		for (Rule& R : List)
		{
			if (R.When == "start")
			{
				Fire(R);
			}
		}
		Thresholds();
	}

	void Game::Step(double Dt)
	{
		if (!bStarted || bOver)
		{
			return;
		}
		Elapsed += Dt;
		if (TimeLeft > 0.0)
		{
			TimeLeft = std::max(0.0, TimeLeft - Dt);
			if (TimeLeft <= 0.0)
			{
				for (Rule& R : List)
				{
					if (R.When == "time_up" && !R.bDone)
					{
						Fire(R);
					}
				}
			}
		}
		for (Rule& R : List)
		{
			if (bOver)
			{
				break;
			}
			if (R.When == "after" && !R.bDone && Elapsed >= R.Number)
			{
				Fire(R);
			}
			else if (R.When == "every" && Elapsed >= R.Next)
			{
				R.Next += R.Number;
				Fire(R);
			}
		}
		Thresholds();
	}

	void Game::SetCourse(int Count, int Laps)
	{
		Checkpoints = std::max(Count, 0);
		LapsWanted = std::max(Laps, 0);
	}

	// A checkpoint reached: only the next counts. The first starts the clock; the last (point to point) or the first
	// again, all the way round (a circuit), ends a lap, and the last lap's the finish
	void Game::Reached(int Index)
	{
		if (Checkpoints < 2 || Index != NextCheckpoint || FinishTime >= 0.0)
		{
			return;
		}
		if (RaceStart < 0.0)
		{
			RaceStart = Elapsed;
			Lap = 1;
			NextCheckpoint = 1;
			Out.push_back({ "say", "Go!" });
			Happened("checkpoint", "0");
			return;
		}
		const double Time = Elapsed - RaceStart;
		Splits.push_back(Time);
		Happened("checkpoint", std::to_string(Index));
		const bool bCircuit = LapsWanted > 0;
		const bool bLapDone = bCircuit ? Index == 0 : Index == Checkpoints - 1;
		if (!bLapDone)
		{
			Out.push_back({ "say", "Checkpoint " + std::to_string(Index + 1) + " / " + std::to_string(Checkpoints) + "   " + Clock(Time) });
			NextCheckpoint = (Index + 1) % Checkpoints;
			return;
		}
		Happened("lap", std::to_string(Lap));
		if (!bCircuit || Lap >= LapsWanted)
		{
			FinishTime = Time;
			Out.push_back({ "say", "Finished: " + Clock(Time) });
			Happened("finish", "");
			return;
		}
		++Lap;
		Out.push_back({ "say", "Lap " + std::to_string(Lap) + " / " + std::to_string(LapsWanted) + "   " + Clock(Time) });
		NextCheckpoint = 1;
	}

	void Game::Happened(const std::string& When, const std::string& Of)
	{
		for (Rule& R : List)
		{
			if (bOver)
			{
				break;
			}
			if (R.When == When && (R.Of.empty() || R.Of == Of) && !(Once(R.When) && R.bDone))
			{
				Fire(R);
			}
		}
	}

	void Game::Event(const std::string& Kind, const std::string& Of)
	{
		if (!bStarted || bOver)
		{
			return;
		}
		if (Kind == "reach")
		{
			Reached(std::atoi(Of.c_str()));
			Thresholds();
			return;
		}
		if (Kind == "kill")
		{
			++KillsAll;
			++Kills[Of];
		}
		else if (Kind == "caught")
		{
			++Caught;
		}
		for (Rule& R : List)
		{
			if (bOver)
			{
				break;
			}
			if (R.When == Kind && (R.Of.empty() || R.Of == Of))
			{
				Fire(R);
			}
		}
		Thresholds();
	}

	void Game::Thresholds()
	{
		// (each time the numbers change: the ones they've come to; each may change them again, so round till none)
		bool bAny = true;
		while (bAny && !bOver)
		{
			bAny = false;
			for (Rule& R : List)
			{
				if (R.bDone || bOver)
				{
					continue;
				}
				const int KillsOf = R.Of.empty() ? KillsAll : (Kills.count(R.Of) ? Kills.at(R.Of) : 0);
				const bool bCome = (R.When == "score" && Score >= R.Number) || (R.When == "kills" && KillsOf >= int(R.Number))
					|| (R.When == "caught_times" && Caught >= int(R.Number));
				if (bCome)
				{
					Fire(R);
					bAny = true;
				}
			}
		}
	}

	void Game::Fire(Rule& R)
	{
		if (Once(R.When))
		{
			R.bDone = true;
		}
		for (const geogame::Step& S : R.Then)
		{
			if (bOver)
			{
				return;
			}
			Do(S);
		}
	}

	void Game::Do(const geogame::Step& S)
	{
		if (S.Do == "score")
		{
			Score += S.Number;
		}
		else if (S.Do == "timer")
		{
			TimeLeft = S.Number;
		}
		else if (S.Do == "time")
		{
			TimeLeft = std::max(0.0, TimeLeft) + S.Number;
		}
		else if (S.Do == "say")
		{
			Out.push_back({ "say", S.Text });
		}
		else if (S.Do == "win" || S.Do == "lose")
		{
			bOver = true;
			Result = S.Do == "win" ? "won" : "lost";
			Out.push_back({ S.Do, S.Text.empty() ? (S.Do == "win" ? std::string("You've won") : std::string("You've lost")) : S.Text });
		}
	}

	std::vector<Said> Game::TakeSaid()
	{
		std::vector<Said> Taken;
		Taken.swap(Out);
		return Taken;
	}
}
