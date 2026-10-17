// Geodrive Godot client - AGPL-3.0 (see LICENSE)
//
// ESA WorldCover's files are Cloud-Optimised GeoTIFFs: 36000 x 36000 pixels (1/12000 degree) over 3 x 3 degrees, in
// 1024-pixel tiles, each compressed (zlib) on its own, the tiles' places listed at the start of the file. So the start
// is fetched once (64 KB), then just the tiles a piece needs (some 50 KB each), and both kept. Each file also carries
// coarser copies of itself (half as many pixels across each time, listed one after another at its start): the far
// land's are read from those.

#include "landcover.h"
#include "http_fetch.h"

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

using namespace godot;

namespace
{
	constexpr double PixelsPerDegree = 12000.0;
	constexpr int32_t FilePixels = 36000;
	const char* Server = "https://esa-worldcover.s3.eu-central-1.amazonaws.com/v200/2021/map/";

	std::mutex CacheLock;	// (two pieces building at once may want the same file)

	bool ReadFile(const std::filesystem::path& Path, std::vector<uint8_t>& Out)
	{
		std::ifstream File(Path, std::ios::binary);
		if (!File)
		{
			return false;
		}
		Out.assign(std::istreambuf_iterator<char>(File), std::istreambuf_iterator<char>());
		return true;
	}

	void WriteFile(const std::filesystem::path& Path, const std::vector<uint8_t>& Data)
	{
		std::error_code Ignore;
		std::filesystem::create_directories(Path.parent_path(), Ignore);
		std::ofstream File(Path, std::ios::binary);
		File.write(reinterpret_cast<const char*>(Data.data()), std::streamsize(Data.size()));
	}

	/** The parts of a file's start that say where its tiles are */
	struct TiffIndex
	{
		int32_t Width = 0, Height = 0, TileWidth = 0, TileHeight = 0, Compression = 0, Predictor = 1;
		std::vector<uint32_t> Offsets, Counts;
		bool IsValid() const { return Width > 0 && TileWidth > 0 && TileHeight > 0 && !Offsets.empty() && Offsets.size() == Counts.size(); }
	};

	TiffIndex ParseTiff(const std::vector<uint8_t>& Head, int32_t Level)
	{
		TiffIndex Out;
		auto U16 = [&Head](size_t At) { return At + 2 <= Head.size() ? uint32_t(Head[At] | (Head[At + 1] << 8)) : 0u; };
		auto U32 = [&Head](size_t At)
		{
			return At + 4 <= Head.size() ? uint32_t(Head[At]) | (uint32_t(Head[At + 1]) << 8) | (uint32_t(Head[At + 2]) << 16) | (uint32_t(Head[At + 3]) << 24) : 0u;
		};
		if (Head.size() < 8 || Head[0] != 0x49 || Head[1] != 0x49 || U16(2) != 42)
		{
			return Out;	// (only little-endian classic TIFF, as WorldCover's are)
		}
		size_t Directory = U32(4);
		for (int32_t i = 0; i < Level && Directory != 0; ++i)
		{
			Directory = U32(Directory + 2 + 12 * size_t(U16(Directory)));	// (the next copy's list, after this one's)
		}
		if (Directory == 0)
		{
			return Out;
		}
		const uint32_t Entries = U16(Directory);
		for (uint32_t i = 0; i < Entries; ++i)
		{
			const size_t E = Directory + 2 + 12 * size_t(i);
			const uint32_t Tag = U16(E), Type = U16(E + 2), Count = U32(E + 4);
			const uint32_t Value = Type == 3 && Count == 1 ? U16(E + 8) : U32(E + 8);
			auto Array = [&](std::vector<uint32_t>& Into)
			{
				Into.resize(Count);
				for (uint32_t k = 0; k < Count; ++k)
				{
					Into[k] = Count == 1 ? Value : (Type == 3 ? U16(Value + 2 * size_t(k)) : U32(Value + 4 * size_t(k)));
				}
			};
			switch (Tag)
			{
			case 256: Out.Width = int32_t(Value); break;
			case 257: Out.Height = int32_t(Value); break;
			case 259: Out.Compression = int32_t(Value); break;
			case 317: Out.Predictor = int32_t(Value); break;
			case 322: Out.TileWidth = int32_t(Value); break;
			case 323: Out.TileHeight = int32_t(Value); break;
			case 324: Array(Out.Offsets); break;
			case 325: Array(Out.Counts); break;
			default: break;
			}
		}
		return Out;
	}

