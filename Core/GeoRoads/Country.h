// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// A country's road rules, as much as the roads need: which side traffic keeps to, whether limits are signed in mph,
// the speed limit an unsigned road has, how wide its lanes and shoulders are, and how its lines are painted. The game
// reads them from a plain file per country (Godot/rules/<code>.json, by the region's country code), so a new country
// needs no code. South Africa's and the UK's detailed rules (their road manuals' cross-sections, the UK's national
// limits and markings) are built in as their styles; any other country starts from South Africa's sizes with its own
// side, limits and white lines.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace georoads
{
	/** Whose detailed rules a country's roads follow where its own file says nothing */
	enum class RoadStyle : uint8_t
	{
		SouthAfrica,	// the CSIR Red Book's cross-sections; yellow edge lines on shoulders
		UK,				// DMRB and Manual for Streets cross-sections, national limits by road type, the UK's markings
		Europe,			// South Africa's sizes, every line white (the Vienna Convention's)
		US,				// North America's (MUTCD): yellow centre lines and left edges of one-way roads, white right edges
	};

	struct CountryRules
	{
		std::string Code = "ZA";	// ISO 3166-1 alpha-2
		std::string Name = "South Africa";
		bool bDriveLeft = true;
		bool bMph = false;
		RoadStyle Style = RoadStyle::SouthAfrica;
		/** The limit (km/h) a road without a maxspeed tag has, by its highway class ("residential", "motorway"...) */
		std::unordered_map<std::string, float> SpeedKmh;
		/** In towns (a lit road: towns light theirs): the limit by class, or for any class not given (0: none) */
		std::unordered_map<std::string, float> UrbanSpeedKmh;
		float UrbanKmh = 0.0f;
		/** A class's cross-section (m): lanes (both ways; a one-way way's own), lane width, outer shoulder, inner shoulder
		 *  (a one-way way's, by the median; -1: as the outer) */
		std::unordered_map<std::string, std::array<float, 4>> Sections;

		bool IsUK() const { return Style == RoadStyle::UK; }
	};

	/** The built-in rules for a country code: ZA and GB as their styles; anything else Europe's (right-hand, km/h) */
	CountryRules BuiltInRules(const std::string& Code);
}
