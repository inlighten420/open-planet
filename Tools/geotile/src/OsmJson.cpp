// Geodrive tile tool - AGPL-3.0 (see LICENSE)

#include "OsmJson.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "json.hpp"

namespace geotile
{
	std::vector<uint8_t> ReadFile(const std::string& Path)
	{
		FILE* F = std::fopen(Path.c_str(), "rb");
		if (!F)
		{
			throw std::runtime_error("can't open " + Path);
		}
		std::vector<uint8_t> Data;
		uint8_t Buf[1 << 16];
		size_t N;
		while ((N = std::fread(Buf, 1, sizeof(Buf), F)) > 0)
		{
			Data.insert(Data.end(), Buf, Buf + N);
		}
		std::fclose(F);
		return Data;
	}

	void WriteFile(const std::string& Path, const std::vector<uint8_t>& Data)
	{
		FILE* F = std::fopen(Path.c_str(), "wb");
		if (!F)
		{
			throw std::runtime_error("can't write " + Path);
		}
		const bool bOk = std::fwrite(Data.data(), 1, Data.size(), F) == Data.size();
		std::fclose(F);
		if (!bOk)
		{
			throw std::runtime_error("can't write " + Path);
		}
	}

	OsmSet LoadOverpassJson(const std::string& Path, bool bKeepAllTags)
	{
		const std::vector<uint8_t> Raw = ReadFile(Path);
		const nlohmann::json Root = nlohmann::json::parse(Raw.begin(), Raw.end());

		OsmSet Set;
		for (const nlohmann::json& E : Root.at("elements"))
		{
			const std::string& Type = E.at("type").get_ref<const std::string&>();
			if (Type == "node")
			{
				// Overpass writes 7 decimals, so rounding the double back to 1e-7 is exact
				LatLon P;
				P.Lat = int32_t(std::llround(E.at("lat").get<double>() * 1e7));
				P.Lon = int32_t(std::llround(E.at("lon").get<double>() * 1e7));
				Set.Nodes[E.at("id").get<int64_t>()] = P;
			}
			else if (Type == "way")
			{
				Way W;
				W.Id = E.at("id").get<int64_t>();
				for (const nlohmann::json& N : E.at("nodes"))
				{
					W.Nodes.push_back(N.get<int64_t>());
				}
				if (E.contains("tags"))
				{
					for (auto It = E["tags"].begin(); It != E["tags"].end(); ++It)
					{
						W.Tags.emplace_back(It.key(), It.value().get<std::string>());
					}
				}
				if (!bKeepAllTags)
				{
					FilterTags(W.Tags);
				}
				Set.Ways.push_back(std::move(W));
			}
		}
		return Set;
	}
}
