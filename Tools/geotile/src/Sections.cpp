// Geodrive tile tool - GPL (see Core/README.md)

#include "Sections.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "BritishGrid.h"
#include "HeightTiles.h"
#include "OsmJson.h"
#include "PMTiles.h"
#include "TileArchive.h"

namespace geotile
{
	namespace
	{
		const char* Classes[] = { "motorway", "trunk", "primary", "secondary", "tertiary", "residential", "service", "lane" };
		constexpr int NumClasses = 8;

		const std::string* TagOf(const Way& W, const char* Key)
		{
			for (const Tag& T : W.Tags)
			{
				if (T.first == Key)
				{
					return &T.second;
				}
			}
			return nullptr;
		}

		/** The road class, or -1 for what isn't a road for cars */
		int ClassOf(const std::string& Highway)
		{
			std::string Base = Highway;
			if (Base.size() > 5 && Base.compare(Base.size() - 5, 5, "_link") == 0)
			{
				Base.resize(Base.size() - 5);
			}
			for (int i = 0; i < 5; ++i)
			{
				if (Base == Classes[i])
				{
					return i;
				}
			}
			if (Base == "residential" || Base == "living_street") { return 5; }
			if (Base == "service") { return 6; }
			if (Base == "unclassified" || Base == "track") { return 7; }
			return -1;
		}

		/** Heights out of a heights archive, square by square as they are needed */
		struct FHeights
		{
			std::vector<uint8_t> File;
			PmHeader Header;
			std::vector<PmEntry> Entries;
			std::unordered_map<uint64_t, HeightTile> Squares;

			explicit FHeights(const std::string& Path) : File(ReadFile(Path))
			{
				std::string Error;
				if (!ReadPmTiles(File.data(), File.size(), Header, Entries, Error))
				{
					throw std::runtime_error(Error);
				}
				std::sort(Entries.begin(), Entries.end(), [](const PmEntry& A, const PmEntry& B) { return A.TileId < B.TileId; });
			}

			const HeightTile* Square(int EKm, int NKm)
			{
				const uint64_t Id = HeightTileId(EKm, NKm);
				auto Found = Squares.find(Id);
				if (Found != Squares.end())
				{
					return Found->second.IsEmpty() ? nullptr : &Found->second;
				}
				HeightTile& Tile = Squares[Id];
				const auto It = std::lower_bound(Entries.begin(), Entries.end(), Id, [](const PmEntry& E, uint64_t V) { return E.TileId < V; });
				std::vector<uint8_t> Raw;
				std::string Error;
				if (It != Entries.end() && It->TileId == Id && UnzipArchiveTile(File.data(), Header, *It, Raw, Error))
				{
					DecodeHeightTile(Raw.data(), Raw.size(), Tile);
					if (!Tile.IsEmpty() && Tile.CellMetres != 1.0)
					{
						throw std::runtime_error("cross-sections need 1 m heights (geotile lidar --cell=1)");
					}
				}
				return Tile.IsEmpty() ? nullptr : &Tile;
			}

			/** Bilinear height at grid metres; NaN where there is none */
			float At(double E, double N)
			{
				auto Cell = [&](long long Col, long long Row) -> float
				{
					// a cell anywhere in the grid: col/row counted from E 0 eastwards and N 0 northwards, cell centres
					const HeightTile* T = Square(int(std::floor(Col / 1000.0)), int(std::floor(Row / 1000.0)));
					if (!T)
					{
						return NoHeight;
					}
					const int X = int(Col - T->SouthWestE), Y = T->Size - 1 - int(Row - T->SouthWestN);
					return (X >= 0 && Y >= 0 && X < T->Size && Y < T->Size) ? T->Heights[size_t(Y) * T->Size + X] : NoHeight;
				};
				// (1 m cells: column = floor(E), row = floor(N), centred at +0.5)
				const double PX = E - 0.5, PY = N - 0.5;
				const long long X0 = (long long)std::floor(PX), Y0 = (long long)std::floor(PY);
				const float FX = float(PX - X0), FY = float(PY - Y0);
				const float H00 = Cell(X0, Y0), H10 = Cell(X0 + 1, Y0), H01 = Cell(X0, Y0 + 1), H11 = Cell(X0 + 1, Y0 + 1);
				const float Low = H00 + (H10 - H00) * FX, High = H01 + (H11 - H01) * FX;
				return Low + (High - Low) * FY;
			}
		};

