// Geodrive Core - GPL (see Core/README.md)
//
// A pack's files, as Core reads and writes them (engine-free: every host reads them the same way): its creatures
// (creatures/*.json: a creature, or a list; "like" starts one from another, only what differs given; its body's shape
// and its behaviour as building blocks) and its weapons (weapons/*.json: a rifle's load or a bow). See
// docs/MAKE_A_CREATURE.md for every key.

#pragma once

#include "Hunt.h"
#include "Json.h"

#include <string>
#include <vector>

namespace geohunt
{
	/** A creature from a file's object: added, or the one with its id replaced (only what's given changes). Its index,
	 *  or -1 and Error (what's wrong, for the one making it) */
	int ReadCreature(const geojson::Value& V, std::string& Error);
	/** A creature as a file has it: every figure, its shape and its behaviour */
	geojson::Value CreatureValue(const Species& S);

	/** A weapon from a file's object (added, or the one with its id replaced): its index, or -1 and Error */
	int ReadLoad(const geojson::Value& V, std::string& Error);
	geojson::Value LoadValue(const Load& L);

	/** A file's text: a creature (or weapon), or a list of them. How many were taken; each one turned away, and why,
	 *  in Errors ("<id>: why"; the file's own mistakes too) */
	int ReadCreatures(const std::string& Text, std::vector<std::string>& Errors);
	int ReadLoads(const std::string& Text, std::vector<std::string>& Errors);

	/** A picture's colour (#rrggbb, or #rgb) as linear red, green, blue; false if it isn't one */
	bool ColourFromHtml(const std::string& Html, float& R, float& G, float& B);
	std::string ColourToHtml(float R, float G, float B);
}
