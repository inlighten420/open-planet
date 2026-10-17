// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "TileCodec.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

#include "Bytes.h"

namespace geotile
{
	namespace
	{
		constexpr uint8_t FormatVersion = 0;
		constexpr int NumBlocks = 7;

		/** Nearest whole number of steps, halves away from zero, in integers only so every machine agrees */
		int64_t RoundToSteps(int64_t V, int64_t Step)
		{
			return V >= 0 ? (V + Step / 2) / Step : -((-V + Step / 2) / Step);
		}
	}

	TileXY TileOf(const LatLon& P, int Zoom)
	{
		// Floating maths here only decides which tile a node lands in, and that's decided once by whoever bakes
		// the tiles. Readers never redo it, so tiny differences between machines can't matter.
		const double N = double(1u << Zoom);
		const double Lon = P.Lon * 1e-7;
		const double LatRad = P.Lat * 1e-7 * 3.14159265358979323846 / 180.0;
		const double X = (Lon + 180.0) / 360.0 * N;
		const double Y = (1.0 - std::asinh(std::tan(LatRad)) / 3.14159265358979323846) / 2.0 * N;
		TileXY T;
		T.X = uint32_t(std::clamp(X, 0.0, N - 1));
		T.Y = uint32_t(std::clamp(Y, 0.0, N - 1));
		return T;
	}

	std::map<TileXY, std::vector<WayPiece>> SplitIntoTiles(const OsmSet& Set, PackStats& Stats)
	{
		std::map<TileXY, std::vector<WayPiece>> Tiles;

		for (const Way& W : Set.Ways)
		{
			// Drop references to nodes the file doesn't have (none expected from Overpass, but count them)
			std::vector<int64_t> Nodes;
			std::vector<TileXY> NodeTiles;
			for (int64_t Id : W.Nodes)
			{
				auto It = Set.Nodes.find(Id);
				if (It == Set.Nodes.end())
				{
					++Stats.MissingNodeRefs;
					continue;
				}
				Nodes.push_back(Id);
				NodeTiles.push_back(TileOf(It->second, TileZoom));
			}
			if (Nodes.empty())
			{
				continue;
			}
			++Stats.Ways;

			// Each segment belongs to the tile of its first node. A new piece starts whenever that changes,
			// and the node at the cut is the last node of one piece and the first of the next.
			std::vector<WayPiece> Pieces;
			WayPiece Cur;
			Cur.WayId = W.Id;
			Cur.Tags = W.Tags;
			Cur.Nodes.push_back(Nodes[0]);
			TileXY CurTile = NodeTiles[0];
			std::vector<TileXY> PieceTiles;
			for (size_t I = 1; I < Nodes.size(); ++I)
			{
				Cur.Nodes.push_back(Nodes[I]);
				const bool bLastNode = I + 1 == Nodes.size();
				if (!bLastNode && !(NodeTiles[I] == CurTile))
				{
					Pieces.push_back(Cur);
					PieceTiles.push_back(CurTile);
					Cur.Nodes.assign(1, Nodes[I]);
					CurTile = NodeTiles[I];
				}
			}
			Pieces.push_back(Cur);
			PieceTiles.push_back(CurTile);

			for (size_t P = 0; P < Pieces.size(); ++P)
			{
				Pieces[P].Piece = uint32_t(P);
				Pieces[P].bLast = P + 1 == Pieces.size();
				Tiles[PieceTiles[P]].push_back(std::move(Pieces[P]));
				++Stats.Pieces;
			}
		}

		for (auto& [Xy, Pieces] : Tiles)
		{
			std::sort(Pieces.begin(), Pieces.end(), [](const WayPiece& A, const WayPiece& B)
			{
				return A.WayId != B.WayId ? A.WayId < B.WayId : A.Piece < B.Piece;
			});
		}
		return Tiles;
	}

	std::unordered_map<int64_t, bool> NodesNeedingIds(const std::map<TileXY, std::vector<WayPiece>>& Tiles)
	{
		std::unordered_map<int64_t, int> Uses;
		std::unordered_map<int64_t, bool> Keep;
		for (const auto& [Xy, Pieces] : Tiles)
		{
			for (const WayPiece& P : Pieces)
			{
				for (size_t I = 0; I < P.Nodes.size(); ++I)
				{
					// The first node of a later piece repeats the cut node, so it isn't a second use
					if (I == 0 && P.Piece > 0)
					{
						Keep[P.Nodes[0]] = true;
						continue;
					}
					if (++Uses[P.Nodes[I]] > 1)
					{
						Keep[P.Nodes[I]] = true;
					}
				}
			}
		}
		return Keep;
	}

