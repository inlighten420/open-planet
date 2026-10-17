# Road research: building roads like a road engineer

Date: 2026-10-11. Test area: Garden Route (`-34.12,22.33,-33.90,23.50`).

## Summary (read this first)

- **The idea works.** OSM gives where the road goes and its class. South African design rules give its shape. The DEM is only a rough guide.
- **OSM gives us very little height help.** No node has an `ele` tag. `incline` is almost never set (2 ways). So heights must come from the DEM plus design rules.
- **Bridges are fully tagged.** 124 bridge ways, and every one has `layer`. So we can find every bridge end and treat it properly.
- **Main roads are well tagged.** Trunk (N2) and motorway have about 100% `lanes`, `maxspeed`, `surface` and `ref`. Small roads are not: residential has 17% `lanes` and 6% `maxspeed`.
- **`width` is useless here.** It is on about 0% of roads. Keep using our class width table.
- **Main roads are drawn with plenty of points.** The N2 has a point about every 10 m, with gentle bends. So fitting real curves to it will work well.
- **Why we get kinks and bumps today:**
  - Roads are straight pieces between OSM points, joined with mitred corners. There is no curve fitting.
  - The height solver only *prefers* smoothness. It has no hard limit on slope or on how fast slope can change.
  - At bridge ends, the DEM pulls the ramp down into the valley. Copernicus is a surface model, so it also sees trees and buildings.
- **Plan in one line:** fit real curves (lines, arcs, clothoids) to OSM; solve heights with hard grade and vertical-curve limits per class; add camber and banking; give junctions proper kerb radii.
- **Tools:**
  - **Clothoids** (BSD-2) for curve fitting.
  - **OSQP** (Apache-2.0) for the height solve.
  - SUMO netconvert only as an offline helper, if at all.
  - Skip Overture.
  - Keep libOpenDRIVE (Apache-2.0) in mind only if we ever store roads as OpenDRIVE.

## 1. Design numbers

### How sure we are
- **SA** = read straight from a South African document.
- **SA-q** = an SA table, quoted in another SA report.
- **SA-f** = worked out from SANRAL's own formula.
- **US** = AASHTO 2018 fallback, not South African.

The main SA sources are:
- the CSIR Red Book, chapter 7 (urban roads, full tables)
- the SANRAL Geometric Design Guide (rural; its tables are images, so some values are quoted or worked out)
- TRH26 (road classes and speeds)

TRH17 (1988) is not online.

### Design speed per road class (km/h)

| SA class (TRH26) | Rural | Urban | Source |
|---|---|---|---|
| Class 1 freeway / principal arterial | 120 | 120 (100 on short routes) | SA |
| Class 2 major arterial | 120 | 80 | SA |
| Class 3 minor arterial | 100-120 | 70 | SA |
| Class 4 collector | 80-100 | 50-60 | SA |
| Class 5 local / residential | 60-80 | 40 | SA |

SANRAL rural design speeds by terrain:
- flat 90-120
- rolling 80-100
- mountainous 60-80

### Geodrive defaults per OSM class (proposed)

This table maps our OSM classes onto the SA rules.
- The **design speed** is the bigger of the class speed and the OSM `maxspeed` + 10. Where OSM shows a curve tighter than allowed, the curve is real (a pass, a hairpin). Keep it, and lower the design speed there.
- **e max** is the maximum banking on curves.
- **Min radius** is the tightest curve allowed at the design speed and e max.
- **Grade** is the maximum slope, in flat / rolling / mountain terrain.
- **K crest** and **K sag** set the vertical curve size: metres of road per 1% change in grade. Crest K uses a 0.6 m object height.

