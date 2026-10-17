// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadNetwork.h"
#include "RoadDesign.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace georoads
{
	namespace
	{
		/** Nodes closer than this (cm) on the same layer are merged, so slightly misaligned junctions still connect */
		constexpr double MergeDistance = 200.0;

		/**
		 *  Road cross-sections per class (m), from South African guidance: CSIR "Red Book" ch. 7 (lanes 3.7 m on major
		 *  and divided roads, 3.4 m for middling roads, 3.1 m residential; shoulders up to 3.0 m at high speed, 1.5 m+
		 *  above 60 km/h) and the RTMC Road Safety Manual (ideal lane 3.7 m, shoulder desirable 3.0 m), with residential
		 *  streets 5.5-7.0 m kerb to kerb. A one-way way is one carriageway of a divided road: its own lanes, a full
		 *  shoulder on the outside and a narrow one by the median (OSM draws each direction of a dual road separately).
		 */
		struct CrossSection
		{
			int32_t Lanes = 2;			// both directions (one direction on one-way ways)
			float LaneWidth = 3.4f;
			float Shoulder = 0.0f;		// each side; on a one-way way the outside (the driver's left keeping left, right keeping right)
			float InnerShoulder = -1.0f;	// one-way: by the median (driver's right); -1 = same as Shoulder
		};

		CrossSection SectionSA(const std::string& Highway, bool bOneway)
		{
			// the wide shoulder on the left of the N2 and other main roads (Street View near George: about 3 m outside,
			// under 1 m by the median)
			const std::string Class = EndsWith(Highway, "_link") ? std::string("link") : Highway;
			if (Class == "motorway") { return { 2, 3.7f, 3.0f, 1.0f }; }		// always one carriageway of a divided road
			if (Class == "trunk") { return bOneway ? CrossSection{ 2, 3.7f, 3.0f, 0.75f } : CrossSection{ 2, 3.7f, 3.0f }; }
			if (Class == "primary") { return bOneway ? CrossSection{ 2, 3.5f, 2.0f, 0.5f } : CrossSection{ 2, 3.5f, 2.0f }; }
			if (Class == "secondary") { return bOneway ? CrossSection{ 2, 3.4f, 1.25f, 0.5f } : CrossSection{ 2, 3.4f, 1.5f }; }
			if (Class == "tertiary") { return bOneway ? CrossSection{ 1, 3.4f, 1.0f, 0.5f } : CrossSection{ 2, 3.4f, 0.5f }; }
			if (Class == "link") { return { 1, 3.7f, 1.0f }; }
			if (Class == "unclassified" || Class == "road") { return { 2, 3.1f, 0.5f }; }
			if (Class == "residential") { return bOneway ? CrossSection{ 1, 3.5f, 0.0f } : CrossSection{ 2, 3.25f, 0.0f }; }
			if (Class == "living_street") { return { 2, 2.75f, 0.0f }; }
			if (Class == "service") { return { 1, 3.5f, 0.5f }; }
			if (Class == "track") { return { 1, 3.0f, 0.0f }; }
			if (Class == "raceway") { return { 2, 6.0f, 1.5f }; }	// (a circuit: ~12 m of track, a little run-off each side)
			return { 2, 3.0f, 0.0f };
		}

		/** UK cross-sections (m), from DMRB CD 127 for motorways and trunk roads and Manual for Streets for streets; the
		 *  rest is our choice (docs/ROAD_RULES_UK.md 7c). Urban roads (40 mph or less) have no hard strips. */
		CrossSection SectionUK(const std::string& Highway, bool bOneway, bool bUrban)
		{
			if (Highway == "motorway") { return { 3, 3.65f, bUrban ? 2.75f : 3.30f, 0.70f }; }
			if (Highway == "motorway_link") { return { 1, 3.70f, 3.30f, 0.70f }; }
			if (Highway == "trunk") { return bOneway ? CrossSection{ 2, 3.65f, bUrban ? 0.0f : 1.0f, bUrban ? 0.0f : 1.0f } : CrossSection{ 2, 3.65f, bUrban ? 0.0f : 1.0f }; }
			if (Highway == "trunk_link") { return { 1, 3.70f, 1.0f, 0.70f }; }
			if (Highway == "primary") { return bOneway ? CrossSection{ 2, 3.65f, bUrban ? 0.0f : 1.0f, bUrban ? 0.0f : 0.70f } : CrossSection{ 2, 3.5f, bUrban ? 0.0f : 0.5f }; }
			if (Highway == "secondary") { return bOneway ? CrossSection{ 1, 3.65f, bUrban ? 0.0f : 0.5f, bUrban ? 0.0f : 0.5f } : CrossSection{ 2, 3.25f, bUrban ? 0.0f : 0.25f }; }
			if (Highway == "tertiary") { return bOneway ? CrossSection{ 1, 3.5f, 0.0f, 0.0f } : CrossSection{ 2, 3.0f, 0.0f }; }
			if (EndsWith(Highway, "_link")) { return { 1, 3.5f, bUrban ? 0.0f : 0.5f, bUrban ? 0.0f : 0.5f }; }
			if (Highway == "residential" || Highway == "unclassified" || Highway == "road") { return bOneway ? CrossSection{ 1, 3.5f, 0.0f } : CrossSection{ 2, 2.75f, 0.0f }; }
			if (Highway == "living_street") { return { 1, 4.5f, 0.0f }; }
			if (Highway == "service") { return { 1, 3.7f, 0.0f }; }
			if (Highway == "track") { return { 1, 3.0f, 0.0f }; }
			if (Highway == "raceway") { return { 2, 6.0f, 1.5f }; }	// (a circuit: ~12 m of track, a little run-off each side)
			return { 2, 2.75f, 0.0f };
		}

		bool Is(const std::string* Value, const char* What) { return Value && *Value == What; }

		/**
		 *  A UK road's speed limit (km/h): the signed limit in mph, or the national limit from maxspeed:type (GB:nsl_single
		 *  60 mph, GB:nsl_dual and motorways 70, restricted roads 30, zones), or the class default: 30 mph in streets and
		 *  wherever the road is lit, 70 on motorways and dual carriageways, 60 elsewhere (docs/ROAD_RULES_UK.md 8b)
		 */
		float SpeedLimitUK(const OsmWay& Way, const std::string& Highway, bool bOneway)
		{
			constexpr float Mph = 1.609f;
			const bool bDual = StartsWith(Highway, "motorway") || (bOneway && (StartsWith(Highway, "trunk") || StartsWith(Highway, "primary")));
			auto FromCode = [&](const std::string& Code) -> float
			{
				if (EndsWith(Code, "nsl_single")) { return 60.0f * Mph; }
				if (EndsWith(Code, "nsl_dual") || EndsWith(Code, "motorway")) { return 70.0f * Mph; }
				if (Code == "GB-WLS:nsl_restricted") { return 20.0f * Mph; }
				if (EndsWith(Code, "nsl_restricted")) { return 30.0f * Mph; }
				if (EndsWith(Code, "zone20")) { return 20.0f * Mph; }
				if (EndsWith(Code, "zone30")) { return 30.0f * Mph; }
				if (EndsWith(Code, "zone40")) { return 40.0f * Mph; }
				if (Code == "national") { return (bDual ? 70.0f : 60.0f) * Mph; }
				return 0.0f;
			};
			if (const std::string* MaxSpeed = Way.Tag("maxspeed"))
			{
				if (const float Signed = ParseMaxSpeed(*MaxSpeed); Signed > 0.0f)
				{
					return Signed;
				}
				if (const float Coded = FromCode(*MaxSpeed); Coded > 0.0f)
				{
					return Coded;
				}
			}
			for (const char* Key : { "maxspeed:type", "source:maxspeed" })
			{
				if (const std::string* Type = Way.Tag(Key))
				{
					if (const float Coded = FromCode(*Type); Coded > 0.0f)
					{
						return Coded;
					}
				}
			}
			if (bDual)
			{
				return 70.0f * Mph;
			}
			if (Highway == "residential" || Highway == "living_street" || Highway == "service" || Is(Way.Tag("lit"), "yes"))
			{
				return 30.0f * Mph;
			}
			return 60.0f * Mph;
		}

		/** Reads a number from tags like "7", "7.5 m" or "7.5m" */
		bool ParseNumber(const std::string* Value, float& Out)
		{
			if (!Value)
			{
				return false;
			}
			std::string Number;
			for (const char C : *Value)
			{
				if (C != 'm')
				{
					Number += C == ',' ? '.' : C;
				}
			}
			size_t First = 0, Last = Number.size();
			while (First < Last && std::isspace(static_cast<unsigned char>(Number[First]))) { ++First; }
			while (Last > First && std::isspace(static_cast<unsigned char>(Number[Last - 1]))) { --Last; }
			Number = Number.substr(First, Last - First);
			// an optional sign, digits and at most one point
			size_t i = (!Number.empty() && (Number[0] == '-' || Number[0] == '+')) ? 1 : 0;
			bool bDot = false;
			for (; i < Number.size(); ++i)
			{
				if (Number[i] == '.' && !bDot)
				{
					bDot = true;
				}
				else if (!std::isdigit(static_cast<unsigned char>(Number[i])))
				{
					return false;
				}
			}
			Out = static_cast<float>(std::atof(Number.c_str()));
			return Out > 0.0f;
		}

		int32_t RoundToInt(float X) { return static_cast<int32_t>(std::floor(X + 0.5f)); }

		/** How a road's width is made up (m): its lanes and the shoulder on each side, where the lines are painted */
		struct Layout
		{
			float Width = 0.0f;
			int32_t Lanes = 2;
			float ShoulderLeft = 0.0f;
			float ShoulderRight = 0.0f;
		};

		bool IsOneway(const OsmWay& Way, const std::string& Highway)
		{
			const std::string* Oneway = Way.Tag("oneway");
			return Is(Oneway, "yes") || Is(Oneway, "1") || Is(Oneway, "-1") || Highway == "motorway" || Is(Way.Tag("junction"), "roundabout");
		}

		Layout RoadLayout(const OsmWay& Way, const std::string& Highway, const CountryRules& Country)
		{
			// tagged lanes count with the class's lane and shoulder widths; otherwise the class's usual cross-section (the
			// country's own, if its rules give one)
			const bool bUK = Country.IsUK();
			const bool bOneway = IsOneway(Way, Highway);
			CrossSection Section = bUK ? SectionUK(Highway, bOneway, SpeedLimitUK(Way, Highway, bOneway) <= 65.0f) : SectionSA(Highway, bOneway);
			const auto Own = Country.Sections.find(Highway);
			if (!bUK && Own != Country.Sections.end())
			{
				const std::array<float, 4>& S = Own->second;
				Section = { std::max(1, RoundToInt(S[0])), S[1], S[2], bOneway ? S[3] : -1.0f };
				if (bOneway && Section.Lanes > 1 && S[0] >= 2.0f && !EndsWith(Highway, "_link") && Highway != "motorway")
				{
					Section.Lanes = std::max(1, Section.Lanes / 2);	// (a class given both ways: one carriageway's)
				}
			}

			// "service" covers very different roads; the service tag says which (SA Red Book sizes): a plain one (an estate
			// or complex road, an access lane) lets two cars pass, a parking aisle is 6 m, a driveway one car
			if (Highway == "service")
			{
				const std::string* Kind = Way.Tag("service");
				if (Is(Kind, "driveway"))
				{
					Section = { 1, 3.0f, 0.0f };
				}
				else if (Is(Kind, "parking_aisle"))
				{
					Section = { 2, 3.0f, 0.0f };
				}
				else if (bOneway || Is(Kind, "alley") || Is(Kind, "drive-through") || Is(Kind, "emergency_access"))
				{
					Section = { 1, 3.5f, 0.0f };
				}
				else
				{
					Section = bUK ? CrossSection{ 1, 3.7f, 0.0f } : CrossSection{ 2, 2.75f, 0.0f };	// UK: Manual for Streets access width
				}
			}

			float Lanes = 0.0f;
			if (ParseNumber(Way.Tag("lanes"), Lanes) && Lanes <= 10.0f)
			{
				Section.Lanes = std::max(1, RoundToInt(Lanes));
			}

			// shoulder=no / left / right / both|yes says which sides have one, and shoulder:width how wide; without them
			// the class's usual shoulder goes on both sides
			bool bLeft = true, bRight = true;
			if (const std::string* Shoulder = Way.Tag("shoulder"))
			{
				bLeft = *Shoulder != "no" && *Shoulder != "right";
				bRight = *Shoulder != "no" && *Shoulder != "left";
			}
			float ShoulderWidth = 0.0f;
			if (ParseNumber(Way.Tag("shoulder:width"), ShoulderWidth) && ShoulderWidth < 5.0f)
			{
				Section.Shoulder = ShoulderWidth;
				Section.InnerShoulder = -1.0f;
			}

			// (the outer shoulder on the driver's left keeping left, on the right keeping right; the narrow one by the median)
			const float Outer = Section.Shoulder, Inner = Section.InnerShoulder >= 0.0f ? Section.InnerShoulder : Section.Shoulder;
			Layout Out;
			Out.Lanes = Section.Lanes;
			Out.ShoulderLeft = bLeft ? (Country.bDriveLeft ? Outer : Inner) : 0.0f;
			Out.ShoulderRight = bRight ? (Country.bDriveLeft ? Inner : Outer) : 0.0f;
			Out.Width = Section.Lanes * Section.LaneWidth + Out.ShoulderLeft + Out.ShoulderRight;

			// a tagged width wins: lanes keep their size where it fits, the rest is shoulder (shared out as tagged)
			float Width = 0.0f;
			if (ParseNumber(Way.Tag("width"), Width) && Width < 40.0f)
			{
				// shared in the same proportion as the usual shoulders (most of it on the outside of a one-way way)
				const float Spare = std::max(0.0f, Width - Section.Lanes * Section.LaneWidth);
				const float Usual = Out.ShoulderLeft + Out.ShoulderRight;
				const int32_t Sides = (bLeft ? 1 : 0) + (bRight ? 1 : 0);
				const float LeftShare = Usual > 0.0f ? Out.ShoulderLeft / Usual : (Sides > 0 && bLeft ? 1.0f / Sides : 0.0f);
				Out.ShoulderLeft = Sides > 0 ? Spare * LeftShare : 0.0f;
				Out.ShoulderRight = Sides > 0 ? Spare - Out.ShoulderLeft : 0.0f;
				Out.Width = Width;
			}
			return Out;
		}

		Surface SurfaceFor(const OsmWay& Way, const std::string& Highway)
		{
			const std::string* Tag = Way.Tag("surface");
			if (!Tag)
			{
				return Highway == "track" ? Surface::Loose : Surface::Asphalt;
			}
			static const std::unordered_set<std::string> Paved = { "paving_stones", "sett", "cobblestone", "unhewn_cobblestone", "bricks", "concrete:plates" };
			static const std::unordered_set<std::string> Loose = { "compacted", "gravel", "fine_gravel", "pebblestone", "dirt", "ground",
				"earth", "mud", "sand", "grass", "unpaved", "rock" };
			if (Paved.count(*Tag))
			{
				return Surface::Paved;
			}
			return Loose.count(*Tag) ? Surface::Loose : Surface::Asphalt;
		}

		/** Merges graph nodes within MergeDistance on the same layer using a spatial hash */
		struct NodeMerger
		{
			RoadGraph& Graph;
			std::unordered_map<uint64_t, std::vector<int32_t>> Cells;

			explicit NodeMerger(RoadGraph& InGraph) : Graph(InGraph) {}

			static uint64_t Key(int32_t X, int32_t Y, int32_t Layer)
			{
				return (static_cast<uint64_t>(static_cast<uint32_t>(X) & 0x3FFFFFF) << 38) | (static_cast<uint64_t>(static_cast<uint32_t>(Y) & 0x3FFFFFF) << 12)
					| (static_cast<uint32_t>(Layer) & 0xFFF);
			}

			int32_t FindOrAdd(const Vec2& Position, int32_t Layer)
			{
				const int32_t CX = static_cast<int32_t>(std::floor(Position.X / MergeDistance));
				const int32_t CY = static_cast<int32_t>(std::floor(Position.Y / MergeDistance));
				for (int32_t X = -1; X <= 1; ++X)
				{
					for (int32_t Y = -1; Y <= 1; ++Y)
					{
						const auto Nearby = Cells.find(Key(CX + X, CY + Y, Layer));
						if (Nearby == Cells.end())
						{
							continue;
						}
						for (const int32_t Index : Nearby->second)
						{
							if ((Graph.Nodes[Index].Position - Position).SizeSquared() < MergeDistance * MergeDistance)
							{
								return Index;
							}
						}
					}
				}

				const int32_t Index = static_cast<int32_t>(Graph.Nodes.size());
				RoadNode& Node = Graph.Nodes.emplace_back();
				Node.Position = Position;
				Node.Layer = Layer;
				Cells[Key(CX, CY, Layer)].push_back(Index);
				return Index;
			}
		};

		/** Eases width changes between roads that carry on into each other (RoadEdge::Taper) */
		void AddTapers(RoadGraph& Graph, const NetworkOptions& Options)
		{
			// where exactly two roads meet (one road carrying on, its tags changing) and their widths or shoulders differ,
			// both ease to the middle over a taper: about 1 in 80 sideways per edge, as a lane gain or drop is laid out, 15-60 m
			int32_t Tapers = 0;
			for (int32_t NodeIndex = 0; NodeIndex < static_cast<int32_t>(Graph.Nodes.size()); ++NodeIndex)
			{
				const RoadNode& Node = Graph.Nodes[NodeIndex];
				if (Node.Edges.size() != 2 || Node.Edges[0] == Node.Edges[1])
				{
					continue;
				}
				RoadEdge& A = Graph.Edges[Node.Edges[0]];
				RoadEdge& B = Graph.Edges[Node.Edges[1]];
				const bool bAStarts = A.StartNode == NodeIndex, bBStarts = B.StartNode == NodeIndex;

				// the shoulders seen going through the node from A into B: one road arriving and the other leaving keep their
				// left and right; two arriving (or two leaving) swap them
				const bool bSameWay = bAStarts != bBStarts;
				const float BLeft = bSameWay ? B.ShoulderLeft : B.ShoulderRight;
				const float BRight = bSameWay ? B.ShoulderRight : B.ShoulderLeft;
				const float Difference = std::max({ std::abs(A.HalfWidth - B.HalfWidth), std::abs(A.ShoulderLeft - BLeft), std::abs(A.ShoulderRight - BRight) });
				if (Difference < 5.0f)
				{
					continue;
				}

				const float MidHalf = (A.HalfWidth + B.HalfWidth) * 0.5f;
				const float MidLeft = (A.ShoulderLeft + BLeft) * 0.5f, MidRight = (A.ShoulderRight + BRight) * 0.5f;
				const float Length = std::clamp(Difference * 40.0f, 1500.0f, 6000.0f);
				auto Set = [&](RoadEdge& Edge, bool bAtStart, float Left, float Right)
				{
					double EdgeLength = 0.0;
					for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
					{
						EdgeLength += Distance(Edge.Points[i], Edge.Points[i + 1]);
					}
					RoadEdge::Taper& Taper = bAtStart ? Edge.StartTaper : Edge.EndTaper;
					Taper.HalfWidth = MidHalf - Edge.HalfWidth;
					Taper.ShoulderLeft = Left - Edge.ShoulderLeft;
					Taper.ShoulderRight = Right - Edge.ShoulderRight;
					Taper.Length = static_cast<float>(std::min(static_cast<double>(Length), EdgeLength * 0.45));
				};
				Set(A, bAStarts, MidLeft, MidRight);
				Set(B, bBStarts, bSameWay ? MidLeft : MidRight, bSameWay ? MidRight : MidLeft);
				++Tapers;
			}
			if (Options.Log)
			{
				Options.Log(Format("[Roads] %d places where a road changes width eased into a taper", Tapers));
			}
		}
	}

	bool IsDrivable(const OsmWay& Way, std::string& OutHighway)
	{
		const std::string* Highway = Way.Tag("highway");
		if (!Highway || Way.Nodes.size() < 2)
		{
			return false;
		}
		// tunnels are kept (built as a tube with a portal at each end); service tunnels (escape bores, car park ramps)
		// are left out. Open galleries and passages through a building are at ground level, ordinary roads
		const std::string* Tunnel = Way.Tag("tunnel");
		if (Tunnel && *Tunnel != "no" && *Tunnel != "avalanche_protector" && *Tunnel != "building_passage" && *Highway == "service")
		{
			return false;
		}
		OutHighway = *Highway;
		return true;
	}

	void BuildRoadGraph(const OsmData& Data, RoadGraph& Out, const NetworkOptions& Options)
	{
		// how many road ways use each node: shared nodes are junctions
		std::unordered_map<int64_t, int32_t> Usage;
		for (const OsmWay& Way : Data.Ways)
		{
			std::string Highway;
			if (IsDrivable(Way, Highway))
			{
				for (const int64_t Node : Way.Nodes)
				{
					++Usage[Node];
				}
			}
		}

		NodeMerger Merger(Out);

		// ways that share an OSM node are joined there whatever their layers: a bridge's end is the same point as the
		// road leading onto it. Only nodes that are merely close by stay apart by layer (a road passing under a bridge)
		std::unordered_map<int64_t, int32_t> GraphNodeOf;

		for (const OsmWay& Way : Data.Ways)
		{
			std::string Highway;
			if (!IsDrivable(Way, Highway))
			{
				continue;
			}

			float LayerValue = 0.0f;
			ParseNumber(Way.Tag("layer"), LayerValue);
			const std::string* Bridge = Way.Tag("bridge");
			const bool bBridge = Bridge && *Bridge != "no";
			// a gallery is on the ground whatever layer it is mapped on (Chapman's Peak has one at -1, cut off from the road)
			const std::string* Tunnel = Way.Tag("tunnel");
			const bool bGallery = Is(Tunnel, "avalanche_protector");
			const int32_t Layer = (bGallery ? 0 : RoundToInt(LayerValue)) + (bBridge ? 1 : 0);

			const std::string* Oneway = Way.Tag("oneway");
			const bool bOneway = IsOneway(Way, Highway);
			const bool bReversed = Is(Oneway, "-1");

			const Layout Shape = RoadLayout(Way, Highway, Options.Country);
			const float HalfWidth = Shape.Width * 50.0f;	// metres -> half width in cm
			const Surface Kind = SurfaceFor(Way, Highway);
			const std::string* NameTag = Way.Tag("name");
			const std::string* RefTag = Way.Tag("ref");
			const std::string Name = NameTag ? *NameTag : (RefTag ? *RefTag : std::string());
			const std::string* MaxSpeedTag = Way.Tag("maxspeed");
			// (the signed limit; without one, the country's limit for the class, if its rules give one)
			float SpeedKmh = Options.bUK ? SpeedLimitUK(Way, Highway, bOneway) : (MaxSpeedTag ? ParseMaxSpeed(*MaxSpeedTag) : 0.0f);
			if (SpeedKmh <= 0.0f && !Options.bUK)
			{
				const CountryRules& C = Options.Country;
				const auto Limit = C.SpeedKmh.find(Highway);
				SpeedKmh = Limit != C.SpeedKmh.end() ? Limit->second : 0.0f;
				// (in town: a lit road; the town's limit for its class, or the town limit, never above the road's own)
				if (Is(Way.Tag("lit"), "yes"))
				{
					const auto Urban = C.UrbanSpeedKmh.find(Highway);
					const float Town = Urban != C.UrbanSpeedKmh.end() ? Urban->second : C.UrbanKmh;
					if (Town > 0.0f)
					{
						SpeedKmh = SpeedKmh > 0.0f ? std::min(SpeedKmh, Town) : Town;
					}
				}
			}

			// oneway=-1 runs against the way's drawing direction: turn it round so the points run the way traffic goes
			std::vector<int64_t> WayNodes = Way.Nodes;
			if (bReversed)
			{
				std::reverse(WayNodes.begin(), WayNodes.end());
			}

			// split the way into edges at every shared node
			std::vector<Vec2> Points;
			int32_t StartNode = -1;
			int32_t Part = 0;
			for (size_t i = 0; i < WayNodes.size(); ++i)
			{
				const auto Found = Data.Nodes.find(WayNodes[i]);
				if (Found == Data.Nodes.end())
				{
					continue;
				}
				const Vec2& Position = Found->second;

				if (Points.empty() || std::abs(Points.back().X - Position.X) > 1.0 || std::abs(Points.back().Y - Position.Y) > 1.0)
				{
					Points.push_back(Position);
				}

				const auto Uses = Usage.find(WayNodes[i]);
				const bool bSplit = i == 0 || i == WayNodes.size() - 1 || (Uses != Usage.end() && Uses->second > 1);
				if (!bSplit)
				{
					continue;
				}

				const auto Shared = GraphNodeOf.find(WayNodes[i]);
				const int32_t GraphNode = Shared != GraphNodeOf.end() ? Shared->second : Merger.FindOrAdd(Position, Layer);
				GraphNodeOf[WayNodes[i]] = GraphNode;

				auto AddEdge = [&](int32_t From, int32_t To, std::vector<Vec2> EdgePoints)
				{
					// snap the ends to the (possibly merged) node positions
					EdgePoints.front() = Out.Nodes[From].Position;
					EdgePoints.back() = Out.Nodes[To].Position;

					const int32_t EdgeIndex = static_cast<int32_t>(Out.Edges.size());
					RoadEdge& Edge = Out.Edges.emplace_back();
					Edge.StartNode = From;
					Edge.EndNode = To;
					Edge.Points = std::move(EdgePoints);
					Edge.HalfWidth = HalfWidth;
					Edge.Lanes = Shape.Lanes;
					// shoulders are tagged for the way's drawing direction; a way reversed for oneway=-1 swaps them
					Edge.ShoulderLeft = (bReversed ? Shape.ShoulderRight : Shape.ShoulderLeft) * 100.0f;
					Edge.ShoulderRight = (bReversed ? Shape.ShoulderLeft : Shape.ShoulderRight) * 100.0f;
					Edge.Kind = Kind;
					Edge.Layer = Layer;
					Edge.bOneway = bOneway;
					Edge.Highway = Highway;
					Edge.Name = Name;
					Edge.SpeedKmh = SpeedKmh;
					Edge.bBridge = bBridge;
					Edge.bGallery = bGallery;
					Edge.bTunnel = Tunnel && *Tunnel != "no" && !bGallery && *Tunnel != "building_passage";
					Edge.WayId = Way.Id;
					Edge.WayPart = Part++;

					Out.Nodes[From].Edges.push_back(EdgeIndex);
					Out.Nodes[To].Edges.push_back(EdgeIndex);
				};

				if (StartNode != -1 && Points.size() >= 2)
				{
					if (GraphNode != StartNode)
					{
						AddEdge(StartNode, GraphNode, Points);
					}
					else if (Points.size() >= 3)
					{
						// a loop back to where it started (a turning circle, a road round a block): an edge can't start and
						// end at the same node, so it's split in the middle instead of being lost
						const size_t Middle = Points.size() / 2;
						const int32_t MiddleNode = Merger.FindOrAdd(Points[Middle], Layer);
						if (MiddleNode != StartNode)
						{
							AddEdge(StartNode, MiddleNode, std::vector<Vec2>(Points.begin(), Points.begin() + Middle + 1));
							AddEdge(MiddleNode, GraphNode, std::vector<Vec2>(Points.begin() + Middle, Points.end()));
						}
					}
				}

				StartNode = GraphNode;
				Points.clear();
				Points.push_back(Position);
			}
		}

		if (Options.bTapers)
		{
			AddTapers(Out, Options);
		}
	}
}
