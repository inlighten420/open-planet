// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// The drivable road network the way the game builds it from OSM: roads (edges) between junctions and ends (nodes),
// each road a line of points in world space (cm, x east / y north of the area's origin) with its width, class and
// what it is (a bridge, a tunnel...). Plain C++: shared by Unreal, the tools and other engines.

#pragma once

#include <cmath>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace georoads
{
	/** A point or direction on the ground (cm) */
	struct Vec2
	{
		double X = 0.0;
		double Y = 0.0;

		Vec2() = default;
		Vec2(double InX, double InY) : X(InX), Y(InY) {}

		Vec2 operator+(const Vec2& O) const { return { X + O.X, Y + O.Y }; }
		Vec2 operator-(const Vec2& O) const { return { X - O.X, Y - O.Y }; }
		Vec2 operator*(double S) const { return { X * S, Y * S }; }
		double SizeSquared() const { return X * X + Y * Y; }
		double Size() const { return std::sqrt(SizeSquared()); }

		/** This direction a unit long, or zero if it is too short to have one */
		Vec2 Normal() const
		{
			const double S = SizeSquared();
			return S > 1.0e-8 ? *this * (1.0 / std::sqrt(S)) : Vec2();
		}
	};

	inline double Dot(const Vec2& A, const Vec2& B) { return A.X * B.X + A.Y * B.Y; }
	inline double Distance(const Vec2& A, const Vec2& B) { return (B - A).Size(); }

	/** What a road is made of */
	enum class Surface : uint8_t
	{
		Asphalt,
		Paved,		// paving stones, sett, cobblestone
		Loose,		// compacted, gravel, dirt, ground, sand
	};

	/** A drivable road between two nodes */
	struct RoadEdge
	{
		int32_t StartNode = -1;
		int32_t EndNode = -1;
		std::vector<Vec2> Points;

		/** Height above sea level of each point (cm), filled by the road height solver */
		std::vector<float> Heights;
		float HalfWidth = 300.0f;	// cm

		/** Lanes (both directions on a two-way road) and the shoulder beyond them each side (cm; left and right going
		 *  from the first point to the last) */
		int32_t Lanes = 2;
		float ShoulderLeft = 0.0f;
		float ShoulderRight = 0.0f;

		Surface Kind = Surface::Asphalt;
		int32_t Layer = 0;
		bool bOneway = false;
		bool bBridge = false;

		/** Its ground is measured (LiDAR under most of it): its heights are trusted, the design rules only keep it comfortable */
		bool bMeasured = false;

		/** An open gallery over it (tunnel=avalanche_protector) */
		bool bGallery = false;

		/** A tunnel through the ground: its heights run straight between its portals */
		bool bTunnel = false;

		/** Part of a roundabout's ring (junction=roundabout): every road joining it levels out before it */
		bool bRoundabout = false;

		/** The OSM highway class ("trunk", "residential", "motorway_link"...) */
		std::string Highway;

		/** Speed limit from the maxspeed tag (km/h), or 0 when untagged */
		float SpeedKmh = 0.0f;

		/** Street name (or road number) */
		std::string Name;

		/** The OSM way this piece is from and its number along it */
		int64_t WayId = 0;
		int32_t WayPart = 0;

		/** A ramp given a longer line than the map's to fit its rules (RerouteRamps): how far its join moved (cm, 0: as mapped) */
		float Rerouted = 0.0f;

		/** Where a road carries straight on as a wider or narrower one (no junction), the two ease into each other instead
		 *  of stepping: at this end it's wider (+) or narrower by this much (cm of half width, and of each shoulder),
		 *  fading out over Length (cm) */
		struct Taper
		{
			float HalfWidth = 0.0f;
			float ShoulderLeft = 0.0f;
			float ShoulderRight = 0.0f;
			float Length = 0.0f;
		};
		Taper StartTaper, EndTaper;

		/** Half width and shoulders at a distance S along the road (cm), of a road Length long, with its tapers */
		float HalfWidthAt(double S, double Length) const { return HalfWidth + Tapered(S, Length, StartTaper.HalfWidth, EndTaper.HalfWidth); }
		float ShoulderLeftAt(double S, double Length) const { return ShoulderLeft + Tapered(S, Length, StartTaper.ShoulderLeft, EndTaper.ShoulderLeft); }
		float ShoulderRightAt(double S, double Length) const { return ShoulderRight + Tapered(S, Length, StartTaper.ShoulderRight, EndTaper.ShoulderRight); }

		/** The widest it gets (cm) */
		float MaxHalfWidth() const
		{
			const float Wider = StartTaper.HalfWidth > EndTaper.HalfWidth ? StartTaper.HalfWidth : EndTaper.HalfWidth;
			return HalfWidth + (Wider > 0.0f ? Wider : 0.0f);
		}

	private:

		static float SmoothStep(float A, float B, float X)
		{
			if (X < A)
			{
				return 0.0f;
			}
			if (X >= B)
			{
				return 1.0f;
			}
			const float T = (X - A) / (B - A);
			return T * T * (3.0f - 2.0f * T);
		}

		float Tapered(double S, double Length, float AtStart, float AtEnd) const
		{
			const float FromStart = StartTaper.Length > 0.0f ? 1.0f - SmoothStep(0.0f, StartTaper.Length, static_cast<float>(S)) : 0.0f;
			const float FromEnd = EndTaper.Length > 0.0f ? 1.0f - SmoothStep(0.0f, EndTaper.Length, static_cast<float>(Length - S)) : 0.0f;
			return AtStart * FromStart + AtEnd * FromEnd;
		}
	};

	struct RoadNode
	{
		Vec2 Position;
		int32_t Layer = 0;
		std::vector<int32_t> Edges;

		/** Height above sea level (cm), filled by the road height solver */
		float Height = 0.0f;
	};

	struct RoadGraph
	{
		std::vector<RoadNode> Nodes;
		std::vector<RoadEdge> Edges;
	};

	/** Runs Body(i) for every i in [0, Count), maybe on several threads at once (each i on its own: no order) */
	using ParallelForFn = std::function<void(int32_t Count, const std::function<void(int32_t)>& Body)>;

	/** A ParallelFor for hosts without their own job system: a pool of threads made once and kept (making threads for
	 *  every call costs seconds in the road solver, which calls it thousands of times). One job at a time: a call made
	 *  while it is busy, or from inside a job, runs on the calling thread. */
	void ThreadsParallelFor(int32_t Count, const std::function<void(int32_t)>& Body);

	/** Sets up that pool before its first use: how many threads (0 = one per core) and what each runs first (to lower
	 *  its priority, so a world being built while driving leaves the game its share) */
	void ConfigureThreadsParallelFor(int32_t Threads, std::function<void()> OnWorkerStart);

	/** A line for the host's log */
	using LogFn = std::function<void(const std::string& Line)>;

	/** printf into a std::string */
	std::string Format(const char* Pattern, ...);

	/** Starts with / ends with, for road classes ("motorway_link" starts with "motorway") */
	inline bool StartsWith(const std::string& S, const char* Prefix)
	{
		const size_t N = std::char_traits<char>::length(Prefix);
		return S.size() >= N && S.compare(0, N, Prefix) == 0;
	}
	inline bool EndsWith(const std::string& S, const char* Suffix)
	{
		const size_t N = std::char_traits<char>::length(Suffix);
		return S.size() >= N && S.compare(S.size() - N, N, Suffix) == 0;
	}
}
