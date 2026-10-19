// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadSweep.h"
#include "RoadStacks.h"

#include <algorithm>
#include <cmath>

namespace georoads
{
	namespace
	{
		/** Cm of road between one placing of the car and the next */
		constexpr double Step = 50.0;

		/** Issues of one cause this close along a road are one issue (cm) */
		constexpr double RunLength = 500.0;

		/** A deck over another road is only as thick as the room allows: what the mesh builds (RoadMesh.cpp, the skirts), so
		 *  what the car meets under it is what the player sees */
		constexpr double TruckAndAir = 460.0, ThinestDeck = 30.0, ThickestDeck = 150.0;

		/** The road's height at a distance along it (cm) */
		struct Profile
		{
			std::vector<double> Along;
			const std::vector<float>& Heights;

			Profile(const RoadEdge& Edge) : Heights(Edge.Heights)
			{
				Along.reserve(Edge.Points.size());
				double Sum = 0.0;
				for (size_t i = 0; i < Edge.Points.size(); ++i)
				{
					Sum += i ? Distance(Edge.Points[i - 1], Edge.Points[i]) : 0.0;
					Along.push_back(Sum);
				}
			}

			double Length() const { return Along.empty() ? 0.0 : Along.back(); }

			double At(double S) const
			{
				S = std::clamp(S, 0.0, Length());
				const size_t Next = std::min<size_t>(std::upper_bound(Along.begin(), Along.end(), S) - Along.begin(), Along.size() - 1);
				const size_t Last = Next > 0 ? Next - 1 : 0;
				const double Span = Along[Next] - Along[Last];
				const double T = Span > 1.0e-6 ? (S - Along[Last]) / Span : 0.0;
				return Heights[Last] + (Heights[Next] - Heights[Last]) * T;
			}
		};

		Vec2 PointAlong(const RoadEdge& Edge, const Profile& P, double S)
		{
			S = std::clamp(S, 0.0, P.Length());
			const size_t Next = std::min<size_t>(std::upper_bound(P.Along.begin(), P.Along.end(), S) - P.Along.begin(), P.Along.size() - 1);
			const size_t Last = Next > 0 ? Next - 1 : 0;
			const double Span = P.Along[Next] - P.Along[Last];
			const double T = Span > 1.0e-6 ? (S - P.Along[Last]) / Span : 0.0;
			return Edge.Points[Last] + (Edge.Points[Next] - Edge.Points[Last]) * T;
		}

		/** Keeps the worst of each run of issues of one cause along a road */
		void Keep(std::vector<SweepIssue>& Found, const SweepIssue& Issue)
		{
			for (auto It = Found.rbegin(); It != Found.rend() && Issue.Along - It->Along < RunLength; ++It)
			{
				if (It->Kind == Issue.Kind && It->Other == Issue.Other)
				{
					const bool bWorse = Issue.Kind == SweepKind::Ceiling ? Issue.Value < It->Value : Issue.Value > It->Value;
					if (bWorse)
					{
						*It = Issue;
					}
					return;
				}
			}
			Found.push_back(Issue);
		}
	}

	SweepCar HatchbackCar()
	{
		SweepCar Car;
		Car.Name = "Hatchback 1.6";
		Car.Height = 151.0f;
		Car.Wheelbase = 243.0f;
		Car.FrontOverhang = 78.0f;
		Car.RearOverhang = 68.0f;
		Car.Clearance = 12.0f;
		Car.FrontLip = 18.0f;
		Car.RearLip = 20.0f;
		Car.MaxGrade = 0.30f;
		return Car;
	}

	SweepCar TruckCar()
	{
		SweepCar Car;
		Car.Name = "Truck 4.5 m";
		Car.Height = 450.0f;
		Car.Wheelbase = 400.0f;
		Car.FrontOverhang = 100.0f;
		Car.RearOverhang = 250.0f;
		Car.Clearance = 20.0f;
		Car.FrontLip = 40.0f;
		Car.RearLip = 40.0f;
		Car.MaxGrade = 0.25f;
		return Car;
	}

	const char* SweepKindName(SweepKind Kind)
	{
		switch (Kind)
		{
		case SweepKind::Ceiling: return "ceiling";
		case SweepKind::Belly: return "belly";
		case SweepKind::Nose: return "nose";
		case SweepKind::Tail: return "tail";
		default: return "climb";
		}
	}