	std::vector<uint8_t> EncodeTile(const std::vector<WayPiece>& Pieces, const OsmSet& Set, int64_t Quantum,
		const std::unordered_map<int64_t, bool>& KeepId)
	{
		ByteWriter Strings, Points, Ids, WayIds, PieceInfo, Tags, Refs;

		// Node table in order of first use, so neighbouring points sit next to each other
		std::unordered_map<int64_t, uint32_t> Local;
		std::vector<int64_t> Order;
		for (const WayPiece& P : Pieces)
		{
			for (int64_t Id : P.Nodes)
			{
				if (Local.emplace(Id, uint32_t(Order.size())).second)
				{
					Order.push_back(Id);
				}
			}
		}

		// Points are stored in steps of Quantum x 1e-7 degrees
		Points.Varint(Quantum);
		Points.Varint(Order.size());
		int64_t PrevLat = 0, PrevLon = 0;
		std::vector<std::pair<uint32_t, int64_t>> Kept;
		for (uint32_t I = 0; I < Order.size(); ++I)
		{
			const LatLon& P = Set.Nodes.at(Order[I]);
			const int64_t Lat = RoundToSteps(P.Lat, Quantum);
			const int64_t Lon = RoundToSteps(P.Lon, Quantum);
			Points.Svarint(Lat - PrevLat);
			Points.Svarint(Lon - PrevLon);
			PrevLat = Lat;
			PrevLon = Lon;
			if (KeepId.count(Order[I]))
			{
				Kept.emplace_back(I, Order[I]);
			}
		}

		Ids.Varint(Kept.size());
		uint32_t PrevIndex = 0;
		int64_t PrevId = 0;
		for (const auto& [Index, Id] : Kept)
		{
			Ids.Varint(Index - PrevIndex);
			Ids.Svarint(Id - PrevId);
			PrevIndex = Index;
			PrevId = Id;
		}

		std::unordered_map<std::string, uint32_t> StringIndex;
		std::vector<const std::string*> StringList;
		auto StringOf = [&](const std::string& S)
		{
			auto [It, bNew] = StringIndex.emplace(S, uint32_t(StringList.size()));
			if (bNew)
			{
				StringList.push_back(&It->first);
			}
			return It->second;
		};


		WayIds.Varint(Pieces.size());
		int64_t PrevWay = 0;
		int64_t PrevRef = 0;
		for (const WayPiece& P : Pieces)
		{
			WayIds.Svarint(P.WayId - PrevWay);
			PrevWay = P.WayId;
			PieceInfo.Varint((uint64_t(P.Piece) << 1) | (P.bLast ? 1 : 0));

			// Keys the format doesn't know are left out (callers filter first, so normally there are none)
			std::vector<std::pair<int, uint32_t>> Known;
			for (const Tag& T : P.Tags)
			{
				const int Key = KeptKeyIndex(T.first);
				if (Key >= 0)
				{
					Known.emplace_back(Key, StringOf(T.second));
				}
			}
			Tags.Varint(Known.size());
			for (const auto& [Key, Value] : Known)
			{
				Tags.Varint(uint32_t(Key));
				Tags.Varint(Value);
			}

			Refs.Varint(P.Nodes.size());
			for (int64_t Id : P.Nodes)
			{
				const int64_t Ref = Local.at(Id);
				Refs.Svarint(Ref - PrevRef);
				PrevRef = Ref;
			}
		}

		Strings.Varint(StringList.size());
		for (const std::string* S : StringList)
		{
			Strings.Str(*S);
		}

		ByteWriter Out;
		Out.U8('G');
		Out.U8('T');
		Out.U8(FormatVersion);
		for (const ByteWriter* Block : { &Strings, &Points, &Ids, &WayIds, &PieceInfo, &Tags, &Refs })
		{
			Out.Varint(Block->Data.size());
			Out.Append(Block->Data);
		}
		return Out.Data;
	}

