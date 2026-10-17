// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Traffic.h"

#include "GeoCar/CarBody.h"
#include "GeoRoads/RoadDesign.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>

namespace geotraffic
{
	using georoads::RoadEdge;
	using georoads::RoadGraph;

	namespace
	{
		/** Traffic appears this far from the player (cm), just out of the way, and goes once this far away */
		// (cm) as a racing game keeps its traffic: the cars there are, all round you; new ones come in out of sight (ahead
		// past SpawnAhead, or behind and to the sides past SpawnNear), and those left behind go to come in again ahead
		constexpr double SpawnNear = 6000.0, SpawnAhead = 18000.0, SpawnFar = 35000.0, Despawn = 45000.0, LeftBehind = 25000.0;

		/** Calm driving: cornering and braking well inside the grip (m/s^2) */
		constexpr float CornerGrip = 2.5f;
		constexpr float Braking = 2.0f;

		/** Keep this much road planned ahead (cm) */
		constexpr double PlanAhead = 30000.0;

		/** Looking out for others along the road ahead: a point every Spacing (cm), as far as stopping takes and more */
		constexpr double Spacing = 200.0;
		double LookAhead(float SpeedMS) { return std::clamp(double(SpeedMS) * 3.0 + 20.0, 30.0, 70.0) * 100.0; }

		/** Car wheelbase (m) for the steering geometry */
		constexpr float Wheelbase = 2.5f;

		bool StartsWith(const std::string& S, const char* Prefix) { return S.rfind(Prefix, 0) == 0; }

		/** Roads traffic drives: through roads and streets, not service lanes, tracks or gravel */
		bool Drivable(const RoadEdge& Edge)
		{
			if (Edge.Kind == georoads::Surface::Loose || Edge.Points.size() < 2)
			{
				return false;
			}
			const std::string& H = Edge.Highway;
			return StartsWith(H, "motorway") || StartsWith(H, "trunk") || StartsWith(H, "primary") || StartsWith(H, "secondary")
				|| StartsWith(H, "tertiary") || H == "unclassified" || H == "residential" || H == "road" || H == "living_street";
		}

		int32_t Rank(const RoadEdge& Edge) { return georoads::RoadRank(Edge.Highway); }

		/** The speed traffic keeps on a road (m/s): its limit, or the class's usual */
		float RoadSpeed(const RoadEdge& Edge)
		{
			if (Edge.SpeedKmh > 0.0f)
			{
				return Edge.SpeedKmh / 3.6f * 0.95f;
			}
			return georoads::DefaultSpeed(Edge.Highway) / 3.6f * 0.9f;
		}

		double EdgeLength(const RoadEdge& Edge)
		{
			double Length = 0.0;
			for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
			{
				Length += georoads::Distance(Edge.Points[i], Edge.Points[i + 1]);
			}
			return Length;
		}

		/** The road nearest a point within Reach (cm), or -1 */
		int32_t NearestEdge(const RoadGraph& Graph, const Vec2& P, double Reach)
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

		double Cross(const Vec2& A, const Vec2& B) { return A.X * B.Y - A.Y * B.X; }
	}

	void PaintColour(int32_t Paint, float& R, float& G, float& B)
	{
		static const float Paints[NumPaints][3] = { { 0.8f, 0.8f, 0.8f }, { 0.05f, 0.05f, 0.06f }, { 0.4f, 0.41f, 0.43f }, { 0.55f, 0.02f, 0.02f },
			{ 0.02f, 0.08f, 0.35f }, { 0.85f, 0.62f, 0.02f }, { 0.15f, 0.3f, 0.12f }, { 0.75f, 0.35f, 0.05f }, { 0.6f, 0.6f, 0.65f }, { 0.25f, 0.05f, 0.25f } };
		const float* P = Paints[std::clamp(Paint, 0, NumPaints - 1)];
		R = P[0];
		G = P[1];
		B = P[2];
	}

	Traffic::Traffic(std::shared_ptr<const RoadGraph> InRoads, TrafficHost& InHost, uint32_t Seed)
		: Roads(std::move(InRoads))
		, Host(InHost)
		, Rng(Seed)
	{
	}

	Traffic::~Traffic()
	{
		Clear();
	}

	void Traffic::Clear()
	{
		for (Car& C : Cars)
		{
			Host.RemoveCar(C.Id);
		}
		Cars.clear();
	}

	std::string Traffic::StatusText() const
	{
		if (Cars.empty() && Problems.empty())
		{
			return std::string();
		}
		char Line[160];
		if (!Problems.empty())
		{
			std::snprintf(Line, sizeof(Line), "Traffic: %d cars, %d road problems logged (Saved/RoadReports)", int(Cars.size()), int(Problems.size()));
		}
		else
		{
			std::snprintf(Line, sizeof(Line), "Traffic: %d cars", int(Cars.size()));
		}
		return Line;
	}

	std::string Traffic::DebugLine() const
	{
		char Part[64];
		std::snprintf(Part, sizeof(Part), "bumps %d:", Bumps);
		std::string Out = Part;
		for (const Car& C : Cars)
		{
			std::snprintf(Part, sizeof(Part), " %.0f/%.0f(%.1fm)", C.LastSpeed * 3.6f, C.LastTarget * 3.6f, C.LastOffLane);
			Out += Part;
		}
		return Out;
	}

