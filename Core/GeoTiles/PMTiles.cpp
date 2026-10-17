// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "PMTiles.h"

#include <algorithm>
#include <utility>

#include "Bytes.h"

namespace geotile
{
	namespace
	{
		constexpr size_t HeaderSize = 127;
		constexpr size_t MaxRootDirectory = 16384 - HeaderSize;

		std::vector<uint8_t> SerializeDirectory(const std::vector<PmEntry>& Entries)
		{
			ByteWriter W;
			W.Varint(Entries.size());
			uint64_t LastId = 0;
			for (const PmEntry& E : Entries)
			{
				W.Varint(E.TileId - LastId);
				LastId = E.TileId;
			}
			for (const PmEntry& E : Entries)
			{
				W.Varint(E.RunLength);
			}
			for (const PmEntry& E : Entries)
			{
				W.Varint(E.Length);
			}
			for (size_t I = 0; I < Entries.size(); ++I)
			{
				// 0 means "straight after the previous tile"
				if (I > 0 && Entries[I].Offset == Entries[I - 1].Offset + Entries[I - 1].Length)
				{
					W.Varint(0);
				}
				else
				{
					W.Varint(Entries[I].Offset + 1);
				}
			}
			return W.Data;
		}
	}

	uint64_t ZxyToTileId(int Z, uint32_t X, uint32_t Y)
	{
		uint64_t Acc = 0;
		for (int T = 0; T < Z; ++T)
		{
			Acc += uint64_t(1) << (2 * T);
		}
		const uint64_t N = uint64_t(1) << Z;
		uint64_t Tx = X;
		uint64_t Ty = Y;
		uint64_t D = 0;
		for (uint64_t S = N / 2; S > 0; S /= 2)
		{
			const uint64_t Rx = (Tx & S) > 0 ? 1 : 0;
			const uint64_t Ry = (Ty & S) > 0 ? 1 : 0;
			D += S * S * ((3 * Rx) ^ Ry);
			if (Ry == 0)
			{
				if (Rx == 1)
				{
					Tx = N - 1 - Tx;
					Ty = N - 1 - Ty;
				}
				std::swap(Tx, Ty);
			}
		}
		return Acc + D;
	}

	bool WritePmTiles(const std::map<uint64_t, std::vector<uint8_t>>& Tiles, PmHeader H, const std::string& MetadataJson,
		std::vector<uint8_t>& OutFile, std::string& Error)
	{
		std::vector<PmEntry> Entries;
		ByteWriter Data;
		for (const auto& [Id, Bytes] : Tiles)
		{
			PmEntry E;
			E.TileId = Id;
			E.Offset = Data.Data.size();
			E.Length = uint32_t(Bytes.size());
			Entries.push_back(E);
			Data.Append(Bytes);
		}

		(void)Error;	// nothing can fail any more: big archives get leaf directories
		// the root directory has to fit in the first 16 KB; more tiles than that (a whole country) go in leaf
		// directories, each a run of tiles, with the root pointing at them (RunLength 0 = a leaf)
		std::vector<uint8_t> Root = SerializeDirectory(Entries);
		std::vector<uint8_t> Leaves;
		for (size_t LeafSize = 4096; Root.size() > MaxRootDirectory; LeafSize *= 2)
		{
			std::vector<PmEntry> Pointers;
			Leaves.clear();
			for (size_t First = 0; First < Entries.size(); First += LeafSize)
			{
				const std::vector<PmEntry> Run(Entries.begin() + First, Entries.begin() + std::min(Entries.size(), First + LeafSize));
				const std::vector<uint8_t> Leaf = SerializeDirectory(Run);
				PmEntry Pointer;
				Pointer.TileId = Run.front().TileId;
				Pointer.Offset = Leaves.size();
				Pointer.Length = uint32_t(Leaf.size());
				Pointer.RunLength = 0;
				Pointers.push_back(Pointer);
				Leaves.insert(Leaves.end(), Leaf.begin(), Leaf.end());
			}
			Root = SerializeDirectory(Pointers);
		}

		H.InternalCompression = PmCompression::None;
		H.RootOffset = HeaderSize;
		H.RootLength = Root.size();
		H.MetadataOffset = H.RootOffset + H.RootLength;
		H.MetadataLength = MetadataJson.size();
		H.LeafOffset = H.MetadataOffset + H.MetadataLength;
		H.LeafLength = Leaves.size();
		H.DataOffset = H.LeafOffset + H.LeafLength;
		H.DataLength = Data.Data.size();
		H.AddressedTiles = Entries.size();
		H.TileEntries = Entries.size();
		H.TileContents = Entries.size();
		H.Clustered = 1;

		ByteWriter Out;
		for (char C : std::string("PMTiles"))
		{
			Out.U8(uint8_t(C));
		}
		Out.U8(3);
		for (uint64_t V : { H.RootOffset, H.RootLength, H.MetadataOffset, H.MetadataLength, H.LeafOffset, H.LeafLength,
			H.DataOffset, H.DataLength, H.AddressedTiles, H.TileEntries, H.TileContents })
		{
			Out.Fixed(V, 8);
		}
		Out.U8(H.Clustered);
		Out.U8(uint8_t(H.InternalCompression));
		Out.U8(uint8_t(H.TileCompression));
		Out.U8(H.TileType);
		Out.U8(H.MinZoom);
		Out.U8(H.MaxZoom);
		for (int32_t V : { H.MinLonE7, H.MinLatE7, H.MaxLonE7, H.MaxLatE7 })
		{
			Out.Fixed(uint32_t(V), 4);
		}
		Out.U8(H.CenterZoom);
		Out.Fixed(uint32_t(H.CenterLonE7), 4);
		Out.Fixed(uint32_t(H.CenterLatE7), 4);

		Out.Append(Root);
		Out.Data.insert(Out.Data.end(), MetadataJson.begin(), MetadataJson.end());
		Out.Append(Leaves);
		Out.Append(Data.Data);
		OutFile = std::move(Out.Data);
		return true;
	}

