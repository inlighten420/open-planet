// Geodrive Core - GPL (see Core/README.md)
//
// A robot test driver: drives the real car (the player's own, same physics and tyres) along routes through the road
// network like a sensible fast driver, and records everything a real driver would hate: the car leaving the ground,
// jolts through the suspension, hitting something, sliding or spinning, leaving the road, getting stuck or dropping
// through the surface. Each problem is judged a map fault (it was driving sensibly) or its own error, with what's round
// it (junction, bridge end, dual carriageway, steep, corner); the report goes to Saved/RoadReports.
//
// Engine-free: the game gives it the car each frame and puts the car where it asks (a new route, or past a problem);
// everything is in the roads' own frame (cm).

#pragma once

#include "GeoCar/CarTypes.h"
#include "GeoRoads/Projection.h"
#include "GeoRoads/RoadGraph.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace geotraffic
{
	using georoads::Vec2;

	class RobotDriver
	{
	public:
		/** Traffic keeps left (South Africa, the UK...) or right: the lane driven in, the kerb parked at */
		bool bDriveLeft = true;

		/** The car as the game sees it this frame */
		struct Car
		{
			Vec2 Position;
			double Height = 0.0;
			Vec2 Forward = Vec2(1.0, 0.0);	// level, unit
			Vec2 Velocity;					// cm/s, level
			geocar::VehicleState State;
			float MaxSteerDegrees = 35.0f;
			float Wheelbase = 0.0f;			// m; 0 = a usual car's
		};

		/** PlaceCar: put the car on the road nearest a point, facing a yaw (degrees, from north towards east) */
		RobotDriver(std::shared_ptr<const georoads::RoadGraph> Roads, const georoads::Projection& Projection,
			std::function<void(const Vec2& Position, float YawDegrees)> PlaceCar);

		/** Plans the routes: "n2" (the trunk road end to end, both ways), "town" (TownRoutes random trips through town),
		 *  "all". Shard / Shards: one of several copies running at once drives every Shards-th route */
		bool Start(const std::string& Mode, int32_t TownRoutes, int32_t Shard = 0, int32_t Shards = 1);

		/** Drives one frame: the controls to put on the car (steering in road-wheel degrees). False once every route is
		 *  done (the report is ready then: Finish) */
		bool Tick(float DeltaTime, const Car& Car, float& OutSteer, float& OutThrottle, float& OutBrake);

		/** Stops; the report so far (empty if none) and its file name (robot_<time>_part<n>.txt) */
		std::string Finish(std::string& OutFileName);

		bool IsDriving() const { return RouteIndex >= 0 && RouteIndex < int32_t(Routes.size()); }

		/** One line for the screen */
		std::string StatusText() const;

		/** Told of each problem (for the log) and when a picture of one is wanted (its number) */
		std::function<void(const std::string&)> Log;
		std::function<void(int32_t Problem)> Screenshot;

		/** The time of day for the report and its file name ("2026-10-14 06:30", "20261014_063000"); set by the game */
		std::string Now, Stamp;

	private:
		struct PathPoint
		{
			Vec2 Position;
			float Height = 0.0f;
			float HalfWidth = 0.0f;
			float TargetSpeed = 0.0f;	// m/s, after braking for the corners ahead
			int32_t Edge = -1;
		};
		struct Route
		{
			std::string Name;
			std::vector<PathPoint> Points;
		};
		struct Problem
		{
			std::string Type;
			Vec2 Position;
			float Size = 0.0f;
			std::string Unit;
			float SpeedKmh = 0.0f;
			int32_t Edge = -1;
			std::string RouteName;
			std::string Verdict;	// "map fault" (driving sensibly) or "driver error"
			std::string Context;	// junction, bridge end, dual carriageway, steep, corner
		};
		/** The last second or so of driving, to judge whose fault a problem was */
		struct Moment
		{
			double Time = 0.0;
			float Overspeed = 0.0f;	// m/s above what the corner allows
			float Slip = 0.0f;		// largest tyre slip angle (degrees)
			float Offset = 0.0f;	// distance from the middle of the road as a share of its half width
			float GroundSpeed = 0.0f;	// m/s
		};

		bool PlanRoute(int32_t From, int32_t To, const std::function<double(const std::string&)>& Penalty, Route& Out) const;
		void PrepareSpeeds(Route& R) const;
		void BeginRoute(int32_t Index);
		void Recover(int32_t AheadPoints);
		void Record(const std::string& Type, const Vec2& Position, float Size, const char* Unit, float SpeedKmh, bool bScreenshot, bool bConsequence = false);
		bool WasDrivingSensibly() const;
		std::string DescribeSurroundings(const Vec2& Position) const;

		std::shared_ptr<const georoads::RoadGraph> Roads;
		georoads::Projection Projection;
		std::function<void(const Vec2&, float)> PlaceCar;

		std::vector<Route> Routes;
		int32_t RouteIndex = -1;
		int32_t Progress = 0;
		std::vector<Moment> Recent;
		double LastMapFault = -100.0;

		float Grace = 0.0f;	// seconds to ignore everything after a teleport (the car is dropping in)
		float AirTime = 0.0f;
		Vec2 TakeOff;
		float OffRoadTime = 0.0f, SpinTime = 0.0f, StuckTimer = 0.0f;
		int32_t StuckProgress = 0;

		std::vector<Problem> Problems;
		int32_t Screenshots = 0;
		double DrivenCm = 0.0;
		double DrivingSeconds = 0.0;
		Vec2 LastPosition;
		bool bHasLastPosition = false;
		std::string ReportName;
	};
}
