// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "RoadNetwork.h"
#include "RoadDesign.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
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

		/** A road that stops just short of another (a car park's entrance drawn to the kerb, not to the road's middle line;
		 *  a carriageway ending a metre from its other half) is a dead end in the map but plainly meant to join it. Each dead
		 *  end within LooseEndGap of another road's edge, on the same level, is joined to it: at that road's end if it is
		 *  close, otherwise where it is nearest (that road split in two there, the dead end's node moved onto it) */
		void JoinLooseEnds(RoadGraph& Graph)
		{
			constexpr double LooseEndGap = 250.0;	// cm beyond the other road's edge
			constexpr double AtEnd = 300.0;	// cm: nearer than this to the other road's end, joined to that end
			constexpr double Cell = 2000.0;
			auto CellKey = [](int64_t X, int64_t Y) { return (static_cast<uint64_t>(X & 0xFFFFFFFF) << 32) | static_cast<uint64_t>(Y & 0xFFFFFFFF); };
			std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Grid;	// cell -> (edge, segment)
			auto Register = [&](int32_t EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				for (int32_t i = 0; i + 1 < static_cast<int32_t>(Edge.Points.size()); ++i)
				{
					const Vec2& A = Edge.Points[i];
					const Vec2& B = Edge.Points[i + 1];
					for (int64_t X = static_cast<int64_t>(std::floor(std::min(A.X, B.X) / Cell)); X <= static_cast<int64_t>(std::floor(std::max(A.X, B.X) / Cell)); ++X)
					{
						for (int64_t Y = static_cast<int64_t>(std::floor(std::min(A.Y, B.Y) / Cell)); Y <= static_cast<int64_t>(std::floor(std::max(A.Y, B.Y) / Cell)); ++Y)
						{
							Grid[CellKey(X, Y)].emplace_back(EdgeIndex, i);
						}
					}
				}
			};
			for (int32_t EdgeIndex = 0; EdgeIndex < static_cast<int32_t>(Graph.Edges.size()); ++EdgeIndex)
			{
				Register(EdgeIndex);
			}
			int32_t Joined = 0;
			const int32_t NumNodes = static_cast<int32_t>(Graph.Nodes.size());
			for (int32_t NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
			{
				if (Graph.Nodes[NodeIndex].Edges.size() != 1)
				{
					continue;
				}
				const int32_t Own = Graph.Nodes[NodeIndex].Edges[0];
				const Vec2 End = Graph.Nodes[NodeIndex].Position;
				// the nearest other road on the same level, within its half width and the gap
				int32_t BestEdge = -1, BestSegment = -1;
				double BestGap = LooseEndGap, BestT = 0.0;
				const int64_t CX = static_cast<int64_t>(std::floor(End.X / Cell)), CY = static_cast<int64_t>(std::floor(End.Y / Cell));
				for (int64_t X = CX - 1; X <= CX + 1; ++X)
				{
					for (int64_t Y = CY - 1; Y <= CY + 1; ++Y)
					{
						const auto Found = Grid.find(CellKey(X, Y));
						if (Found == Grid.end())
						{
							continue;
						}
						for (const auto& [Other, Segment] : Found->second)
						{
							const RoadEdge& Road = Graph.Edges[Other];
							if (Other == Own || Road.StartNode == NodeIndex || Road.EndNode == NodeIndex || Road.Layer != Graph.Nodes[NodeIndex].Layer
								|| Road.bTunnel != Graph.Edges[Own].bTunnel)
							{
								continue;
							}
							const Vec2 A = Road.Points[Segment], AB = Road.Points[Segment + 1] - A;
							const double Length2 = AB.SizeSquared();
							const double T = Length2 > 1.0 ? std::clamp(Dot(End - A, AB) / Length2, 0.0, 1.0) : 0.0;
							const double Gap = Distance(A + AB * T, End) - Road.HalfWidth;
							if (Gap < BestGap)
							{
								BestGap = Gap;
								BestEdge = Other;
								BestSegment = Segment;
								BestT = T;
							}
						}
					}
				}
				if (BestEdge < 0)
				{
					continue;
				}
				RoadEdge& Road = Graph.Edges[BestEdge];
				const Vec2 Onto = Road.Points[BestSegment] + (Road.Points[BestSegment + 1] - Road.Points[BestSegment]) * BestT;
				RoadEdge& Mine = Graph.Edges[Own];
				Vec2& MyEnd = Mine.StartNode == NodeIndex ? Mine.Points.front() : Mine.Points.back();
				// near one of its ends: joined there (this dead end's node left with no road)
				for (const int32_t Target : { Road.StartNode, Road.EndNode })
				{
					if (Distance(Graph.Nodes[Target].Position, Onto) < AtEnd)
					{
						(Mine.StartNode == NodeIndex ? Mine.StartNode : Mine.EndNode) = Target;
						MyEnd = Graph.Nodes[Target].Position;
						Graph.Nodes[Target].Edges.push_back(Own);
						Graph.Nodes[NodeIndex].Edges.clear();
						break;
					}
				}
				if (Graph.Nodes[NodeIndex].Edges.empty())
				{
					++Joined;
					continue;
				}
				// otherwise the road is split where it is nearest, and this dead end's node moved there to be the junction
				const int32_t Second = static_cast<int32_t>(Graph.Edges.size());
				RoadEdge Rest = Road;
				Rest.Points.assign(Road.Points.begin() + BestSegment + 1, Road.Points.end());
				Rest.Points.insert(Rest.Points.begin(), Onto);
				Rest.StartNode = NodeIndex;
				Rest.WayPart = Road.WayPart;
				Road.Points.resize(BestSegment + 1);
				Road.Points.push_back(Onto);
				std::vector<int32_t>& FarEdges = Graph.Nodes[Road.EndNode].Edges;
				std::replace(FarEdges.begin(), FarEdges.end(), BestEdge, Second);
				Road.EndNode = NodeIndex;
				Graph.Nodes[NodeIndex].Position = Onto;
				MyEnd = Onto;
				Graph.Nodes[NodeIndex].Edges.push_back(BestEdge);
				Graph.Nodes[NodeIndex].Edges.push_back(Second);
				Graph.Edges.push_back(std::move(Rest));
				// (the split road's two halves found again for the dead ends still to come)
				for (auto& Item : Grid)
				{
					Item.second.erase(std::remove_if(Item.second.begin(), Item.second.end(), [BestEdge](const std::pair<int32_t, int32_t>& Entry) { return Entry.first == BestEdge; }), Item.second.end());
				}
				Register(BestEdge);
				Register(Second);
				++Joined;
			}
			(void)Joined;
		}

		/** Tunnels and bridges as the map draws them, tidied (BeamNG.Earth's roads.py does the same): a short stretch in the
		 *  open between two pieces of tunnel (under ShortGap) is a slip of the map, not daylight, and becomes tunnel, so the
		 *  road doesn't pop out and back in (Monaco's tunnels were full of them); a short tunnel between two bridges (under
		 *  ShortTunnel) is a passage through a bridge's tower or pier (Tower Bridge's), and becomes bridge */
		void TidyTunnels(RoadGraph& Graph, const LogFn& Log)
		{
			constexpr double ShortGap = 3000.0, ShortTunnel = 4000.0;	// cm
			auto LengthOf = [](const RoadEdge& Edge)
			{
				double L = 0.0;
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					L += Distance(Edge.Points[i], Edge.Points[i + 1]);
				}
				return L;
			};
			// the road carrying on through a plain join (a node of just two roads) at either end; -1 at a junction or a dead end
			auto Beyond = [&](int32_t EdgeIndex, int32_t Node)
			{
				const std::vector<int32_t>& Here = Graph.Nodes[Node].Edges;
				if (Here.size() != 2)
				{
					return -1;
				}
				return Here[0] == EdgeIndex ? Here[1] : Here[0];
			};
			int32_t Closed = 0, Bridged = 0;
			for (int32_t EdgeIndex = 0; EdgeIndex < static_cast<int32_t>(Graph.Edges.size()); ++EdgeIndex)
			{
				RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (Edge.StartNode == Edge.EndNode)
				{
					continue;
				}
				const int32_t A = Beyond(EdgeIndex, Edge.StartNode), B = Beyond(EdgeIndex, Edge.EndNode);
				if (A < 0 || B < 0 || A == EdgeIndex || B == EdgeIndex)
				{
					continue;
				}
				const RoadEdge& Before = Graph.Edges[A];
				const RoadEdge& After = Graph.Edges[B];
				if (!Edge.bTunnel && !Edge.bBridge && Before.bTunnel && After.bTunnel && LengthOf(Edge) < ShortGap)
				{
					Edge.bTunnel = true;
					Edge.Layer = std::max(Before.Layer, After.Layer);
					++Closed;
				}
				else if (Edge.bTunnel && Before.bBridge && After.bBridge && LengthOf(Edge) < ShortTunnel)
				{
					Edge.bTunnel = false;
					Edge.bBridge = true;
					Edge.Layer = std::min(Before.Layer, After.Layer);
					++Bridged;
				}
			}
			if (Log && Closed + Bridged > 0)
			{
				Log(Format("[Roads] tunnels tidied: %d short stretches between tunnels made tunnel, %d short tunnels between bridges made bridge", Closed, Bridged));
			}
		}

		/** A road underground is drawn by hand from below ground (nobody could trace it from the air), and its line comes out
		 *  kinked: Monaco's tunnels turned up to 50 degrees in 10 m where the real ones curve smoothly. Each is laid as a road
		 *  is: its points spread evenly (every FairStep), then faired: each moved towards where its neighbours two either
		 *  side would put it on a straight or evenly bending line (the fourth difference to nothing), held lightly to the
		 *  map's line and never further from it than FairReach; beyond each end, the road it carries straight on from there
		 *  stands in for its next points, so it leaves and meets its junctions in line with them. The road editor's "flow"
		 *  (road_editor.gd, L) is the same. Roads in the open (traced from photos), roundabouts and short links stay as mapped */
		void FairUnderground(RoadGraph& Graph, const LogFn& Log)
		{
			constexpr double FairStep = 400.0;		// cm between a faired road's points
			constexpr double MinLength = 2000.0;	// cm: shorter roads (links in junctions) stay as mapped
			constexpr double FairReach = 400.0;		// cm a point may move from the map's line, at most
			constexpr int32_t Rounds = 300;
			constexpr double Fair = 0.04, Hold = 0.005;
			int32_t Faired = 0;
			double Most = 0.0;
			for (int32_t EdgeIndex = 0; EdgeIndex < static_cast<int32_t>(Graph.Edges.size()); ++EdgeIndex)
			{
				RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (!(Edge.bTunnel || Edge.Layer < 0) || Edge.bRoundabout || Edge.StartNode == Edge.EndNode || Edge.Points.size() < 2)
				{
					continue;
				}
				// (evenly spread)
				double Length = 0.0;
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					Length += Distance(Edge.Points[i], Edge.Points[i + 1]);
				}
				if (Length < MinLength)
				{
					continue;
				}
				const int32_t N = std::max(4, static_cast<int32_t>(std::lround(Length / FairStep)) + 1);
				std::vector<Vec2> P(N);
				{
					size_t Segment = 0;
					double SegmentStart = 0.0;
					for (int32_t k = 0; k < N; ++k)
					{
						const double Want = Length * k / (N - 1);
						while (Segment + 2 < Edge.Points.size() && SegmentStart + Distance(Edge.Points[Segment], Edge.Points[Segment + 1]) < Want)
						{
							SegmentStart += Distance(Edge.Points[Segment], Edge.Points[Segment + 1]);
							++Segment;
						}
						const double L = Distance(Edge.Points[Segment], Edge.Points[Segment + 1]);
						const double T = L > 1.0e-6 ? std::clamp((Want - SegmentStart) / L, 0.0, 1.0) : 0.0;
						P[k] = Edge.Points[Segment] + (Edge.Points[Segment + 1] - Edge.Points[Segment]) * T;
					}
					P.front() = Edge.Points.front();
					P.back() = Edge.Points.back();
				}
				const std::vector<Vec2> Mapped = P;
				// (beyond each end: where it would be carrying straight on from the road it joins there most in line)
				auto CarryOn = [&](int32_t Node, const Vec2& At, const Vec2& Next)
				{
					const Vec2 Out = (Next - At).Normal();
					const double Step = Distance(At, Next);
					Vec2 Best = At - (Next - At);
					double BestDot = 0.3;
					for (const int32_t Other : Graph.Nodes[Node].Edges)
					{
						const RoadEdge& Road = Graph.Edges[Other];
						if (Other == EdgeIndex || Road.Points.size() < 2)
						{
							continue;
						}
						const Vec2 Beside = Road.StartNode == Node ? Road.Points[1] : Road.Points[Road.Points.size() - 2];
						const Vec2 In = (At - Beside).Normal();
						const double D = Dot(In, Out);
						if (D > BestDot)
						{
							BestDot = D;
							Best = At + In * Step;
						}
					}
					return Best;
				};
				const Vec2 Before = CarryOn(Edge.StartNode, P[0], P[1]);
				const Vec2 After = CarryOn(Edge.EndNode, P[N - 1], P[N - 2]);
				std::vector<Vec2> Q(N);
				for (int32_t Round = 0; Round < Rounds; ++Round)
				{
					Q = P;
					for (int32_t i = 1; i + 1 < N; ++i)
					{
						const Vec2 A = i >= 2 ? P[i - 2] : Before;
						const Vec2 D = i + 2 < N ? P[i + 2] : After;
						const Vec2 Fourth = A - P[i - 1] * 4.0 + P[i] * 6.0 - P[i + 1] * 4.0 + D;
						Vec2 Next = P[i] - Fourth * Fair + (Mapped[i] - P[i]) * Hold;
						const Vec2 Off = Next - Mapped[i];
						if (Off.Size() > FairReach)
						{
							Next = Mapped[i] + Off * (FairReach / Off.Size());
						}
						Q[i] = Next;
					}
					std::swap(P, Q);
				}
				for (int32_t i = 0; i < N; ++i)
				{
					Most = std::max(Most, Distance(P[i], Mapped[i]));
				}
				Edge.Points = std::move(P);
				++Faired;
			}
			if (Log && Faired > 0)
			{
				Log(Format("[Roads] %d roads underground laid to flow (kinks of the hand-drawn map eased into curves, at most %.1f m off it)", Faired, Most / 100.0));
			}
		}

		/** How wide a street really is, where OSM doesn't say (no width or lanes tag), as BeamNG.Earth does it (server/
		 *  roadwidth.py): where buildings stand at the back of its pavements (a town centre, an old town; not a suburb
		 *  with front gardens, nor the country) the gap between their walls across it is the street, pavements and all;
		 *  a pavement each side (a share of it) taken off leaves the road. Every MeasureStep along it, away from its ends,
		 *  a look each way for the nearest wall; trusted only where both sides find one most of the way and the gaps
		 *  agree. Elsewhere in town, in a country whose town streets have parking along them (the UK, the rest of
		 *  Europe), a parking strip each side; a South African suburb keeps its class's width, its cars park on the verge */
		void MeasureStreets(RoadGraph& Graph, const OsmData& Land, const std::vector<uint8_t>& Tagged, const NetworkOptions& Options)
		{
			constexpr double MeasureStep = 600.0, EndSkip = 1000.0, Reach = 2500.0;	// cm
			constexpr int32_t MinSamples = 4;
			constexpr double Walled = 0.6;			// share of the looks finding a wall both sides
			constexpr double Agree = 0.3;			// the gaps' spread (IQR) at most this share of their median
			constexpr double InTown = 0.5;			// share finding a wall on at least one side: a street in town
			constexpr double WalkShare = 0.2, WalkMin = 180.0, WalkMax = 450.0;	// each pavement: this share of the gap, within these (cm)
			constexpr double CarriageMin = 500.0;	// a measured road narrower than this is not trusted (cm)
			constexpr double MaxTimes = 3.0;		// nor one more than this many times its class's usual width
			constexpr float Parking = 200.0f;		// cm of parking strip each side (Europe's 2-2.5 m parking lanes)
			constexpr double Cell = 1600.0;
			auto IsMeasured = [](const std::string& H)
			{
				return H == "residential" || H == "unclassified" || H == "tertiary" || H == "secondary" || H == "primary" || H == "living_street";
			};
			auto HasParking = [](const std::string& H)
			{
				return H == "residential" || H == "unclassified" || H == "tertiary" || H == "secondary" || H == "living_street";
			};
			const bool bParkingCountry = Options.Country.Style == RoadStyle::UK || Options.Country.Style == RoadStyle::Europe || Options.Country.Style == RoadStyle::US;

			// every building's walls, bucketed
			using Wall = std::pair<Vec2, Vec2>;
			std::unordered_map<uint64_t, std::vector<Wall>> Walls;
			auto Key = [](int64_t X, int64_t Y) { return (static_cast<uint64_t>(static_cast<uint32_t>(X)) << 32) | static_cast<uint32_t>(Y); };
			for (const OsmWay& Way : Land.Ways)
			{
				if (!Way.Tag("building") || Way.Nodes.size() < 3)
				{
					continue;
				}
				for (size_t i = 0; i + 1 < Way.Nodes.size(); ++i)
				{
					const auto A = Land.Nodes.find(Way.Nodes[i]), B = Land.Nodes.find(Way.Nodes[i + 1]);
					if (A == Land.Nodes.end() || B == Land.Nodes.end())
					{
						continue;
					}
					const int64_t X0 = static_cast<int64_t>(std::floor(std::min(A->second.X, B->second.X) / Cell)), X1 = static_cast<int64_t>(std::floor(std::max(A->second.X, B->second.X) / Cell));
					const int64_t Y0 = static_cast<int64_t>(std::floor(std::min(A->second.Y, B->second.Y) / Cell)), Y1 = static_cast<int64_t>(std::floor(std::max(A->second.Y, B->second.Y) / Cell));
					for (int64_t X = X0; X <= X1; ++X)
					{
						for (int64_t Y = Y0; Y <= Y1; ++Y)
						{
							Walls[Key(X, Y)].emplace_back(A->second, B->second);
						}
					}
				}
			}
			if (Walls.empty() && !bParkingCountry)
			{
				return;
			}
			// how far from P along Dir to the nearest wall (cm), within Reach; -1 for none
			auto Look = [&](const Vec2& P, const Vec2& Dir)
			{
				const Vec2 End = P + Dir * Reach;
				double Best = -1.0;
				const int64_t X0 = static_cast<int64_t>(std::floor(std::min(P.X, End.X) / Cell)), X1 = static_cast<int64_t>(std::floor(std::max(P.X, End.X) / Cell));
				const int64_t Y0 = static_cast<int64_t>(std::floor(std::min(P.Y, End.Y) / Cell)), Y1 = static_cast<int64_t>(std::floor(std::max(P.Y, End.Y) / Cell));
				for (int64_t X = X0; X <= X1; ++X)
				{
					for (int64_t Y = Y0; Y <= Y1; ++Y)
					{
						const auto Found = Walls.find(Key(X, Y));
						if (Found == Walls.end())
						{
							continue;
						}
						for (const Wall& W : Found->second)
						{
							const Vec2 E = W.second - W.first;
							const double Denominator = Dir.X * E.Y - Dir.Y * E.X;
							if (std::abs(Denominator) < 1.0e-9)
							{
								continue;
							}
							const Vec2 D = W.first - P;
							const double T = (D.X * E.Y - D.Y * E.X) / Denominator;	// (along the look)
							const double U = (D.X * Dir.Y - D.Y * Dir.X) / Denominator;	// (along the wall)
							if (T > 0.0 && T <= Reach && U >= 0.0 && U <= 1.0 && (Best < 0.0 || T < Best))
							{
								Best = T;
							}
						}
					}
				}
				return Best;
			};

			int32_t Measured = 0, Parked = 0;
			for (int32_t EdgeIndex = 0; EdgeIndex < static_cast<int32_t>(Graph.Edges.size()); ++EdgeIndex)
			{
				RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (Tagged[EdgeIndex] || !IsMeasured(Edge.Highway) || Edge.bBridge || Edge.bTunnel || Edge.Layer != 0 || Edge.bOneway)
				{
					continue;	// (a one-way way is often one half of a divided road: the gap between the buildings is both)
				}
				double Length = 0.0;
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					Length += Distance(Edge.Points[i], Edge.Points[i + 1]);
				}
				std::vector<double> Gaps, Sides;
				int32_t Looks = 0, Either = 0;
				double Along = 0.0;
				for (size_t i = 0; i + 1 < Edge.Points.size(); ++i)
				{
					const Vec2 A = Edge.Points[i], B = Edge.Points[i + 1];
					const double L = Distance(A, B);
					const Vec2 Dir = (B - A).Normal();
					const Vec2 Left(-Dir.Y, Dir.X);
					for (double S = std::fmod(MeasureStep - std::fmod(Along, MeasureStep), MeasureStep); S < L; S += MeasureStep)
					{
						const double At = Along + S;
						if (At < EndSkip || At > Length - EndSkip)
						{
							continue;
						}
						const Vec2 P = A + Dir * S;
						const double DL = Look(P, Left), DR = Look(P, Left * -1.0);
						++Looks;
						Either += DL > 0.0 || DR > 0.0 ? 1 : 0;
						for (const double D : { DL, DR })
						{
							if (D > 0.0)
							{
								Sides.push_back(D);
							}
						}
						if (DL > 0.0 && DR > 0.0)
						{
							Gaps.push_back(DL + DR);
						}
					}
					Along += L;
				}
				if (Looks < MinSamples)
				{
					continue;
				}
				const double Usual = 2.0 * Edge.HalfWidth;
				if (Gaps.size() >= Walled * Looks)
				{
					std::sort(Gaps.begin(), Gaps.end());
					const double Median = Gaps[Gaps.size() / 2];
					const double Spread = Gaps[(Gaps.size() * 3) / 4] - Gaps[Gaps.size() / 4];
					if (Median > 0.0 && Spread <= Agree * Median)
					{
						const double Walk = std::clamp(WalkShare * Median, WalkMin, WalkMax);
						const double Carriage = Median - 2.0 * Walk;
						if (Carriage >= CarriageMin && Carriage <= MaxTimes * Usual)
						{
							Edge.HalfWidth = static_cast<float>(0.5 * Carriage);
							Edge.ShoulderLeft = Edge.ShoulderRight = 0.0f;	// (kerbs: the road is lanes and parking, the pavements beyond)
							++Measured;
							continue;
						}
					}
				}
				// (parking only where it fits: the walls near it most of the way stand clear of the wider road and a pavement)
				std::sort(Sides.begin(), Sides.end());
				const bool bRoom = !Sides.empty() && Sides[Sides.size() / 4] >= Edge.HalfWidth + Parking + WalkMin;
				if (bParkingCountry && HasParking(Edge.Highway) && Either >= InTown * Looks && bRoom)
				{
					Edge.HalfWidth += Parking;
					Edge.ShoulderLeft += Parking;
					Edge.ShoulderRight += Parking;
					++Parked;
				}
			}
			if (Options.Log)
			{
				Options.Log(Format("[Roads] street widths: %d measured between the buildings either side, %d given parking along them (in town, %s)",
					Measured, Parked, bParkingCountry ? "this country parks on the street" : "none: this country parks off it"));
			}
		}

		/** A roundabout is mapped as a ring of roads; given its lanes, the ring the map drew can leave no island in the middle
		 *  and its roads' mouths crowding each other (Giratoire Canton under Monaco: a ring 12 m across with 9.5 m of lanes,
		 *  six roads joining it). The ring is made as wide as its joins need: round its outer edge, each two neighbouring
		 *  roads' mouths and a kerb between them; never less than a real island in the middle. It grows out from its centre,
		 *  the roads joining it easing out to meet it, never more than the shortest of them leaves room for. */
		void WidenRoundabouts(RoadGraph& Graph, const LogFn& Log)
		{
			constexpr double MinIsland = 200.0;		// cm, the island's radius at least (UK CD 116: a normal roundabout's island is 4 m across or more)
			constexpr double MouthGap = 0.0;		// cm round the outer edge between two neighbouring roads' mouths beyond their own widths (their kerbs may meet)
			// how far its radius may grow (cm, and times the map's): a ring underground was drawn from memory and may move further; one in
			// the open was traced from aerial photos and is mostly right
			constexpr double MaxGrowUnder = 1000.0, MaxScaleUnder = 2.0, MaxGrowOpen = 400.0, MaxScaleOpen = 1.25;
			constexpr double Blend = 3000.0;		// cm beyond its outer edge over which the stretch fades to nothing

			const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
			std::vector<int32_t> Parent(NumEdges);
			for (int32_t i = 0; i < NumEdges; ++i)
			{
				Parent[i] = i;
			}
			auto Find = [&Parent](int32_t i) { while (Parent[i] != i) { i = Parent[i] = Parent[Parent[i]]; } return i; };
			for (const RoadNode& Node : Graph.Nodes)
			{
				int32_t First = -1;
				for (const int32_t Edge : Node.Edges)
				{
					if (!Graph.Edges[Edge].bRoundabout)
					{
						continue;
					}
					if (First < 0)
					{
						First = Edge;
					}
					else
					{
						Parent[Find(Edge)] = Find(First);
					}
				}
			}
			std::unordered_map<int32_t, std::vector<int32_t>> Rings;
			for (int32_t Edge = 0; Edge < NumEdges; ++Edge)
			{
				if (Graph.Edges[Edge].bRoundabout && Graph.Edges[Edge].StartNode != Graph.Edges[Edge].EndNode)
				{
					Rings[Find(Edge)].push_back(Edge);
				}
			}

			int32_t Widened = 0;
			for (auto& [Root, Edges] : Rings)
			{
				// a closed ring: every node on it has two of its roads
				std::unordered_map<int32_t, int32_t> Uses;
				for (const int32_t Edge : Edges)
				{
					++Uses[Graph.Edges[Edge].StartNode];
					++Uses[Graph.Edges[Edge].EndNode];
				}
				bool bClosed = Edges.size() >= 2;
				for (const auto& [Node, Count] : Uses)
				{
					bClosed &= Count == 2;
				}
				if (!bClosed)
				{
					continue;	// (cut by the edge of the map, or not a ring)
				}
				// its centre and radius (along its middle), from its line weighted by length; its lanes' half width
				Vec2 Centre;
				double Length = 0.0;
				float RingHalf = 0.0f;
				for (const int32_t Edge : Edges)
				{
					const std::vector<Vec2>& P = Graph.Edges[Edge].Points;
					for (size_t i = 0; i + 1 < P.size(); ++i)
					{
						const double L = Distance(P[i], P[i + 1]);
						Centre = Centre + (P[i] + P[i + 1]) * (0.5 * L);
						Length += L;
					}
					RingHalf = std::max(RingHalf, Graph.Edges[Edge].MaxHalfWidth());
				}
				if (Length < 1000.0)
				{
					continue;
				}
				Centre = Centre * (1.0 / Length);
				double Radius = 0.0;
				for (const int32_t Edge : Edges)
				{
					const std::vector<Vec2>& P = Graph.Edges[Edge].Points;
					for (size_t i = 0; i + 1 < P.size(); ++i)
					{
						Radius += Distance(Centre, (P[i] + P[i + 1]) * 0.5) * Distance(P[i], P[i + 1]);
					}
				}
				Radius /= Length;

				// the roads joining it: where round the ring (each node's mouth: its roads side by side), and how wide
				struct Mouth { double Angle; double Half; int32_t Node; std::string Name; };
				std::vector<Mouth> Mouths;
				for (const auto& [Node, Count] : Uses)
				{
					double Half = 0.0;
					std::string Name;
					for (const int32_t Edge : Graph.Nodes[Node].Edges)
					{
						const RoadEdge& Road = Graph.Edges[Edge];
						if (Road.bRoundabout)
						{
							continue;
						}
						Half += Road.MaxHalfWidth();
						Name = Road.Name;
					}
					if (Half > 0.0)
					{
						const Vec2 D = Graph.Nodes[Node].Position - Centre;
						Mouths.push_back({ std::atan2(D.Y, D.X), Half, Node, Name });
					}
				}
				std::sort(Mouths.begin(), Mouths.end(), [](const Mouth& A, const Mouth& B) { return A.Angle < B.Angle; });
				double Needed = MinIsland + RingHalf;
				for (size_t i = 0; i < Mouths.size() && Mouths.size() >= 2; ++i)
				{
					const Mouth& A = Mouths[i];
					const Mouth& B = Mouths[(i + 1) % Mouths.size()];
					double Gap = B.Angle - A.Angle;
					if (Gap <= 0.0)
					{
						Gap += 2.0 * 3.14159265358979;
					}
					if (!A.Name.empty() && A.Name == B.Name)
					{
						continue;	// (the two halves of one road, in and out: side by side as the map has them)
					}
					// (round the outer edge: the arc between the two nodes holds half of each mouth and the kerb between)
					Needed = std::max(Needed, (A.Half + B.Half + MouthGap) / std::max(Gap, 0.05) - RingHalf);
				}
				bool bUnder = false;
				for (const int32_t Edge : Edges)
				{
					bUnder |= Graph.Edges[Edge].bTunnel || Graph.Edges[Edge].Layer < 0;
				}
				const double NewRadius = std::min({ Needed, Radius + (bUnder ? MaxGrowUnder : MaxGrowOpen), (bUnder ? MaxScaleUnder : MaxScaleOpen) * Radius });
				if (NewRadius < Radius + 50.0)
				{
					continue;
				}
				const double Scale = NewRadius / Radius;

				// the map stretched round it like a rubber sheet: the ring and everything inside grow with it; beyond its outer
				// edge every road and junction (at any level) is pushed out by less and less, to nothing Blend further on, so
				// whatever joins it, and the junctions just beyond, stay joined exactly as the map has them
				const double Outer = Radius + RingHalf, Push = (NewRadius - Radius) * (Outer / Radius);
				const double Reach = std::max(Blend, 2.5 * Push);	// (wide enough that the stretch never folds back)
				auto Warp = [&](Vec2& P)
				{
					const Vec2 D = P - Centre;
					const double R = D.Size();
					if (R < 1.0 || R > Outer + Reach)
					{
						return;
					}
					const double T = std::clamp((R - Outer) / Reach, 0.0, 1.0);
					const double NewR = R <= Outer ? R * (Outer + Push) / Outer : R + Push * (1.0 - T * T * (3.0 - 2.0 * T));
					P = Centre + D * (NewR / R);
				};
				for (RoadNode& Node : Graph.Nodes)
				{
					Warp(Node.Position);
				}
				for (RoadEdge& Road : Graph.Edges)
				{
					for (Vec2& P : Road.Points)
					{
						Warp(P);
					}
				}
				++Widened;
				if (Log)
				{
					Log(Format("[Roads] roundabout%s at x %.0f y %.0f widened: %.1f -> %.1f m across its middle (%d roads join it; island %.1f -> %.1f m across)",
						bUnder ? " (underground)" : "", Centre.X, Centre.Y, 2.0 * Radius / 100.0, 2.0 * NewRadius / 100.0, static_cast<int32_t>(Mouths.size()),
						std::max(0.0, 2.0 * (Radius - RingHalf)) / 100.0, std::max(0.0, 2.0 * (NewRadius - RingHalf)) / 100.0));
				}
			}
			if (Log && Widened > 0)
			{
				Log(Format("[Roads] %d roundabouts made wide enough for the roads joining them", Widened));
			}
		}

		/** A dual carriageway is mapped as two one-way roads, one each way, their middle lines as far apart as the map drew
		 *  them; given the class's usual width each, the two halves can lie over each other (Boulevard du Larvotto's were
		 *  9.6 m each with their lines 6 m apart). Where a one-way road runs beside another going the other way on the same
		 *  level, both are made to fit the space between their lines: their shoulders first, then their lanes, never under
		 *  a narrow lane each */
		void NarrowTwins(RoadGraph& Graph)
		{
			constexpr double Cell = 2000.0;
			constexpr float MinHalf = 160.0f;	// cm: the narrowest a one-way half gets
			constexpr double Gap = 30.0;	// cm left between the two
			auto CellKey = [](int64_t X, int64_t Y) { return (static_cast<uint64_t>(X & 0xFFFFFFFF) << 32) | static_cast<uint64_t>(Y & 0xFFFFFFFF); };
			std::unordered_map<uint64_t, std::vector<std::pair<int32_t, int32_t>>> Grid;
			const int32_t NumEdges = static_cast<int32_t>(Graph.Edges.size());
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (!Edge.bOneway)
				{
					continue;
				}
				for (int32_t i = 0; i + 1 < static_cast<int32_t>(Edge.Points.size()); ++i)
				{
					const Vec2 Middle = (Edge.Points[i] + Edge.Points[i + 1]) * 0.5;
					Grid[CellKey(static_cast<int64_t>(std::floor(Middle.X / Cell)), static_cast<int64_t>(std::floor(Middle.Y / Cell)))].emplace_back(EdgeIndex, i);
				}
			}
			// the closest each one-way road comes to a twin beside it (its line to the twin's line)
			std::vector<double> Closest(NumEdges, std::numeric_limits<double>::max());
			std::vector<int32_t> Twin(NumEdges, -1);
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				const RoadEdge& Edge = Graph.Edges[EdgeIndex];
				if (!Edge.bOneway)
				{
					continue;
				}
				for (int32_t i = 0; i + 1 < static_cast<int32_t>(Edge.Points.size()); ++i)
				{
					const Vec2 A = Edge.Points[i], AB = Edge.Points[i + 1] - A;
					const double Length = AB.Size();
					if (Length < 100.0)
					{
						continue;
					}
					const Vec2 Middle = A + AB * 0.5;
					const int64_t CX = static_cast<int64_t>(std::floor(Middle.X / Cell)), CY = static_cast<int64_t>(std::floor(Middle.Y / Cell));
					for (int64_t X = CX - 1; X <= CX + 1; ++X)
					{
						for (int64_t Y = CY - 1; Y <= CY + 1; ++Y)
						{
							const auto Found = Grid.find(CellKey(X, Y));
							if (Found == Grid.end())
							{
								continue;
							}
							for (const auto& [Other, Segment] : Found->second)
							{
								const RoadEdge& Road = Graph.Edges[Other];
								if (Other == EdgeIndex || Road.Layer != Edge.Layer || Road.bTunnel != Edge.bTunnel)
								{
									continue;
								}
								const Vec2 C = Road.Points[Segment], CD = Road.Points[Segment + 1] - C;
								const double LengthCD = CD.Size();
								// going the other way, side by side (not a junction's corner)
								if (LengthCD < 100.0 || Dot(AB, CD) / (Length * LengthCD) > -0.9)
								{
									continue;
								}
								const double T = Dot(Middle - C, CD) / (LengthCD * LengthCD);
								if (T <= 0.0 || T >= 1.0)
								{
									continue;
								}
								const double Apart = Distance(C + CD * T, Middle);
								if (Apart < Edge.HalfWidth + Road.HalfWidth + std::max(Edge.ShoulderLeft, Edge.ShoulderRight) + std::max(Road.ShoulderLeft, Road.ShoulderRight)
									&& Apart < Closest[EdgeIndex])
								{
									Closest[EdgeIndex] = Apart;
									Twin[EdgeIndex] = Other;
								}
							}
						}
					}
				}
			}
			for (int32_t EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
			{
				if (Twin[EdgeIndex] < 0)
				{
					continue;
				}
				// this half's share of the space: in proportion to its own width and its twin's
				RoadEdge& Edge = Graph.Edges[EdgeIndex];
				const RoadEdge& Other = Graph.Edges[Twin[EdgeIndex]];
				const double Own = Edge.HalfWidth + std::max(Edge.ShoulderLeft, Edge.ShoulderRight);
				const double Theirs = Other.HalfWidth + std::max(Other.ShoulderLeft, Other.ShoulderRight);
				const double Share = (Closest[EdgeIndex] - Gap) * Own / std::max(1.0, Own + Theirs);
				double Over = Own - Share;
				if (Over <= 0.0)
				{
					continue;
				}
				const double Cut = std::min(Over, Own - Edge.HalfWidth);	// (the shoulders first)
				for (float* Shoulder : { &Edge.ShoulderLeft, &Edge.ShoulderRight })
				{
					*Shoulder = static_cast<float>(std::max(0.0, *Shoulder - Cut));
				}
				Over -= Cut;
				if (Over > 0.0)
				{
					Edge.HalfWidth = static_cast<float>(std::max(static_cast<double>(MinHalf), Edge.HalfWidth - Over));
				}
			}
		}

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
		std::vector<uint8_t> WidthTagged;	// (per edge: its way's width or lanes are mapped, so it isn't measured)
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
			const uint8_t bWidthTagged = Way.Tag("width") || Way.Tag("lanes") ? 1 : 0;
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
					Edge.bRoundabout = Is(Way.Tag("junction"), "roundabout") || Is(Way.Tag("junction"), "circular");
					Edge.WayId = Way.Id;
					Edge.WayPart = Part++;

					Out.Nodes[From].Edges.push_back(EdgeIndex);
					Out.Nodes[To].Edges.push_back(EdgeIndex);
					WidthTagged.push_back(bWidthTagged);
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

		JoinLooseEnds(Out);
		WidthTagged.resize(Out.Edges.size(), 0);	// (pieces JoinLooseEnds split off: as if untagged)
		if (Options.Land)
		{
			MeasureStreets(Out, *Options.Land, WidthTagged, Options);
		}
		TidyTunnels(Out, Options.Log);
		NarrowTwins(Out);
		WidenRoundabouts(Out, Options.Log);
		FairUnderground(Out, Options.Log);
		if (Options.bTapers)
		{
			AddTapers(Out, Options);
		}
	}
}