| OSM class | Design speed | e max | Min radius (m) | Max grade % (flat / rolling / mountain) | K crest | K sag | Lane (m) | Shoulder (m) | Camber |
|---|---|---|---|---|---|---|---|---|---|
| motorway | 120 | 10% | 600 | 3-4 (all terrain, SANRAL) | 113 | 52-69 | 3.7 | 3.0 outer, 1.0 inner | 2% |
| trunk (N2) | 100-120 | 8-10% | 360-600 | 4 / 5 / 6 | 61-113 | 36-69 | 3.7 | 2.5 (SANRAL two-lane 13.4 m) | 2% |
| primary / secondary (R roads) | 100 | 8% | 390 | 5 / 6 / 7 | 61 | 36-49 | 3.4-3.7 | 1.5-2.5 | 2% |
| tertiary | 80 | 8% rural, 6% urban | 230-255 | 6 / 7 / 8 | 30 | 25-32 | 3.4 | 1.0-1.5 | 2% |
| unclassified (rural minor) | 60 | 6% | 130 | 10 (12 short) | 12 | 16-18 | 3.1 | 0.5 | 2% sealed, 4% gravel |
| residential | 40 | 0-4% | 60-70 | 12 (16 for 50 m or less) | 4-6 | 8 | 3.1-3.25 | 0 | 2% |
| living_street / service | 30 | 0-2% | 30-40 | 12-16 | 2-4 | 4-8 | 2.75-3.5 | 0 | 2% |
| track (gravel) | 30-40 | 0 | follow OSM | 16-20 (Geodrive choice, no SA source) | 2-4 | 4 | 3.0 | 0 | 4% (SA-q) |
| *_link (ramps) | 50-60 | 6-8% | 80-130 | 6 preferred, 8 max (SANRAL) | 7-12 | 13-18 | 3.7 | 1.0 | 2% |

Where the numbers come from:
- **Min radius:** SANRAL 60-100 km/h (SA-q), Red Book urban (SA), and AASHTO above 100 km/h (US).
- **Grades:** Red Book Table 7.16 (SA) and SANRAL (SA).
- **K values:** SANRAL crest K (SA-q) and the sag formula (SA-f).
- **Widths:** SANRAL, the Red Book and TRH26 (SA).

### Minimum curve radius (m), by speed and banking

Formula: R = V² / 127 (e + f).

| Speed | e 4% | e 6% | e 8% | e 10% | Source |
|---|---|---|---|---|---|
| 40 | 60 | 55 | 50 | 38 | Red Book; e10 from US |
| 60 | 150 | 130 | 120 | 110 | SANRAL (SA-q) |
| 70 | 200 | 190 | 170 | 150 | SANRAL (SA-q) |
| 80 | 280 | 250 | 230 | 210 | SANRAL (SA-q) |
| 100 | 490 | 440 | 390 | 360 | SANRAL (SA-q) |
| 120 | 760 | 670-756 | 600-667 | 597 | Red Book and US |
| 130 | - | 951 | 832 | 739 | US |

### Banking (superelevation) and transition curves

**Banking limits:**
- e max is 10% on rural roads and freeways.
- Urban roads use 6%, or up to 8% on arterials between junctions.
- Mixed urban streets use 2-6%.
- Use no more than 8% where trucks climb steep hills.

**How fast the banking may change (max relative gradient, SANRAL):**

| Speed | Max relative gradient |
|---|---|
| 20 km/h | 0.8% |
| 40 km/h | 0.72% |
| 60 km/h | 0.64% |
| 80 km/h | 0.55% |
| 100 km/h | 0.47% |
| 120 km/h | 0.39% |
| 130 km/h | 0.35% |

**Minimum runoff length (Red Book):**

| Speed (km/h) | 40 | 60 | 80 | 100 | 120 |
|---|---|---|---|---|---|
| Runoff (m) | 35 | 40 | 50 | 60 | 70 |

**Rules:**
- Runoff length = lane width × lanes rotated × e × adjustment factor ÷ relative gradient.
- If there is no transition curve, put about 2/3 of the runoff on the straight and 1/3 on the curve.
- **Transition curve:** a clothoid (R = A²/L). All the banking change goes on the clothoid.
- A transition curve is only needed below these radii:

