// Geodrive Core - AGPL-3.0 (see LICENSE)

#include "Country.h"

#include <unordered_set>

namespace georoads
{
	CountryRules BuiltInRules(const std::string& Code)
	{
		CountryRules Rules;
		Rules.Code = Code;
		if (Code == "ZA" || Code.empty())
		{
			Rules.Code = "ZA";
			return Rules;	// (the defaults: South Africa)
		}
		if (Code == "GB" || Code == "IM" || Code == "JE" || Code == "GG")
		{
			Rules.Name = "United Kingdom";
			Rules.bDriveLeft = true;
			Rules.bMph = true;
			Rules.Style = RoadStyle::UK;
			return Rules;
		}
		Rules.Name = Code;
		// the countries that keep left (the rest of the world keeps right); southern Africa's neighbours paint as South
		// Africa does (SADC's road traffic signs manual)
		static const std::unordered_set<std::string> Southern = { "NA", "BW", "ZW", "LS", "SZ", "MZ", "ZM", "MW", "KE", "TZ", "UG" };
		static const std::unordered_set<std::string> Left = { "AU", "NZ", "JP", "IN", "PK", "BD", "LK", "NP", "BT", "MV", "TH", "MY",
			"SG", "ID", "BN", "TL", "HK", "MO", "IE", "MT", "CY", "MU", "SC", "JM", "TT", "BB", "BS", "GY", "SR", "FJ", "PG", "SB",
			"WS", "TO", "KI", "TV", "NR", "VC", "LC", "GD", "DM", "AG", "KN", "VG", "VI", "AI", "MS", "KY", "TC", "BM", "FK", "SH" };
		if (Southern.count(Code))
		{
			Rules.bDriveLeft = true;
			Rules.Style = RoadStyle::SouthAfrica;
			return Rules;
		}
		Rules.bDriveLeft = Left.count(Code) > 0;
		Rules.bMph = Code == "US" || Code == "LR" || Code == "MM";	// (signed in mph; Liberia and Myanmar too)
		Rules.Style = RoadStyle::Europe;
		return Rules;
	}
}
