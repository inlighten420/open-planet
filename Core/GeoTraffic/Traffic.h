// Geodrive Core - GPL (see Core/README.md)
//
// Traffic: cars on the same physics as the player's, driving the real roads around them. Each keeps to the left lane at
// the speed limit, slows for bends, eases up before giving way to a bigger road, keeps a gap to whatever is ahead
// (other traffic or the player), eases out round parked cars and picks its way at junctions as it goes. They appear
// 150-450 m from the player, mostly ahead and on the roads they can turn into, and go beyond 700 m; a third are parked
// at the kerb, and some of those pull out as the player comes near.
//
// Every traffic car is also a road tester: launches and jolts it feels while driving calmly are reported (OnProblem).
//
// Engine-free: the game puts the cars in its world and moves them (TrafficHost); everything here is in the roads' own
// frame (cm, x north, y east), as the road graph is.

#pragma once

#include "GeoCar/CarTypes.h"
#include "GeoRoads/RoadGraph.h"

#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace geotraffic
{
	using georoads::Vec2;

	/** A traffic car as the game sees it now (the roads' frame, cm) */
	struct CarPose
	{
		Vec2 Position;
		double Height = 0.0;
		Vec2 Forward = Vec2(1.0, 0.0);	// level, unit
		float UpZ = 1.0f;				// its up axis' height: below 0.3 it's on its side or roof
		Vec2 Velocity;					// cm/s
		geocar::VehicleState State;
		float Mass = 1200.0f;
		float MaxSteerDegrees = 35.0f;
	};

	/** What the game does for the traffic */
	class TrafficHost
	{
	public:
		virtual ~TrafficHost() = default;

		/** Puts a car in the world (Body: geocar::CarBody; Paint: one of NumPaints), facing Heading, already going at
		 *  Speed (m/s) along it; parked: held on its brakes. Returns its id, or -1 */
		virtual int32_t SpawnCar(const Vec2& Position, double Height, const Vec2& Heading, int32_t Body, int32_t Paint, float Speed, bool bParked) = 0;
		virtual void RemoveCar(int32_t Id) = 0;

		/** Where a car is; false if it's gone */
		virtual bool GetCar(int32_t Id, CarPose& Out) const = 0;

		/** Its controls: steering (road-wheel degrees, + right), pedals 0-1 */
		virtual void DriveCar(int32_t Id, float SteerDegrees, float Throttle, float Brake) = 0;
	};

	/** The colours traffic comes in (linear RGB) */
	constexpr int32_t NumPaints = 10;
	void PaintColour(int32_t Paint, float& R, float& G, float& B);

	/** The player, for where traffic goes and to keep a gap to */
	struct PlayerPose
	{
		Vec2 Position;
		Vec2 Forward = Vec2(1.0, 0.0);
		Vec2 Velocity;	// cm/s
	};

	/** A road problem a traffic car felt */
	struct Problem
	{
		std::string Type;	// "launch (left the ground)", "jolt", "fell through the road"
		Vec2 Position;
		std::string Road, Highway;
		float SpeedKmh = 0.0f;
		float Size = 0.0f;	// seconds in the air, times the static load, metres under the road
	};

	class Traffic
	{
	public:
		/** Traffic keeps left (South Africa, the UK...) or right: the lane driven in, the kerb parked at */
		bool bDriveLeft = true;

		Traffic(std::shared_ptr<const georoads::RoadGraph> Roads, TrafficHost& Host, uint32_t Seed);
		~Traffic();

		/** Keeps Wanted cars round the player and drives them one frame */
		void Tick(float DeltaTime, const PlayerPose& Player, int32_t Wanted);

		/** The next piece of the world was built: carries on with its roads (the cars keep driving). Shift: what to add
		 *  to a position in the old roads' frame to have it in the new one's */
		void SetRoads(std::shared_ptr<const georoads::RoadGraph> NewRoads, const Vec2& Shift);
		const std::shared_ptr<const georoads::RoadGraph>& GetRoads() const { return Roads; }

		/** Removes every traffic car */
		void Clear();

		int32_t NumCars() const { return int32_t(Cars.size()); }
		int32_t NumProblems() const { return int32_t(Problems.size()); }

		/** Times two traffic cars have touched (each meeting once): how well they keep out of each other's way */
		int32_t NumBumps() const { return Bumps; }

		/** One line for the screen (empty with no traffic) */
		std::string StatusText() const;

		/** Each car: speed / planned speed (km/h) and distance from its lane (m) */
		std::string DebugLine() const;

		/** Told of each road problem, once per spot */
		std::function<void(const Problem&)> OnProblem;

		/** Told of moves to the next piece, for the log */
		std::function<void(const std::string&)> Log;

	private:
		struct Point
		{
			Vec2 Position;
			float Height = 0.0f;
			float TargetSpeed = 0.0f;	// m/s
			int32_t Edge = -1;
			bool bGiveWay = false;		// the end of a smaller road at a junction with a bigger one
		};
		struct Car
		{
			int32_t Id = -1;
			std::vector<Point> Path;
			int32_t Progress = 0;
			int32_t LastEdge = -1;
			bool bLastForward = true;
			float StuckTime = 0.0f, FlippedTime = 0.0f, AirTime = 0.0f;
			float Grace = 2.0f;	// seconds after appearing before it's judged (it settles onto its wheels)
			Vec2 TakeOff;
			float LastSpeed = 0.0f, LastTarget = 0.0f, LastOffLane = 0.0f;
			bool bParked = false, bWillPullOut = false;
			bool bGone = false;
			bool bRollingLogged = false;
		};

		/** Someone moving to keep out of the way of (the player or a traffic car): where, how fast, and (traffic) the
		 *  road it will drive next, every 2 m from where it is */
		struct Mover
		{
			int32_t Id = -1;	// (-1: the player)
			Vec2 Position, Velocity, Forward;
			float Speed = 0.0f;	// m/s
			std::vector<Vec2> Ahead;
		};

		bool Spawn(const PlayerPose& Player, bool bParked);
		void UpdateNetwork(const Vec2& Player);
		void RoadAhead(const Car& C, const Vec2& Here, double Reach, std::vector<Vec2>& Out) const;
		void Drive(Car& C, float DeltaTime, const CarPose& Pose, const std::vector<Mover>& Others, const std::vector<Vec2>& Parked);
		void AppendEdge(Car& C, int32_t Edge, bool bForward);
		bool Extend(Car& C);
		void PlanSpeeds(Car& C) const;
		void Report(const std::string& Type, const Vec2& Position, int32_t Edge, float SpeedKmh, float Size);
		double Random01() { return std::uniform_real_distribution<double>(0.0, 1.0)(Rng); }
		int32_t RandomBelow(int32_t N) { return N <= 1 ? 0 : std::uniform_int_distribution<int32_t>(0, N - 1)(Rng); }

		std::shared_ptr<const georoads::RoadGraph> Roads;
		TrafficHost& Host;
		std::mt19937 Rng;
		std::vector<Car> Cars;
		std::map<int32_t, CarPose> Poses;	// this frame's
		float SpawnCooldown = 0.0f;
		std::map<int32_t, float> NetworkDistance;	// edge -> cm along the roads from the player's
		int32_t PlayerEdge = -1;
		float NetworkTimer = 0.0f;
		double Range = 1.0;	// how far out traffic goes, x the distances for 20 cars
		std::vector<std::pair<std::string, Vec2>> Problems;
		int32_t Bumps = 0;
		std::set<std::pair<int32_t, int32_t>> Touching;	// (the pairs touching now)
	};
}