| Speed (km/h) | 60 | 80 | 100 | 120 |
|---|---|---|---|---|
| Radius (m) | 214 | 380 | 594 | 855 |

### Vertical curves and sight distance

**Formulas:**
- Stopping sight distance (SSD) = v(0.694 + 0.013v), where v is in km/h.
- Crest K = S² / [200(√h1 + √h2)²], with eye height 1.05 m and object height 0.6 m.
- Sag K = S² / (120 + 3.5S).

| Speed | SSD (m) | K crest | K sag | Min curve length (Red Book, m) |
|---|---|---|---|---|
| 40 | 49 | 4 | 8 | 80 |
| 60 | 88 | 12 | 18 | 100 |
| 80 | 139 | 30 | 32 | 140 |
| 100 | 199 | 61 | 49 | 180 |
| 120 | 270 | 113 | 69 | 220 |
| 130 | 310 | 148 | 80 | - |

**Rules:**
- A vertical curve is at least the design speed long, in metres.
- On freeways it is at least 2 × the design speed, and 400 m or more is preferred.
- Skip the curve if the grade changes by under 0.5%.

### Cross-section

**Lanes:**
- Lanes are 3.1, 3.4 or 3.7 m. Use 3.7 m for fast or divided roads.

**Shoulders:**
- 3.0 m on the outside of dual roads, 1.0 m on the inside.
- SANRAL says avoid paved shoulders between 1.5 and 2.5 m wide.

**Camber:**
- Sealed roads: 2%, or up to 3% in wet areas.
- Gravel shoulders: 4%.
- Gravel roads: about 4-6%. This is not confirmed; the TRH20 draft was unreachable.

### Junctions

**Angles:**
- Roads should meet at 75-120°. Below 60°, use a staggered junction.

**Approach slope:**
- On roads over 80 km/h, approach grades are 3% at most.
- On slow urban roads, 6% at most.

**Banking:**
- Don't put a junction on a curve with more than 6% banking.

**Kerb radii (Red Book Table 7.21):**

| Junction shape | Kerb radius |
|---|---|
| Simple curve | 10 m |
| Simple curve with tapers | 6 m |
| Three-centred curve | 6 m |
| Channelised turn | 15-25 m |

- The smallest car turning circle is 6.2 m.
- SUMO's default kerb radius is 4 m.

### Sources

- CSIR Red Book ch. 7: https://www.csir.co.za/sites/default/files/Documents/Chapter_07_Vol_II_Roads.pdf
- SANRAL Geometric Design Guide (text copy): https://idoc.tips/sanral-geometric-design-guide-3-pdf-free.html
- SANRAL tables quoted in a uMkhomazi 2018 report: https://www.dws.gov.za/iwrp/uMkhomazi/Documents/10492-20180718-uMWP-1%20RW%20Addendum%20-%20Appendix%20B1.pdf
- TRH26 (2012): https://wayleave.tshwane.gov.za/document/download/335975/
- RTMC Road Safety Manual Vol 7: https://www.rtmc.co.za/images/rtmc/docs/RoadSafetyEngineering/SA%20Road%20Safety%20Manual/SA%20Road%20Safety%20Manual/Volume%207/Chapter%206-9%20and%20cross%20reference.pdf
- Not reachable or not online: TMH16 Vol 2, the draft TRH20 (gravel roads), and TRH17. AASHTO f-values come from recall and should be checked against the Green Book.

## 2. OSM tag coverage (Garden Route cache)

**The data:**
- File: `%LOCALAPPDATA%\Geodrive\Cache\osm2_-34.1200_22.3300_-33.9000_23.5000.json` (54 MB).
- It holds 455k nodes and 59.9k ways.
- Coverage below is the share of road **length** that has the tag.
- There is **no `primary`** road in this area. The N2 is `trunk`, and the R roads are `secondary` and `tertiary`.