	bool DecodeTile(const uint8_t* Data, size_t Size, DecodedTile& T)
	{
		T = DecodedTile();
		ByteReader R(Data, Size);
		if (R.U8() != 'G' || R.U8() != 'T' || R.U8() != FormatVersion)
		{
			return false;
		}
		ByteReader Blocks[NumBlocks];
		for (ByteReader& Block : Blocks)
		{
			Block = R.Sub(R.Varint());
		}
		ByteReader& Strings = Blocks[0];
		ByteReader& Points = Blocks[1];
		ByteReader& Ids = Blocks[2];
		ByteReader& WayIds = Blocks[3];
		ByteReader& PieceInfo = Blocks[4];
		ByteReader& Tags = Blocks[5];
		ByteReader& Refs = Blocks[6];

		// A count can't be more than the bytes left, so broken data can't ask for a huge allocation
		auto Count = [](ByteReader& B)
		{
			const uint64_t N = B.Varint();
			if (N > uint64_t(B.End - B.Ptr))
			{
				B.bFailed = true;
				return size_t(0);
			}
			return size_t(N);
		};

		std::vector<std::string> StringList(Count(Strings));
		for (std::string& S : StringList)
		{
			S = Strings.Str();
		}

		T.Quantum = int64_t(Points.Varint());
		T.Points.resize(Count(Points));
		int64_t Lat = 0, Lon = 0;
		for (LatLon& P : T.Points)
		{
			Lat += Points.Svarint();
			Lon += Points.Svarint();
			P.Lat = int32_t(Lat * T.Quantum);
			P.Lon = int32_t(Lon * T.Quantum);
		}

		T.GlobalIds.assign(T.Points.size(), 0);
		const size_t NumIds = Count(Ids);
		uint64_t Index = 0;
		int64_t Id = 0;
		for (size_t I = 0; I < NumIds && Ids.Ok(); ++I)
		{
			Index += Ids.Varint();
			Id += Ids.Svarint();
			if (Index >= T.GlobalIds.size())
			{
				return false;
			}
			T.GlobalIds[size_t(Index)] = Id;
		}

		const std::vector<std::string>& Keys = KeptKeys();
		T.Pieces.resize(Count(WayIds));
		int64_t PrevWay = 0;
		int64_t PrevRef = 0;
		for (DecodedTile::TilePiece& P : T.Pieces)
		{
			PrevWay += WayIds.Svarint();
			P.WayId = PrevWay;
			const uint64_t Info = PieceInfo.Varint();
			P.Piece = uint32_t(Info >> 1);
			P.bLast = (Info & 1) != 0;

			P.Tags.resize(Count(Tags));
			for (Tag& Tg : P.Tags)
			{
				const uint64_t Key = Tags.Varint();
				const uint64_t Value = Tags.Varint();
				if (Key >= Keys.size() || Value >= StringList.size())
				{
					return false;
				}
				Tg.first = Keys[size_t(Key)];
				Tg.second = StringList[size_t(Value)];
			}

			P.Refs.resize(Count(Refs));
			for (uint32_t& Ref : P.Refs)
			{
				PrevRef += Refs.Svarint();
				if (PrevRef < 0 || size_t(PrevRef) >= T.Points.size())
				{
					return false;
				}
				Ref = uint32_t(PrevRef);
			}
		}

		for (const ByteReader& Block : Blocks)
		{
			if (!Block.Ok())
			{
				return false;
			}
		}
		return R.Ok() && T.Quantum > 0;
	}

	bool Assemble(const std::vector<DecodedTile>& Tiles, OsmSet& Set, std::string& Error)
	{
		return Assemble(Tiles, Set, Error, false);
	}

	bool Assemble(const std::vector<DecodedTile>& Tiles, OsmSet& Set, std::string& Error, bool bPartial)
	{
		struct PieceRef
		{
			const DecodedTile::TilePiece* Piece;
			std::vector<int64_t> NodeIds;
		};
		std::map<int64_t, std::vector<PieceRef>> ByWay;

		Set = OsmSet();
		int64_t NextMadeUpId = -1;
		for (const DecodedTile& T : Tiles)
		{
			// Give nodes without a global id an id that can't clash with real OSM ids
			std::vector<int64_t> NodeIds(T.Points.size());
			for (size_t I = 0; I < T.Points.size(); ++I)
			{
				NodeIds[I] = T.GlobalIds[I] != 0 ? T.GlobalIds[I] : NextMadeUpId--;
				Set.Nodes[NodeIds[I]] = T.Points[I];
			}
			for (const DecodedTile::TilePiece& P : T.Pieces)
			{
				PieceRef Ref{ &P, {} };
				for (uint32_t R : P.Refs)
				{
					Ref.NodeIds.push_back(NodeIds[R]);
				}
				ByWay[P.WayId].push_back(std::move(Ref));
			}
		}

		int64_t NextMadeUpWay = -1;
		for (auto& [WayId, Pieces] : ByWay)
		{
			std::sort(Pieces.begin(), Pieces.end(), [](const PieceRef& A, const PieceRef& B) { return A.Piece->Piece < B.Piece->Piece; });
			if (bPartial)
			{
				// only some tiles were read (an area cut out of a bigger archive): the pieces that follow on from each
				// other are joined; a way leaving the area and coming back is two ways (the second with a made-up id)
				size_t Start = 0;
				for (size_t I = 1; I <= Pieces.size(); ++I)
				{
					if (I < Pieces.size() && Pieces[I].Piece->Piece == Pieces[I - 1].Piece->Piece + 1)
					{
						continue;
					}
					Way W;
					W.Id = Start == 0 ? WayId : NextMadeUpWay--;
					W.Tags = Pieces[Start].Piece->Tags;
					for (size_t J = Start; J < I; ++J)
					{
						const std::vector<int64_t>& N = Pieces[J].NodeIds;
						W.Nodes.insert(W.Nodes.end(), N.begin() + (J > Start ? 1 : 0), N.end());
					}
					Set.Ways.push_back(std::move(W));
					Start = I;
				}
				continue;
			}
			for (size_t I = 0; I < Pieces.size(); ++I)
			{
				if (Pieces[I].Piece->Piece != I || Pieces[I].Piece->bLast != (I + 1 == Pieces.size()))
				{
					Error = "way " + std::to_string(WayId) + " has missing or extra pieces";
					return false;
				}
			}
			Way W;
			W.Id = WayId;
			W.Tags = Pieces[0].Piece->Tags;
			for (size_t I = 0; I < Pieces.size(); ++I)
			{
				const std::vector<int64_t>& N = Pieces[I].NodeIds;
				W.Nodes.insert(W.Nodes.end(), N.begin() + (I > 0 ? 1 : 0), N.end());
			}
			Set.Ways.push_back(std::move(W));
		}
		return true;
	}
}
