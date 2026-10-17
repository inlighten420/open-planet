// Geodrive Core - GPL (see Core/README.md)

#include "CreatureFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace geohunt
{
	using geojson::Value;

	namespace
	{
		const char* const DietNames[] = { "grazer", "browser", "mixed", "carnivore" };
		const char* const HornNames[] = { "none", "pair", "nose" };
		const char* const ManeNames[] = { "none", "lion", "crest" };
		const char* const CoatNames[] = { "plain", "countershade", "stripes", "patches", "mask", "pinstripes" };
		const char* const FrameNames[] = { "four_legs", "upright" };
		// (the zones a wound can kill from, as Core's Zone numbers them)
		const std::pair<const char*, Zone> VitalNames[] = { { "brain", Zone::Brain }, { "spine", Zone::Spine }, { "heart_lung", Zone::HeartLung },
			{ "gut", Zone::Gut }, { "neck", Zone::Neck } };

		std::string Lower(std::string S)
		{
			std::transform(S.begin(), S.end(), S.begin(), [](unsigned char C) { return char(std::tolower(C)); });
			return S;
		}

		// a word's place in Names (-1: none of them)
		template <size_t N>
		int Word(const Value& V, const char* const (&Names)[N])
		{
			if (!V.IsString())
			{
				return -1;
			}
			const std::string Given = Lower(V.Text);
			for (size_t i = 0; i < N; ++i)
			{
				if (Given == Names[i])
				{
					return int(i);
				}
			}
			return -1;
		}

		// An object's fields read into what's there already: each a number, flag, text or [least, most]; the first
		// that's the wrong kind is told
		struct Fields
		{
			const Value& From;
			std::string& Error;

			bool Number(const char* Key, double& Out)
			{
				const Value* V = From.Find(Key);
				if (!V)
				{
					return true;
				}
				if (!V->IsNumber())
				{
					Error = std::string(Key) + " is a number";
					return false;
				}
				Out = V->Number;
				return true;
			}
			bool Whole(const char* Key, int& Out)
			{
				double N = Out;
				if (!Number(Key, N))
				{
					return false;
				}
				Out = int(std::lround(N));
				return true;
			}
			bool Flag(const char* Key, bool& Out)
			{
				const Value* V = From.Find(Key);
				if (!V)
				{
					return true;
				}
				if (!V->IsBool())
				{
					Error = std::string(Key) + " is true or false";
					return false;
				}
				Out = V->Flag;
				return true;
			}
			bool Text(const char* Key, std::string& Out)
			{
				const Value* V = From.Find(Key);
				if (!V)
				{
					return true;
				}
				if (!V->IsString())
				{
					Error = std::string(Key) + " is text, in quotes";
					return false;
				}
				Out = V->Text;
				return true;
			}
			// [a, b]: two numbers (Ordered: the first no more than the second, and not below 0)
			bool Pair(const char* Key, double& A, double& B, bool bOrdered, const char* Why)
			{
				const Value* V = From.Find(Key);
				if (!V)
				{
					return true;
				}
				if (!V->IsList() || V->Items.size() != 2 || !V->Items[0].IsNumber() || !V->Items[1].IsNumber()
					|| (bOrdered && (V->Items[0].Number > V->Items[1].Number || V->Items[0].Number < 0.0)))
				{
					Error = Why;
					return false;
				}
				A = V->Items[0].Number;
				B = V->Items[1].Number;
				return true;
			}
		};

		Value Pair(double A, double B) { return Value::List({ Value(A), Value(B) }); }

		double ToLinear(double C) { return C <= 0.04045 ? C / 12.92 : std::pow((C + 0.055) / 1.055, 2.4); }
		double ToSrgb(double C) { return C <= 0.0031308 ? C * 12.92 : 1.055 * std::pow(C, 1.0 / 2.4) - 0.055; }

		std::string IdOf(const Value& V)
		{
			const Value* Id = V.Find("id");
			return Id && Id->IsString() ? Id->Text : std::string("?");
		}

		template <typename ReadOne>
		int ReadList(const std::string& Text, std::vector<std::string>& Errors, ReadOne&& Read)
		{
			Value Root;
			std::string Error;
			if (!geojson::Parse(Text, Root, Error))
			{
				Errors.push_back(Error);
				return 0;
			}
			if (!Root.IsList() && !Root.IsObject())
			{
				Errors.push_back("not an object { } or a list [ ] of them");
				return 0;
			}
			const std::vector<Value> One = Root.IsObject() ? std::vector<Value>{ Root } : std::vector<Value>();
			const std::vector<Value>& List = Root.IsObject() ? One : Root.Items;
			int Taken = 0;
			for (const Value& Item : List)
			{
				Error.clear();
				if (!Item.IsObject())
				{
					Errors.push_back("an item that isn't an object { }");
				}
				else if (Read(Item, Error) >= 0)
				{
					++Taken;
				}
				else
				{
					Errors.push_back(IdOf(Item) + ": " + Error);
				}
			}
			return Taken;
		}
	}

	bool ColourFromHtml(const std::string& Html, float& R, float& G, float& B)
	{
		std::string Hex = Html.size() > 0 && Html[0] == '#' ? Html.substr(1) : Html;
		if (Hex.size() == 3)
		{
			Hex = std::string{ Hex[0], Hex[0], Hex[1], Hex[1], Hex[2], Hex[2] };
		}
		if (Hex.size() != 6 || !std::all_of(Hex.begin(), Hex.end(), [](unsigned char C) { return std::isxdigit(C) != 0; }))
		{
			return false;
		}
		const unsigned long Bits = std::stoul(Hex, nullptr, 16);
		R = float(ToLinear(((Bits >> 16) & 0xFF) / 255.0));
		G = float(ToLinear(((Bits >> 8) & 0xFF) / 255.0));
		B = float(ToLinear((Bits & 0xFF) / 255.0));
		return true;
	}

	std::string ColourToHtml(float R, float G, float B)
	{
		auto Byte = [](float C) { return int(std::lround(std::clamp(ToSrgb(C), 0.0, 1.0) * 255.0)); };
		char Out[8];
		std::snprintf(Out, sizeof(Out), "#%02x%02x%02x", Byte(R), Byte(G), Byte(B));
		return Out;
	}

	int ReadCreature(const Value& V, std::string& Error)
	{
		Fields F{ V, Error };
		std::string Id;
		if (!F.Text("id", Id) || Id.empty())
		{
			Error = Error.empty() ? "no id" : Error;
			return -1;
		}
		// what it starts from: another creature (like), itself (replacing one: only what's given changes), or nothing
		Species S{};
		S.HerdMin = S.HerdMax = 1;
		S.Eats = Diet::Grazer;
		S.R = S.G = S.B = 0.3f;
		S.Acts = ActsFor(Diet::Grazer);
		std::string Like = Id;
		if (!F.Text("like", Like))
		{
			return -1;
		}
		const int From = FindSpecies(Like);
		if (From >= 0)
		{
			S = AllSpecies()[size_t(From)];
		}
		else if (V.Find("like"))
		{
			Error = "no creature '" + Like + "' to be like";
			return -1;
		}
		const bool bItself = From >= 0 && Like == Id;
		S.Id = Id;
		S.Name = bItself ? S.Name : Id;
		double HerdMin = S.HerdMin, HerdMax = S.HerdMax, SeasonFrom = S.SeasonFrom, SeasonTo = S.SeasonTo;
		if (!F.Text("name", S.Name) || !F.Number("mass", S.Mass) || !F.Number("shoulder", S.Shoulder) || !F.Number("length", S.Length)
			|| !F.Number("walk", S.Walk) || !F.Number("run", S.Run) || !F.Pair("herd", HerdMin, HerdMax, false, "herd is [fewest, most]")
			|| !F.Flag("horns", S.bHorns) || !F.Flag("protected", S.bProtected)
			|| !F.Pair("season", SeasonFrom, SeasonTo, false, "season is [first month, last month] (0, 0: closed)") || !F.Whole("bag", S.BagPerDay))
		{
			return -1;
		}
		S.HerdMin = int(std::lround(HerdMin));
		S.HerdMax = int(std::lround(HerdMax));
		S.SeasonFrom = int(std::lround(SeasonFrom));
		S.SeasonTo = int(std::lround(SeasonTo));
		if (const Value* D = V.Find("diet"))
		{
			const int Kind = Word(*D, DietNames);
			if (Kind < 0)
			{
				Error = "diet is grazer, browser, mixed or carnivore";
				return -1;
			}
			// (a new diet: its ways too, unless the behaviour's given)
			if (Diet(Kind) != S.Eats)
			{
				S.Acts = ActsFor(Diet(Kind));
			}
			S.Eats = Diet(Kind);
		}
		if (const Value* C = V.Find("colour"))
		{
			// (as a picture's colour, #rrggbb; kept linear)
			if (!C->IsString() || !ColourFromHtml(C->Text, S.R, S.G, S.B))
			{
				Error = "colour is #rrggbb";
				return -1;
			}
		}
		// a creature like another without horns, given horns: a pair, as most have
		BodyShape& B = S.Shape;
		if (S.bHorns && B.HornKind == BodyShape::Horns::None)
		{
			B.HornKind = BodyShape::Horns::Pair;
		}
		if (!S.bHorns)
		{
			B.HornKind = BodyShape::Horns::None;
		}
		if (const Value* Shape = V.Find("shape"))
		{
			Fields Sh{ *Shape, Error };
			if (!Shape->IsObject())
			{
				Error = "shape is an object { }";
				return -1;
			}
			if (!Sh.Number("head_up", B.HeadUp) || !Sh.Number("torso_depth", B.TorsoDepth) || !Sh.Number("back_slope", B.BackSlope)
				|| !Sh.Number("ears", B.Ears) || !Sh.Number("horn_length", B.HornLength) || !Sh.Flag("long_tail", B.bLongTail)
				|| !Sh.Flag("arms_forward", B.bArmsForward))
			{
				Error = "shape's " + Error;
				return -1;
			}
			if (const Value* Frame = Shape->Find("frame"))
			{
				const int Kind = Word(*Frame, FrameNames);
				if (Kind < 0)
				{
					Error = "shape's frame is four_legs or upright";
					return -1;
				}
				B.Build = BodyShape::Frame(Kind);
			}
			if (const Value* H = Shape->Find("horns"))
			{
				const int Kind = Word(*H, HornNames);
				if (Kind < 0)
				{
					Error = "shape's horns are none, pair or nose";
					return -1;
				}
				B.HornKind = BodyShape::Horns(Kind);
				S.bHorns = Kind != 0;
			}
			if (const Value* Bend = Shape->Find("bend"))
			{
				if (!Bend->IsList() || Bend->Items.size() != 4
					|| !std::all_of(Bend->Items.begin(), Bend->Items.end(), [](const Value& A) { return A.IsNumber(); }))
				{
					Error = "shape's bend is four angles (rad): back, out, then back, out again";
					return -1;
				}
				for (int k = 0; k < 4; ++k)
				{
					B.Bend[k] = Bend->Items[size_t(k)].Number;
				}
			}
			if (const Value* M = Shape->Find("mane"))
			{
				const int Kind = Word(*M, ManeNames);
				if (Kind < 0)
				{
					Error = "shape's mane is none, lion or crest";
					return -1;
				}
				B.ManeKind = BodyShape::Mane(Kind);
			}
			if (const Value* C = Shape->Find("coat"))
			{
				const int Kind = Word(*C, CoatNames);
				if (Kind < 0)
				{
					Error = "shape's coat is plain, countershade, stripes, patches, mask or pinstripes";
					return -1;
				}
				B.Coat = Pattern(Kind);
			}
		}
		// its behaviour: building blocks, each with its numbers (only what's given changes)
		Behaviour& A = S.Acts;
		if (const Value* Acts = V.Find("behaviour"))
		{
			if (!Acts->IsObject())
			{
				Error = "behaviour is an object { }";
				return -1;
			}
			Fields Be{ *Acts, Error };
			const char* PairWhy = "behaviour's meal, thirsty_after, wary and flees_for are [least, most] (s)";
			if (!Be.Number("browsing", A.Browsing) || !Be.Flag("rests", A.bRests) || !Be.Number("hears", A.Hears) || !Be.Flag("prey", A.bPrey)
				|| !Be.Flag("flees", A.bFlees) || !Be.Whole("wounds_to_drop", A.WoundsToDrop)
				|| !Be.Pair("meal", A.MealMin, A.MealMax, true, PairWhy) || !Be.Pair("thirsty_after", A.ThirstMin, A.ThirstMax, true, PairWhy)
				|| !Be.Pair("wary", A.WaryMin, A.WaryMax, true, PairWhy) || !Be.Pair("flees_for", A.FleeMin, A.FleeMax, true, PairWhy))
			{
				Error = Error.rfind("behaviour", 0) == 0 ? Error : "behaviour's " + Error;
				return -1;
			}
			if (const Value* Vital = Acts->Find("vital"))
			{
				// (the zones a wound kills from: the rest only hurt)
				uint32_t Bits = 0;
				bool bAll = Vital->IsList();
				for (const Value& Name : Vital->IsList() ? Vital->Items : std::vector<Value>())
				{
					bool bKnown = false;
					for (const auto& [Word, Z] : VitalNames)
					{
						if (Name.IsString() && Lower(Name.Text) == Word)
						{
							Bits |= 1u << uint32_t(Z);
							bKnown = true;
						}
					}
					bAll = bAll && bKnown;
				}
				if (!bAll)
				{
					Error = "behaviour's vital is a list of brain, spine, heart_lung, gut, neck";
					return -1;
				}
				A.Vital = Bits;
			}
			if (const Value* People = Acts->Find("goes_for_people"))
			{
				if (People->IsBool())
				{
					A.bGoesForPeople = People->Flag;
				}
				else if (People->IsObject())
				{
					A.bGoesForPeople = true;
					Fields G{ *People, Error };
					if (!G.Number("within", A.NoticeWithin) || !G.Number("speed", A.PursueSpeed) || !G.Number("reach", A.Reach) || !G.Number("gives_up", A.GiveUp))
					{
						Error = "behaviour's goes_for_people's " + Error;
						return -1;
					}
				}
				else
				{
					Error = "behaviour's goes_for_people is false, true, or its numbers";
					return -1;
				}
				if (A.bGoesForPeople && (A.NoticeWithin <= 0.0 || A.PursueSpeed <= 0.0 || A.Reach <= 0.0 || A.GiveUp < A.NoticeWithin))
				{
					Error = "behaviour's goes_for_people: within, speed and reach over 0; gives_up at least within";
					return -1;
				}
			}
			if (const Value* Hunts = Acts->Find("hunts"))
			{
				if (Hunts->IsBool())
				{
					A.bHunts = Hunts->Flag;
				}
				else if (Hunts->IsObject())
				{
					A.bHunts = true;
					Fields H{ *Hunts, Error };
					const char* HuntWhy = "behaviour's hunts' every and feeds_for are [least, most] (s)";
					if (!H.Number("within", A.HuntWithin) || !H.Number("prey_up_to", A.PreyUpTo) || !H.Number("rush_from", A.RushFrom)
						|| !H.Number("rush_for", A.RushFor) || !H.Number("rush_speed", A.RushSpeed)
						|| !H.Pair("every", A.HuntMin, A.HuntMax, true, HuntWhy) || !H.Pair("feeds_for", A.FeedMin, A.FeedMax, true, HuntWhy))
					{
						Error = Error.rfind("behaviour", 0) == 0 ? Error : "behaviour's hunts' " + Error;
						return -1;
					}
				}
				else
				{
					Error = "behaviour's hunts is false, true, or its numbers";
					return -1;
				}
			}
		}
		if (A.Browsing < 0.0 || A.Browsing > 1.0 || A.Hears < 0.0 || A.WoundsToDrop < 0 || (A.bHunts && (A.HuntMin <= 0.0 || A.RushFor < 8.0 || A.RushSpeed <= 0.0)))
		{
			Error = "browsing is 0-1; hears and wounds_to_drop 0 or more; a hunter hunts every so often (over 0 s), its rush at least 8 s and some speed";
			return -1;
		}
		// (what no animal could be)
		if (!(S.Mass > 0.0) || !(S.Shoulder > 0.0) || !(S.Length > 0.0))
		{
			Error = "mass, shoulder and length must be over 0";
			return -1;
		}
		if (!(S.Walk > 0.0) || S.Run < S.Walk)
		{
			Error = "walk must be over 0, and run at least walk";
			return -1;
		}
		if (S.HerdMin < 1 || S.HerdMax < S.HerdMin)
		{
			Error = "herd's fewest at least 1, its most at least that";
			return -1;
		}
		if (S.SeasonFrom < 0 || S.SeasonFrom > 12 || S.SeasonTo < 0 || S.SeasonTo > 12 || S.BagPerDay < 0)
		{
			Error = "season's months are 1-12 (0, 0: closed); bag 0 or more";
			return -1;
		}
		return AddSpecies(S);
	}

	Value CreatureValue(const Species& S)
	{
		Value D = Value::Object();
		D.Set("id", S.Id);
		D.Set("name", S.Name);
		D.Set("mass", S.Mass);
		D.Set("shoulder", S.Shoulder);
		D.Set("length", S.Length);
		D.Set("walk", S.Walk);
		D.Set("run", S.Run);
		D.Set("herd", Pair(S.HerdMin, S.HerdMax));
		D.Set("diet", DietNames[int(S.Eats)]);
		D.Set("colour", ColourToHtml(S.R, S.G, S.B));
		D.Set("horns", S.bHorns);
		D.Set("protected", S.bProtected);
		D.Set("season", Pair(S.SeasonFrom, S.SeasonTo));
		D.Set("bag", S.BagPerDay);
		const BodyShape& B = S.Shape;
		Value& Shape = D.Set("shape", Value::Object());
		Shape.Set("head_up", B.HeadUp);
		Shape.Set("torso_depth", B.TorsoDepth);
		Shape.Set("back_slope", B.BackSlope);
		Shape.Set("ears", B.Ears);
		Shape.Set("horns", HornNames[int(B.HornKind)]);
		Shape.Set("horn_length", B.HornLength);
		Shape.Set("bend", Value::List({ Value(B.Bend[0]), Value(B.Bend[1]), Value(B.Bend[2]), Value(B.Bend[3]) }));
		Shape.Set("mane", ManeNames[int(B.ManeKind)]);
		Shape.Set("long_tail", B.bLongTail);
		Shape.Set("coat", CoatNames[int(B.Coat)]);
		if (B.Build != BodyShape::Frame::FourLegs)
		{
			Shape.Set("frame", FrameNames[int(B.Build)]);
			Shape.Set("arms_forward", B.bArmsForward);
		}
		const Behaviour& A = S.Acts;
		Value& Acts = D.Set("behaviour", Value::Object());
		Acts.Set("browsing", A.Browsing);
		Acts.Set("rests", A.bRests);
		Acts.Set("meal", Pair(A.MealMin, A.MealMax));
		Acts.Set("thirsty_after", A.ThirstMax > 0.0 ? Pair(A.ThirstMin, A.ThirstMax) : Pair(0.0, 0.0));	// ([0, 0]: never drinks)
		Acts.Set("hears", A.Hears);
		Acts.Set("wary", Pair(A.WaryMin, A.WaryMax));
		Acts.Set("flees_for", Pair(A.FleeMin, A.FleeMax));
		Acts.Set("prey", A.bPrey);
		Acts.Set("flees", A.bFlees);
		Acts.Set("wounds_to_drop", A.WoundsToDrop);
		Value Vital = Value::List();
		for (const auto& [Word, Z] : VitalNames)
		{
			if (A.Vital & (1u << uint32_t(Z)))
			{
				Vital.Items.push_back(Value(Word));
			}
		}
		Acts.Set("vital", Vital);
		if (A.bHunts)
		{
			Value& H = Acts.Set("hunts", Value::Object());
			H.Set("every", Pair(A.HuntMin, A.HuntMax));
			H.Set("within", A.HuntWithin);
			H.Set("prey_up_to", A.PreyUpTo);
			H.Set("rush_from", A.RushFrom);
			H.Set("rush_for", A.RushFor);
			H.Set("rush_speed", A.RushSpeed);
			H.Set("feeds_for", Pair(A.FeedMin, A.FeedMax));
		}
		else
		{
			Acts.Set("hunts", false);
		}
		if (A.bGoesForPeople)
		{
			Value& G = Acts.Set("goes_for_people", Value::Object());
			G.Set("within", A.NoticeWithin);
			G.Set("speed", A.PursueSpeed);
			G.Set("reach", A.Reach);
			G.Set("gives_up", A.GiveUp);
		}
		return D;
	}

	int ReadLoad(const Value& V, std::string& Error)
	{
		Fields F{ V, Error };
		Load L{};
		std::string Id;
		if (!F.Text("id", Id) || Id.empty())
		{
			Error = Error.empty() ? "no id" : Error;
			return -1;
		}
		const int From = FindLoad(Id);
		if (From >= 0)
		{
			L = AllLoads()[size_t(From)];
		}
		else
		{
			L.ZeroAt = 100.0;
			L.SightHeight = 0.045;
			L.Magazine = 1;
		}
		L.Id = Id;
		L.Name = From >= 0 ? L.Name : Id;
		if (!F.Text("name", L.Name) || !F.Number("grains", L.Grains) || !F.Number("bc", L.BC) || !F.Number("muzzle", L.Muzzle)
			|| !F.Number("zero", L.ZeroAt) || !F.Number("sight_height", L.SightHeight) || !F.Whole("magazine", L.Magazine) || !F.Flag("arrow", L.bArrow)
			|| !F.Text("action", L.Action) || !F.Number("rate", L.Rate) || !F.Whole("rounds", L.Rounds) || !F.Number("reload_time", L.ReloadTime)
			|| !F.Text("sight", L.Sight) || !F.Number("kick", L.Kick) || !F.Text("model", L.Model))
		{
			return -1;
		}
		if (const Value* Look = V.Find("look"))
		{
			// (its look: the host's, kept as it is)
			L.Look = geojson::Write(*Look);
		}
		if (L.Action != "bolt" && L.Action != "semi" && L.Action != "auto")
		{
			Error = "action is bolt, semi or auto";
			return -1;
		}
		if (L.Sight != "scope" && L.Sight != "iron")
		{
			Error = "sight is scope or iron";
			return -1;
		}
		if (L.Action == "auto" && !(L.Rate >= 60.0 && L.Rate <= 2000.0))
		{
			Error = "an automatic's rate is 60 to 2,000 rounds a minute";
			return -1;
		}
		if (L.Rounds < L.Magazine || L.ReloadTime < 0.5 || L.Kick < 0.0)
		{
			Error = "rounds at least a magazine; reload_time at least 0.5 s; kick 0 or more";
			return -1;
		}
		if (!(L.Grains > 0.0) || !(L.Muzzle > 0.0) || !(L.ZeroAt > 0.0) || L.SightHeight < 0.0 || L.Magazine < 1)
		{
			Error = "grains, muzzle (m/s) and zero (m) must be over 0; sight_height 0 or more; magazine at least 1";
			return -1;
		}
		if (!L.bArrow && !(L.BC > 0.0))
		{
			Error = "a bullet's bc (its G1 ballistic coefficient) must be over 0";
			return -1;
		}
		return AddLoad(L);
	}

	Value LoadValue(const Load& L)
	{
		Value D = Value::Object();
		D.Set("id", L.Id);
		D.Set("name", L.Name);
		D.Set("grains", L.Grains);
		D.Set("bc", L.BC);
		D.Set("muzzle", L.Muzzle);
		D.Set("zero", L.ZeroAt);
		D.Set("sight_height", L.SightHeight);
		D.Set("magazine", L.Magazine);
		D.Set("arrow", L.bArrow);
		D.Set("action", L.Action);
		D.Set("rate", L.Rate);
		D.Set("rounds", L.Rounds);
		D.Set("reload_time", L.ReloadTime);
		D.Set("sight", L.Sight);
		D.Set("kick", L.Kick);
		D.Set("model", L.Model);
		if (!L.Look.empty())
		{
			Value Look;
			std::string Ignored;
			if (geojson::Parse(L.Look, Look, Ignored))
			{
				D.Set("look", Look);
			}
		}
		return D;
	}

	int ReadCreatures(const std::string& Text, std::vector<std::string>& Errors)
	{
		return ReadList(Text, Errors, [](const Value& V, std::string& Error) { return ReadCreature(V, Error); });
	}

	int ReadLoads(const std::string& Text, std::vector<std::string>& Errors)
	{
		return ReadList(Text, Errors, [](const Value& V, std::string& Error) { return ReadLoad(V, Error); });
	}
}