	std::vector<SweepIssue> SweepRoads(const RoadGraph& Graph, const SweepCar& Car, const ParallelForFn& ParallelFor)
	{
		const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
		const ParallelForFn Run = ParallelFor ? ParallelFor : ParallelForFn(&ThreadsParallelFor);

		// what is over each road: the room between its surface and the road over it, where the two cross
		struct Over { int32_t Upper; double Along; double Gap; };
		std::vector<std::vector<Over>> Overs(NumEdges);
		{
			const std::vector<LevelPair> Pairs = FindLevelPairs(Graph, [&Graph](int32_t Edge) -> const std::vector<Vec2>& { return Graph.Edges[Edge].Points; });
			for (const LevelPair& Pair : Pairs)
			{
				const RoadEdge& Up = Graph.Edges[Pair.UpperEdge];
				const RoadEdge& Down = Graph.Edges[Pair.LowerEdge];
				if (Up.Heights.size() != Up.Points.size() || Down.Heights.size() != Down.Points.size())
				{
					continue;
				}
				const double UpperHeight = Up.Heights[Pair.UpperSegment] + (Up.Heights[Pair.UpperSegment + 1] - Up.Heights[Pair.UpperSegment]) * Pair.UpperT;
				const double LowerHeight = Down.Heights[Pair.LowerSegment] + (Down.Heights[Pair.LowerSegment + 1] - Down.Heights[Pair.LowerSegment]) * Pair.LowerT;
				double Along = 0.0;
				for (int32_t i = 0; i < Pair.LowerSegment; ++i)
				{
					Along += Distance(Down.Points[i], Down.Points[i + 1]);
				}
				Along += Distance(Down.Points[Pair.LowerSegment], Down.Points[Pair.LowerSegment + 1]) * Pair.LowerT;
				Overs[Pair.LowerEdge].push_back({ Pair.UpperEdge, Along, UpperHeight - LowerHeight });
			}
		}

		std::vector<std::vector<SweepIssue>> PerEdge(NumEdges);
		Run(NumEdges, [&](int32_t EdgeIndex)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (Edge.Points.size() < 2 || Edge.Heights.size() != Edge.Points.size())
			{
				return;
			}
			const Profile Road(Edge);
			const double Length = Road.Length();
			std::vector<SweepIssue>& Found = PerEdge[EdgeIndex];
			auto Add = [&](SweepKind Kind, double S, double Value, int32_t Other = -1)
			{
				SweepIssue Issue;
				Issue.Kind = Kind;
				Issue.Edge = EdgeIndex;
				Issue.Other = Other;
				Issue.Along = S;
				Issue.At = PointAlong(Edge, Road, S);
				Issue.Value = Value;
				Issue.Car = Car.Name;
				Keep(Found, Issue);
			};

			// the ceiling over it: what the player sees under a deck (a thin one where there is little room), against the car
			for (const Over& Above : Overs[EdgeIndex])
			{
				const double Deck = std::clamp(Above.Gap - TruckAndAir, ThinestDeck, ThickestDeck);
				const double Room = Above.Gap - Deck;
				if (Room < Car.Height)
				{
					Add(SweepKind::Ceiling, Above.Along, Room, Above.Upper);
				}
			}

			// the car slid along: rear axle at S, front axle a wheelbase on
			for (double S = 0.0; S + Car.Wheelbase <= Length + 1.0e-6; S += Step)
			{
				const double Rear = Road.At(S), Front = Road.At(S + Car.Wheelbase), Middle = Road.At(S + Car.Wheelbase * 0.5);
				const double Slope = (Front - Rear) / Car.Wheelbase;
				const double Centre = S + Car.Wheelbase * 0.5;
				if (std::abs(Slope) > Car.MaxGrade)
				{
					Add(SweepKind::Climb, Centre, std::abs(Slope));
				}
				const double Bump = Middle - (Rear + Front) * 0.5;
				if (Bump > Car.Clearance)
				{
					Add(SweepKind::Belly, Centre, Bump);
				}
				if (S + Car.Wheelbase + Car.FrontOverhang <= Length)
				{
					const double Rise = Road.At(S + Car.Wheelbase + Car.FrontOverhang) - (Front + Slope * Car.FrontOverhang);
					if (Rise > Car.FrontLip)
					{
						Add(SweepKind::Nose, S + Car.Wheelbase + Car.FrontOverhang, Rise);
					}
				}
				if (S - Car.RearOverhang >= 0.0)
				{
					const double Rise = Road.At(S - Car.RearOverhang) - (Rear - Slope * Car.RearOverhang);
					if (Rise > Car.RearLip)
					{
						Add(SweepKind::Tail, S - Car.RearOverhang, Rise);
					}
				}
			}
		});

		std::vector<SweepIssue> All;
		for (std::vector<SweepIssue>& Found : PerEdge)
		{
			All.insert(All.end(), Found.begin(), Found.end());
		}
		return All;
	}

	std::string DescribeIssue(const RoadGraph& Graph, const SweepIssue& Issue)
	{
		auto Name = [&Graph](int32_t Index)
		{
			const RoadEdge& E = Graph.Edges[Index];
			return Format("%s %s (layer %d%s%s)", E.Name.empty() ? "unnamed" : E.Name.c_str(), E.Highway.c_str(), E.Layer, E.bBridge ? ", bridge" : "", E.bTunnel ? ", tunnel" : "");
		};
		switch (Issue.Kind)
		{
		case SweepKind::Ceiling:
			return Format("%s: %s has %.1f m under %s", Issue.Car.c_str(), Name(Issue.Edge).c_str(), Issue.Value / 100.0, Name(Issue.Other).c_str());
		case SweepKind::Climb:
			return Format("%s: %s too steep to climb, %.0f%%", Issue.Car.c_str(), Name(Issue.Edge).c_str(), Issue.Value * 100.0);
		case SweepKind::Belly:
			return Format("%s: %s scrapes its belly, the road rises %.0f cm into it", Issue.Car.c_str(), Name(Issue.Edge).c_str(), Issue.Value);
		case SweepKind::Nose:
			return Format("%s: %s digs in the nose, %.0f cm", Issue.Car.c_str(), Name(Issue.Edge).c_str(), Issue.Value);
		default:
			return Format("%s: %s drags the tail, %.0f cm", Issue.Car.c_str(), Name(Issue.Edge).c_str(), Issue.Value);
		}
	}
}
