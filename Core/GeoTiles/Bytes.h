// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// Little helpers for writing and reading packed numbers.
// Everything is little-endian and fixed width or LEB128 varints, so every machine reads the same bytes.
// No exceptions: a reader that runs off the end, or meets a broken number, just stops and reports !Ok().

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace geotile
{
	/** Growing byte buffer with varint and fixed-width writers */
	struct ByteWriter
	{
		std::vector<uint8_t> Data;

		void U8(uint8_t V) { Data.push_back(V); }

		void Varint(uint64_t V)
		{
			while (V >= 0x80)
			{
				Data.push_back(uint8_t(V) | 0x80);
				V >>= 7;
			}
			Data.push_back(uint8_t(V));
		}

		/** Signed numbers as zigzag varints, so small negative steps stay small */
		void Svarint(int64_t V) { Varint((uint64_t(V) << 1) ^ uint64_t(V >> 63)); }

		void Fixed(uint64_t V, int Bytes)
		{
			for (int I = 0; I < Bytes; ++I)
			{
				Data.push_back(uint8_t(V >> (8 * I)));
			}
		}

		void Str(const std::string& S)
		{
			Varint(S.size());
			Data.insert(Data.end(), S.begin(), S.end());
		}

		void Append(const std::vector<uint8_t>& Other) { Data.insert(Data.end(), Other.begin(), Other.end()); }
	};

	/** Reader over a byte range. After the first error every read returns 0 and Ok() is false. */
	struct ByteReader
	{
		const uint8_t* Ptr = nullptr;
		const uint8_t* End = nullptr;
		bool bFailed = false;

		ByteReader() = default;
		ByteReader(const uint8_t* InData, size_t Size) : Ptr(InData), End(InData + Size) {}

		bool Ok() const { return !bFailed; }
		bool AtEnd() const { return Ptr == End; }

		bool Need(size_t N)
		{
			if (bFailed || size_t(End - Ptr) < N)
			{
				bFailed = true;
				return false;
			}
			return true;
		}

		uint8_t U8() { return Need(1) ? *Ptr++ : 0; }

		uint64_t Varint()
		{
			uint64_t V = 0;
			for (int Shift = 0; Shift < 64; Shift += 7)
			{
				if (!Need(1))
				{
					return 0;
				}
				const uint8_t B = *Ptr++;
				V |= uint64_t(B & 0x7f) << Shift;
				if (!(B & 0x80))
				{
					return V;
				}
			}
			bFailed = true;
			return 0;
		}

		int64_t Svarint()
		{
			const uint64_t V = Varint();
			return int64_t(V >> 1) ^ -int64_t(V & 1);
		}

		uint64_t Fixed(int Bytes)
		{
			if (!Need(size_t(Bytes)))
			{
				return 0;
			}
			uint64_t V = 0;
			for (int I = 0; I < Bytes; ++I)
			{
				V |= uint64_t(Ptr[I]) << (8 * I);
			}
			Ptr += Bytes;
			return V;
		}

		std::string Str()
		{
			const uint64_t N = Varint();
			if (!Need(size_t(N)))
			{
				return std::string();
			}
			std::string S(reinterpret_cast<const char*>(Ptr), size_t(N));
			Ptr += N;
			return S;
		}

		/** A sub-reader over the next N bytes (a failed one if there aren't N bytes) */
		ByteReader Sub(uint64_t N)
		{
			if (!Need(size_t(N)))
			{
				ByteReader Bad;
				Bad.bFailed = true;
				return Bad;
			}
			ByteReader R(Ptr, size_t(N));
			Ptr += N;
			return R;
		}
	};
}
