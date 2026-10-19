// Geodrive tile tool - AGPL-3.0 (see LICENSE)

#include "OsmPbfRead.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "OsmJson.h"
#include "Progress.h"
#include "miniz.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace geotile
{
	namespace
	{
		/** A protobuf message being read field by field */
		struct Pb
		{
			const uint8_t* P = nullptr;
			const uint8_t* End = nullptr;

			Pb(const uint8_t* InData, size_t Size) : P(InData), End(InData + Size) {}

			bool AtEnd() const { return P >= End; }

			uint64_t Varint()
			{
				uint64_t V = 0;
				for (int Shift = 0; P < End && Shift < 64; Shift += 7)
				{
					const uint8_t B = *P++;
					V |= uint64_t(B & 0x7F) << Shift;
					if (!(B & 0x80))
					{
						return V;
					}
				}
				throw std::runtime_error("broken varint in the .osm.pbf");
			}

			int64_t Svarint()
			{
				const uint64_t V = Varint();
				return int64_t(V >> 1) ^ -int64_t(V & 1);
			}

			/** The next field's number and wire type; false at the end */
			bool Next(int& Field, int& Wire)
			{
				if (AtEnd())
				{
					return false;
				}
				const uint64_t Key = Varint();
				Field = int(Key >> 3);
				Wire = int(Key & 7);
				return true;
			}

			Pb Bytes()
			{
				const uint64_t N = Varint();
				if (N > uint64_t(End - P))
				{
					throw std::runtime_error("broken field length in the .osm.pbf");
				}
				Pb Sub(P, size_t(N));
				P += N;
				return Sub;
			}

			void Skip(int Wire)
			{
				switch (Wire)
				{
				case 0: Varint(); break;
				case 1: P += 8; break;
				case 2: Bytes(); break;
				case 5: P += 4; break;
				default: throw std::runtime_error("unknown wire type in the .osm.pbf");
				}
			}
		};

		/** Where one data block sits in the file */
		struct BlockRef
		{
			size_t Offset = 0;
			size_t Size = 0;
			bool bHasWays = true;	// until the first pass knows better
			bool bHasNodes = true;
		};

		/** A file mapped into memory: the system pages it in as blocks are read, so a 1.7 GB country costs no RAM of ours */
		struct MappedFile
		{
			const uint8_t* Data = nullptr;
			size_t Size = 0;

			explicit MappedFile(const std::string& Path)
			{
#ifdef _WIN32
				File = CreateFileA(Path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
				LARGE_INTEGER Length{};
				if (File == INVALID_HANDLE_VALUE || !GetFileSizeEx(File, &Length))
				{
					throw std::runtime_error("can't open " + Path);
				}
				Size = size_t(Length.QuadPart);
				Mapping = Size ? CreateFileMappingA(File, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
				Data = Mapping ? static_cast<const uint8_t*>(MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0)) : nullptr;
				if (Size && !Data)
				{
					throw std::runtime_error("can't map " + Path);
				}
#else
				Copy = ReadFile(Path);
				Data = Copy.data();
				Size = Copy.size();
#endif
			}

			~MappedFile()
			{
#ifdef _WIN32
				if (Data) { UnmapViewOfFile(Data); }
				if (Mapping) { CloseHandle(Mapping); }
				if (File != INVALID_HANDLE_VALUE) { CloseHandle(File); }
#endif
			}

			MappedFile(const MappedFile&) = delete;
			MappedFile& operator=(const MappedFile&) = delete;

		private:
#ifdef _WIN32
			HANDLE File = INVALID_HANDLE_VALUE;
			HANDLE Mapping = nullptr;
#else
			std::vector<uint8_t> Copy;
#endif
		};

		/** All OSMData blocks, from their headers (nothing is unpacked yet) */
		std::vector<BlockRef> FindBlocks(const uint8_t* File, size_t FileSize)
		{
			std::vector<BlockRef> Blocks;
			size_t At = 0;
			while (At + 4 <= FileSize)
			{
				const uint32_t HeaderSize = (uint32_t(File[At]) << 24) | (uint32_t(File[At + 1]) << 16) | (uint32_t(File[At + 2]) << 8) | uint32_t(File[At + 3]);
				At += 4;
				if (At + HeaderSize > FileSize)
				{
					throw std::runtime_error("broken block header in the .osm.pbf");
				}
				Pb Header(File + At, HeaderSize);
				std::string Type;
				uint64_t DataSize = 0;
				int Field = 0, Wire = 0;
				while (Header.Next(Field, Wire))
				{
					if (Field == 1 && Wire == 2)
					{
						const Pb S = Header.Bytes();
						Type.assign(reinterpret_cast<const char*>(S.P), size_t(S.End - S.P));
					}
					else if (Field == 3 && Wire == 0)
					{
						DataSize = Header.Varint();
					}
					else
					{
						Header.Skip(Wire);
					}
				}
				At += HeaderSize;
				if (At + DataSize > FileSize)
				{
					throw std::runtime_error("broken block in the .osm.pbf");
				}
				if (Type == "OSMData")
				{
					Blocks.push_back({ At, size_t(DataSize), true });
				}
				At += DataSize;
			}
			return Blocks;
		}

		/** Unpacks a Blob (raw or zlib) */
		std::vector<uint8_t> Unpack(const uint8_t* Data, size_t Size)
		{
			Pb Blob(Data, Size);
			uint64_t RawSize = 0;
			Pb Zlib(nullptr, 0), Raw(nullptr, 0);
			bool bZlib = false, bRaw = false;
			int Field = 0, Wire = 0;
			while (Blob.Next(Field, Wire))
			{
				if (Field == 1 && Wire == 2) { Raw = Blob.Bytes(); bRaw = true; }
				else if (Field == 2 && Wire == 0) { RawSize = Blob.Varint(); }
				else if (Field == 3 && Wire == 2) { Zlib = Blob.Bytes(); bZlib = true; }
				else { Blob.Skip(Wire); }
			}
			if (bRaw)
			{
				return std::vector<uint8_t>(Raw.P, Raw.End);
			}
			if (!bZlib || RawSize > (256u << 20))
			{
				throw std::runtime_error("a block in the .osm.pbf isn't zlib (lzma/zstd blocks aren't supported)");
			}
			std::vector<uint8_t> Out(static_cast<size_t>(RawSize));
			mz_ulong OutSize = mz_ulong(RawSize);
			if (mz_uncompress(Out.data(), &OutSize, Zlib.P, mz_ulong(Zlib.End - Zlib.P)) != MZ_OK || OutSize != RawSize)
			{
				throw std::runtime_error("a block in the .osm.pbf didn't unzip");
			}
			return Out;
		}

		/** The same ways the game asks Overpass for (GeoFetch.cpp): drivable roads, buildings, water, wood, sand, forest */
		bool Wanted(const std::vector<Tag>& Tags)
		{
			auto Get = [&](const char* Key) -> const std::string*
			{
				for (const Tag& T : Tags)
				{
					if (T.first == Key)
					{
						return &T.second;
					}
				}
				return nullptr;
			};
			if (const std::string* Highway = Get("highway"))
			{
				static const char* Drivable[] = { "motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential",
					"living_street", "service", "road", "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link", "track",
					"raceway" };
				const bool bDrivable = std::any_of(std::begin(Drivable), std::end(Drivable), [&](const char* H) { return *Highway == H; });
				const std::string* Area = Get("area");
				const std::string* Access = Get("access");
				// (a race track's often private, a circuit's own: kept, it's what its race is on)
				const bool bClosed = Access && (*Access == "no" || *Access == "private") && *Highway != "raceway";
				if (bDrivable && !(Area && *Area == "yes") && !bClosed)
				{
					return true;
				}
			}
			if (Get("building"))
			{
				return true;
			}
			if (const std::string* Natural = Get("natural"))
			{
				if (*Natural == "water" || *Natural == "wood" || *Natural == "beach" || *Natural == "sand")
				{
					return true;
				}
			}
			const std::string* Landuse = Get("landuse");
			return Landuse && *Landuse == "forest";
		}

		std::vector<std::string> StringTable(Pb Table)
		{
			std::vector<std::string> Strings;
			int Field = 0, Wire = 0;
			while (Table.Next(Field, Wire))
			{
				if (Field == 1 && Wire == 2)
				{
					const Pb S = Table.Bytes();
					Strings.emplace_back(reinterpret_cast<const char*>(S.P), size_t(S.End - S.P));
				}
				else
				{
					Table.Skip(Wire);
				}
			}
			return Strings;
		}

		std::vector<uint64_t> PackedVarints(Pb Field)
		{
			std::vector<uint64_t> Out;
			while (!Field.AtEnd())
			{
				Out.push_back(Field.Varint());
			}
			return Out;
		}

		std::vector<int64_t> PackedSvarints(Pb Field)
		{
			std::vector<int64_t> Out;
			while (!Field.AtEnd())
			{
				Out.push_back(Field.Svarint());
			}
			return Out;
		}

		/** What a primitive block holds, split out of its groups */
		struct Block
		{
			std::vector<std::string> Strings;
			int64_t Granularity = 100, LatOffset = 0, LonOffset = 0;
			std::vector<Pb> Groups;
		};

		Block ReadBlock(const std::vector<uint8_t>& Raw)
		{
			Block B;
			Pb Message(Raw.data(), Raw.size());
			int Field = 0, Wire = 0;
			while (Message.Next(Field, Wire))
			{
				if (Field == 1 && Wire == 2) { B.Strings = StringTable(Message.Bytes()); }
				else if (Field == 2 && Wire == 2) { B.Groups.push_back(Message.Bytes()); }
				else if (Field == 17 && Wire == 0) { B.Granularity = int64_t(Message.Varint()); }
				else if (Field == 19 && Wire == 0) { B.LatOffset = int64_t(Message.Varint()); }
				else if (Field == 20 && Wire == 0) { B.LonOffset = int64_t(Message.Varint()); }
				else { Message.Skip(Wire); }
			}
			return B;
		}

		/** The ways in a block that the game wants; says whether the block has any ways, and any nodes */
		bool ReadWays(const Block& B, std::vector<Way>& Out, uint64_t& Seen, bool& bAnyNodes, const WayFilter& Keep)
		{
			bool bAnyWays = false;
			bAnyNodes = false;
			for (Pb Group : B.Groups)
			{
				int Field = 0, Wire = 0;
				while (Group.Next(Field, Wire))
				{
					if (Field != 3 || Wire != 2)
					{
						bAnyNodes |= Field == 1 || Field == 2;
						Group.Skip(Wire);
						continue;
					}
					bAnyWays = true;
					++Seen;
					Pb Message = Group.Bytes();
					Way W;
					std::vector<uint64_t> Keys, Values;
					std::vector<int64_t> Refs;
					int F = 0, Wt = 0;
					while (Message.Next(F, Wt))
					{
						if (F == 1 && Wt == 0) { W.Id = int64_t(Message.Varint()); }
						else if (F == 2 && Wt == 2) { Keys = PackedVarints(Message.Bytes()); }
						else if (F == 3 && Wt == 2) { Values = PackedVarints(Message.Bytes()); }
						else if (F == 8 && Wt == 2) { Refs = PackedSvarints(Message.Bytes()); }
						else { Message.Skip(Wt); }
					}
					for (size_t i = 0; i < Keys.size() && i < Values.size(); ++i)
					{
						if (Keys[i] < B.Strings.size() && Values[i] < B.Strings.size())
						{
							W.Tags.emplace_back(B.Strings[Keys[i]], B.Strings[Values[i]]);
						}
					}
					if (!Keep(W.Tags))
					{
						continue;
					}
					int64_t Id = 0;
					W.Nodes.reserve(Refs.size());
					for (const int64_t Delta : Refs)
					{
						Id += Delta;
						W.Nodes.push_back(Id);
					}
					Out.push_back(std::move(W));
				}
			}
			return bAnyWays;
		}

		/** Every node of a block: Visit(id, position) (1e-7 degrees) */
		template <typename F>
		void ForEachNode(const Block& B, F Visit)
		{
			// positions: 1e-9 degrees = offset + granularity x stored value; ours are 1e-7
			auto ToE7 = [](int64_t Nano) { return int32_t(Nano >= 0 ? (Nano + 50) / 100 : (Nano - 50) / 100); };
			auto Keep = [&](int64_t Id, int64_t Lat, int64_t Lon)
			{
				LatLon P;
				P.Lat = ToE7(B.LatOffset + B.Granularity * Lat);
				P.Lon = ToE7(B.LonOffset + B.Granularity * Lon);
				Visit(Id, P);
			};
			for (Pb Group : B.Groups)
			{
				int Field = 0, Wire = 0;
				while (Group.Next(Field, Wire))
				{
					if (Field == 2 && Wire == 2)
					{
						// dense nodes: ids, latitudes and longitudes as runs of differences
						Pb Dense = Group.Bytes();
						std::vector<int64_t> Ids, Lats, Lons;
						int F = 0, Wt = 0;
						while (Dense.Next(F, Wt))
						{
							if (F == 1 && Wt == 2) { Ids = PackedSvarints(Dense.Bytes()); }
							else if (F == 8 && Wt == 2) { Lats = PackedSvarints(Dense.Bytes()); }
							else if (F == 9 && Wt == 2) { Lons = PackedSvarints(Dense.Bytes()); }
							else { Dense.Skip(Wt); }
						}
						int64_t Id = 0, Lat = 0, Lon = 0;
						for (size_t i = 0; i < Ids.size() && i < Lats.size() && i < Lons.size(); ++i)
						{
							Id += Ids[i];
							Lat += Lats[i];
							Lon += Lons[i];
							Keep(Id, Lat, Lon);
						}
					}
					else if (Field == 1 && Wire == 2)
					{
						Pb Node = Group.Bytes();
						int64_t Id = 0, Lat = 0, Lon = 0;
						int F = 0, Wt = 0;
						while (Node.Next(F, Wt))
						{
							if (F == 1 && Wt == 0) { Id = Node.Svarint(); }
							else if (F == 8 && Wt == 0) { Lat = Node.Svarint(); }
							else if (F == 9 && Wt == 0) { Lon = Node.Svarint(); }
							else { Node.Skip(Wt); }
						}
						Keep(Id, Lat, Lon);
					}
					else
					{
						Group.Skip(Wire);
					}
				}
			}
		}

		/** The nodes of a block whose ids are in Needed (sorted) */
		void ReadNodes(const Block& B, const std::vector<int64_t>& Needed, std::vector<std::pair<int64_t, LatLon>>& Out, uint64_t& Seen)
		{
			ForEachNode(B, [&](int64_t Id, const LatLon& P)
			{
				++Seen;
				if (std::binary_search(Needed.begin(), Needed.end(), Id))
				{
					Out.emplace_back(Id, P);
				}
			});
		}

		/** Runs Work(i) for every block on all cores */
		template <typename F>
		void ForEachBlock(size_t Count, F Work, const ProgressSpan* Span = nullptr)
		{
			std::atomic<size_t> NextIndex{ 0 };
			std::atomic<size_t> Finished{ 0 };
			std::vector<std::thread> Threads;
			const unsigned Cores = std::max(1u, std::thread::hardware_concurrency());
			std::mutex ErrorLock;
			std::string Error;
			for (unsigned t = 0; t < Cores; ++t)
			{
				Threads.emplace_back([&]()
				{
					for (size_t i = NextIndex++; i < Count; i = NextIndex++)
					{
						try
						{
							Work(i);
						}
						catch (const std::exception& E)
						{
							std::lock_guard<std::mutex> Guard(ErrorLock);
							Error = E.what();
						}
						ReportChunk(Span, ++Finished, Count);
					}
				});
			}
			for (std::thread& T : Threads)
			{
				T.join();
			}
			if (!Error.empty())
			{
				throw std::runtime_error(Error);
			}
		}
	}

	bool GameWays(std::vector<Tag>& Tags)
	{
		if (!Wanted(Tags))
		{
			return false;
		}
		FilterTags(Tags);
		return true;
	}

	OsmSet ReadOsmPbf(const std::string& Path, PbfReadStats& Stats, const WayFilter& Keep)
	{
		const MappedFile File(Path);
		std::vector<BlockRef> Blocks = FindBlocks(File.Data, File.Size);
		Stats.Blocks = Blocks.size();

		// pass 1: the ways, block by block in parallel, kept in file order
		std::vector<std::vector<Way>> WaysByBlock(Blocks.size());
		std::atomic<uint64_t> WaysSeen{ 0 };
		ForEachBlock(Blocks.size(), [&](size_t i)
		{
			uint64_t Seen = 0;
			Blocks[i].bHasWays = ReadWays(ReadBlock(Unpack(File.Data + Blocks[i].Offset, Blocks[i].Size)), WaysByBlock[i], Seen, Blocks[i].bHasNodes, Keep);
			WaysSeen += Seen;
		});
		Stats.WaysSeen = WaysSeen;

		OsmSet Set;
		std::vector<int64_t> Needed;
		for (std::vector<Way>& Ways : WaysByBlock)
		{
			for (Way& W : Ways)
			{
				Needed.insert(Needed.end(), W.Nodes.begin(), W.Nodes.end());
				Set.Ways.push_back(std::move(W));
			}
			std::vector<Way>().swap(Ways);
		}
		std::sort(Needed.begin(), Needed.end());
		Needed.erase(std::unique(Needed.begin(), Needed.end()), Needed.end());
		Stats.WaysKept = Set.Ways.size();

		// pass 2: only the nodes those ways use (blocks without nodes are skipped)
		std::vector<std::vector<std::pair<int64_t, LatLon>>> NodesByBlock(Blocks.size());
		std::atomic<uint64_t> NodesSeen{ 0 };
		ForEachBlock(Blocks.size(), [&](size_t i)
		{
			if (!Blocks[i].bHasNodes)
			{
				return;
			}
			uint64_t Seen = 0;
			ReadNodes(ReadBlock(Unpack(File.Data + Blocks[i].Offset, Blocks[i].Size)), Needed, NodesByBlock[i], Seen);
			NodesSeen += Seen;
		});
		Stats.NodesSeen = NodesSeen;

		Set.Nodes.reserve(Needed.size());
		for (std::vector<std::pair<int64_t, LatLon>>& Nodes : NodesByBlock)
		{
			for (const auto& [Id, P] : Nodes)
			{
				Set.Nodes.emplace(Id, P);
			}
			std::vector<std::pair<int64_t, LatLon>>().swap(Nodes);
		}
		Stats.NodesKept = Set.Nodes.size();
		Stats.MissingNodes = Needed.size() - Set.Nodes.size();
		return Set;
	}
}

namespace geotile
{
	std::vector<Place> ReadPlaces(const std::string& Path, bool bAerodromes)
	{
		const MappedFile File(Path);
		const std::vector<BlockRef> Blocks = FindBlocks(File.Data, File.Size);
		std::vector<std::vector<Place>> ByBlock(Blocks.size());
		ForEachBlock(Blocks.size(), [&](size_t i)
		{
			const std::vector<uint8_t> Raw = Unpack(File.Data + Blocks[i].Offset, Blocks[i].Size);	// the block points into this
			const Block B = ReadBlock(Raw);
			auto ToE7 = [](int64_t Nano) { return int32_t(Nano >= 0 ? (Nano + 50) / 100 : (Nano - 50) / 100); };
			auto Add = [&](int64_t Lat, int64_t Lon, const std::vector<Tag>& Tags)
			{
				Place P;
				for (const Tag& T : Tags)
				{
					if (T.first == "place" && !bAerodromes) { P.Kind = T.second; }
					else if (T.first == "aeroway" && bAerodromes && T.second == "aerodrome") { P.Kind = T.second; }
					else if (T.first == "name") { P.Name = T.second; }
					else if (T.first == "population") { P.Population = std::atoll(T.second.c_str()); }
					else if (T.first == "icao") { P.Icao = T.second; }
					else if (T.first == "iata") { P.Iata = T.second; }
				}
				static const char* Kinds[] = { "city", "town", "village", "suburb", "hamlet", "neighbourhood", "quarter" };
				if (bAerodromes ? P.Kind != "aerodrome" : (P.Name.empty() || std::none_of(std::begin(Kinds), std::end(Kinds), [&](const char* K) { return P.Kind == K; })))
				{
					return;
				}
				P.Position.Lat = ToE7(B.LatOffset + B.Granularity * Lat);
				P.Position.Lon = ToE7(B.LonOffset + B.Granularity * Lon);
				ByBlock[i].push_back(std::move(P));
			};
			for (Pb Group : B.Groups)
			{
				int Field = 0, Wire = 0;
				while (Group.Next(Field, Wire))
				{
					if (Field == 2 && Wire == 2)
					{
						// dense nodes: tags as key, value, key, value, 0 for each node in turn
						Pb Dense = Group.Bytes();
						std::vector<int64_t> Lats, Lons;
						std::vector<uint64_t> KeysVals;
						int F = 0, Wt = 0;
						while (Dense.Next(F, Wt))
						{
							if (F == 8 && Wt == 2) { Lats = PackedSvarints(Dense.Bytes()); }
							else if (F == 9 && Wt == 2) { Lons = PackedSvarints(Dense.Bytes()); }
							else if (F == 10 && Wt == 2) { KeysVals = PackedVarints(Dense.Bytes()); }
							else { Dense.Skip(Wt); }
						}
						int64_t Lat = 0, Lon = 0;
						size_t K = 0;
						for (size_t n = 0; n < Lats.size() && n < Lons.size(); ++n)
						{
							Lat += Lats[n];
							Lon += Lons[n];
							std::vector<Tag> Tags;
							while (K < KeysVals.size() && KeysVals[K] != 0)
							{
								if (K + 1 < KeysVals.size() && KeysVals[K] < B.Strings.size() && KeysVals[K + 1] < B.Strings.size())
								{
									Tags.emplace_back(B.Strings[KeysVals[K]], B.Strings[KeysVals[K + 1]]);
								}
								K += 2;
							}
							++K;	// the 0 between nodes
							if (!Tags.empty())
							{
								Add(Lat, Lon, Tags);
							}
						}
					}
					else if (Field == 1 && Wire == 2)
					{
						Pb Node = Group.Bytes();
						int64_t Lat = 0, Lon = 0;
						std::vector<uint64_t> Keys, Values;
						int F = 0, Wt = 0;
						while (Node.Next(F, Wt))
						{
							if (F == 2 && Wt == 2) { Keys = PackedVarints(Node.Bytes()); }
							else if (F == 3 && Wt == 2) { Values = PackedVarints(Node.Bytes()); }
							else if (F == 8 && Wt == 0) { Lat = Node.Svarint(); }
							else if (F == 9 && Wt == 0) { Lon = Node.Svarint(); }
							else { Node.Skip(Wt); }
						}
						std::vector<Tag> Tags;
						for (size_t k = 0; k < Keys.size() && k < Values.size(); ++k)
						{
							if (Keys[k] < B.Strings.size() && Values[k] < B.Strings.size())
							{
								Tags.emplace_back(B.Strings[Keys[k]], B.Strings[Values[k]]);
							}
						}
						Add(Lat, Lon, Tags);
					}
					else
					{
						Group.Skip(Wire);
					}
				}
			}
		});
		std::vector<Place> Places;
		for (std::vector<Place>& Some : ByBlock)
		{
			for (Place& P : Some)
			{
				Places.push_back(std::move(P));
			}
		}
		return Places;
	}

	struct PbfBands::Impl
	{
		MappedFile File;
		std::vector<BlockRef> Blocks;

		explicit Impl(const std::string& Path) : File(Path), Blocks(FindBlocks(File.Data, File.Size)) {}
	};

	PbfBands::PbfBands(const std::string& Path, PbfReadStats& Stats, const WayFilter& InKeep, const LatLon* BoxMin, const LatLon* BoxMax)
		: P(std::make_unique<Impl>(Path)), Keep(InKeep)
	{
		std::vector<BlockRef>& Blocks = P->Blocks;
		Stats.Blocks = Blocks.size();

		// an area only: first the ids of the points inside it, so only the ways touching it are kept from here on
		std::vector<int64_t> InBox;
		if (BoxMin && BoxMax)
		{
			std::vector<std::vector<int64_t>> ByBlock(Blocks.size());
			ForEachBlock(Blocks.size(), [&](size_t i)
			{
				ForEachNode(ReadBlock(Unpack(P->File.Data + Blocks[i].Offset, Blocks[i].Size)), [&](int64_t Id, const LatLon& Where)
				{
					if (Where.Lat >= BoxMin->Lat && Where.Lat <= BoxMax->Lat && Where.Lon >= BoxMin->Lon && Where.Lon <= BoxMax->Lon)
					{
						ByBlock[i].push_back(Id);
					}
				});
			});
			for (std::vector<int64_t>& Some : ByBlock)
			{
				InBox.insert(InBox.end(), Some.begin(), Some.end());
				std::vector<int64_t>().swap(Some);
			}
			std::sort(InBox.begin(), InBox.end());
		}
		const bool bBox = BoxMin && BoxMax;
		auto Touches = [&](const Way& W)
		{
			return !bBox || std::any_of(W.Nodes.begin(), W.Nodes.end(), [&](int64_t Id) { return std::binary_search(InBox.begin(), InBox.end(), Id); });
		};

		// pass 1: only the node ids of the kept ways (every use, so the ones used twice can be told)
		std::vector<std::vector<int64_t>> RefsByBlock(Blocks.size());
		std::atomic<uint64_t> WaysSeen{ 0 }, WaysKept{ 0 };
		const ProgressSpan WaysSpan{ 0.0, 0.03, "reading the map" };
		ForEachBlock(Blocks.size(), [&](size_t i)
		{
			uint64_t Seen = 0;
			std::vector<Way> Ways;
			Blocks[i].bHasWays = ReadWays(ReadBlock(Unpack(P->File.Data + Blocks[i].Offset, Blocks[i].Size)), Ways, Seen, Blocks[i].bHasNodes, Keep);
			WaysSeen += Seen;
			for (const Way& W : Ways)
			{
				if (Touches(W))
				{
					++WaysKept;
					RefsByBlock[i].insert(RefsByBlock[i].end(), W.Nodes.begin(), W.Nodes.end());
				}
			}
		}, &WaysSpan);
		std::vector<int64_t>().swap(InBox);
		Stats.WaysSeen = WaysSeen;
		Stats.WaysKept = WaysKept;

		std::vector<int64_t> Refs;
		for (std::vector<int64_t>& Some : RefsByBlock)
		{
			Refs.insert(Refs.end(), Some.begin(), Some.end());
			std::vector<int64_t>().swap(Some);
		}
		std::sort(Refs.begin(), Refs.end());
		for (size_t i = 0; i < Refs.size();)
		{
			size_t j = i + 1;
			while (j < Refs.size() && Refs[j] == Refs[i])
			{
				++j;
			}
			NodeIds.push_back(Refs[i]);
			if (j - i > 1)
			{
				Shared.push_back(Refs[i]);
			}
			i = j;
		}
		std::vector<int64_t>().swap(Refs);

		// pass 2: where those nodes are
		Positions.resize(NodeIds.size());
		Found.assign(NodeIds.size(), 0);
		std::atomic<uint64_t> NodesSeen{ 0 }, NodesKept{ 0 };
		const ProgressSpan NodesSpan{ 0.03, 0.06, "finding where things are" };
		ForEachBlock(Blocks.size(), [&](size_t i)
		{
			if (!Blocks[i].bHasNodes)
			{
				return;
			}
			uint64_t Seen = 0;
			std::vector<std::pair<int64_t, LatLon>> Nodes;
			ReadNodes(ReadBlock(Unpack(P->File.Data + Blocks[i].Offset, Blocks[i].Size)), NodeIds, Nodes, Seen);
			NodesSeen += Seen;
			for (const auto& [Id, Where] : Nodes)
			{
				const size_t Index = size_t(std::lower_bound(NodeIds.begin(), NodeIds.end(), Id) - NodeIds.begin());
				Positions[Index] = Where;	// each node is in one block, so no two threads write the same one
				Found[Index] = 1;
			}
			NodesKept += Nodes.size();
		}, &NodesSpan);
		Stats.NodesSeen = NodesSeen;
		Stats.NodesKept = NodesKept;
		Stats.MissingNodes = NodeIds.size() - Stats.NodesKept;
	}

	PbfBands::~PbfBands() = default;

	OsmSet PbfBands::ReadBand(const std::function<bool(const LatLon&)>& Inside, const ProgressSpan* Span) const
	{
		const std::vector<BlockRef>& Blocks = P->Blocks;
		auto Where = [&](int64_t Id) -> const LatLon*
		{
			const auto It = std::lower_bound(NodeIds.begin(), NodeIds.end(), Id);
			const size_t Index = size_t(It - NodeIds.begin());
			return It != NodeIds.end() && *It == Id && Found[Index] ? &Positions[Index] : nullptr;
		};

		std::vector<std::vector<Way>> WaysByBlock(Blocks.size());
		ForEachBlock(Blocks.size(), [&](size_t i)
		{
			if (!Blocks[i].bHasWays)
			{
				return;
			}
			uint64_t Seen = 0;
			bool bNodes = false;
			std::vector<Way> Ways;
			ReadWays(ReadBlock(Unpack(P->File.Data + Blocks[i].Offset, Blocks[i].Size)), Ways, Seen, bNodes, Keep);
			for (Way& W : Ways)
			{
				if (std::any_of(W.Nodes.begin(), W.Nodes.end(), [&](int64_t Id) { const LatLon* L = Where(Id); return L && Inside(*L); }))
				{
					WaysByBlock[i].push_back(std::move(W));
				}
			}
		}, Span);

		OsmSet Set;
		for (std::vector<Way>& Ways : WaysByBlock)
		{
			for (Way& W : Ways)
			{
				for (int64_t Id : W.Nodes)
				{
					if (const LatLon* L = Where(Id))
					{
						Set.Nodes.emplace(Id, *L);
					}
				}
				Set.Ways.push_back(std::move(W));
			}
			std::vector<Way>().swap(Ways);
		}
		return Set;
	}
}
