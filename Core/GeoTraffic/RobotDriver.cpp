// Geodrive Core - GPL (see Core/README.md)

#include "RobotDriver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <queue>
#include <random>
#include <set>

namespace geotraffic
{
	using georoads::RoadEdge;
	using georoads::RoadGraph;

	namespace robot
	{
		/** Car wheelbase (m), for the steering geometry, when the game doesn't say */
		constexpr float RobotWheelbase = 2.64f;

		/** Cornering and braking the robot allows itself (m/s^2): brisk, but well inside the grip of an everyday car on road
		 *  tyres, and of a loaded bakkie on all-terrains (6 had the bakkie sliding through town corners) */
		constexpr float RobotCornerGrip = 4.5f;
		constexpr float RobotBraking = 5.0f;

		/** Problem thresholds */
		constexpr float MinAirTime = 0.12f;			// s with no wheel on the ground
		constexpr float JoltLoad = 9000.0f;			// N on one wheel (about three times its normal load)
		constexpr float HitDeceleration = 20.0f;	// m/s^2 of sudden slowing (beyond what the brakes can do)
		constexpr float OffRoadMargin = 300.0f;		// cm beyond the road's edge
		constexpr float SpinAngle = 40.0f;			// degrees between the car and the road
		constexpr float FallDepth = 500.0f;			// cm below the road
		constexpr double RepeatDistance = 3000.0;	// cm: the same problem closer than this is one problem
		constexpr int32_t MaxScreenshots = 30;

		constexpr double Pi = 3.14159265358979;

		bool RobotStartsWith(const std::string& S, const char* Prefix) { return S.rfind(Prefix, 0) == 0; }
		bool EndsWith(const std::string& S, const char* Suffix)
		{
			const size_t N = std::char_traits<char>::length(Suffix);
			return S.size() >= N && S.compare(S.size() - N, N, Suffix) == 0;
		}

		/** Typical speed per road class (m/s); roads the robot doesn't drive return 0 */
		float RobotRoadSpeed(const std::string& Highway)
		{
			if (RobotStartsWith(Highway, "motorway") || Highway == "trunk") { return 33.0f; }
			if (Highway == "primary") { return 28.0f; }
			if (Highway == "secondary") { return 22.0f; }
			if (Highway == "tertiary" || EndsWith(Highway, "_link")) { return 19.0f; }
			if (Highway == "residential" || Highway == "unclassified" || Highway == "road" || Highway == "living_street") { return 16.0f; }
			return 0.0f;
		}

		double RobotEdgeLength(const RoadEdge& Edge)
		{
			double Length = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				Length += georoads::Distance(Edge.Points[i], Edge.Points[i + 1]);
			}
			return Length;
		}

		int32_t RobotNearestEdge(const RoadGraph& Graph, const Vec2& P, double Reach)
		{
			int32_t Best = -1;
			double BestDistance = Reach * Reach;
			for (size_t e = 0; e < Graph.Edges.size(); ++e)
			{
				const std::vector<Vec2>& Points = Graph.Edges[e].Points;
				for (size_t i = 0; i + 1 < Points.size(); ++i)
				{
					const Vec2 A = Points[i], AB = Points[i + 1] - A;
					const double Length2 = AB.SizeSquared();
					const double T = Length2 > 1.0 ? std::clamp(georoads::Dot(P - A, AB) / Length2, 0.0, 1.0) : 0.0;
					const double D = (A + AB * T - P).SizeSquared();
					if (D < BestDistance)
					{
						BestDistance = D;
						Best = int32_t(e);
					}
				}
			}
			return Best;
		}

		template <typename... Args>
		std::string Format(const char* Pattern, Args... Values)
		{
			char Buffer[1024];
			std::snprintf(Buffer, sizeof(Buffer), Pattern, Values...);
			return Buffer;
		}

