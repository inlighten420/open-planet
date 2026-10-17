// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "OsmTypes.h"

#include <algorithm>

namespace geotile
{
	const std::vector<std::string>& KeptKeys()
	{
		static const std::vector<std::string> Keys = {
			// Roads
			"highway", "tunnel", "layer", "bridge", "oneway", "junction", "width", "lanes", "surface", "name", "ref", "maxspeed",
			// Buildings
			"building", "height", "building:levels",
			// Land cover
			"natural", "landuse",
			// Road shoulders (added after v0's first files; new keys only ever go on the end)
			"shoulder", "shoulder:width",
			// What kind of service road (driveway, parking aisle...): sets its width
			"service",
			// UK speed limits: national limits are tagged by type (GB:nsl_single...), and lit roads are 30 mph
			"maxspeed:type", "source:maxspeed", "lit",
		};
		return Keys;
	}

	int KeptKeyIndex(const std::string& Key)
	{
		const std::vector<std::string>& Keys = KeptKeys();
		for (size_t I = 0; I < Keys.size(); ++I)
		{
			if (Keys[I] == Key)
			{
				return int(I);
			}
		}
		return -1;
	}

	void FilterTags(std::vector<Tag>& Tags)
	{
		Tags.erase(std::remove_if(Tags.begin(), Tags.end(), [](const Tag& T) { return KeptKeyIndex(T.first) < 0; }), Tags.end());
		std::sort(Tags.begin(), Tags.end(), [](const Tag& A, const Tag& B) { return KeptKeyIndex(A.first) < KeptKeyIndex(B.first); });
	}
}