| Class | km | lanes | width | maxspeed | oneway | surface | incline | bridge | tunnel | layer | turn:lanes | placement |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| track | 1897 | 0% | 0% | 0% | 0% | 47% | 0% | 0% | 0% | 0% | 0% | 0% |
| residential | 1390 | 17% | 0% | 6% | 2% | 54% | 0% | 0% | 0% | 0% | 0% | 0% |
| unclassified | 743 | 11% | 0% | 8% | 3% | 65% | 0% | 0% | 0% | 0% | 0% | 0% |
| service | 495 | 1% | 0% | 1% | 5% | 31% | 0% | 0% | 1% | 1% | 0% | 0% |
| tertiary | 221 | 89% | 0% | 40% | 6% | 100% | 0% | 1% | 0% | 1% | 1% | 2% |
| trunk | 157 | 100% | 0% | 99% | 89% | 100% | 4% | 1% | 0% | 1% | 11% | 14% |
| secondary | 122 | 55% | 0% | 30% | 25% | 100% | 0% | 1% | 0% | 1% | 2% | 1% |
| motorway | 36 | 100% | 0% | 100% | 100% | 100% | 0% | 3% | 0% | 31% | 6% | 0% |
| motorway_link | 10 | 100% | 0% | 23% | 100% | 100% | 0% | 0% | 0% | 0% | 3% | 81% |
| trunk_link | 3 | 83% | 0% | 17% | 100% | 92% | 0% | 0% | 0% | 0% | 0% | 35% |

**Useful extras:**
- **Bridges:** 124 bridge ways, 6.0 km in total. **All of them have `layer`.** The longest is 291 m (motorway) and 201 m (trunk).
- **Tunnels:** 15, nearly all `building_passage` on service roads. There are no real road tunnels.
- **No `ele`** on any node. Only 2 `incline` tags. Heights must come from the DEM.
- **Trunk `shoulder` tag:** on 53% of trunk ways (`no` 307, `left` 182, `both` 20). It can set shoulders on the N2.
- **`smoothness`:** on most trunk and motorway ways (mostly `excellent`).
- **`tracktype`:** almost always `grade3` (mixed gravel and earth).
- **`junction=roundabout`:** 180 ways. There are 15,732 junction nodes.
- **`turn:lanes`:** 11% of trunk. Good enough for lane markings later, not for geometry.

**How well the shape is drawn** (spacing between OSM points, and the turn angle at each point):

| Class | Median spacing | Median turn | 95% of turns under | Turns over 20° |
|---|---|---|---|---|
| motorway | 13 m | 0.8° | 4° | 0% |
| trunk | 10 m | 2.2° | 10° | 0.4% |
| secondary | 28 m | 4° | 17° | 2% |
| tertiary | 20 m | 5° | 18° | 4% |
| residential | 12 m | 7° | 25° | 10% |
| track | 23 m | 11° | 46° | 28% |

What this means:
- Main roads are drawn densely and smoothly, so curve fitting will work well on them.
- Tracks are rough. Fit them loosely and let them follow the terrain.

## 3. Tool verdicts

