// Geodrive tile tool - GPL (see Core/README.md)

#include "OsmPbf.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "Bytes.h"
#include "miniz.h"

namespace geotile
{
	namespace
	{
		constexpr size_t ItemsPerBlock = 8000;

		/** Protobuf field writers on top of ByteWriter */
		struct Proto : ByteWriter
		{
			void Key(int Field, int WireType) { Varint((uint64_t(Field) << 3) | uint64_t(WireType)); }
			void VarintField(int Field, uint64_t V) { Key(Field, 0); Varint(V); }
			void BytesField(int Field, const std::vector<uint8_t>& B) { Key(Field, 2); Varint(B.size()); Append(B); }
			void StringField(int Field, const std::string& S) { Key(Field, 2); Str(S); }
		};

		std::vector<uint8_t> Deflate(const std::vector<uint8_t>& Raw)
		{
			mz_ulong Size = mz_compressBound(mz_ulong(Raw.size()));
			std::vector<uint8_t> Out(Size);
			if (mz_compress2(Out.data(), &Size, Raw.data(), mz_ulong(Raw.size()), 9) != MZ_OK)
			{
				throw std::runtime_error("zlib compress failed");
			}
			Out.resize(Size);
			return Out;
		}

		void WriteFileBlock(ByteWriter& Out, const char* Type, const std::vector<uint8_t>& Raw)
		{
			Proto Blob;
			Blob.VarintField(2, Raw.size());
			Blob.BytesField(3, Deflate(Raw));

			Proto Header;
			Header.StringField(1, Type);
			Header.VarintField(3, Blob.Data.size());

			// The header length is a 4-byte big-endian number
			const uint32_t N = uint32_t(Header.Data.size());
			Out.U8(uint8_t(N >> 24));
			Out.U8(uint8_t(N >> 16));
			Out.U8(uint8_t(N >> 8));
			Out.U8(uint8_t(N));
			Out.Append(Header.Data);
			Out.Append(Blob.Data);
		}

		/** A primitive block: its string table plus one PrimitiveGroup message */
		std::vector<uint8_t> PrimitiveBlock(const std::vector<std::string>& Strings, const std::vector<uint8_t>& Group)
		{
			Proto Table;
			for (const std::string& S : Strings)
			{
				Table.StringField(1, S);
			}
			Proto Block;
			Block.BytesField(1, Table.Data);
			Block.BytesField(2, Group);
			return Block.Data;
		}
	}

	std::vector<uint8_t> WriteOsmPbf(const OsmSet& Set)
	{
		ByteWriter Out;

		Proto Header;
		Header.StringField(4, "OsmSchema-V0.6");
		Header.StringField(4, "DenseNodes");
		Header.StringField(16, "geotile");
		WriteFileBlock(Out, "OSMHeader", Header.Data);

		std::vector<int64_t> NodeIds;
		NodeIds.reserve(Set.Nodes.size());
		for (const auto& [Id, P] : Set.Nodes)
		{
			NodeIds.push_back(Id);
		}
		std::sort(NodeIds.begin(), NodeIds.end());

		for (size_t Start = 0; Start < NodeIds.size(); Start += ItemsPerBlock)
		{
			const size_t End = std::min(NodeIds.size(), Start + ItemsPerBlock);
			Proto Ids, Lats, Lons;
			int64_t PrevId = 0, PrevLat = 0, PrevLon = 0;
			for (size_t I = Start; I < End; ++I)
			{
				// Granularity 100 nanodegrees, so values are in 1e-7 degrees, the same as ours
				const LatLon& P = Set.Nodes.at(NodeIds[I]);
				Ids.Svarint(NodeIds[I] - PrevId);
				Lats.Svarint(P.Lat - PrevLat);
				Lons.Svarint(P.Lon - PrevLon);
				PrevId = NodeIds[I];
				PrevLat = P.Lat;
				PrevLon = P.Lon;
			}
			Proto Dense;
			Dense.BytesField(1, Ids.Data);
			Dense.BytesField(8, Lats.Data);
			Dense.BytesField(9, Lons.Data);
			Proto Group;
			Group.BytesField(2, Dense.Data);
			WriteFileBlock(Out, "OSMData", PrimitiveBlock({ "" }, Group.Data));
		}

		std::vector<const Way*> Ways;
		for (const Way& W : Set.Ways)
		{
			Ways.push_back(&W);
		}
		std::sort(Ways.begin(), Ways.end(), [](const Way* A, const Way* B) { return A->Id < B->Id; });

		for (size_t Start = 0; Start < Ways.size(); Start += ItemsPerBlock)
		{
			const size_t End = std::min(Ways.size(), Start + ItemsPerBlock);
			std::vector<std::string> Strings = { "" };
			std::unordered_map<std::string, uint32_t> StringIndex;
			auto StringOf = [&](const std::string& S)
			{
				auto [It, bNew] = StringIndex.emplace(S, uint32_t(Strings.size()));
				if (bNew)
				{
					Strings.push_back(S);
				}
				return uint64_t(It->second);
			};

			Proto Group;
			for (size_t I = Start; I < End; ++I)
			{
				const Way& W = *Ways[I];
				Proto Keys, Vals, Refs;
				for (const Tag& T : W.Tags)
				{
					Keys.Varint(StringOf(T.first));
					Vals.Varint(StringOf(T.second));
				}
				int64_t Prev = 0;
				for (int64_t Id : W.Nodes)
				{
					Refs.Svarint(Id - Prev);
					Prev = Id;
				}
				Proto Msg;
				Msg.VarintField(1, uint64_t(W.Id));
				Msg.BytesField(2, Keys.Data);
				Msg.BytesField(3, Vals.Data);
				Msg.BytesField(8, Refs.Data);
				Group.BytesField(3, Msg.Data);
			}
			WriteFileBlock(Out, "OSMData", PrimitiveBlock(Strings, Group.Data));
		}
		return Out.Data;
	}
}
