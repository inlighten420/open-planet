# Road rules by country

One file per country, named by its ISO 3166-1 code (`DE.json`, `JP.json`), and per state or province where its limits
differ (`US-CA.json`, `CA-ON.json`, `AU-NSW.json`: read over the country's, by the code in a region's region.json).

Every country and state is here: the limits of roads without a speed sign come from the OpenStreetMap wiki's
[Default speed limits](https://wiki.openstreetmap.org/wiki/Default_speed_limits) (machine-read as
[osm-legal-default-speeds](https://github.com/westnordost/osm-legal-default-speeds) by Tobias Zwick and contributors),
the side from the [REST Countries](https://gitlab.com/restcountries/restcountries) data. `python Tools/make_rules.py`
makes them again (keeping any hand-written "style" and "sections"); better figures for a country belong on that wiki
page, where every OpenStreetMap tool can use them. South Africa and the UK keep their own detailed rules (built in). A region gets its country's file by the code
`geotile region` writes into its `<name>.region.json`. Without a file a country still works: it keeps left or right
as the country does (the list is built into `Core/GeoRoads/Country.cpp`), signs in km/h (mph for the US, Liberia and
Myanmar), uses South Africa's lane sizes and white lines.

A rules file in the game's data folder (`data/rules/DE.json` beside a packaged game) is read after this one, so a
player can try changes without touching the game.

```json
{
	"name": "Germany",
	"drive": "right",                 // "left" or "right"
	"mph": false,                     // limits shown in mph
	"style": "europe",                // "europe" (white lines), "south_africa" (yellow edge lines), "uk" (the UK's own rules)
	"speed_kmh": { "residential": 50, "motorway": 130 },   // the limit of a road with no maxspeed tag, by highway class
	"urban_kmh": 50,                  // in towns (a lit road), for any class...
	"urban_speed_kmh": { "primary": 60 },                  // ...or by class
	"sections": { "motorway": [2, 3.75, 2.5, 0.75] }       // lanes (both ways), lane width, outer shoulder, inner shoulder (m)
}
```

The classes are OpenStreetMap's `highway` values: motorway, trunk, primary, secondary, tertiary, unclassified,
residential, living_street, service, track, and the `_link` roads. A road's own `maxspeed`, `lanes`, `width` and
`shoulder` tags always win over these. "about" says where the numbers come from: please say, for others to check.