		float Yaw(const Vec2& Direction) { return float(std::atan2(Direction.Y, Direction.X) * 180.0 / Pi); }
	}
	using namespace robot;

	RobotDriver::RobotDriver(std::shared_ptr<const RoadGraph> InRoads, const georoads::Projection& InProjection, std::function<void(const Vec2&, float)> InPlaceCar)
		: Roads(std::move(InRoads))
		, Projection(InProjection)
		, PlaceCar(std::move(InPlaceCar))
	{
	}

	bool RobotDriver::PlanRoute(int32_t From, int32_t To, const std::function<double(const std::string&)>& Penalty, Route& Out) const
	{
		const RoadGraph& Graph = *Roads;
		// Dijkstra over nodes; one-way roads only forwards
		std::vector<double> Cost(Graph.Nodes.size(), 1e300);
		std::vector<int32_t> ViaEdge(Graph.Nodes.size(), -1);
		std::vector<char> ViaForward(Graph.Nodes.size(), 1);
		using Entry = std::pair<double, int32_t>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> Open;
		Cost[size_t(From)] = 0.0;
		Open.push({ 0.0, From });
		while (!Open.empty())
		{
			const Entry Current = Open.top();
			Open.pop();
			if (Current.first > Cost[size_t(Current.second)])
			{
				continue;
			}
			if (Current.second == To)
			{
				break;
			}
			for (const int32_t EdgeIndex : Graph.Nodes[size_t(Current.second)].Edges)
			{
				const RoadEdge& Edge = Graph.Edges[size_t(EdgeIndex)];
				const float Speed = RobotRoadSpeed(Edge.Highway);
				if (Speed <= 0.0f)
				{
					continue;
				}
				const bool bForward = Edge.StartNode == Current.second;
				if (!bForward && Edge.bOneway)
				{
					continue;
				}
				const int32_t Next = bForward ? Edge.EndNode : Edge.StartNode;
				const double NextCost = Current.first + RobotEdgeLength(Edge) / (Speed * 100.0) * Penalty(Edge.Highway);
				if (NextCost < Cost[size_t(Next)])
				{
					Cost[size_t(Next)] = NextCost;
					ViaEdge[size_t(Next)] = EdgeIndex;
					ViaForward[size_t(Next)] = bForward ? 1 : 0;
					Open.push({ NextCost, Next });
				}
			}
		}
		if (ViaEdge[size_t(To)] < 0)
		{
			return false;
		}

		// walk back, then lay the roads end to end in driving order
		std::vector<std::pair<int32_t, bool>> Steps;
		for (int32_t Node = To; Node != From && ViaEdge[size_t(Node)] >= 0;)
		{
			Steps.insert(Steps.begin(), { ViaEdge[size_t(Node)], ViaForward[size_t(Node)] != 0 });
			const RoadEdge& Edge = Graph.Edges[size_t(ViaEdge[size_t(Node)])];
			Node = ViaForward[size_t(Node)] ? Edge.StartNode : Edge.EndNode;
		}
		for (const auto& [EdgeIndex, bForward] : Steps)
		{
			const RoadEdge& Edge = Graph.Edges[size_t(EdgeIndex)];
			const float Speed = RobotRoadSpeed(Edge.Highway);
			const int32_t N = int32_t(Edge.Points.size());

			// in the kerb-side lane going this way (the leftmost keeping left, the rightmost keeping right), as traffic
			// does: its middle, measured from the centreline towards the kerb
			const float LeftShoulder = bForward ? Edge.ShoulderLeft : Edge.ShoulderRight;
			const float RightShoulder = bForward ? Edge.ShoulderRight : Edge.ShoulderLeft;
			const float TravelLeft = Edge.HalfWidth - LeftShoulder, TravelRight = Edge.HalfWidth - RightShoulder;
			const float LaneWidth = (TravelLeft + TravelRight) / float(std::max(Edge.Lanes, 1));
			const float Offset = std::max(0.0f, (bDriveLeft ? TravelLeft : TravelRight) - LaneWidth * 0.5f);	// (to the kerb side)
			for (int32_t k = 0; k < N; ++k)
			{
				const int32_t i = bForward ? k : N - 1 - k;
				const int32_t Prev = bForward ? std::max(i - 1, 0) : std::min(i + 1, N - 1);
				const int32_t Next = bForward ? std::min(i + 1, N - 1) : std::max(i - 1, 0);
				const Vec2 Direction = (Edge.Points[size_t(Next)] - Edge.Points[size_t(Prev)]).Normal();
				const Vec2 Left(Direction.Y, -Direction.X);	// north = +X, east = +Y: facing +X, the left is -Y
				const Vec2 At = Edge.Points[size_t(i)] + Left * (bDriveLeft ? Offset : -Offset);
				if (!Out.Points.empty() && (Out.Points.back().Position - At).SizeSquared() < 400.0)
				{
					continue;	// the joining point is already there
				}
				PathPoint Point;
				Point.Position = At;
				Point.Height = size_t(i) < Edge.Heights.size() ? Edge.Heights[size_t(i)] : 0.0f;
				Point.HalfWidth = Edge.HalfWidth;
				Point.TargetSpeed = Speed;
				Point.Edge = EdgeIndex;
				Out.Points.push_back(Point);
			}
		}
		return Out.Points.size() >= 2;
	}

	void RobotDriver::PrepareSpeeds(Route& R) const
	{
		std::vector<PathPoint>& P = R.Points;
		const int32_t N = int32_t(P.size());
		// slow enough for each corner: the heading change over ~10 m either side gives the bend's radius
		for (int32_t i = 1; i + 1 < N; ++i)
		{
			int32_t Back = i, Ahead = i;
			while (Back > 0 && georoads::Distance(P[size_t(Back)].Position, P[size_t(i)].Position) < 1000.0) { --Back; }
			while (Ahead + 1 < N && georoads::Distance(P[size_t(Ahead)].Position, P[size_t(i)].Position) < 1000.0) { ++Ahead; }
			const Vec2 In = (P[size_t(i)].Position - P[size_t(Back)].Position).Normal();
			const Vec2 Out = (P[size_t(Ahead)].Position - P[size_t(i)].Position).Normal();
			const double Turn = std::acos(std::clamp(georoads::Dot(In, Out), -1.0, 1.0));
			const double Span = (georoads::Distance(P[size_t(Back)].Position, P[size_t(i)].Position) + georoads::Distance(P[size_t(i)].Position, P[size_t(Ahead)].Position)) / 100.0;
			if (Turn > 1.0e-3 && Span > 1.0)
			{
				P[size_t(i)].TargetSpeed = std::min(P[size_t(i)].TargetSpeed, float(std::sqrt(RobotCornerGrip * Span / Turn)));
			}
			P[size_t(i)].TargetSpeed = std::max(P[size_t(i)].TargetSpeed, 4.0f);
		}
		// brake in time: work backwards so the car can always slow down for what's coming
		for (int32_t i = N - 2; i >= 0; --i)
		{
			const float Distance = float(georoads::Distance(P[size_t(i)].Position, P[size_t(i) + 1].Position) / 100.0);
			P[size_t(i)].TargetSpeed = std::min(P[size_t(i)].TargetSpeed, std::sqrt(P[size_t(i) + 1].TargetSpeed * P[size_t(i) + 1].TargetSpeed + 2.0f * RobotBraking * Distance));
		}
		P.back().TargetSpeed = 0.0f;
	}

	bool RobotDriver::Start(const std::string& Mode, int32_t TownRoutes, int32_t Shard, int32_t Shards)
	{
		if (!Roads || Roads->Nodes.empty())
		{
			return false;
		}
		const RoadGraph& Graph = *Roads;
		Routes.clear();
		Problems.clear();

		if (Mode == "n2" || Mode == "all")
		{
			// the far west and far east ends of the trunk roads, then the fastest way between them sticking to trunk
			// roads. An end can be a one-way carriageway leading out of the area, so a few ends are tried each way.
			auto Main = [](const std::string& H) { return H == "trunk" || H == "motorway"; };
			std::vector<int32_t> Ends;
			for (const RoadEdge& Edge : Graph.Edges)
			{
				if (Main(Edge.Highway))
				{
					for (const int32_t Node : { Edge.StartNode, Edge.EndNode })
					{
						if (std::find(Ends.begin(), Ends.end(), Node) == Ends.end())
						{
							Ends.push_back(Node);
						}
					}
				}
			}
			// only the main connected stretch: where the area's edge cuts a road it leaves short stubs leading nowhere
			{
				std::map<int32_t, int32_t> Component;
				std::vector<int32_t> Sizes;
				for (const int32_t Start : Ends)
				{
					if (Component.count(Start))
					{
						continue;
					}
					const int32_t Id = int32_t(Sizes.size());
					Sizes.push_back(0);
					std::vector<int32_t> Stack = { Start };
					while (!Stack.empty())
					{
						const int32_t Node = Stack.back();
						Stack.pop_back();
						if (Component.count(Node))
						{
							continue;
						}
						Component[Node] = Id;
						++Sizes[size_t(Id)];
						for (const int32_t EdgeIndex : Graph.Nodes[size_t(Node)].Edges)
						{
							const RoadEdge& Edge = Graph.Edges[size_t(EdgeIndex)];
							if (Main(Edge.Highway))
							{
								Stack.push_back(Edge.StartNode == Node ? Edge.EndNode : Edge.StartNode);
							}
						}
					}
				}
				int32_t Largest = 0;
				for (int32_t i = 1; i < int32_t(Sizes.size()); ++i)
				{
					Largest = Sizes[size_t(i)] > Sizes[size_t(Largest)] ? i : Largest;
				}
				Ends.erase(std::remove_if(Ends.begin(), Ends.end(), [&](int32_t Node)
				{
					const auto Found = Component.find(Node);
					return Found == Component.end() || Found->second != Largest;
				}), Ends.end());
			}
			std::sort(Ends.begin(), Ends.end(), [&Graph](int32_t A, int32_t B) { return Graph.Nodes[size_t(A)].Position.Y < Graph.Nodes[size_t(B)].Position.Y; });
			auto MainRoads = [](const std::string& H) { return (RobotStartsWith(H, "trunk") || RobotStartsWith(H, "motorway")) ? 1.0 : 20.0; };
			auto Drive = [&](bool bEastbound)
			{
				const int32_t Tries = std::min(12, int32_t(Ends.size()));
				for (int32_t i = 0; i < Tries; ++i)
				{
					for (int32_t j = 0; j < Tries; ++j)
					{
						const int32_t From = bEastbound ? Ends[size_t(i)] : Ends[Ends.size() - 1 - size_t(i)];
						const int32_t To = bEastbound ? Ends[Ends.size() - 1 - size_t(j)] : Ends[size_t(j)];
						Route R;
						R.Name = bEastbound ? "N2 eastbound" : "N2 westbound";
						if (PlanRoute(From, To, MainRoads, R))
						{
							Routes.push_back(std::move(R));
							return;
						}
					}
				}
				if (Log)
				{
					Log(std::string("[Robot] no route found along the N2 (") + (bEastbound ? "eastbound" : "westbound") + ")");
				}
			};
			if (Ends.size() >= 2)
			{
				Drive(true);
				Drive(false);
			}
		}

		if (Mode == "town" || Mode == "all")
		{
			// random trips of 2-6 km between ordinary roads (fixed seed, so a run can be repeated after a fix)
			std::vector<int32_t> Candidates;
			for (size_t NodeIndex = 0; NodeIndex < Graph.Nodes.size(); ++NodeIndex)
			{
				for (const int32_t EdgeIndex : Graph.Nodes[NodeIndex].Edges)
				{
					if (RobotRoadSpeed(Graph.Edges[size_t(EdgeIndex)].Highway) > 0.0f && Graph.Nodes[NodeIndex].Edges.size() >= 3)
					{
						Candidates.push_back(int32_t(NodeIndex));
						break;
					}
				}
			}
			std::mt19937 Random(1234);
			auto Pick = [&]() { return Candidates[std::uniform_int_distribution<size_t>(0, Candidates.size() - 1)(Random)]; };
			auto AnyRoad = [](const std::string&) { return 1.0; };
			const int32_t Wanted = TownRoutes + (Mode == "all" ? 2 : 0);
			for (int32_t Attempt = 0; Attempt < TownRoutes * 20 && int32_t(Routes.size()) < Wanted && Candidates.size() > 1; ++Attempt)
			{
				const int32_t From = Pick();
				const int32_t To = Pick();
				const double Distance = georoads::Distance(Graph.Nodes[size_t(From)].Position, Graph.Nodes[size_t(To)].Position);
				if (Distance < 200000.0 || Distance > 600000.0)
				{
					continue;
				}
				Route R;
				R.Name = "town route " + std::to_string(Routes.size() + 1);
				if (PlanRoute(From, To, AnyRoad, R))
				{
					Routes.push_back(std::move(R));
				}
			}
		}

		// running as one of several copies at once: this one drives every Shards-th route
		if (Shards > 1)
		{
			std::vector<Route> Mine;
			for (size_t i = 0; i < Routes.size(); ++i)
			{
				if (int32_t(i % size_t(Shards)) == Shard)
				{
					Mine.push_back(std::move(Routes[i]));
				}
			}
			Routes = std::move(Mine);
		}
		for (Route& R : Routes)
		{
			PrepareSpeeds(R);
		}
		if (Routes.empty())
		{
			return false;
		}
		double TotalKm = 0.0;
		for (const Route& R : Routes)
		{
			for (size_t i = 0; i + 1 < R.Points.size(); ++i)
			{
				TotalKm += georoads::Distance(R.Points[i].Position, R.Points[i + 1].Position) / 100000.0;
			}
		}
		ReportName = "robot_" + Stamp + "_part" + std::to_string(Shard + 1) + ".txt";
		if (Log)
		{
			Log(Format("[Robot] %d routes, %.1f km planned (%s)", int(Routes.size()), TotalKm, Mode.c_str()));
		}
		DrivenCm = 0.0;
		DrivingSeconds = 0.0;
		Screenshots = 0;
		BeginRoute(0);
		return true;
	}

	void RobotDriver::BeginRoute(int32_t Index)
	{
		RouteIndex = Index;
		const Route& R = Routes[size_t(Index)];
		// a few points in, clear of the junction it starts from (put on the nearest road there, the car could land on the
		// crossing road)
		Progress = std::min(3, int32_t(R.Points.size()) - 2);
		PlaceCar(R.Points[size_t(Progress)].Position, Yaw((R.Points[size_t(Progress) + 1].Position - R.Points[size_t(Progress)].Position).Normal()));
		Grace = 2.0f;
		StuckTimer = 0.0f;
		Recent.clear();
		StuckProgress = Progress;
		bHasLastPosition = false;
		if (Log)
		{
			Log(Format("[Robot] starting %s (%d points)", R.Name.c_str(), int(R.Points.size())));
		}
	}

	void RobotDriver::Recover(int32_t AheadPoints)
	{
		const Route& R = Routes[size_t(RouteIndex)];
		Progress = std::min(Progress + AheadPoints, int32_t(R.Points.size()) - 2);
		PlaceCar(R.Points[size_t(Progress)].Position, Yaw((R.Points[size_t(Progress) + 1].Position - R.Points[size_t(Progress)].Position).Normal()));
		Grace = 2.0f;
		StuckTimer = 0.0f;
		Recent.clear();
		StuckProgress = Progress;
		bHasLastPosition = false;
		OffRoadTime = SpinTime = AirTime = 0.0f;
	}

	std::string RobotDriver::DescribeSurroundings(const Vec2& Position) const
	{
		const RoadGraph& Graph = *Roads;
		std::vector<std::string> Tags;
		// a junction within 25 m
		for (const georoads::RoadNode& Node : Graph.Nodes)
		{
			if (Node.Edges.size() >= 3 && (Node.Position - Position).SizeSquared() < 2500.0 * 2500.0)
			{
				Tags.push_back("junction");
				break;
			}
		}
		// on a bridge or within 40 m of one's end
		for (const RoadEdge& Edge : Graph.Edges)
		{
			if (Edge.bBridge && Edge.Points.size() >= 2
				&& ((Edge.Points.front() - Position).SizeSquared() < 4000.0 * 4000.0 || (Edge.Points.back() - Position).SizeSquared() < 4000.0 * 4000.0))
			{
				Tags.push_back("bridge end");
				break;
			}
		}
		// one carriageway of a divided road
		const int32_t Nearest = RobotNearestEdge(Graph, Position, 2500.0);
		if (Nearest >= 0 && Graph.Edges[size_t(Nearest)].bOneway && RobotRoadSpeed(Graph.Edges[size_t(Nearest)].Highway) >= 22.0f)
		{
			Tags.push_back("dual carriageway");
		}
		// steep, or a corner, on the route here
		if (IsDriving())
		{
			const std::vector<PathPoint>& P = Routes[size_t(RouteIndex)].Points;
			const int32_t A = std::max(Progress - 2, 0), B = std::min(Progress + 2, int32_t(P.size()) - 1);
			const double Run = georoads::Distance(P[size_t(A)].Position, P[size_t(B)].Position);
			if (Run > 100.0 && std::fabs(P[size_t(B)].Height - P[size_t(A)].Height) / Run > 0.10)
			{
				Tags.push_back("steep");
			}
			if (P[size_t(Progress)].Edge >= 0 && P[size_t(Progress)].TargetSpeed < RobotRoadSpeed(Graph.Edges[size_t(P[size_t(Progress)].Edge)].Highway) - 2.0f)
			{
				Tags.push_back("corner");
			}
		}
		if (Tags.empty())
		{
			return "open road";
		}
		std::string Out;
		for (const std::string& Tag : Tags)
		{
			Out += (Out.empty() ? "" : ", ") + Tag;
		}
		return Out;
	}

	bool RobotDriver::WasDrivingSensibly() const
	{
		// the moments from 1.5 s to 0.2 s ago (not the instant itself: a jolt makes the tyres slip, that's its effect)
		for (const Moment& M : Recent)
		{
			if (M.Time > DrivingSeconds - 0.2)
			{
				continue;
			}
			if (M.Overspeed > 3.0f || M.Slip > 7.0f || M.Offset > 1.0f)
			{
				return false;
			}
		}
		return true;
	}

	void RobotDriver::Record(const std::string& Type, const Vec2& Position, float Size, const char* Unit, float SpeedKmh, bool bScreenshot, bool bConsequence)
	{
		for (const Problem& Other : Problems)
		{
			if (Other.Type == Type && (Other.Position - Position).SizeSquared() < RepeatDistance * RepeatDistance)
			{
				return;
			}
		}
		// whose fault: a slide or leaving the road right after a map fault was caused by it; otherwise a problem counts
		// against the map only if the robot was driving sensibly (not too fast, tyres gripping, on the road)
		const bool bAfterMapFault = bConsequence && DrivingSeconds - LastMapFault < 2.0;
		const bool bMapFault = bAfterMapFault || (!bConsequence && WasDrivingSensibly());
		if (bMapFault && !bConsequence)
		{
			LastMapFault = DrivingSeconds;
		}
		Problem P;
		P.Type = Type;
		P.Position = Position;
		P.Size = Size;
		P.Unit = Unit;
		P.SpeedKmh = SpeedKmh;
		P.Edge = RobotNearestEdge(*Roads, Position, 2500.0);
		P.RouteName = IsDriving() ? Routes[size_t(RouteIndex)].Name : std::string();
		P.Verdict = bMapFault ? (bAfterMapFault ? "map fault (caused by the one before)" : "map fault") : "driver error";
		P.Context = DescribeSurroundings(Position);
		Problems.push_back(P);
		if (bScreenshot && bMapFault && Screenshots < MaxScreenshots && Screenshot)
		{
			++Screenshots;
			Screenshot(int32_t(Problems.size()));
		}
		if (Log)
		{
			double Lat = 0.0, Lon = 0.0;
			Projection.ToLatLon(Position, Lat, Lon);
			Log(Format("[Robot] problem %d (%s): %s %.1f %s at %.0f km/h, %.6f,%.6f", int(Problems.size()), P.Verdict.c_str(), Type.c_str(), Size, Unit, SpeedKmh, Lat, Lon));
		}
	}

	bool RobotDriver::Tick(float DeltaTime, const Car& C, float& OutSteer, float& OutThrottle, float& OutBrake)
	{
		OutSteer = 0.0f;
		OutThrottle = 0.0f;
		OutBrake = 1.0f;
		if (!IsDriving() || DeltaTime <= 0.0f)
		{
			return IsDriving();
		}
		const Route& R = Routes[size_t(RouteIndex)];
		const std::vector<PathPoint>& P = R.Points;
		const int32_t N = int32_t(P.size());
		const Vec2 Here = C.Position;
		const Vec2 Forward = C.Forward;
		const float Speed = C.State.SpeedMS;
		DrivingSeconds += DeltaTime;
		if (bHasLastPosition)
		{
			DrivenCm += georoads::Distance(Here, LastPosition);
		}
		LastPosition = Here;
		bHasLastPosition = true;

		// where on the route the car is (only ever moving forwards along it)
		double BestDistance = 1e30;
		int32_t Best = Progress;
		for (int32_t i = Progress; i < std::min(Progress + 80, N - 1); ++i)
		{
			const Vec2 AB = P[size_t(i) + 1].Position - P[size_t(i)].Position;
			const double T = AB.SizeSquared() > 1.0 ? std::clamp(georoads::Dot(Here - P[size_t(i)].Position, AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
			const double Distance = georoads::Distance(P[size_t(i)].Position + AB * T, Here);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = i;
			}
		}
		Progress = Best;

		// route finished: on to the next one
		if (Progress >= N - 3 || georoads::Distance(Here, P.back().Position) < 1500.0)
		{
			if (RouteIndex + 1 < int32_t(Routes.size()))
			{
				BeginRoute(RouteIndex + 1);
				return true;
			}
			RouteIndex = int32_t(Routes.size());	// done: Finish writes the report
			return false;
		}

		// steer for a point a little ahead on the route (further at speed), like a driver looking up the road
		const double Lookahead = std::clamp(double(Speed) * 0.9, 8.0, 35.0) * 100.0;
		Vec2 Target = P[size_t(Progress) + 1].Position;
		double Travelled = georoads::Distance(Here, P[size_t(Progress) + 1].Position);
		for (int32_t i = Progress + 1; i + 1 < N && Travelled < Lookahead; ++i)
		{
			const double Segment = georoads::Distance(P[size_t(i)].Position, P[size_t(i) + 1].Position);
			Target = Travelled + Segment > Lookahead ? P[size_t(i)].Position + (P[size_t(i) + 1].Position - P[size_t(i)].Position) * ((Lookahead - Travelled) / std::max(Segment, 1.0))
				: P[size_t(i) + 1].Position;
			Travelled += Segment;
		}
		const Vec2 ToTarget = Target - Here;
		const double Alpha = std::atan2(Forward.X * ToTarget.Y - Forward.Y * ToTarget.X, georoads::Dot(Forward, ToTarget));
		const double SteerRadians = std::atan(2.0 * (C.Wheelbase > 0.5f ? C.Wheelbase : RobotWheelbase) * std::sin(Alpha) / std::max(ToTarget.Size() / 100.0, 1.0));
		const float Steer = std::clamp(float(SteerRadians * 180.0 / Pi), -C.MaxSteerDegrees, C.MaxSteerDegrees);

		// speed: the slowest target over the next second and a bit of road, so braking starts in good time
		float TargetSpeed = P[size_t(Progress)].TargetSpeed;
		double Ahead = 0.0;
		for (int32_t i = Progress; i + 1 < N && Ahead < (double(Speed) * 1.2 + 10.0) * 100.0; ++i)
		{
			TargetSpeed = std::min(TargetSpeed, P[size_t(i) + 1].TargetSpeed);
			Ahead += georoads::Distance(P[size_t(i)].Position, P[size_t(i) + 1].Position);
		}
		const float Error = TargetSpeed - Speed;
		// less throttle while steering hard, firm braking when too fast
		const float SteerShare = std::fabs(Steer) / std::max(C.MaxSteerDegrees, 1.0f);
		const float Throttle = std::clamp(Error * 0.35f, 0.0f, 1.0f) * std::clamp(1.2f - SteerShare, 0.2f, 1.0f);
		const float Brake = Error < -0.5f ? std::clamp(-Error * 0.5f, 0.0f, 1.0f) : 0.0f;
		OutSteer = Steer;
		OutThrottle = Grace > 1.0f ? 0.0f : Throttle;
		OutBrake = Brake;

		// --- watching for problems (not while the car is still dropping in after a teleport) ---
		const Vec2 PathAt = P[size_t(Progress)].Position;
		const float RoadHeight = P[size_t(Progress)].Height;
		if (Grace > 0.0f)
		{
			Grace -= DeltaTime;
			return true;
		}
		const float Kmh = Speed * 3.6f;

		// the moment, for judging whose fault a problem is
		float Slip = 0.0f;
		int32_t Contacts = 0;
		float MaxLoad = 0.0f;
		for (const geocar::WheelState& Wheel : C.State.Wheels)
		{
			Slip = Wheel.bContact ? std::max(Slip, std::fabs(Wheel.SlipAngleDegrees)) : Slip;
			Contacts += Wheel.bContact ? 1 : 0;
			MaxLoad = std::max(MaxLoad, Wheel.Load);
		}
		Moment M;
		M.Time = DrivingSeconds;
		M.Overspeed = Speed - P[size_t(Progress)].TargetSpeed;
		M.Slip = Slip;
		M.Offset = float(BestDistance / std::max(P[size_t(Progress)].HalfWidth, 100.0f));
		M.GroundSpeed = float(C.Velocity.Size() / 100.0);
		Recent.push_back(M);
		Recent.erase(std::remove_if(Recent.begin(), Recent.end(), [this](const Moment& Old) { return Old.Time < DrivingSeconds - 2.0; }), Recent.end());

		// in the air: no wheel touching anything
		if (Contacts == 0)
		{
			if (AirTime == 0.0f)
			{
				TakeOff = Here;
			}
			AirTime += DeltaTime;
		}
		else
		{
			if (AirTime >= MinAirTime)
			{
				Record("launch (car left the ground)", TakeOff, AirTime, "s in the air", Kmh, true);
			}
			AirTime = 0.0f;
		}

		// a hard jolt through one wheel
		if (MaxLoad > JoltLoad)
		{
			Record("jolt", Here, MaxLoad / 3000.0f, "x normal wheel load", Kmh, true);
		}

		// slowing far harder than the brakes can: it hit something (over a quarter of a second, as single frames jump)
		const auto Before = std::find_if(Recent.begin(), Recent.end(), [this](const Moment& Old) { return Old.Time >= DrivingSeconds - 0.25; });
		if (Before != Recent.end() && DrivingSeconds - Before->Time > 0.15)
		{
			const float Deceleration = float((Before->GroundSpeed - Recent.back().GroundSpeed) / (DrivingSeconds - Before->Time));
			if (Deceleration > HitDeceleration)
			{
				Record("hit something", Here, Deceleration / 9.81f, "g", Kmh, true);
			}
		}

		// dropped through the road
		if (C.Height < RoadHeight - FallDepth)
		{
			Record("fell through the road", Here, float((RoadHeight - C.Height) / 100.0), "m below", Kmh, true);
			Recover(5);
			return true;
		}

		// off the road
		OffRoadTime = BestDistance > P[size_t(Progress)].HalfWidth + OffRoadMargin ? OffRoadTime + DeltaTime : 0.0f;
		if (OffRoadTime > 0.5f)
		{
			Record("left the road", PathAt, float(BestDistance / 100.0), "m from the middle", Kmh, false, true);
			Recover(3);
			return true;
		}

		// sliding or spinning: pointing well away from the road while moving
		const Vec2 RoadDirection = (P[size_t(Progress) + 1].Position - P[size_t(Progress)].Position).Normal();
		const float Heading = float(std::acos(std::clamp(georoads::Dot(Forward, RoadDirection), -1.0, 1.0)) * 180.0 / Pi);
		SpinTime = (Heading > SpinAngle && Speed > 8.0f) ? SpinTime + DeltaTime : 0.0f;
		if (SpinTime > 0.5f)
		{
			Record("slide or spin", Here, Heading, "degrees off the road", Kmh, true, true);
			Recover(5);
			return true;
		}

		// stuck: barely any progress for 6 seconds
		StuckTimer += DeltaTime;
		if (StuckTimer > 6.0f)
		{
			if (Progress - StuckProgress < 2 && Speed < 2.0f)
			{
				Record("stuck", Here, StuckTimer, "s", Kmh, true);
				Recover(5);
				return true;
			}
			StuckTimer = 0.0f;
			StuckProgress = Progress;
		}
		return true;
	}

	std::string RobotDriver::Finish(std::string& OutFileName)
	{
		OutFileName = ReportName;
		if (ReportName.empty())
		{
			return std::string();
		}
		// the report: a summary, then every problem with where to find it
		std::map<std::string, int32_t> Counts;
		for (const Problem& P : Problems)
		{
			++Counts[P.Type];
			const bool bMap = RobotStartsWith(P.Verdict, "map");
			++Counts[bMap ? "MAP FAULTS (to fix)" : "driver errors (the robot's own)"];
			if (bMap)
			{
				size_t Start = 0;
				while (Start <= P.Context.size())
				{
					const size_t Comma = P.Context.find(", ", Start);
					++Counts["  map faults at: " + P.Context.substr(Start, Comma == std::string::npos ? std::string::npos : Comma - Start)];
					if (Comma == std::string::npos)
					{
						break;
					}
					Start = Comma + 2;
				}
			}
		}
		const double Km = DrivenCm / 100000.0;
		std::string Report = Format("Robot driver report %s\r\n%.1f km driven in %.0f min (average %.0f km/h), %d of %d routes, %d problems\r\n",
			Now.c_str(), Km, DrivingSeconds / 60.0, Km / std::max(DrivingSeconds / 3600.0, 1.0e-6),
			std::min(RouteIndex + 1, int32_t(Routes.size())), int(Routes.size()), int(Problems.size()));
		for (const auto& [Key, Count] : Counts)
		{
			Report += Format("  %s: %d\r\n", Key.c_str(), Count);
		}
		Report += "\r\n";
		for (size_t i = 0; i < Problems.size(); ++i)
		{
			const Problem& P = Problems[i];
			double Lat = 0.0, Lon = 0.0;
			Projection.ToLatLon(P.Position, Lat, Lon);
			const std::string Road = P.Edge >= 0 ? (Roads->Edges[size_t(P.Edge)].Name.empty() ? std::string("unnamed") : Roads->Edges[size_t(P.Edge)].Name)
				+ " (" + Roads->Edges[size_t(P.Edge)].Highway + ")" : std::string("no road nearby");
			Report += Format("Robot %d | %s | %s %.1f %s | %.6f,%.6f | %s | %.0f km/h | %s | %s\r\n", int(i + 1), P.Verdict.c_str(), P.Type.c_str(), P.Size,
				P.Unit.c_str(), Lat, Lon, Road.c_str(), P.SpeedKmh, P.RouteName.c_str(), P.Context.c_str());
		}
		if (Log)
		{
			Log(Format("[Robot] finished: %.1f km, %d problems, report %s", Km, int(Problems.size()), ReportName.c_str()));
		}
		ReportName.clear();
		Routes.clear();
		RouteIndex = -1;
		return Report;
	}

	std::string RobotDriver::StatusText() const
	{
		if (!IsDriving())
		{
			return std::string();
		}
		const Route& R = Routes[size_t(RouteIndex)];
		return Format("Robot driver: %s (route %d of %d, %d%%), %.1f km driven, %d problems found", R.Name.c_str(), RouteIndex + 1, int(Routes.size()),
			R.Points.size() > 1 ? int(Progress * 100 / int32_t(R.Points.size() - 1)) : 0, DrivenCm / 100000.0, int(Problems.size()));
	}
}