| Tool | Verdict | Licence | Why |
|---|---|---|---|
| **SUMO netconvert** | Offline helper only (optional) | EPL-2.0 | Builds junction shapes. Its defaults are a 4 m corner radius, 10 m junction join, and a 10% max-grade check. It can read a GeoTIFF heightmap (`--heightmap.geotiff`, marked experimental). Its OpenDRIVE export uses only lines and paramPoly3, with **no arcs or clothoids**. Never link it into the game. **Not tried here:** this PC has no Python. |
| **Overture transportation** | Skip for now | ODbL | Mostly built from OSM. Extras: `road_flags` (bridge, tunnel), `level_rules`, `width_rules`, `speed_limits`. It has **no lanes field**. OSM already gives us bridges and layers. |
| **libOpenDRIVE** | Use only if we adopt OpenDRIVE | Apache-2.0 | Small C++ library. Reads .xodr files and builds road and lane meshes. Not needed while we build meshes ourselves. |
| **esmini RoadManager** | Borrow ideas | MPL-2.0 | Heavier than libOpenDRIVE. Prefer libOpenDRIVE. |
| **Clothoids** (Bertolazzi and Frego) | **Use** | BSD-2 | C++11. Fits clothoids through points, smoothly (G1 and G2), and handles biarcs. This is the core maths for horizontal curves. |
| **McCrae and Singh method** | Borrow the idea | Paper | Plot curvature against distance, fit straight lines to that plot, and you get line, arc and clothoid pieces. A HERE patent (US7912879) is close to this; check it before shipping. |
| **OSQP** | **Use** | Apache-2.0 | Sparse quadratic-programming solver. Does the height solve with hard limits on grade and grade change. |
| **CARLA OSM import** | Borrow lessons | MIT, plus SUMO under EPL | It is netconvert to OpenDRIVE. Lessons: roads cut at the area edge need proper ends, and dual carriageways need special handling. |
| **osm2xodr** | Skip | GPL-3.0 | GPL, and unmaintained. |
| **Valhalla elevation** | Skip | MIT | Only gives grades for routing, not road shapes. |
| **Vertical alignment papers** (e.g. arXiv 2508.15953) | Borrow ideas | Papers | The "least squares plus hard limits" model is what we need. |

Links:
- https://sumo.dlr.de/docs/netconvert.html
- https://sumo.dlr.de/docs/Networks/Export.html
- https://sumo.dlr.de/docs/Networks/Elevation.html
- https://docs.overturemaps.org/guides/transportation/
- https://github.com/pageldev/libOpenDRIVE
- https://github.com/esmini/esmini
- https://github.com/ebertolazzi/Clothoids
- https://www.dgp.toronto.edu/~karan/papers/sbim2008mccrae.pdf
- https://patents.google.com/patent/US7912879
- https://osqp.org
- https://arxiv.org/abs/2508.15953
- https://carla.readthedocs.io/en/latest/tuto_G_openstreetmap/

## 4. How Geodrive builds roads today

All of this is in `Plugins\GeoWorld`.

1. **`Parse\OsmData.cpp`** turns OSM ways into a road graph (`FRoadGraph`).
   - Roads are split where they share nodes.
   - Width comes from `lanes` and our SA class table.
   - `bridge` and `layer` are read.
2. **`Elevation\RoadHeightSolver.cpp`** solves all heights in one least-squares system.
   - Each point is pulled towards the DEM height.
   - A bending penalty smooths the slope. Its length depends on class: 70 m for trunk, 25 m for track.
   - Bridges get only a faint pull towards a straight line.
   - **There are no hard limits.** A steep DEM can still make a steep road.
3. **`Build\GeoMeshBuilder.cpp`** builds the road meshes.
   - Ribbons follow the raw OSM points with **mitred corners**.
   - Junctions are trimmed and filled as fans.
   - The cross-section is **flat**: no camber, no banking.
4. **`Build\RoadSurface.cpp`** gives every road vertex one shared height field, so road pieces that meet agree on their height.

**Where the known problems come from:**
- **Kinks** come from straight pieces with sharp corners between OSM points, plus junction fans meeting ribbons at an angle.
- **Bridge-end launches:**
  - The DEM pulls the ramp down just before the deck.
  - Nothing forces a sag or crest curve of the right size where the ramp meets the deck.
- **Scanner "holes"** are probably gaps where junction fans and ribbons don't match. Check this against the scanner output.

## 5. Recommended plan (step by step)

Each step can ship on its own. Measure every step with the robot driver and the scanner.