	void Traffic::AppendEdge(Car& C, int32_t EdgeIndex, bool bForward)
	{
		const RoadGraph& Graph = *Roads;
		const RoadEdge& Edge = Graph.Edges[EdgeIndex];
		const int32_t N = int32_t(Edge.Points.size());

		// the kerb-side lane going this way (the leftmost keeping left, the rightmost keeping right): its middle,
		// measured from the centreline towards the kerb
		const float LeftShoulder = bForward ? Edge.ShoulderLeft : Edge.ShoulderRight;
		const float RightShoulder = bForward ? Edge.ShoulderRight : Edge.ShoulderLeft;
		const float TravelLeft = Edge.HalfWidth - LeftShoulder, TravelRight = Edge.HalfWidth - RightShoulder;
		const float LaneWidth = (TravelLeft + TravelRight) / float(std::max(Edge.Lanes, 1));
		const float Offset = std::max(0.0f, (bDriveLeft ? TravelLeft : TravelRight) - LaneWidth * 0.5f);

		// at its far end: a smaller road meeting a bigger one gives way
		const int32_t EndNode = bForward ? Edge.EndNode : Edge.StartNode;
		int32_t BiggestOther = 0;
		for (const int32_t Other : Graph.Nodes[EndNode].Edges)
		{
			if (Other != EdgeIndex)
			{
				BiggestOther = std::max(BiggestOther, Rank(Graph.Edges[Other]));
			}
		}
		const bool bGiveWay = Graph.Nodes[EndNode].Edges.size() >= 3 && Rank(Edge) < BiggestOther;

		const float Speed = RoadSpeed(Edge);
		for (int32_t k = 0; k < N; ++k)
		{
			const int32_t i = bForward ? k : N - 1 - k;
			const int32_t Prev = bForward ? std::max(i - 1, 0) : std::min(i + 1, N - 1);
			const int32_t Next = bForward ? std::min(i + 1, N - 1) : std::max(i - 1, 0);
			const Vec2 Direction = (Edge.Points[Next] - Edge.Points[Prev]).Normal();
			const Vec2 Left(Direction.Y, -Direction.X);	// north = +X, east = +Y: facing +X, the left is -Y
			Point P;
			P.Position = Edge.Points[i] + Left * (bDriveLeft ? Offset : -Offset);
			P.Height = i < int32_t(Edge.Heights.size()) ? Edge.Heights[i] : 0.0f;
			P.TargetSpeed = Speed;
			P.Edge = EdgeIndex;
			P.bGiveWay = bGiveWay && k == N - 1;
			if (!C.Path.empty() && (C.Path.back().Position - P.Position).SizeSquared() < 400.0)
			{
				continue;	// the joining point is already there
			}
			C.Path.push_back(P);
		}
		C.LastEdge = EdgeIndex;
		C.bLastForward = bForward;
	}