	bool ReadPmTiles(const uint8_t* File, size_t Size, PmHeader& H, std::vector<PmEntry>& Entries, std::string& Error)
	{
		H = PmHeader();
		Entries.clear();
		if (Size < HeaderSize || std::string(reinterpret_cast<const char*>(File), 7) != "PMTiles" || File[7] != 3)
		{
			Error = "not a PMTiles v3 file";
			return false;
		}
		ByteReader R(File + 8, HeaderSize - 8);
		for (uint64_t* V : { &H.RootOffset, &H.RootLength, &H.MetadataOffset, &H.MetadataLength, &H.LeafOffset, &H.LeafLength,
			&H.DataOffset, &H.DataLength, &H.AddressedTiles, &H.TileEntries, &H.TileContents })
		{
			*V = R.Fixed(8);
		}
		H.Clustered = R.U8();
		H.InternalCompression = PmCompression(R.U8());
		H.TileCompression = PmCompression(R.U8());
		H.TileType = R.U8();
		H.MinZoom = R.U8();
		H.MaxZoom = R.U8();
		for (int32_t* V : { &H.MinLonE7, &H.MinLatE7, &H.MaxLonE7, &H.MaxLatE7 })
		{
			*V = int32_t(R.Fixed(4));
		}
		H.CenterZoom = R.U8();
		H.CenterLonE7 = int32_t(R.Fixed(4));
		H.CenterLatE7 = int32_t(R.Fixed(4));

		if (H.InternalCompression != PmCompression::None)
		{
			Error = "compressed directories aren't supported yet";
			return false;
		}
		if (H.RootOffset > Size || H.RootLength > Size - H.RootOffset || H.DataOffset > Size || H.DataLength > Size - H.DataOffset
			|| H.LeafOffset > Size || H.LeafLength > Size - H.LeafOffset)
		{
			Error = "PMTiles sections run past the end of the file";
			return false;
		}

		// one directory: tiles, or (RunLength 0) pointers to leaf directories, read in turn
		auto ReadDirectory = [&](const uint8_t* Bytes, size_t Length, int Depth, auto& Self) -> bool
		{
			ByteReader D(Bytes, Length);
			const uint64_t Count = D.Varint();
			if (Count > Length || Depth > 3)
			{
				Error = "PMTiles directory is broken";
				return false;
			}
			std::vector<PmEntry> Directory(static_cast<size_t>(Count));
			uint64_t LastId = 0;
			for (PmEntry& E : Directory)
			{
				LastId += D.Varint();
				E.TileId = LastId;
			}
			for (PmEntry& E : Directory)
			{
				E.RunLength = uint32_t(D.Varint());
			}
			for (PmEntry& E : Directory)
			{
				E.Length = uint32_t(D.Varint());
			}
			for (size_t I = 0; I < Directory.size(); ++I)
			{
				const uint64_t V = D.Varint();
				Directory[I].Offset = (V == 0 && I > 0) ? Directory[I - 1].Offset + Directory[I - 1].Length : V - 1;
			}
			if (!D.Ok())
			{
				Error = "PMTiles directory is broken";
				return false;
			}
			for (const PmEntry& E : Directory)
			{
				if (E.RunLength == 0)
				{
					if (E.Offset > H.LeafLength || E.Length > H.LeafLength - E.Offset
						|| !Self(File + H.LeafOffset + E.Offset, size_t(E.Length), Depth + 1, Self))
					{
						Error = Error.empty() ? "a leaf directory lies outside the file" : Error;
						return false;
					}
					continue;
				}
				if (E.Offset > H.DataLength || E.Length > H.DataLength - E.Offset)
				{
					Error = "a tile lies outside the file";
					return false;
				}
				Entries.push_back(E);
			}
			return true;
		};
		return ReadDirectory(File + H.RootOffset, size_t(H.RootLength), 0, ReadDirectory);
	}
}