### Step 1. A design table per class (small)
- Add a `RoadDesign` struct in `Parse\` (or a new `Design\` folder).
  - Lookup: OSM class and `maxspeed` → design speed, e max, minimum radius, maximum grade, K crest, K sag, lane width, shoulder, camber.
  - Use the table in section 1.
- Use the `shoulder` tag on the N2.
- Pick the terrain type (flat, rolling or mountain) from the DEM slope around the road.

### Step 2. Join roads into long "strokes" (small)
- Where a graph node has just two roads, or a road carries straight on through a junction, treat them as one long road for fitting.
- This stops a kink appearing at every split.

### Step 3. Fit horizontal curves (medium)
- **Resample** each stroke every 2-5 m.
- **Fit a clothoid spline** with the Clothoids library (BSD-2). Keep it within a tolerance of the OSM line:

  | Class | Tolerance |
  |---|---|
  | motorway / trunk | 1.5 m |
  | minor roads | 2.5 m |
  | track | 3 m |

- **Respect the minimum radius** for the design speed. If the OSM curve is truly tighter (a pass), keep it and lower the local design speed.
- **Junction nodes stay fixed.**
- **Output:** dense points, each with its curvature. The mesh builder then makes smooth ribbons with no mitre corners.
- **Cheaper first try:** smoothing that limits curvature, without full clothoids. That alone may fix most kinks.

### Step 4. Hard limits in the height solve (medium)
- **Keep the current system:** same unknowns, same DEM pull, same junction coupling.
- **Add hard limits**, solved with OSQP, or with extra penalty passes in our existing conjugate-gradient solver:
  - Grade |g| ≤ max grade for the class and terrain.
  - The grade change per metre ≤ 1/(100·K), with crest K and sag K chosen by sign. This gives proper parabolic vertical curves.
  - The vertical curve is at least the minimum length.
- **Robust DEM pull:** use a Huber or L1 pull, so trees and buildings in the Copernicus surface model don't drag the road up.
- **Bridges:**
  - The deck is a straight grade or a single crest curve.
  - Ignore the DEM for one stopping-sight distance (about 50-200 m) before and after each bridge end, so the approach is a sag or crest curve sized by K.
  - The terrain is then raised or cut to meet the road: an embankment.
- **Junctions:** the minor road's last 20-40 m has a grade of 3% or less (6% at low speed), and it meets the main road's camber.

### Step 5. Camber and banking (medium)
- **Cross-section per point:**
  - Two-way roads get a 2% crown.
  - A one-way carriageway gets a single cross-fall away from the median.
- **On curves:** banking e from AASHTO Method 5, capped at e max, with the runoff length from the relative-gradient table.
- **Near junctions:** cap e at 6%, and blend to the junction plane.
- **`RoadSurface` change:** the shared field returns centreline height + lateral offset × cross slope, so every piece still agrees.
- **Force feedback:** this also gives the GT3 real camber feel.

### Step 6. Better junctions (medium)
- **Trim each road** back by kerb radius + half width.
- **Draw kerb corners as arcs.** This is SUMO's approach, with about 5 points per corner.
- **Kerb radius by class:**

  | Road type | Kerb radius |
  |---|---|
  | Residential | 6 m |
  | Normal junctions | 10 m |
  | Main-road slip turns | 15-25 m |

- The junction surface is a plane, or the main road's banked surface carried through.
- The minor roads bend to meet it, as `RoadSurface` already does.

### Step 7. Check against the rules (small)
- Extend the road scanner to flag every point that breaks the design table: grade, K, radius, and banking change.
- Run the robot test before and after each step.

### Optional later
- Run netconvert offline once on a 2 x 2 km piece (needs Python or the SUMO installer). Compare its junction shapes with ours.
- Use Overture only if OSM widths turn out to be missing somewhere that matters.
- Use libOpenDRIVE only if we ever save roads as `.xodr` (for example for AI traffic or an offline world baker).

## What was not done
- **netconvert was not run.** This PC has no Python, so the pip route was not possible.
- **The AASHTO side-friction values** (above 100 km/h) come from recall. Check them against the Green Book.
- **The SANRAL radius tables for 110-130 km/h** were not readable (they are images). The values shown use the Red Book or AASHTO instead.
