// Geodrive Core - GPL (see Core/README.md)

#include "RoadDesign.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace georoads
{
	namespace
	{
		/**
		 *  Minimum K values by design speed (crest, sag), from SANRAL's stopping sight distance formulas: crest
		 *  K = S^2 / 200(sqrt 1.05 + sqrt 0.6)^2, sag K = S^2 / (120 + 3.5 S), S = v(0.694 + 0.013 v)
		 */
		struct KRow { float Speed; float Crest; float Sag; };
		constexpr KRow KTable[] = {
			{ 30.0f, 2.0f, 5.0f }, { 40.0f, 4.0f, 8.0f }, { 60.0f, 12.0f, 18.0f }, { 80.0f, 30.0f, 32.0f },
			{ 100.0f, 61.0f, 49.0f }, { 120.0f, 113.0f, 69.0f }, { 130.0f, 148.0f, 80.0f },
		};

		/** Steepest grades by design speed, flat/rolling and mountainous (CSIR Red Book table 7.16, SANRAL) */
		struct GradeRow { float Speed; float Grade; float Mountain; };
		constexpr GradeRow GradeTable[] = {
			{ 30.0f, 0.16f, 0.20f }, { 40.0f, 0.12f, 0.16f }, { 60.0f, 0.10f, 0.12f }, { 80.0f, 0.06f, 0.08f },
			{ 100.0f, 0.05f, 0.07f }, { 120.0f, 0.035f, 0.04f },
		};

		template <typename RowType, int N, typename Getter>
		float Interpolate(const RowType (&Table)[N], float Speed, Getter Get)
		{
			if (Speed <= Table[0].Speed)
			{
				return Get(Table[0]);
			}
			for (int i = 1; i < N; ++i)
			{
				if (Speed <= Table[i].Speed)
				{
					const float T = (Speed - Table[i - 1].Speed) / (Table[i].Speed - Table[i - 1].Speed);
					return Get(Table[i - 1]) + T * (Get(Table[i]) - Get(Table[i - 1]));
				}
			}
			return Get(Table[N - 1]);
		}

		std::string Trim(const std::string& S)
		{
			size_t First = 0, Last = S.size();
			while (First < Last && std::isspace(static_cast<unsigned char>(S[First]))) { ++First; }
			while (Last > First && std::isspace(static_cast<unsigned char>(S[Last - 1]))) { --Last; }
			return S.substr(First, Last - First);
		}

		void ReplaceAll(std::string& S, const std::string& From)
		{
			for (size_t At = S.find(From); At != std::string::npos; At = S.find(From))
			{
				S.erase(At, From.size());
			}
		}

		/** An optional sign, digits and at most one point */
		bool IsNumeric(const std::string& S)
		{
			size_t i = (!S.empty() && (S[0] == '-' || S[0] == '+')) ? 1 : 0;
			bool bDot = false;
			for (; i < S.size(); ++i)
			{
				if (S[i] == '.')
				{
					if (bDot)
					{
						return false;
					}
					bDot = true;
				}
				else if (!std::isdigit(static_cast<unsigned char>(S[i])))
				{
					return false;
				}
			}
			return true;
		}
	}

	float ParseMaxSpeed(const std::string& Value)
	{
		// South African implicit limits (OSM "ZA:..." values)
		if (Value == "ZA:urban") { return 60.0f; }
		if (Value == "ZA:rural") { return 100.0f; }
		if (Value == "ZA:motorway" || Value == "ZA:freeway") { return 120.0f; }
		if (Value == "walk") { return 10.0f; }

		std::string Number = Trim(Value);
		const bool bMph = EndsWith(Number, "mph");
		ReplaceAll(Number, "mph");
		ReplaceAll(Number, "km/h");
		Number = Trim(Number);
		if (!IsNumeric(Number))
		{
			return 0.0f;
		}
		const float Speed = static_cast<float>(std::atof(Number.c_str())) * (bMph ? 1.609f : 1.0f);
		return Speed > 0.0f ? Speed : 0.0f;
	}

	float DefaultSpeed(const std::string& Highway)
	{
		if (Highway == "motorway") { return 120.0f; }
		if (Highway == "trunk" || Highway == "primary") { return 100.0f; }
		if (Highway == "secondary" || Highway == "tertiary") { return 80.0f; }
		if (EndsWith(Highway, "_link")) { return 60.0f; }
		if (Highway == "unclassified" || Highway == "road") { return 60.0f; }
		if (Highway == "residential") { return 40.0f; }
		return 30.0f;	// living streets, service roads, tracks
	}

	int32_t RoadRank(const std::string& Highway)
	{
		if (StartsWith(Highway, "motorway") || StartsWith(Highway, "trunk")) { return 4; }
		if (StartsWith(Highway, "primary") || StartsWith(Highway, "secondary")) { return 3; }
		if (StartsWith(Highway, "tertiary")) { return 2; }
		return 1;
	}

	RoadDesign DesignFor(const std::string& Highway, float SpeedKmh, bool bMeasured, bool bUK)
	{
		RoadDesign Design;
		// roads are designed for a little over their speed limit
		const float Floor = bUK && SpeedKmh > 0.0f ? 0.0f : DefaultSpeed(Highway);
		Design.SpeedKmh = std::clamp(std::max(Floor, SpeedKmh > 0.0f ? SpeedKmh + 10.0f : 0.0f), 20.0f, 130.0f);
		Design.Rank = RoadRank(Highway);

		Design.MaxGrade = Interpolate(GradeTable, Design.SpeedKmh, [](const GradeRow& Row) { return Row.Grade; });
		Design.MaxGradeMountain = Interpolate(GradeTable, Design.SpeedKmh, [](const GradeRow& Row) { return Row.Mountain; });
		if (Highway == "track")
		{
			// farm and forestry tracks just follow the land
			Design.MaxGrade = 0.15f;
			Design.MaxGradeMountain = 0.25f;
		}
		Design.CrestK = Interpolate(KTable, Design.SpeedKmh, [](const KRow& Row) { return Row.Crest; });
		Design.SagK = Interpolate(KTable, Design.SpeedKmh, [](const KRow& Row) { return Row.Sag; });

		// big roads are built with deep cuttings and high embankments; small ones mostly follow the ground
		Design.MaxLift = Design.Rank >= 4 ? 600.0f : Design.Rank == 3 ? 400.0f : Design.Rank == 2 ? 300.0f : Highway == "track" ? 100.0f : 200.0f;

		// a side road levels out before the junction: longer and flatter on faster roads
		Design.LandingLength = Design.SpeedKmh >= 80.0f ? 4000.0f : 2000.0f;
		Design.LandingGrade = Design.SpeedKmh >= 80.0f ? 0.03f : 0.06f;

		// measured ground (LiDAR): the heights are the real road to a few centimetres, so the rules that tidy up 30 m
		// satellite heights would only pull it away from them (the truth test found roads 4-6 m above real underpasses and
		// tunnel approaches, which dip with comfort curves, not sight-distance ones). Comfort only: K = V^2 / 395 (AASHTO),
		// grades up to half as steep again, and never more than a metre off the ground
		if (bMeasured)
		{
			const float ComfortK = Design.SpeedKmh * Design.SpeedKmh / 395.0f;
			Design.CrestK = std::min(Design.CrestK, ComfortK);
			Design.SagK = std::min(Design.SagK, ComfortK);
			Design.MaxGrade = std::min(Design.MaxGrade * 1.5f, 0.2f);
			Design.MaxGradeMountain = std::min(Design.MaxGradeMountain * 1.5f, 0.25f);
			Design.MaxLift = 100.0f;
		}
		return Design;
	}
}