	std::string FileName(int32_t Lat, int32_t Lon)
	{
		char Name[64];
		std::snprintf(Name, sizeof(Name), "ESA_WorldCover_10m_2021_v200_%c%02d%c%03d_Map", Lat < 0 ? 'S' : 'N', std::abs(Lat), Lon < 0 ? 'W' : 'E', std::abs(Lon));
		return Name;
	}
}

bool FetchWorldCover(geoground::LandCover& Cover, const std::string& CacheDir, std::string& OutLog, int32_t Level)
{
	const double PixelsPerDegree = ::PixelsPerDegree / double(1 << Level);
	const int32_t FilePixels = ::FilePixels >> Level;
	const std::filesystem::path Folder = std::filesystem::u8path(CacheDir) / "landcover";
	// the raster: the box on WorldCover's own pixel grid (whole pixels, so the files' pixels drop straight in)
	const int64_t LeftPixel = int64_t(std::floor(Cover.West * PixelsPerDegree));
	const int64_t TopPixel = int64_t(std::ceil(Cover.North * PixelsPerDegree));
	const int32_t Width = int32_t(int64_t(std::ceil(Cover.East * PixelsPerDegree)) - LeftPixel);
	const int32_t Height = int32_t(TopPixel - int64_t(std::floor(Cover.South * PixelsPerDegree)));
	if (Width <= 0 || Height <= 0 || int64_t(Width) * Height > 40000LL * 40000LL)
	{
		return false;
	}
	std::vector<uint8_t> Classes(size_t(Width) * size_t(Height), 0);
	int32_t Fetched = 0, Kept = 0, Missing = 0;
	bool bAny = false;

	for (int32_t FileLat = int32_t(std::floor(Cover.South / 3.0)) * 3; FileLat <= int32_t(std::floor(Cover.North / 3.0)) * 3; FileLat += 3)
	{
		for (int32_t FileLon = int32_t(std::floor(Cover.West / 3.0)) * 3; FileLon <= int32_t(std::floor(Cover.East / 3.0)) * 3; FileLon += 3)
		{
			const std::string Name = FileName(FileLat, FileLon);
			std::vector<uint8_t> Head;
			{
				std::lock_guard<std::mutex> Guard(CacheLock);
				if (std::filesystem::exists(Folder / (Name + ".none")))
				{
					continue;	// (no such file: open sea)
				}
				if (!ReadFile(Folder / (Name + ".head"), Head))
				{
					const HttpResult Got = HttpGet(std::string(Server) + Name + ".tif", 0, 65535);
					if (Got.Code == 403 || Got.Code == 404)
					{
						WriteFile(Folder / (Name + ".none"), {});
						continue;
					}
					if (Got.Body.empty())
					{
						++Missing;	// (offline)
						continue;
					}
					Head = Got.Body;
					WriteFile(Folder / (Name + ".head"), Head);
				}
			}
			const TiffIndex Index = ParseTiff(Head, Level);
			if (!Index.IsValid() || Index.Compression != 8 || Index.Predictor != 1 || Index.Width != FilePixels)
			{
				OutLog += "[Vegetation] " + Name + ": not a WorldCover file as expected; ";
				continue;
			}
			const int32_t TilesAcross = (Index.Width + Index.TileWidth - 1) / Index.TileWidth;
			// the file's top-left pixel in the world's pixels (the file's top-left corner is FileLat + 3, FileLon)
			const int64_t FileLeftPixel = int64_t(FileLon) * int64_t(PixelsPerDegree);
			const int64_t FileTopPixel = int64_t(FileLat + 3) * int64_t(PixelsPerDegree);
			// the box in the file's pixels (x right, y down)
			const int64_t PX0 = std::max<int64_t>(0, LeftPixel - FileLeftPixel);
			const int64_t PX1 = std::min<int64_t>(Index.Width - 1, LeftPixel + Width - 1 - FileLeftPixel);
			const int64_t PY0 = std::max<int64_t>(0, FileTopPixel - TopPixel);
			const int64_t PY1 = std::min<int64_t>(Index.Height - 1, FileTopPixel - TopPixel + Height - 1);
			if (PX0 > PX1 || PY0 > PY1)
			{
				continue;
			}
			for (int64_t TY = PY0 / Index.TileHeight; TY <= PY1 / Index.TileHeight; ++TY)
			{
				for (int64_t TX = PX0 / Index.TileWidth; TX <= PX1 / Index.TileWidth; ++TX)
				{
					const size_t Tile = size_t(TY) * size_t(TilesAcross) + size_t(TX);
					if (Tile >= Index.Offsets.size() || Index.Counts[Tile] == 0)
					{
						continue;
					}
					char TileName[128];
					if (Level > 0)
					{
						std::snprintf(TileName, sizeof(TileName), "%s_L%d_%d_%d.z", Name.c_str(), Level, int32_t(TX), int32_t(TY));
					}
					else
					{
						std::snprintf(TileName, sizeof(TileName), "%s_%d_%d.z", Name.c_str(), int32_t(TX), int32_t(TY));
					}
					std::vector<uint8_t> Packed;
					if (ReadFile(Folder / TileName, Packed) && !Packed.empty())
					{
						++Kept;
					}
					else
					{
						const HttpResult Got = HttpGet(std::string(Server) + Name + ".tif", Index.Offsets[Tile], int64_t(Index.Offsets[Tile]) + Index.Counts[Tile] - 1);
						if (Got.Body.size() != Index.Counts[Tile])
						{
							++Missing;
							continue;
						}
						Packed = Got.Body;
						std::lock_guard<std::mutex> Guard(CacheLock);
						WriteFile(Folder / TileName, Packed);
						++Fetched;
					}
					PackedByteArray Bytes;
					Bytes.resize(int64_t(Packed.size()));
					std::memcpy(Bytes.ptrw(), Packed.data(), Packed.size());
					const int64_t Size = int64_t(Index.TileWidth) * Index.TileHeight;
					const PackedByteArray Pixels = Bytes.decompress(Size, FileAccess::COMPRESSION_DEFLATE);
					if (Pixels.size() != Size)
					{
						++Missing;
						continue;
					}
					const uint8_t* From = Pixels.ptr();
					// each pixel of this tile inside the box, into the raster
					for (int64_t Row = 0; Row < Index.TileHeight; ++Row)
					{
						const int64_t FileY = TY * Index.TileHeight + Row;
						if (FileY < PY0 || FileY > PY1)
						{
							continue;
						}
						const int64_t Y = FileY + TopPixel - FileTopPixel;	// (the raster's rows from its top, southwards)
						for (int64_t Col = 0; Col < Index.TileWidth; ++Col)
						{
							const int64_t FileX = TX * Index.TileWidth + Col;
							if (FileX < PX0 || FileX > PX1)
							{
								continue;
							}
							const int64_t X = FileX + FileLeftPixel - LeftPixel;
							Classes[size_t(Y) * size_t(Width) + size_t(X)] = From[size_t(Row) * size_t(Index.TileWidth) + size_t(Col)];
						}
					}
					bAny = true;
				}
			}
		}
	}
	char Line[200];
	std::snprintf(Line, sizeof(Line), "[Vegetation] land cover (ESA WorldCover%s): %d tiles fetched, %d kept from before, %d missing", Level > 0 ? ", coarse" : "",
		Fetched, Kept, Missing);
	OutLog += Line;
	if (!bAny)
	{
		return false;
	}
	Cover.Top = double(TopPixel) / PixelsPerDegree;
	Cover.Left = double(LeftPixel) / PixelsPerDegree;
	Cover.CellDegrees = 1.0 / PixelsPerDegree;
	Cover.Width = Width;
	Cover.Height = Height;
	Cover.Classes = std::move(Classes);
	return true;
}