		float Median(std::vector<float>& V)
		{
			if (V.empty())
			{
				return NoHeight;
			}
			std::nth_element(V.begin(), V.begin() + V.size() / 2, V.end());
			return V[V.size() / 2];
		}

		float Quantile(std::vector<float> V, double Q)
		{
			if (V.empty())
			{
				return NoHeight;
			}
			std::sort(V.begin(), V.end());
			return V[std::min(V.size() - 1, size_t(Q * (V.size() - 1)))];
		}
	}

	int RoadSections(const std::string& RegionPath, const std::string& HeightsPath, double Lat, double Lon, double Km, const std::string& OutCsv)
	{
		// the roads and buildings of the area
		const double HalfLat = Km / 111.32, HalfLon = Km / (111.32 * std::cos(Lat * 3.14159265358979323846 / 180.0));
		const std::vector<uint8_t> Region = ReadFile(RegionPath);
		OsmSet Set;
		std::string Error;
		auto E7 = [](double D) { return int32_t(std::llround(D * 1e7)); };
		if (!ReadArchiveArea(Region.data(), Region.size(), E7(Lat - HalfLat), E7(Lon - HalfLon), E7(Lat + HalfLat), E7(Lon + HalfLon), Set, Error))
		{
			throw std::runtime_error(Error);
		}
		FHeights Heights(HeightsPath);

		std::unordered_map<int64_t, std::pair<double, double>> Grid;	// node -> E, N
		for (const auto& [Id, P] : Set.Nodes)
		{
			double E, N;
			ToBritishGrid(P.Lat / 1e7, P.Lon / 1e7, E, N);
			Grid[Id] = { E, N };
		}

		// towns: buildings within 40 m (centres, in 50 m cells)
		std::map<std::pair<int, int>, int> BuildingCells;
		std::unordered_map<int64_t, int> RoadUses;
		for (const Way& W : Set.Ways)
		{
			if (TagOf(W, "building") && !W.Nodes.empty())
			{
				double SE = 0.0, SN = 0.0;
				int Count = 0;
				for (int64_t Id : W.Nodes)
				{
					auto It = Grid.find(Id);
					if (It != Grid.end()) { SE += It->second.first; SN += It->second.second; ++Count; }
				}
				if (Count > 0)
				{
					++BuildingCells[{ int(std::floor(SE / Count / 50.0)), int(std::floor(SN / Count / 50.0)) }];
				}
			}
			const std::string* Highway = TagOf(W, "highway");
			if (Highway && ClassOf(*Highway) >= 0)
			{
				for (int64_t Id : W.Nodes)
				{
					++RoadUses[Id];
				}
			}
		}
		auto Urban = [&](double E, double N)
		{
			const int CX = int(std::floor(E / 50.0)), CY = int(std::floor(N / 50.0));
			int Count = 0;
			for (int DX = -1; DX <= 1; ++DX)
			{
				for (int DY = -1; DY <= 1; ++DY)
				{
					auto It = BuildingCells.find({ CX + DX, CY + DY });
					Count += It == BuildingCells.end() ? 0 : It->second;
				}
			}
			return Count >= 3;
		};

		// sections every 10 m, away from junctions and ends, out to 15 m each side every 0.25 m
		constexpr int Bins = 121;
		struct FGroup
		{
			std::vector<float> Profile[Bins];
			std::vector<float> KerbOffset, KerbHeight, Width, Crossfall;
			int Sections = 0, KerbSides = 0, Sides = 0;
		};
		std::map<std::tuple<int, int, int>, FGroup> Groups;	// class, town, one-way
		long long Skipped = 0;
		for (const Way& W : Set.Ways)
		{
			const std::string* Highway = TagOf(W, "highway");
			const int Class = Highway ? ClassOf(*Highway) : -1;
			const std::string* Bridge = TagOf(W, "bridge");
			const std::string* Tunnel = TagOf(W, "tunnel");
			const std::string* Layer = TagOf(W, "layer");
			if (Class < 0 || (Bridge && *Bridge != "no") || (Tunnel && *Tunnel != "no") || (Layer && *Layer != "0"))
			{
				continue;
			}
			const std::string* Oneway = TagOf(W, "oneway");
			const int OneWay = (Oneway && (*Oneway == "yes" || *Oneway == "-1")) ? 1 : 0;
			std::vector<std::pair<double, double>> Points;
			std::vector<bool> Junction;
			for (int64_t Id : W.Nodes)
			{
				auto It = Grid.find(Id);
				if (It != Grid.end())
				{
					Points.push_back(It->second);
					Junction.push_back(RoadUses[Id] >= 2);
				}
			}
			if (Points.size() < 2)
			{
				continue;
			}
			Junction.front() = Junction.back() = true;	// the ends join something, or stop
			// distance along of every junction point, to keep 15 m clear of them
			std::vector<double> Along(Points.size(), 0.0), Clear;
			for (size_t i = 1; i < Points.size(); ++i)
			{
				Along[i] = Along[i - 1] + std::hypot(Points[i].first - Points[i - 1].first, Points[i].second - Points[i - 1].second);
			}
			for (size_t i = 0; i < Points.size(); ++i)
			{
				if (Junction[i]) { Clear.push_back(Along[i]); }
			}
			double Next = 5.0;
			for (size_t i = 0; i + 1 < Points.size(); ++i)
			{
				const double Length = Along[i + 1] - Along[i];
				if (Length < 0.01)
				{
					continue;
				}
				const double DE = (Points[i + 1].first - Points[i].first) / Length, DN = (Points[i + 1].second - Points[i].second) / Length;
				for (; Next <= Along[i + 1]; Next += 10.0)
				{
					bool bNear = false;
					for (const double C : Clear) { bNear |= std::abs(C - Next) < 15.0; }
					if (bNear)
					{
						continue;
					}
					const double T = Next - Along[i];
					const double CE = Points[i].first + DE * T, CN = Points[i].second + DN * T;
					const float Centre = Heights.At(CE, CN);
					if (std::isnan(Centre))
					{
						++Skipped;
						continue;
					}
					float Profile[Bins];
					bool bWhole = true;
					for (int b = 0; b < Bins; ++b)
					{
						const double Offset = (b - Bins / 2) * 0.25;	// + is to the left of the way's direction
						Profile[b] = Heights.At(CE - DN * Offset, CN + DE * Offset) - Centre;
						bWhole &= !std::isnan(Profile[b]);
					}
					if (!bWhole)
					{
						++Skipped;
						continue;
					}
					FGroup& G = Groups[{ Class, Urban(CE, CN) ? 1 : 0, OneWay }];
					++G.Sections;
					for (int b = 0; b < Bins; ++b)
					{
						G.Profile[b].push_back(Profile[b]);
					}
					// kerbs: from 1.5 m out, the first rise of 7 cm or more within half a metre, on each side
					float Kerb[2] = { NoHeight, NoHeight };
					for (int Side = 0; Side < 2; ++Side)
					{
						++G.Sides;
						for (int Step = 6; Step <= 36; ++Step)	// 1.5 .. 9 m
						{
							auto H = [&](int s) { return Profile[Bins / 2 + (Side ? -s : s)]; };
							if (H(Step + 2) - H(Step) >= 0.07f)
							{
								Kerb[Side] = Step * 0.25f;
								++G.KerbSides;
								G.KerbOffset.push_back(Kerb[Side] + 0.25f);
								G.KerbHeight.push_back(H(Step + 4) - H(Step - 1));
								break;
							}
						}
					}
					if (!std::isnan(Kerb[0]) && !std::isnan(Kerb[1]) && Kerb[0] >= 1.0f && Kerb[1] >= 1.0f)
					{
						G.Width.push_back(Kerb[0] + Kerb[1] + 0.5f);
						// fall from the middle to just inside each kerb, % (positive: the middle is higher)
						const int In0 = int(Kerb[0] / 0.25f) - 2, In1 = int(Kerb[1] / 0.25f) - 2;
						const float Fall = (-Profile[Bins / 2 + In0] / (In0 * 0.25f) - Profile[Bins / 2 - In1] / (In1 * 0.25f)) * 0.5f * 100.0f;
						G.Crossfall.push_back(Fall);
					}
				}
			}
		}

		// one profile per group: both sides folded together for two-way roads (a one-way keeps its own sides)
		FILE* Out = std::fopen(OutCsv.c_str(), "w");
		if (!Out)
		{
			throw std::runtime_error("can't write " + OutCsv);
		}
		std::fprintf(Out, "class,setting,oneway,sections,kerb_sides_pct,kerb_offset_m,kerb_height_cm,width_m,crossfall_pct");
		for (int b = 0; b < Bins; ++b)
		{
			std::fprintf(Out, ",%.2f", (b - Bins / 2) * 0.25);
		}
		std::fprintf(Out, "\n");
		std::printf("road cross-sections from 1 m LiDAR, %.1f km round %.4f, %.4f (%lld sections without full LiDAR skipped):\n", Km, Lat, Lon, Skipped);
		std::printf("  class       setting  1way  sections  kerbs  kerb at  kerb  width  fall | ground (cm) at 0, 1, 2, 3, 4, 5, 6, 8, 10, 15 m from the middle\n");
		for (auto& [Key, G] : Groups)
		{
			if (G.Sections < 15)
			{
				continue;
			}
			const auto [Class, Town, OneWay] = Key;
			float Median2[Bins];
			for (int b = 0; b < Bins; ++b)
			{
				Median2[b] = Median(G.Profile[b]) * 100.0f;
			}
			auto Side = [&](double Metres)
			{
				const int b = int(std::lround(Metres / 0.25));
				return OneWay ? Median2[Bins / 2 + b] : (Median2[Bins / 2 + b] + Median2[Bins / 2 - b]) * 0.5f;
			};
			const float KerbPct = G.Sides ? 100.0f * G.KerbSides / G.Sides : 0.0f;
			std::fprintf(Out, "%s,%s,%d,%d,%.0f,%.2f,%.1f,%.2f,%.2f", Classes[Class], Town ? "town" : "country", OneWay, G.Sections, KerbPct,
				Median(G.KerbOffset), Median(G.KerbHeight) * 100.0f, Median(G.Width), Median(G.Crossfall));
			for (int b = 0; b < Bins; ++b)
			{
				std::fprintf(Out, ",%.1f", Median2[b]);
			}
			std::fprintf(Out, "\n");
			std::printf("  %-11s %-8s %4d  %8d  %4.0f%%  %5.2f m  %3.0f cm %5.1f m %4.1f%% |", Classes[Class], Town ? "town" : "country", OneWay, G.Sections,
				KerbPct, Median(G.KerbOffset), Median(G.KerbHeight) * 100.0f, Median(G.Width), Median(G.Crossfall));
			for (const double M : { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 15.0 })
			{
				std::printf(" %5.0f", Side(M));
			}
			std::printf("\n");
		}
		std::fclose(Out);
		return 0;
	}
}