	bool Traffic::Extend(Car& C)
	{
		const RoadGraph& Graph = *Roads;
		const RoadEdge& Last = Graph.Edges[C.LastEdge];
		const int32_t Node = C.bLastForward ? Last.EndNode : Last.StartNode;
		const size_t N = Last.Points.size();
		const Vec2 Arriving = C.bLastForward ? (Last.Points[N - 1] - Last.Points[N - 2]).Normal() : (Last.Points[0] - Last.Points[1]).Normal();

		// carry on mostly straight, sometimes turn; bigger roads a little more likely
		std::vector<std::pair<int32_t, bool>> Options;
		std::vector<double> Weights;
		for (const int32_t EdgeIndex : Graph.Nodes[Node].Edges)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (EdgeIndex == C.LastEdge || !Drivable(Edge))
			{
				continue;
			}
			const bool bForward = Edge.StartNode == Node;
			if (!bForward && Edge.bOneway)
			{
				continue;
			}
			const size_t M = Edge.Points.size();
			const Vec2 Leaving = bForward ? (Edge.Points[1] - Edge.Points[0]).Normal() : (Edge.Points[M - 2] - Edge.Points[M - 1]).Normal();
			const double Straight = std::max(0.0, georoads::Dot(Arriving, Leaving));
			Options.emplace_back(EdgeIndex, bForward);
			Weights.push_back(0.25 + 3.0 * Straight * Straight + 0.3 * Rank(Edge));
		}
		if (Options.empty())
		{
			// a dead end: turn round if it's a two-way road
			if (Last.bOneway)
			{
				return false;
			}
			AppendEdge(C, C.LastEdge, !C.bLastForward);
			return true;
		}
		double Total = 0.0;
		for (const double W : Weights)
		{
			Total += W;
		}
		double Pick = Random01() * Total;
		size_t Choice = 0;
		for (; Choice + 1 < Options.size() && Pick > Weights[Choice]; ++Choice)
		{
			Pick -= Weights[Choice];
		}
		AppendEdge(C, Options[Choice].first, Options[Choice].second);
		return true;
	}

	void Traffic::PlanSpeeds(Car& C) const
	{
		std::vector<Point>& P = C.Path;
		const int32_t N = int32_t(P.size());
		// the road's limit, then slow enough for each bend (heading change over about 10 m either side), and nearly
		// stopped where it gives way
		for (int32_t i = 0; i < N; ++i)
		{
			P[i].TargetSpeed = RoadSpeed(Roads->Edges[P[i].Edge]);
			if (i > 0 && i + 1 < N)
			{
				int32_t Back = i, Ahead = i;
				while (Back > 0 && georoads::Distance(P[Back].Position, P[i].Position) < 1000.0) { --Back; }
				while (Ahead + 1 < N && georoads::Distance(P[Ahead].Position, P[i].Position) < 1000.0) { ++Ahead; }
				const Vec2 In = (P[i].Position - P[Back].Position).Normal();
				const Vec2 Out = (P[Ahead].Position - P[i].Position).Normal();
				const double Turn = std::acos(std::clamp(georoads::Dot(In, Out), -1.0, 1.0));
				const double Span = (georoads::Distance(P[Back].Position, P[i].Position) + georoads::Distance(P[i].Position, P[Ahead].Position)) / 100.0;
				if (Turn > 1.0e-3 && Span > 1.0)
				{
					P[i].TargetSpeed = std::min(P[i].TargetSpeed, float(std::sqrt(CornerGrip * Span / Turn)));
				}
			}
			if (P[i].bGiveWay)
			{
				P[i].TargetSpeed = std::min(P[i].TargetSpeed, 3.0f);
			}
			P[i].TargetSpeed = std::max(P[i].TargetSpeed, 2.0f);
		}
		// brake in time for what's coming
		for (int32_t i = N - 2; i >= 0; --i)
		{
			const float Distance = float(georoads::Distance(P[i].Position, P[i + 1].Position) / 100.0);
			P[i].TargetSpeed = std::min(P[i].TargetSpeed, std::sqrt(P[i + 1].TargetSpeed * P[i + 1].TargetSpeed + 2.0f * Braking * Distance));
		}
	}

	void Traffic::UpdateNetwork(const Vec2& Player)
	{
		// along the roads from the one the player is on, out to about 450 m: where traffic belongs
		const double Reach = Despawn * Range;
		const RoadGraph& Graph = *Roads;
		NetworkDistance.clear();
		PlayerEdge = NearestEdge(Graph, Player, 5000.0);
		if (PlayerEdge < 0)
		{
			return;
		}
		std::map<int32_t, double> NodeCost;
		using Entry = std::pair<double, int32_t>;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> Open;
		const RoadEdge& Start = Graph.Edges[PlayerEdge];
		for (const int32_t Node : { Start.StartNode, Start.EndNode })
		{
			const double Cost = georoads::Distance(Graph.Nodes[Node].Position, Player);
			NodeCost[Node] = Cost;
			Open.push({ Cost, Node });
		}
		NetworkDistance[PlayerEdge] = 0.0f;
		while (!Open.empty())
		{
			const Entry Current = Open.top();
			Open.pop();
			if (Current.first > NodeCost[Current.second] || Current.first > Reach)
			{
				continue;
			}
			for (const int32_t EdgeIndex : Graph.Nodes[Current.second].Edges)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (!Drivable(Edge))
				{
					continue;
				}
				const auto Known = NetworkDistance.find(EdgeIndex);
				NetworkDistance[EdgeIndex] = Known == NetworkDistance.end() ? float(Current.first) : std::min(Known->second, float(Current.first));
				const int32_t Next = Edge.StartNode == Current.second ? Edge.EndNode : Edge.StartNode;
				const double NextCost = Current.first + EdgeLength(Edge);
				const auto Seen = NodeCost.find(Next);
				if (Seen == NodeCost.end() || NextCost < Seen->second)
				{
					NodeCost[Next] = NextCost;
					Open.push({ NextCost, Next });
				}
			}
		}
	}

	bool Traffic::Spawn(const PlayerPose& Player, bool bParked)
	{
		const RoadGraph& Graph = *Roads;
		if (Graph.Edges.empty() || NetworkDistance.empty() || PlayerEdge < 0 || PlayerEdge >= int32_t(Graph.Edges.size()))
		{
			return false;
		}
		// the player's own road first, then the roads it can turn into (bigger ones busier, ahead more than behind);
		// parked cars only along streets wide enough to leave room past them
		const RoadEdge& Own = Graph.Edges[PlayerEdge];
		std::vector<int32_t> Nearby;
		std::vector<double> Weights;
		double Total = 0.0;
		for (const auto& [EdgeIndex, Along] : NetworkDistance)
		{
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			if (bParked && (Edge.HalfWidth < 260.0f || StartsWith(Edge.Highway, "motorway") || StartsWith(Edge.Highway, "trunk") || StartsWith(Edge.Highway, "primary")))
			{
				continue;
			}
			const bool bOwnRoad = EdgeIndex == PlayerEdge || (!Own.Name.empty() && Edge.Name == Own.Name)
				|| (Own.Name.empty() && Edge.Highway == Own.Highway && Along < 20000.0f);
			const int32_t R = Rank(Edge);
			const Vec2 Middle = Edge.Points[Edge.Points.size() / 2];
			const bool bAhead = georoads::Dot((Middle - Player.Position).Normal(), Player.Forward) > 0.3;
			const double Weight = (bOwnRoad ? 8.0 : R >= 4 ? 4.0 : R == 3 ? 3.0 : R == 2 ? 2.0 : 1.0) * (bAhead ? 3.0 : 1.0)
				* std::max(EdgeLength(Edge) / 10000.0, 0.2);
			Nearby.push_back(EdgeIndex);
			Weights.push_back(Weight);
			Total += Weight;
		}
		const double Near = SpawnNear, Far = (bParked ? SpawnFar * 0.7 : SpawnFar) * Range;
		for (int32_t Attempt = 0; Attempt < 25 && !Nearby.empty(); ++Attempt)
		{
			double Pick = Random01() * Total;
			size_t Choice = 0;
			for (; Choice + 1 < Nearby.size() && Pick > Weights[Choice]; ++Choice)
			{
				Pick -= Weights[Choice];
			}
			const int32_t EdgeIndex = Nearby[Choice];
			const RoadEdge& Edge = Graph.Edges[EdgeIndex];
			const int32_t Count = int32_t(Edge.Points.size());
			// parked cars keep clear of the corners
			const int32_t PointIndex = bParked ? 1 + RandomBelow(std::max(Count - 3, 1)) : RandomBelow(Count - 1);
			if (PointIndex + 1 >= Count)
			{
				continue;
			}
			const Vec2 Where = Edge.Points[PointIndex];
			const double Distance = georoads::Distance(Where, Player.Position);
			// (not where you'd see it appear: ahead only far off; behind and to the sides nearer)
			const bool bInView = georoads::Dot((Where - Player.Position).Normal(), Player.Forward) > 0.5;
			if (Distance < (bInView && !bParked ? SpawnAhead : Near) || Distance > Far)
			{
				continue;
			}
			bool bTooClose = false;
			for (const Car& Other : Cars)
			{
				const auto Pose = Poses.find(Other.Id);
				if (Pose != Poses.end() && georoads::Distance(Pose->second.Position, Where) < (bParked || Other.bParked ? 1200.0 : 1500.0))	// (15 m: they bunch, queue)
				{
					bTooClose = true;
					break;
				}
			}
			if (bTooClose)
			{
				continue;
			}

			// a way to drive it (a parked car too: the way it goes when it pulls out), and a few roads planned ahead
			Car C;
			const bool bForward = Edge.bOneway || Random01() < 0.5;
			AppendEdge(C, EdgeIndex, bForward);
			C.Progress = std::clamp(bForward ? PointIndex : Count - 1 - PointIndex, 0, std::max(int32_t(C.Path.size()) - 2, 0));
			for (int32_t More = 0; More < 3 && Extend(C); ++More) {}
			if (int32_t(C.Path.size()) < C.Progress + 2)
			{
				continue;
			}
			PlanSpeeds(C);

			const Point& Start = C.Path[C.Progress];
			const Vec2 Heading = (C.Path[C.Progress + 1].Position - Start.Position).Normal();
			Vec2 Place = Start.Position;
			if (bParked)
			{
				// at the kerb (on its left keeping left, its right keeping right), facing the way it will drive off
				const Vec2 Left(Heading.Y, -Heading.X);
				Place = Edge.Points[PointIndex] + Left * ((bDriveLeft ? 1.0f : -1.0f) * (Edge.HalfWidth - 110.0f));
			}
			// mostly small hatches, a good share of bakkies (as on South Africa's roads); all sorts of colours. A driving
			// car is already going at the road's pace, like the traffic it joins: never a standing start in the road
			const int32_t Body = Random01() < 0.7 ? int32_t(geocar::CarBody::Hatch) : int32_t(geocar::CarBody::Bakkie);
			C.Id = Host.SpawnCar(Place, Start.Height + 60.0, Heading, Body, RandomBelow(NumPaints), bParked ? 0.0f : Start.TargetSpeed, bParked);
			if (C.Id < 0)
			{
				continue;
			}
			C.bParked = bParked;
			C.bWillPullOut = bParked && Random01() < 0.35;
			Cars.push_back(std::move(C));
			return true;
		}
		return false;
	}

	void Traffic::Tick(float DeltaTime, const PlayerPose& Player, int32_t Wanted)
	{
		if (!Roads || DeltaTime <= 0.0f)
		{
			return;
		}
		// more cars, busier roads round you (they used to spread further out, up to 2.5 times as far: the same thin
		// traffic, only more of it out of sight); only very many reach a little further
		Range = std::clamp(std::sqrt(Wanted / 60.0), 1.0, 1.6);
		NetworkTimer -= DeltaTime;
		if (NetworkTimer <= 0.0f)
		{
			NetworkTimer = 1.0f;
			UpdateNetwork(Player.Position);
		}

		// where each is now
		Poses.clear();
		for (const Car& C : Cars)
		{
			CarPose Pose;
			if (Host.GetCar(C.Id, Pose))
			{
				Poses[C.Id] = Pose;
			}
		}

		// bumps: two cars' boxes (4.6 m by 1.9 m) overlapping, each meeting counted once
		{
			std::set<std::pair<int32_t, int32_t>> Now;
			for (auto A = Poses.begin(); A != Poses.end(); ++A)
			{
				for (auto B = std::next(A); B != Poses.end(); ++B)
				{
					const Vec2 Between = B->second.Position - A->second.Position;
					if (Between.SizeSquared() > 500.0 * 500.0)
					{
						continue;
					}
					bool bApart = false;
					for (const Vec2& Axis : { A->second.Forward, Vec2(-A->second.Forward.Y, A->second.Forward.X), B->second.Forward, Vec2(-B->second.Forward.Y, B->second.Forward.X) })
					{
						auto Reach = [&Axis](const Vec2& F) { return 230.0 * std::fabs(georoads::Dot(F, Axis)) + 95.0 * std::fabs(Cross(F, Axis)); };
						if (std::fabs(georoads::Dot(Between, Axis)) > Reach(A->second.Forward) + Reach(B->second.Forward))
						{
							bApart = true;
							break;
						}
					}
					if (!bApart)
					{
						Now.insert({ A->first, B->first });
						if (!Touching.count({ A->first, B->first }))
						{
							++Bumps;
							if (Log)
							{
								// (what kind of bump: each one's state and speed, how they were heading, where)
								auto State = [this](int32_t Id) { for (const Car& C : Cars) { if (C.Id == Id) { return C.bParked ? "parked" : (C.Grace > 0.0f ? "new" : "driving"); } } return "?"; };
								const double Angle = std::acos(std::clamp(georoads::Dot(A->second.Forward, B->second.Forward), -1.0, 1.0)) * 57.2958;
								char Line[200];
								std::snprintf(Line, sizeof(Line), "bump %d: %s %.0f km/h, %s %.0f km/h, %.0f deg apart, at %.0f %.0f", Bumps, State(A->first), A->second.State.SpeedMS * 3.6f,
									State(B->first), B->second.State.SpeedMS * 3.6f, Angle, A->second.Position.X / 100.0, A->second.Position.Y / 100.0);
								Log(Line);
							}
						}
					}
				}
			}
			Touching = std::move(Now);
		}

		// a third of them parked along the streets, the rest driving
		const int32_t WantedParked = Wanted / 3, WantedDriving = Wanted - WantedParked;
		int32_t Driving = 0, Parked = 0;
		for (int32_t i = int32_t(Cars.size()) - 1; i >= 0; --i)
		{
			Car& C = Cars[size_t(i)];
			const auto Pose = Poses.find(C.Id);
			const double Distance = Pose != Poses.end() ? georoads::Distance(Pose->second.Position, Player.Position) : 0.0;
			const bool bTooMany = C.bParked ? Parked >= WantedParked : Driving >= WantedDriving;
			// (left behind, out of sight: gone, to come in again ahead)
			const bool bBehind = Pose != Poses.end() && Distance > LeftBehind * Range
				&& georoads::Dot((Pose->second.Position - Player.Position).Normal(), Player.Forward) < -0.3;
			if (Pose == Poses.end() || Distance > (C.bParked ? SpawnFar : Despawn) * Range || bBehind || bTooMany)
			{
				Host.RemoveCar(C.Id);
				Cars.erase(Cars.begin() + i);
				continue;
			}
			// some parked cars pull out as the player comes near
			if (C.bParked && C.bWillPullOut && Distance < 9000.0)
			{
				C.bParked = false;
				C.Grace = 2.0f;
			}
			(C.bParked ? Parked : Driving)++;
			if (C.bParked && Log && std::fabs(Pose->second.State.SpeedMS) > 3.0f && !C.bRollingLogged)
			{
				C.bRollingLogged = true;
				char Line[200];
				std::snprintf(Line, sizeof(Line), "parked car %d moving: %.0f km/h, gear %d, %.0f rpm, up %.2f, %d wheels down", C.Id, Pose->second.State.SpeedMS * 3.6f,
					Pose->second.State.Gear, Pose->second.State.EngineRPM, Pose->second.UpZ, int(std::count_if(std::begin(Pose->second.State.Wheels), std::end(Pose->second.State.Wheels), [](const geocar::WheelState& W) { return W.bContact; })));
				Log(Line);
			}
		}
		SpawnCooldown -= DeltaTime;
		if (SpawnCooldown <= 0.0f && (Driving < WantedDriving || Parked < WantedParked))
		{
			SpawnCooldown = 0.15f / float(Range * Range);	// several a second: the roads fill quickly, without a hitch
			Spawn(Player, Parked < WantedParked && (Driving >= WantedDriving || Random01() < 0.4));
		}

		// everyone to keep out of the way of (where, how fast, the road each will drive next), and the parked cars to pass
		std::vector<Mover> Others(1);
		Others[0].Position = Player.Position;
		Others[0].Velocity = Player.Velocity;
		Others[0].Forward = Player.Forward;
		Others[0].Speed = float(Player.Velocity.Size() / 100.0);
		std::vector<Vec2> ParkedAt;
		for (const Car& C : Cars)
		{
			const auto Pose = Poses.find(C.Id);
			if (Pose != Poses.end())
			{
				if (C.bParked)
				{
					ParkedAt.push_back(Pose->second.Position);
				}
				else
				{
					Mover M;
					M.Id = C.Id;
					M.Position = Pose->second.Position;
					M.Velocity = Pose->second.Velocity;
					M.Forward = Pose->second.Forward;
					M.Speed = Pose->second.State.SpeedMS;
					RoadAhead(C, M.Position, LookAhead(M.Speed), M.Ahead);
					Others.push_back(std::move(M));
				}
			}
		}
		for (Car& C : Cars)
		{
			const auto Pose = Poses.find(C.Id);
			if (!C.bParked && Pose != Poses.end())
			{
				Drive(C, DeltaTime, Pose->second, Others, ParkedAt);
			}
		}
		for (int32_t i = int32_t(Cars.size()) - 1; i >= 0; --i)
		{
			if (Cars[size_t(i)].bGone)
			{
				Host.RemoveCar(Cars[size_t(i)].Id);
				Cars.erase(Cars.begin() + i);
			}
		}
	}

	void Traffic::RoadAhead(const Car& C, const Vec2& Here, double Reach, std::vector<Vec2>& Out) const
	{
		// from where it is along its path, a point every Spacing out to Reach (the first: where it is)
		Out.clear();
		Out.push_back(Here);
		const std::vector<Point>& P = C.Path;
		Vec2 From = Here;
		double Next = Spacing, Done = 0.0;	// (Next: along this piece, where the next point goes)
		for (size_t i = size_t(std::max(C.Progress, 0)) + 1; i < P.size() && Done < Reach; ++i)
		{
			const Vec2 To = P[i].Position;
			const double Length = georoads::Distance(From, To);
			for (; Next <= Length && Done + Next <= Reach; Next += Spacing)
			{
				Out.push_back(From + (To - From) * (Next / std::max(Length, 1.0)));
			}
			Next -= Length;
			Done += Length;
			From = To;
		}
	}

	void Traffic::Drive(Car& C, float DeltaTime, const CarPose& Pose, const std::vector<Mover>& Others, const std::vector<Vec2>& Parked)
	{
		std::vector<Point>& P = C.Path;
		if (P.size() < 2)
		{
			return;
		}
		const Vec2 Here = Pose.Position;
		const Vec2 Forward = Pose.Forward;
		const float Speed = Pose.State.SpeedMS;
		const int32_t N = int32_t(P.size());

		// where on its path it is (only ever forwards), the road behind dropped, more road planned ahead
		double BestDistance = 1e30;
		int32_t Best = C.Progress;
		for (int32_t i = C.Progress; i < std::min(C.Progress + 40, N - 1); ++i)
		{
			const Vec2 AB = P[i + 1].Position - P[i].Position;
			const double T = AB.SizeSquared() > 1.0 ? std::clamp(georoads::Dot(Here - P[i].Position, AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
			const double Distance = georoads::Distance(P[i].Position + AB * T, Here);
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = i;
			}
		}
		C.Progress = Best;
		if (C.Progress > 60)
		{
			P.erase(P.begin(), P.begin() + 50);
			C.Progress -= 50;
		}
		double Remaining = 0.0;
		for (size_t i = size_t(C.Progress); i + 1 < P.size(); ++i)
		{
			Remaining += georoads::Distance(P[i].Position, P[i + 1].Position);
		}
		if (Remaining < PlanAhead)
		{
			for (int32_t More = 0; More < 4 && Remaining < PlanAhead && Extend(C); ++More)
			{
				Remaining = PlanAhead;	// planned again below
			}
			PlanSpeeds(C);
		}
		if (C.Progress + 2 >= int32_t(P.size()))
		{
			C.bGone = true;	// nowhere left to go
			return;
		}

		// steer for a point a little up the road, further at speed
		const double Lookahead = std::clamp(double(Speed) * 0.8, 6.0, 30.0) * 100.0;
		Vec2 Target = P[size_t(C.Progress) + 1].Position;
		double Travelled = georoads::Distance(Here, Target);
		for (size_t i = size_t(C.Progress) + 1; i + 1 < P.size() && Travelled < Lookahead; ++i)
		{
			const double Segment = georoads::Distance(P[i].Position, P[i + 1].Position);
			Target = Travelled + Segment > Lookahead ? P[i].Position + (P[i + 1].Position - P[i].Position) * ((Lookahead - Travelled) / std::max(Segment, 1.0)) : P[i + 1].Position;
			Travelled += Segment;
		}
		// a parked car ahead at the kerb (by the road it will drive, round bends too): ease out round it, as far as
		// needed for a little room ("Left": the kerb side); one too far out to get round (a narrow street): wait behind
		std::vector<Vec2> Mine;
		RoadAhead(C, Here, LookAhead(Speed), Mine);
		const Vec2 Left = Vec2(Forward.Y, -Forward.X) * (bDriveLeft ? 1.0 : -1.0);
		float PassingShift = 0.0f, BlockedAt = -1.0f;
		for (const Vec2& Other : Parked)
		{
			if ((Other - Here).SizeSquared() > 4000.0 * 4000.0)
			{
				continue;
			}
			size_t Nearest = 0;
			double NearestDistance = 1e30;
			for (size_t i = 0; i < Mine.size() && i * Spacing < 3500.0; ++i)
			{
				const double D = (Mine[i] - Other).SizeSquared();
				if (D < NearestDistance)
				{
					NearestDistance = D;
					Nearest = i;
				}
			}
			const Vec2 Way = Nearest + 1 < Mine.size() ? (Mine[Nearest + 1] - Mine[Nearest]).Normal() : Forward;
			const double ToLeft = georoads::Dot(Other - Mine[Nearest], Vec2(Way.Y, -Way.X) * (bDriveLeft ? 1.0 : -1.0));
			if (std::sqrt(NearestDistance) < 300.0 && ToLeft > -50.0 && ToLeft < 230.0)
			{
				const float Needed = float(230.0 - ToLeft);
				PassingShift = std::max(PassingShift, Needed);
				if (Needed > 250.0f && Nearest > 0 && (BlockedAt < 0.0f || Nearest * Spacing < BlockedAt))
				{
					BlockedAt = float(Nearest * Spacing);
				}
			}
		}
		Target = Target - Left * std::min(PassingShift, 250.0f);
		const Vec2 ToTarget = Target - Here;
		const double Alpha = std::atan2(Cross(Forward, ToTarget), georoads::Dot(Forward, ToTarget));
		const double SteerRadians = std::atan(2.0 * Wheelbase * std::sin(Alpha) / std::max(ToTarget.Size() / 100.0, 1.0));
		const float Steer = std::clamp(float(SteerRadians * 180.0 / 3.14159265358979), -Pose.MaxSteerDegrees, Pose.MaxSteerDegrees);

		// speed: the slowest planned over the next stretch, and a gap to anything ahead in its lane (the player or traffic)
		float TargetSpeed = P[size_t(C.Progress)].TargetSpeed;
		double Ahead = 0.0;
		for (size_t i = size_t(C.Progress); i + 1 < P.size() && Ahead < (double(Speed) * 1.5 + 10.0) * 100.0; ++i)
		{
			TargetSpeed = std::min(TargetSpeed, P[i + 1].TargetSpeed);
			Ahead += georoads::Distance(P[i].Position, P[i + 1].Position);
		}
		// keeping out of the way, looking along the road it will drive (round bends, through junctions), not just
		// straight ahead: whoever is on it ahead it follows; whoever's road crosses or joins it there, the one getting
		// to the crossing later gives way (at the same time: the newer car)
		auto KeepBack = [&TargetSpeed](float Gap, float TheirSpeed)
		{
			// never closer than it can stop in, braking at about 0.4 g, and a couple of seconds back when following
			const float Stoppable = std::sqrt(std::max(0.0f, TheirSpeed * TheirSpeed + 2.0f * 4.0f * (Gap - 3.0f)));
			const float Following = Gap > 3.0f ? TheirSpeed + (Gap - 2.0f * TheirSpeed) * 0.25f : 0.0f;
			TargetSpeed = std::min(TargetSpeed, std::max(0.0f, std::min(Stoppable, std::max(Following, 0.0f) + 2.0f)));
		};
		if (BlockedAt >= 0.0f)
		{
			KeepBack(BlockedAt / 100.0f - 6.0f, 0.0f);
		}
		const double Reach = double(Mine.size()) * Spacing;
		for (const Mover& M : Others)
		{
			if (M.Id == C.Id || georoads::Distance(M.Position, Here) > Reach + 2000.0)
			{
				continue;
			}
			// on my road ahead (going my way: its body near my line; coming the other way: only if it's really in it)
			size_t Nearest = 0;
			double NearestDistance = 1e30;
			for (size_t i = 0; i < Mine.size(); ++i)
			{
				const double D = (Mine[i] - M.Position).SizeSquared();
				if (D < NearestDistance)
				{
					NearestDistance = D;
					Nearest = i;
				}
			}
			NearestDistance = std::sqrt(NearestDistance);
			const Vec2 Way = Nearest + 1 < Mine.size() ? (Mine[Nearest + 1] - Mine[Nearest]).Normal() : (Nearest > 0 ? (Mine[Nearest] - Mine[Nearest - 1]).Normal() : Forward);
			const double Facing = georoads::Dot(M.Forward, Way);
			if (Nearest > 0 && NearestDistance < (Facing > -0.5 ? 230.0 : 170.0))
			{
				KeepBack(float(Nearest * Spacing / 100.0) - 6.0f, float(std::max(0.0, georoads::Dot(M.Velocity * 0.01, Way))));
				continue;
			}
			// its road crossing or joining mine ahead
			if (M.Ahead.size() < 2 || georoads::Dot(M.Position - Here, Forward) < -300.0)
			{
				continue;	// (the player, whose way isn't known; or behind)
			}
			bool bFound = false;
			for (size_t i = 1; i < Mine.size() && !bFound; ++i)
			{
				for (size_t j = 0; j < M.Ahead.size(); ++j)
				{
					if ((Mine[i] - M.Ahead[j]).SizeSquared() > 300.0 * 300.0)
					{
						continue;
					}
					bFound = true;
					const Vec2 MyWay = (Mine[i] - Mine[i - 1]).Normal();
					const Vec2 TheirWay = j + 1 < M.Ahead.size() ? (M.Ahead[j + 1] - M.Ahead[j]).Normal() : (j > 0 ? (M.Ahead[j] - M.Ahead[j - 1]).Normal() : M.Forward);
					if (georoads::Dot(MyWay, TheirWay) < -0.7)
					{
						break;	// (meeting head on: each keeps to its own lane and they pass)
					}
					const float Mt = float(i * Spacing / 100.0) / std::max(Speed, 2.0f);
					const float Tt = float(j * Spacing / 100.0) / std::max(M.Speed, 2.0f);
					if (Mt > Tt + 0.3f || (Mt > Tt - 0.3f && C.Id > M.Id))
					{
						KeepBack(float(i * Spacing / 100.0) - 4.0f, 0.0f);	// (stopping short of it till it's by)
					}
					break;
				}
			}
		}
		C.LastSpeed = Speed;
		C.LastTarget = TargetSpeed;
		C.LastOffLane = float(BestDistance / 100.0);
		const float Error = TargetSpeed - Speed;
		const float SteerShare = std::fabs(Steer) / std::max(Pose.MaxSteerDegrees, 1.0f);
		float Throttle = std::clamp(Error * 0.3f, 0.0f, 0.8f) * std::clamp(1.2f - SteerShare, 0.2f, 1.0f);
		float Brake = Error < -0.3f ? std::clamp(-Error * 0.4f, 0.0f, 1.0f) : 0.0f;
		if (TargetSpeed < 0.3f && Speed < 0.8f)
		{
			Throttle = 0.0f;
			Brake = 1.0f;	// hold still behind whatever is stopped ahead
		}
		Host.DriveCar(C.Id, Steer, Throttle, Brake);

		// stuck (not just waiting behind someone) or on its roof for a while: it goes, and a new car comes elsewhere
		const Point& At = P[size_t(C.Progress)];
		C.Grace = std::max(0.0f, C.Grace - DeltaTime);
		C.StuckTime = (Speed < 0.5f && TargetSpeed > 1.0f) ? C.StuckTime + DeltaTime : 0.0f;
		C.FlippedTime = Pose.UpZ < 0.3f ? C.FlippedTime + DeltaTime : 0.0f;
		const bool bUnder = Pose.Height < At.Height - 800.0;
		if (C.StuckTime > 15.0f || C.FlippedTime > 3.0f || bUnder)
		{
			if (C.Grace <= 0.0f && bUnder)
			{
				Report("fell through the road", Here, At.Edge, Speed * 3.6f, float((At.Height - Pose.Height) / 100.0));
			}
			C.bGone = true;
			return;
		}

		// --- the road tester: what it feels while driving calmly is the road's fault ---
		if (C.Grace > 0.0f)
		{
			return;
		}
		int32_t Contacts = 0;
		float MaxLoad = 0.0f, MaxSlip = 0.0f;
		for (const geocar::WheelState& Wheel : Pose.State.Wheels)
		{
			Contacts += Wheel.bContact ? 1 : 0;
			MaxLoad = std::max(MaxLoad, Wheel.Load);
			MaxSlip = std::max(MaxSlip, Wheel.bContact ? std::fabs(Wheel.SlipAngleDegrees) : 0.0f);
		}
		const bool bCalm = MaxSlip < 8.0f && Speed < At.TargetSpeed + 3.0f && BestDistance < 400.0;
		if (Contacts == 0)
		{
			if (C.AirTime == 0.0f)
			{
				C.TakeOff = Here;
			}
			C.AirTime += DeltaTime;
		}
		else
		{
			if (C.AirTime >= 0.15f && bCalm)
			{
				Report("launch (left the ground)", C.TakeOff, At.Edge, Speed * 3.6f, C.AirTime);
			}
			C.AirTime = 0.0f;
		}
		const float StaticLoad = Pose.Mass * 9.81f * 0.25f;
		if (MaxLoad > StaticLoad * 3.2f && bCalm)
		{
			Report("jolt", Here, At.Edge, Speed * 3.6f, MaxLoad / StaticLoad);
		}
	}

	void Traffic::Report(const std::string& Type, const Vec2& Position, int32_t Edge, float SpeedKmh, float Size)
	{
		// the same kind of problem within 30 m is the same problem
		for (const auto& Known : Problems)
		{
			if (Known.first == Type && georoads::Distance(Known.second, Position) < 3000.0)
			{
				return;
			}
		}
		Problems.push_back({ Type, Position });
		if (!OnProblem)
		{
			return;
		}
		Problem Out;
		Out.Type = Type;
		Out.Position = Position;
		if (Edge >= 0 && Edge < int32_t(Roads->Edges.size()))
		{
			Out.Road = Roads->Edges[Edge].Name;
			Out.Highway = Roads->Edges[Edge].Highway;
		}
		Out.SpeedKmh = SpeedKmh;
		Out.Size = Size;
		OnProblem(Out);
	}

	void Traffic::SetRoads(std::shared_ptr<const RoadGraph> NewRoads, const Vec2& Shift)
	{
		// the next piece of the world: the same roads (with the same heights where the pieces overlap), numbered
		// differently and measured from its own point. Each car's roads are found again by their OSM way and ends; a car
		// on a road the new piece doesn't have whole goes.
		const RoadGraph& Old = *Roads;
		const RoadGraph& New = *NewRoads;
		std::multimap<int64_t, int32_t> NewByWay;
		for (size_t e = 0; e < New.Edges.size(); ++e)
		{
			NewByWay.insert({ New.Edges[e].WayId, int32_t(e) });
		}
		std::map<int32_t, int32_t> Mapped;
		auto Map = [&](int32_t OldEdge)
		{
			const auto Found = Mapped.find(OldEdge);
			if (Found != Mapped.end())
			{
				return Found->second;
			}
			int32_t Result = -1;
			if (OldEdge >= 0 && OldEdge < int32_t(Old.Edges.size()))
			{
				const RoadEdge& Edge = Old.Edges[OldEdge];
				const auto Range = NewByWay.equal_range(Edge.WayId);
				for (auto It = Range.first; It != Range.second; ++It)
				{
					const RoadEdge& Same = New.Edges[It->second];
					if (Same.Points.size() >= 2 && georoads::Distance(Same.Points.front(), Edge.Points.front() + Shift) < 500.0
						&& georoads::Distance(Same.Points.back(), Edge.Points.back() + Shift) < 500.0)
					{
						Result = It->second;
						break;
					}
				}
			}
			Mapped[OldEdge] = Result;
			return Result;
		};

		int32_t Kept = 0, Gone = 0;
		for (int32_t i = int32_t(Cars.size()) - 1; i >= 0; --i)
		{
			Car& C = Cars[size_t(i)];
			// what is behind it is no longer needed
			if (C.Progress > 1)
			{
				C.Path.erase(C.Path.begin(), C.Path.begin() + (C.Progress - 1));
				C.Progress = 1;
			}
			bool bFound = Map(C.LastEdge) >= 0;
			for (const Point& Pt : C.Path)
			{
				bFound = bFound && Map(Pt.Edge) >= 0;
			}
			if (!bFound)
			{
				Host.RemoveCar(C.Id);
				Cars.erase(Cars.begin() + i);
				++Gone;
				continue;
			}
			for (Point& Pt : C.Path)
			{
				Pt.Edge = Map(Pt.Edge);
				Pt.Position = Pt.Position + Shift;
			}
			C.TakeOff = C.TakeOff + Shift;
			C.LastEdge = Map(C.LastEdge);
			++Kept;
		}
		for (auto& Known : Problems)
		{
			Known.second = Known.second + Shift;
		}
		if (Log)
		{
			Log("[Traffic] onto the next piece of the world: " + std::to_string(Kept) + " cars carry on, " + std::to_string(Gone) + " had to go");
		}
		Roads = std::move(NewRoads);
		NetworkDistance.clear();
		PlayerEdge = -1;
		NetworkTimer = 0.0f;
	}
}
