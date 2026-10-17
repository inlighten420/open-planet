// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// How a road of a given class and speed is allowed to be shaped, the way a road engineer would design it: how steep
// it may climb, how gently crests and dips must be rounded, and how a side road levels out before it meets a bigger
// road. Numbers follow the SANRAL Geometric Design Guide and the CSIR Red Book; see docs/ROAD_RESEARCH.md.

#pragma once

#include "RoadGraph.h"

#include <string>

namespace georoads
{
	struct RoadDesign
	{
		float SpeedKmh = 60.0f;

		/** Steepest grade (fraction, 0.06 = 6%) in flat/rolling country, and where the land itself is mountainous */
		float MaxGrade = 0.08f;
		float MaxGradeMountain = 0.10f;

		/** Vertical curve K values (m of road per 1% change of grade): the sharpest crest and dip allowed */
		float CrestK = 11.0f;
		float SagK = 18.0f;

		/** How far (cm) the road may sit above or below the measured ground to obey the rules (embankments and cuttings) */
		float MaxLift = 250.0f;

		/** A side road's approach to a bigger road: this long (cm) at no more than this grade */
		float LandingLength = 2000.0f;
		float LandingGrade = 0.03f;

		/** Bigger roads keep their shape at junctions: motorway/trunk 4, primary/secondary 3, tertiary 2, others 1 */
		int32_t Rank = 1;
	};

	/** The design speed (km/h) from an OSM maxspeed tag ("80", "60 mph", "ZA:urban"), or 0 if it can't be read */
	float ParseMaxSpeed(const std::string& Value);

	/** The usual design speed (km/h) of a road class when it has no maxspeed tag (South African defaults) */
	float DefaultSpeed(const std::string& Highway);

	/** Motorway/trunk 4, primary/secondary 3, tertiary 2, others 1 */
	int32_t RoadRank(const std::string& Highway);

	/** The rules for one road. bUK: UK roads always know their limit (tag or national limits), so it alone decides */
	RoadDesign DesignFor(const std::string& Highway, float SpeedKmh, bool bMeasured, bool bUK);
	inline RoadDesign DesignFor(const RoadEdge& Edge, bool bUK) { return DesignFor(Edge.Highway, Edge.SpeedKmh, Edge.bMeasured, bUK); }
}
