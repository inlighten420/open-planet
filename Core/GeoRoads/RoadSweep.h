// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The pretend drive: a car-shaped box slid along every road of a solved network, no driver and no physics. At each step
// the car stands with its axles on the road, and the road is asked what a car would meet: a ceiling too low, a belly
// that scrapes on a crest, a nose or tail that digs into a dip, a climb too steep for it. What it finds is a list of
// places, each with its cause, for the road solver to be given as a rule (or for a person to look at).

#pragma once

#include "RoadGraph.h"

#include <string>
#include <vector>

namespace georoads
{
	/** A car as a box on four wheels (all in cm) */
	struct SweepCar
	{
		std::string Name;
		float Height = 150.0f;
		float Wheelbase = 240.0f;
		float FrontOverhang = 80.0f, RearOverhang = 70.0f;
		float Clearance = 12.0f;		// under the middle of the car
		float FrontLip = 18.0f, RearLip = 20.0f;	// under the bumpers: how far the road may rise above the line of the wheels there
		float MaxGrade = 0.30f;			// what it can climb or hold on over its wheelbase (rise over run)
	};

	/** The smallest car the game has: the Hatchback 1.6 (after a Suzuki Swift Sport) */
	SweepCar HatchbackCar();

	/** The tallest vehicle a road is made for: a truck at the legal 4.5 m */
	SweepCar TruckCar();

	enum class SweepKind : uint8_t { Ceiling, Belly, Nose, Tail, Climb };

	struct SweepIssue
	{
		SweepKind Kind = SweepKind::Ceiling;
		int32_t Edge = -1;
		int32_t Other = -1;		// (a ceiling: the road over it)
		Vec2 At;
		double Along = 0.0;		// cm from the road's first point
		double Value = 0.0;		// Ceiling: the room (cm); Belly, Nose, Tail: how much the road rises into the car (cm); Climb: the grade
		std::string Car;
	};

	const char* SweepKindName(SweepKind Kind);

	/** Every place the car meets the road, the worst of each run of them (a run: the same cause within 5 m of road) */
	std::vector<SweepIssue> SweepRoads(const RoadGraph& Graph, const SweepCar& Car, const ParallelForFn& ParallelFor);

	/** What an issue says, for a log or a page */
	std::string DescribeIssue(const RoadGraph& Graph, const SweepIssue& Issue);
}
