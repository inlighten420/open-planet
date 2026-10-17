# Road rules: United Kingdom (England) for our algorithm

Date: 2026-10-12. Scope: what to paint and how wide to build, for roads in England. This is the UK version of [ROAD_MARKINGS.md](ROAD_MARKINGS.md) (markings) and of the cross-section part of [ROAD_RESEARCH.md](ROAD_RESEARCH.md).

## Read this first

- **The rules come from these documents.** All are free online.
  - **[TSRGD]** The Traffic Signs Regulations and General Directions 2016 (SI 2016/362). This is the law. It gives each marking a **diagram number** (for example 1008). In the manuals the reference looks like "S11-4-6", which means Schedule 11, Part 4, item 6.
    https://www.legislation.gov.uk/uksi/2016/362/contents
  - **[TSM5]** Traffic Signs Manual, Chapter 5, "Road Markings" (DfT, 2018; first published 2019). This has the guidance: which line, where, and how big. **Most numbers below come from here.**
    https://assets.publishing.service.gov.uk/media/5c4ace6ded915d38a0611abc/traffic-signs-manual-chapter-05.pdf
  - **[TSM3]** Traffic Signs Manual, Chapter 3, "Regulatory Signs" (2019). This covers STOP and GIVE WAY lines and yellow lines.
    https://assets.publishing.service.gov.uk/media/5c78f895e5274a0ebfec719b/traffic-signs-manual-chapter-03.pdf
  - **[TSM6]** Traffic Signs Manual, Chapter 6, "Traffic Control" (2019). This covers signal stop lines and zebra crossings.
    https://assets.publishing.service.gov.uk/media/5df0e29fed915d15f42c4820/dft-traffic-signs-manual-chapter-6.pdf
  - **[CD127]** DMRB CD 127 "Cross-sections and headrooms" (v1.0.1, 2021). This has lane, hard strip and hard shoulder widths for motorways and trunk roads.
    https://www.standardsforhighways.co.uk/tses/attachments/10442706-b592-42c8-85f8-2a0c779a8e37
  - **[CD109]** DMRB CD 109 "Highway link design" (Revision 1, 2020). This has design speeds and sight distances.
    https://www.milton-keynes.gov.uk/sites/default/files/2022-04/CD13j%20Highway%20link%20design.PDF (council-hosted copy of the standard)
  - **[MfS]** Manual for Streets (DfT, 2007). This covers residential streets.
    https://assets.publishing.service.gov.uk/media/5a7e0035ed915d74e6223743/pdfmanforstreets.pdf
  - **[HC]** The Highway Code. This gives what the markings mean to drivers. Rules 124 (speed limits), 127–132 (lines and studs), 184–190 (roundabouts), 238–247 (waiting and parking).
    https://www.gov.uk/guidance/the-highway-code
  - **[OSM]** OpenStreetMap wiki: Key:maxspeed, Key:maxspeed:type, Roads in the United Kingdom.
- **Units:**
  - Widths are in **mm**.
  - Lengths and gaps are in **m**.
  - **Speed limits are in mph.** Road widths are metric.
  - 30 mph = 48 km/h, 40 = 64, 50 = 80, 60 = 97, 70 = 113.
- **"Speed limit" vs "85th percentile speed":** many tables are keyed on the **speed limit**. Others use the **85th percentile speed** (the speed that 85% of cars stay under). The no-overtaking rules use the 85th percentile speed. We do not have measured speeds. So we use the limit as a stand-in (see section 2).
- **What "unverified" means here:** I could not read that value in a primary source. Often it was only in a figure I could not read.

## 1. Centre lines, warning lines and lane lines

### 1a. The patterns

The UK uses **two module sizes** (one mark plus one gap):
- the **6 m module** at **40 mph or less**;
- the **9 m module** above **40 mph**.

[TSM5] 2.1, 2.2.1, Table 7-3.

| Diagram | Name | Mark (m) | Gap (m) | Module (m) | Width (mm) | When used | Source |
|---|---|---|---|---|---|---|---|
| **1008** | Centre line | **2** | **4** | 6 | 100 (150 on roads 10 m or more wide, and on 3- or 4-lane roads) | Single carriageway, limit 40 mph or less. It may also be used above 40 mph where real speeds are under 40 mph. | [TSM5] Fig 2-1, Table 2-1 |
| **1008.1** | Centre line | **3** | **6** | 9 | 100 (150 on roads 10 m or more wide, and on 3- or 4-lane roads) | Single carriageway, limit over 40 mph. | [TSM5] Fig 2-1, Table 2-1 |
| **1004** | Hazard warning line | **4** | **2** | 6 | 100 or 150 | It replaces the centre line at hazards, 40 mph or less. | [TSM5] Fig 2-4, 2.7.1, Table 7-3 |
| **1004.1** | Hazard warning line | **6** | **3** | 9 | 100 or 150 | The same, over 40 mph. | [TSM5] Fig 2-4, 2.7.1, Table 7-3 |
| **1005** | Lane line | **1** | **5** | 6 | 100 (150 optional) | Lanes going the same way, 40 mph or less. | [TSM5] Fig 2-3, Table 2-2, Table 7-3 |
| **1005.1** | Lane line | **2** | **7** | 9 | 100 or 150. **Use 150 on 70 mph dual carriageways and motorways.** | Lanes going the same way, over 40 mph. | [TSM5] 2.3.5, Table 2-2, Table 7-3 |

- **The warning line is the centre line flipped.** It has long marks and short gaps, where the centre line has short marks and long gaps. Keep the same module so the change looks continuous ([TSM5] 2.2.3).
- **Rules for the centre line ([TSM5] 2.2.2, 2.2.6, 2.5.9):**
  - Use it on single carriageways only.
  - **Leave it out on roads narrower than 5.5 m.** Use edge lines instead, if any.
  - On a road with one lane each way, 100 mm is normally enough.
  - The centre line must never be narrower than a lane line.
- **Lane lines are 100 mm, with one exception.** On 70 mph dual carriageways and motorways, use 150 mm ([TSM5] 2.3.5; Fig 2-18, 2-19 show "0.10 / 0.15").
- **Lane lines become warning lines before roundabouts and signals.** On the approach to a roundabout or a signal junction, the lane lines change to 1004 or 1004.1 ([TSM5] 2.3.3, 2.4.13).
  - The exception is at grade-separated junctions. There, lane lines carry on through the junction ([TSM5] 7.2.5).

### 1b. Where the warning line replaces the centre line

The warning line (1004 or 1004.1) is used ([TSM5] 2.4.1, 2.4.5):
- at bends and crests, where forward visibility is less than **W** in Table 1;
- on the approach to junctions, refuges, roundabouts and other hazards;
- as the centre line on **4-lane** single carriageways, 150 mm wide (or use double white lines; see section 2).

**Table 1: visibility distances ([TSM5] Table 2-4 = Table 3-2).**
- Measure from an eye at **1.05 m** to an object at **1.05 m**, both above the centre of the carriageway. The sight line can be curved by vertical and horizontal curves ([TSM5] 3.8.1).
- **Do not interpolate.** Use the row the speed falls in. For example, at 45 mph, V = 120 and W = 195.

| 85th percentile speed (mph) | Up to 30 | 31–40 | 41–50 | 51–60 | Over 60 |
|---|---|---|---|---|---|
| **V**, desirable minimum visibility (m). Below this, use double white lines. | 75 | 95 | 120 | 150 | 175 |
| **W**, warning line visibility (m). Below this, use a warning line. | 115 | 160 | 195 | 240 | 275 |

**Minimum number of warning-line marks on each approach ([TSM5] Table 2-3, Table 5-1):**

| Where | Diagram, width | 30 mph | 40 mph | Over 40 mph |
|---|---|---|---|---|
| Centre of a 2-lane road under 10 m wide | 1004 (100) or 1004.1 (100) | 5 | 7 | 7 |
| Centre of a 2-lane road 10 m or more wide | 1004 (150) or 1004.1 (150) | 5 | 7 | 7 |
| Centre of a 4- or 6-lane single carriageway | 1004 (150) or 1004.1 (150) | 7 | 10 | 10 |
| Lane line before a roundabout or signals, and on the minor road at a priority junction | 1004 (100) at 40 mph or less; 1004.1 at 100 / 100 / 150 for 50 / 60 / 70 mph | 5 | 7 | 7 |

- **Main road at a junction:** use warning lines when the side road sends about **100 vehicles per hour or more**, or when the junction is hard to see ([TSM5] 2.4.9).
  - Sim rule: use them where the side road is tertiary or above, or where the junction is on a bend or crest.
- **Speed-limit changes:** when the limit drops from 60 to 30 or 40, put 1004.1 on the approach and 1004 inside the lower limit. If 1004 would give 7 marks or fewer, keep 1004.1 instead ([TSM5] 2.2.3–2.2.4, Fig 2-2).
- **Two hazards close together:** put a short piece of normal centre line between the two warning lines ([TSM5] 2.4.14).
- **Overuse:** don't use warning lines where they are not needed. Take particular care in towns ([TSM5] 2.4.4).

### 1c. Road studs ("cat's eyes") ([TSM5] 4.4–4.5, Tables 2-1, 2-2, 2-5; [HC] 132)

| Colour | Where | Meaning |
|---|---|---|
| White | Centre lines, lane lines, warning lines, double white lines | Guidance |
| **Red** | **Left** edge of the carriageway | Do not cross |
| **Amber** | **Right** edge, next to the central reservation, and the offside of one-way roads and slip roads | Do not cross |
| **Green** | Across lay-bys and slip roads, at 1010 lines | You may cross here |

- **Spacing:**
  - Centre lines: **12 m** at 6 m modules and **18 m** at 9 m modules. The 150 mm lines get 6 m and 9 m.
  - Lane lines: 12 m and 18 m.
  - Edge lines: 18 m.
  - Double white lines: 3–4.5 m (**4.5 m** recommended).
  - Use **9 m** on bends under 450 m radius, and where fog or glare is a problem.
  - Green studs on 1010 lines: 8 m.
- **Studs are always used on motorways.** They are recommended on fast all-purpose roads ([TSM5] 4.1.3).

## 2. Double white lines (no overtaking)

### 2a. The marking ([TSM5] 3.1–3.3; [TSRGD] diagram 1013.1, S9-6-23)

| Item | Value |
|---|---|
| Lines | One solid (prohibitory) line plus either a second solid line or a broken (permissive) line |
| Line width | 100 mm minimum; **150 mm normally** |
| Spacing | **175 mm** apart normally (90 mm minimum). The total width must not exceed **900 mm** (version A). |
| Broken line | **1 m marks, 5 m gaps** ([TSM5] 3.2.1 text; the figure dimensions 1000 and 5000 agree). **Check this against photos.** It is short marks with long gaps, unlike the warning line. |
| Version B (wide) | Up to **1200 mm** overall, hatched between the solid lines. The hatch marks are as wide as the lines and at most 3 m apart. Open the lines out at a taper no sharper than 1 in 50. Use this on sharp bends and crests, on 2+1 roads and at climbing lanes ([TSM5] 3.2.1, 3.5). |
| Studs | Required. One row of **white** bi-directional studs between the lines at 3–4.5 m ([TSM5] 3.3.1). |
| Material | Always reflectorised. Never raised-rib ([TSM5] 3.2.2). |
| Position | Either centred on the road centre, or with the solid line on the centre. On narrow roads, use the second layout to give more width to the side with the solid line ([TSM5] 3.2.2). |

- **Meaning ([HC] 128–129; [TSRGD] S9-7):**
  - **Solid line nearest you:** you must not cross or straddle it. The exceptions are entering a side road or property, and passing a stationary vehicle, a cyclist, horse or road maintenance vehicle doing 10 mph or less.
  - **Broken line nearest you:** you may cross it when safe.
  - **No stopping either way:** you must not stop on a road with double white lines, even where the broken line is on your side.
    - This does **not** apply on roads with 2 or more lanes each way ([TSM5] 3.1.1).
- **Where not to use them:**
  - Not where the carriageway is **under 6.1 m** wide. Use a warning line instead ([TSM5] 3.1.4).
  - Not normally in built-up areas ([TSM5] 3.1.8).
- **4-lane single carriageways:** these may have double solid lines whatever the visibility ([TSM5] 3.14).

### 2b. The design procedure ([TSM5] 3.9.1, Table 3-2)

This is the UK equivalent of our SA RM1 sweep. It works **per direction** and uses V and W from Table 1.

1. Find the 85th percentile speed for each direction. From it, get V and W.
2. **Mark the zones.** In one direction, mark where visibility first drops below V and where it is regained. Each pair becomes a solid-line segment.
   - Drop a segment if it is **shorter than V/4** and **at least V away** from the segments before and after it. Keep it only with a very good reason.
3. **Exit of a left-hand bend.** Extend the solid line until visibility W is reached, or until the road stops curving left, whichever is shorter.
   - Left-hand bends are the ones where the sight line crosses the opposing lane, because we drive on the left.
4. **Close short gaps.** Close gaps **shorter than V** between solid segments.
5. Repeat steps 2–4 for the other direction.
6. **Pair the lines.** Where only one direction has a solid line, add a broken line beside it, on the side of the direction that can see.
7. **Join systems.** If two double-white-line systems are **less than V/2 apart**, extend them, without changing the pattern, to meet in the gap.
8. **Fill short overtaking gaps.** After joining, replace any broken element **shorter than V** inside a system with a solid line.
9. **Warning-line gaps.** If the gap between systems is more than V/2 but less than W, lay a warning line in it.
10. **Optional:** start the inbound solid line earlier if the overlap would confuse drivers.

**What this gives ([TSM5] 3.9.2):**
- An overtaking stretch is at least **V** long.
- A broken element inside a system is at least **V** long.
- A warning line between systems is at least **V/2** long.

**Speed:** use measured speeds, not the limit ([TSM5] 3.9.1).
- Sim rule: 85th percentile = the limit for 60 mph roads.
- On curves, use the lower of the limit and the speed the curve allows. Our design-speed code already works this out.

**Not covered by the manual:** sight distance is measured from the centre of the carriageway. There is **no margin for the verge** like the SA "shoulder break" rule. In practice, hedges and walls block the view. Our `SightMargin` (1.5 m past the edge) is a reasonable stand-in for a hedge line. That is a Geodrive choice, not a rule (**unverified**).

### 2c. Warning arrows before a solid line ([TSM5] 3.4, Table 3-1)

- Put deflection arrows (diagram **1014**) in the centre line before any solid line starts on the driver's side.
  - Use at least 1. Normally use 2. Add a third at crests.
  - Each arrow replaces one centre-line mark.
- Inside a double-white-line system, put the arrows about 500 mm to the right of the solid line.
- **No arrows are needed:**
  - where the solid line restarts after a junction or refuge;
  - where it starts just after the end of a central reservation;
  - where it starts just after a roundabout ([TSM5] 3.4.4).

| Speed limit (mph) | 30 | 40 | 50 | 60 |
|---|---|---|---|---|
| Arrow length (m) | 4.5 | 4.5 | 6 | 6 |
| 1st arrow tip, before the start of the solid line (m) | 13.75 | 19.75 | 21.00 | 30.00 |
| 2nd arrow (m) | 43.75 | 55.75 | 66.00 | 84.00 |
| 3rd arrow, if needed (m) | 79.75 | 109.75 | 138.00 | 165.00 |

- The arrows sit about 1, 3 and 6 seconds of travel before the line. They are moved to fall midway between warning-line marks ([TSM5] 3.4.3).
- Before hatching (Table 2-6), the same distances are used, keyed by 85th percentile speed (up to 30 / 31–40 / 41–50 / 51–60 mph). A fifth row covers **61–70 mph: 9 m arrow at 37.5 / 91.5 / 190.5 m**.

### 2d. Hatched markings ([TSM5] 2.7–2.11)

| Diagram | Name | Border | Inside | When used |
|---|---|---|---|---|
| **1040** | Hatched area in the centre of a two-way road | Broken: 4 m mark / 2 m gap at 40 mph or less; 6 m / 3 m above 40 mph. Width 100 or 150. | White **diagonal** stripes | Ghost islands, before refuges, and to separate opposing traffic at bends or crests. Unless it starts at a physical island, it must start with a tapered end. [HC] 130: do not enter unless it is necessary and safe. |
| 1040 with a solid border (permitted variant, S11-5-47) | Hatched area next to double white lines | One border is diagram 1013.1 | White diagonals | Splayed double white lines wider than 1200 mm, and right-turn lanes protected by double white lines ([TSM5] 3.5.1, 5.3.12). [HC] 130: solid border, do not enter except in an emergency. |
| **1040.3** | Loss of a lane (motorway or dual carriageway) | 200 mm solid (150 on all-purpose roads without a hard shoulder) | Diagonals | Always on the **right-hand** side. Preceded by 3 deflection arrows. Amber studs at 9 m. |
| **1040.4** | Area at the edge of the carriageway | Broken | Diagonals | Buildouts and traffic calming. Can be reversed. |
| **1040.5** | End of a hard shoulder | 200 mm | Diagonals | Taper normally 1 in 15. |
| **1041** | Chevrons, same direction on both sides | Broken | Chevrons pointing **towards** oncoming traffic | Before a refuge in a one-way street, and between traffic streams going the same way. |
| **1042** | Chevrons, **must not enter** | Solid, 200 mm (150 at roundabouts and on all-purpose duals without hard shoulders) | Chevrons | Gores between the main carriageway and slip roads, and segregated left-turn lanes. Red studs at 3 m. |

- **Colours:** all hatching is **white**. The SA RM5 islands have yellow bars; the UK has none ([TSM5] 1.8, 12.5).
- **Tapers before hatching ([TSM5] Table 2-6):**

| 85th percentile speed (mph) | Up to 30 | 31–40 | 41–50 | 51–60 | 61–70 |
|---|---|---|---|---|---|
| Taper for 1040, 1040.3 and 1040.4 | 1 in 40 | 1 in 40 | 1 in 45 | 1 in 50 | 1 in 55 |

- **Tapers before refuges ([TSM5] Table 2-7):**

| 85th percentile speed (mph) | Up to 30 | 31–40 | 41–50 | 51–60 |
|---|---|---|---|---|
| Preferred | 1 in 40 | 1 in 40 | 1 in 45 | 1 in 50 |
| Absolute minimum | 1 in 20 | 1 in 30 | 1 in 40 | 1 in 50 |

- **Stripe sizes:**
  - Stripes are at **45°**. Figure 2-8 shows dimensions "800 min, 8000 max" and widths "100 / 150 / 200".
  - The stripe width and spacing are linked to the border module. The longer border gets the closer and wider stripes ([TSM5] 2.7.1, Fig 2-8).
  - **Unverified:** the exact stripe spacing for each module. The figure could not be read in full.
- **Kerb clearance:** a warning line or hatching that ends at a refuge stops **300 mm** from the island edge. This may drop to 150 mm at 40 mph or less ([TSM5] 2.8.2).

## 3. Edge lines

**Colour: white.** UK edge lines are **white** on both sides. Yellow is only for waiting and loading restrictions (section 6) and yellow bar markings ([TSM5] 1.8, 12.5; [TSRGD] diagrams 1009A, 1010, 1012.1–1012.3). **This is the biggest visible change from SA**, where the left edge line is yellow.

| Diagram | Name | Pattern | Width (mm) by speed limit: 40 or less / 50–60 / 70 | Studs | When used | Source |
|---|---|---|---|---|---|---|
| **1012.1** | Edge line | Solid | All-purpose road **without** a hard strip: **100 / 100 / 150**. **With** a hard strip or hard shoulder: **150 / 150 / 200**. Motorway: **200**. | Red on the left, amber on the right, 18 m. Not normally on roads without a hard strip. | Unlit classified roads, roads without clear kerbs, all roads with hard strips or hard shoulders, all motorways. Recommended on the **offside** of unlit dual carriageways. | [TSM5] 2.5.6, 2.5.11, Table 2-5 |
| **1012.2** | Raised-rib edge line, motorway | Solid with ribs up to 11 mm high. Rib spacing **500 mm** (250 mm on slip roads). | 200 | 18 m | **Required** on both edges of motorway mainline and slip roads ([CD127] 2.23). | [TSM5] 2.6 |
| **1012.3** | Raised-rib edge line, all-purpose road | Ribs up to 8 mm high, at 500 mm | 150, or **200 with a hard shoulder** | 18 m | All-purpose roads with a hard strip or hard shoulder (optional). | [TSM5] 2.6.2 |
| **1010** | Broken edge line | **1 m mark, 1 m gap** | 100 / 150 / 200 | **Green**, 8 m | Across lay-bys, private accesses, merges and diverges (slip roads), and lane drops. **Not** at priority junctions; those get a give-way line. | [TSM5] 2.5.4, Table 2-5, Table 7-3 |
| **1009A** | Short broken edge line | **600 mm mark, 300 mm gap** | 100 (match the edge line if it is 150 or 200) | – | Across the mouth of a side road, next to the give-way or stop line. Also across field gates and narrow drives. | [TSM5] 2.5.3, Fig 2-6; [TSM3] 2.3.2, 3.2.3 |

- **Position:** about **225 mm** in from the nearside edge of the carriageway ([TSM5] 2.5.8). On the offside of a dual carriageway, about 225 mm from the central reservation ([TSM5] 2.5.11).
  - Where there is a hard strip, the line sits between the lane and the hard strip ([TSM5] Fig 2-18; [CD127] 2.3: lane width is measured to the traffic side of the edge line).
- **Back of a hard shoulder:** a 150 mm line, 50 mm from the back edge ([TSM5] 2.5.10(g)).
- **Don't run a solid edge line across side roads, lay-bys or median gaps.** Use 1009A or 1010 there ([TSM5] 2.5.8).
- **Roads under 5.5 m wide:** no centre line, but edge lines may be used ([TSM5] 2.5.9).
- **Kerbed urban streets:** these normally have **no** edge line. The kerb does the job (implied by [TSM5] 2.5.6, "not having clearly defined raised kerbs").
- **Raised ribs:** stop them for **1 m** either side of crossing points. On bends under 1000 m radius, they may be left out ([TSM5] 2.6.7–2.6.9).

## 4. Multi-lane roads and motorways

### 4a. Rural motorway, per carriageway, left to right ([TSM5] Fig 2-19; [CD127] Fig 2.1.1N1a)

| Part | Width | Marking |
|---|---|---|
| Hard shoulder | **3.30 m** | 150 mm line at the back (optional) |
| Nearside edge line | 200 mm | 1012.2 raised rib, red studs, 18 m |
| Lanes (D2M / D3M / D4M) | 3.65 + 3.65 = 7.30 / 3.65 + **3.70** + 3.65 = 11.00 / 3.65 + 3.70 + 3.70 + 3.65 = 14.70 | 1005.1 lane lines, 2 m / 7 m, 100–150 mm (150 recommended), white studs, 18 m |
| Offside edge line | 200 mm | 1012.2, amber studs, 18 m |
| Offside hard strip | **0.70 m** (max 1.00) | – |
| Central reserve | **3.10 m** | Barrier |

**Urban motorway ([CD127] Fig 2.1.1N1c):**
- Hard shoulder **2.75 m**.
- Same lanes as rural.
- Offside hard strip 0.70 m.
- Central reserve **1.60 m** (D2UM) or **2.10 m** (D3UM, D4UM).

**Limits ([CD127] 2.14–2.15):**
- No more than **4** mainline lanes on a motorway.
- No more than **3** on an all-purpose dual carriageway.
- Auxiliary lanes are as wide as the nearside lane ([CD127] 2.16).

**Smart motorways:** the hard shoulder is a running lane.
- Use 1012.1, not 1012.2, between the lanes and the hard shoulder.
- Red studs.
- Emergency areas are bordered by 1010 ([TSM5] 2.5.4, 2.6.3). The sim can ignore this.

### 4b. Merges, diverges and lane drops ([TSM5] 7.2–7.4, Table 7-3)

- **The gore (nose):** 1042 chevrons, with red studs at 3 m on both edges.
- **Across the slip road:** the main-carriageway edge line becomes **1010** (1 m / 1 m), with green studs at 8 m.
  - The red studs on the slip road's nearside close up from 18 m to 9 m.
- **Through the junction:** the lane lines continue as normal 1005.1. They do **not** become warning lines.
- **Lane drop:** 1010 between the main carriageway and the lane that leaves ([TSM5] Table 2-5).
- **Loss of the right-hand lane:** 1040.3, after 3 deflection arrows.
- **Lane-width tapers ([CD127] Table 2.28):**

| Design speed (km/h) | 50 | 60 | 70 | 85 | 100 | 120 |
|---|---|---|---|---|---|---|
| Minimum taper | 1:25 | 1:30 | 1:35 | 1:45 | 1:50 | 1:55 |

### 4c. End of a dual carriageway ([TSM5] 2.10)

1. Hatch off the **outside** (right) lane, using the Table 2-6 taper.
2. Then taper to the single carriageway's centre line, using 1040 plus 1040.4.
3. Leave out the lane line over the taper.
4. Put 2 deflection arrows in the lane being lost.
5. Before the taper, the lane line becomes a warning line.

## 5. Junctions

### 5a. Which road gets what

| Control | Minor road | Main road | Source |
|---|---|---|---|
| **Give way** (default) | Give-way line **1003A**. Triangle **1023A** optional, required with the upright GIVE WAY sign (602). Warning line (1004 or 1004.1) in the centre, at least 5 or 7 marks. | Warning line in place of the centre line, if the side road is busy or the junction is hidden (section 1b). 1009A across the mouth. | [TSM3] 3.1–3.3; [TSM5] 5.2, Table 5-1 |
| **Stop** (rare) | Stop line **1002.1** + word **STOP** (1022) + upright STOP sign (601.1). These must always go together. | The same as for give way. | [TSM3] 2.1–2.3 |
| **Traffic signals** | Stop line **1001** on every approach | – | [TSM6] 4.2 |
| **None (minor residential)** | Give-way markings are not normally used at private accesses, or on minor residential roads with low speed and flow and good visibility. | – | [TSM3] 3.1.6 |

- **Never mark give-way lines on every approach** to a junction ([TSM3] 3.1.4; TSRGD Sch 9 General Direction 3). Give priority to the road with the most traffic.
- **STOP is only for very poor visibility ([TSM3] Table 2-1).** Consider it when the view from **3 m** back along the side road's centre line (4.5 m if it carries through traffic), at **1.05 m** eye height, is less than:

| Main-road 85th percentile speed (mph) | 20 | 30 | 40 | 50 | 60 | 70 |
|---|---|---|---|---|---|---|
| Visibility along the main road (m) | 15 | 30 | 45 | 70 | 90 | 120 |

  Interpolate between columns ([TSM3] 2.2.3). Otherwise use give way.
- **Sim rule:** use OSM `highway=stop` or `highway=give_way` where tagged. Otherwise give the lower road class a give-way line. UK STOP signs are rare, so don't infer STOP.

### 5b. The transverse lines

| Diagram | Name | Pattern | Width | Position | Source |
|---|---|---|---|---|---|
| **1003A** (TSRGD S9-6-3; older drawings call it 1003) | Give-way line at a junction | **Two** broken lines. **600 mm mark, 300 mm gap.** | **200 mm** each, **300 mm** apart | The edge nearest the main road follows the main road's edge, or its edge line, even at skew junctions. It is never set back. On a two-way side road it runs to the centre; the rest is 1009A. **If that would make it shorter than 2.75 m, run it across the full width** and drop the centre line. Where the main road has a hard strip, line it up with the back of the hard strip. | [TSM3] 3.2.2–3.2.6, Fig 3-5; council drawing schedules for the dimensions |
| **1023A** | Give-way triangle | Elongated triangle | – | Leading edge **2.1–2.75 m** before the line (up to 15 m). Centred in each approach lane. | [TSM3] 3.2.5 |
| **1002.1** | Stop line at a junction | One solid line | **300 mm** (read from Fig 2-5; **unverified** in the text) | Positioned like the give-way line. The word STOP goes 2.1–2.75 m before it (up to 15 m). STOP is **1600 mm** tall up to 40 mph and **2800 mm** above. | [TSM3] 2.3, Table 2-2 |
| **1001** | Stop line at signals | One solid line | **200 mm** urban; **300 mm** rural or above 35 mph | Square to the road. At least 1.5 m before the primary signal (2.5 m preferred). | [TSM6] 4.2 |
| **1003.1** | Give-way line at a roundabout | One broken line (**pattern unverified**; probably the same 600 / 300) | **200 mm**; **300 mm** where the approach uses 1004.1 or 6/3 hatching | Across the entry, following the circle. | [TSM5] 6.2.2 |
| **1003.3** | Give-way line at a mini-roundabout | One broken line (**dimensions unverified**) | – | With the upright sign 611.1 about 1.5 m before it. | [TSM5] 6.3.6 |

- **Dual carriageways:** do not use give-way lines where traffic joins a fast dual carriageway from a slip road or a merge taper. Use **1010** there ([TSM3] 3.2.7).

### 5c. Ghost islands (right-turn lanes) ([TSM5] 5.3)

- **The island:** 1040 hatching, symmetric about the road centre line, at the TD 42 / CD 123 tapers.
- **Widths:**
  - Through lane: **3.0 to 3.65 m**.
  - Turning lane: **3.5 m** desirable. It may be 3.0 m, and up to 5 m in town.
  - On rural roads over 50 mph, keep the turning lane at 3.65 m or less.
  - Minimum island width: 2.5 m.
- **Turning length:** **10 m** from the side road's centre line, plus queue length.
- **Lines:**
  - The line between the through lane and the turn lane is a warning line (1004 or 1004.1).
  - Put 2 deflection arrows (1014) on each approach when speeds are 40 mph or more.
  - Put a bifurcation arrow (1039) at the start of the turn lane: **8 m** with 1004, **16 m** with 1004.1, **32 m** at 70 mph.
  - Turn arrows (1038): 4 m with 1004, 6 m with 1004.1, 9 m at 70 mph.
- **Staggered junctions:** right/left staggers at least **50 m** apart.
- **Dual carriageway junctions:** the turn lane is separated by **1010** (100 / 150 / 200 mm at 40 or less / 50–60 / 70 mph). The lane line through the junction becomes a warning line ([TSM5] 5.4).
- **Single-lane dual:** the through lane is **4.0 m**, plus 1.0 m hard strips ([TSM5] 5.5.1).

### 5d. Roundabouts ([TSM5] section 6; [HC] 184–190)

- **Circulation:** **clockwise**. **Give way to traffic from the right** at the entry. This is **the same as SA**: both drive on the left.
- **Every entry gets a give-way line**, except at full-time signals. On approach roads, centre lines and lane lines become **warning lines** ([TSM5] 6.1.2–6.1.3).
- **Flared entries:**
  - Lanes are at least **2 m** wide at the start of the taper.
  - Lanes are at least **3 m** wide at the give-way line.
- **Splitter islands:** physical islands, or 1040 hatching ahead of them.
- **Circulating lane markings** (optional; always used with signals): concentric, partial concentric, concentric-spiral or spiral. Use 1004 / 1004.1 and 1040.4. Lane arrows and destinations may be added ([TSM5] 6.6–6.7).
- **Yellow bar markings (1067):** 90 transverse yellow bars, 600 mm wide, on fast dual-carriageway approaches with a crash record. The first bar is 50 m before the give-way line ([TSM5] 6.10). **Sim: optional decoration, at most.**
- **Mini-roundabouts ([TSM5] 6.3; [TSRGD] diagram 1003.4, S9-6-5):**
  - **A white painted disc** with three clockwise arrows around it. No kerbs and no street furniture. Vehicles may drive over it.
  - Use only where **all** approaches are **30 mph or less**.
  - Not on new junctions or dual carriageways.
  - Sizes:

| Size | Disc diameter (mm) | Arrow-circle diameter (mm) | Arrow length (mm) |
|---|---|---|---|
| 1 | 1000–2400 | 3500–7000 | 3025 |
| 2 | 2500–4000 | 5000–10000 | 4450 |

  - The disc may be domed up to **125 mm** high, and no more than 6 mm at the rim. It is white and reflective.
  - **No rings** may be added round the disc.
  - Give-way line: 1003.3, with the blue sign 611.1. Triangle 1023A is optional.
  - OSM: `highway=mini_roundabout` on a node.

### 5e. Pedestrian crossings ([TSM6] 15.8, 16.2)

- **Zebra (4007):**
  - Black and white stripes across the full width, **2.4–10 m** long in the direction of travel.
  - White stripes are **500–715 mm** wide. Black and white are equal.
  - The black stripe at the kerb is up to 1.3 m.
  - Give-way line: one broken line, **500 mm mark, 500 mm gap, 200 mm wide**, **1.1–3 m** before the stripes.
- **Zig-zags (1001.3) at all controlled crossings:**
  - The standard is **8 marks of 2 m** each side, with a terminal line.
  - They are 250 mm from the kerb.
  - The minimum is 2 marks.
  - They replace the centre line and edge lines there.
  - They ban parking and overtaking.

## 6. Yellow lines (parking; the sim may ignore them)

They are not edge lines. They control waiting along the kerb ([TSM3] 13.4; [HC] 238–247).

| Diagram | Marking | Meaning |
|---|---|---|
| **1018.1** | Double yellow line | No waiting at any time |
| **1017** | Single yellow line | No waiting during times shown on signs |
| 1019 / 1020 | Yellow kerb marks (one or two short dashes on the kerb) | No loading |
| Red lines (red routes) | Single or double red | No stopping |
| 1025 | Yellow zig-zag | School keep-clear zone |
| 1043 / 1044 | Yellow box junction (crossed diagonals) | Don't enter unless your exit is clear |

- **Width:**
  - **75 mm** at 40 mph or less.
  - **100 mm** above 40 mph.
  - 50 mm in sensitive areas.
- **Double lines:** the gap between them equals the line width.
- **Position:** about 250 mm from the kerb ([TSM3] 13.4.4–13.4.5).
- **OSM:** `parking:lane:*` / `parking:*:restriction=no_parking` (rarely tagged).

## 7. Cross-sections in England

### 7a. From DMRB (motorways and trunk roads only) ([CD127] Figs 2.1.1N1a–h; [TSM5] Fig 2-18)

All widths are in metres. "Lane" widths are measured from the traffic side of the edge line to the centre of the lane line ([CD127] 2.3).

| Road type | Lanes per direction | Lane widths | Nearside hard shoulder or strip | Offside hard strip | Central reserve | Verge |
|---|---|---|---|---|---|---|
| Rural motorway D2M / D3M / D4M | 2 / 3 / 4 | 3.65 (middle lanes 3.70) | **3.30 hard shoulder** | **0.70** (max 1.00) | 3.10 | 1.50 |
| Urban motorway D2UM / D3UM / D4UM | 2 / 3 / 4 | 3.65 / 3.70 | **2.75 hard shoulder** | 0.70 | 1.60 / 2.10 / 2.10 | varies |
| Rural all-purpose dual D2AP / D3AP | 2 / 3 | 3.65 (middle 3.70) | **1.00 hard strip** | **1.00** | 2.50 | 2.50 |
| Urban all-purpose dual D2UAP / D3UAP | 2 / 3 | 3.65 / 3.70 | none (kerb) | none | 1.80 | varies |
| Rural single S2 | 1 | **3.65** (7.30 total) | **1.00 hard strip each side** | – | – | 2.50 |
| Rural wide single WS2 | 1 | **5.00** (10.00 total) | 1.00 each side | – | – | 2.50 |
| Wide single 2+1 (WS2+1) | 2 + 1 | 3.50, with a **1.00** double-white-line strip in the middle (11.50 total) | 1.00 each side | – | – | 2.50 |
| WS2 climbing-lane section | 2 up + 1 down | 3.50 each (10.50) | 1.00 each side | – | – | 2.50 |
| Urban single SU2 | 1 | 3.65 (7.30) | none (kerb) | – | – | varies |
| Motorway or dual slip road, 1 lane (MG1A / DG1A) | 1 | **3.70** | 3.30 hard shoulder | 0.70 | – | 1.50 |
| Dual slip road, 2 lanes (DG2A) | 2 | 3.65 + 3.65 | 1.00 hard strip | 1.00 | – | 2.50 |
| Motorway slip road, 2 lanes (MG2C / DG2C) | 2 | 3.65 + 3.65 | 3.30 hard shoulder | 1.00 | – | 1.50 |

- **Notes:**
  - Offside hard shoulders are not allowed ([CD127] 2.8).
  - Lane widths for curves of 90–400 m radius are widened according to [CD109].
- **Climbing lane on a 10 m road:** the climbing lane is **3.2 m** and the other lanes **3.4 m** each ([TSM5] 3.11.2; older TD 9 layout).

### 7b. Local roads and streets (not covered by DMRB)

Most A, B and C roads belong to councils. They have no single national width standard.

| Road | Typical carriageway (m) | Source / confidence |
|---|---|---|
| Local A road (single) | 6.5–7.3, usually no hard strip, sometimes 0.5–1.0 m strips | 7.3 is the DMRB S2 standard. **Typical local values are unverified.** |
| B road | 6.0–7.3 | **Unverified** (no national standard) |
| C or unclassified through road | 5.5–6.5 | **Unverified**. Centre lines start at 5.5 m ([TSM5] 2.2.6). |
| Country lane (single track) | 2.75–4.0, with passing places | **Unverified** typical values. MfS gives 2.75 m as the single-lane minimum and 3.7 m for fire access ([MfS] 6.7.3, 7.2.3). |
| Residential street | **4.8–5.5** (new). Older estates are often 5.5–7.3. | [MfS] Fig 7.1, 7.2 case study (min 4.8, max 8.8) |
| Shared surface / home zone | No formal carriageway; often 4.1–4.8 of usable width | [MfS] 7.2.8–7.2.18. Width **unverified**. |

**What a given width allows ([MfS] Fig 7.1).** These are illustrations, "not necessarily recommendations". The figure shows 5500, 4800, 4100 and 2750 mm. The usual reading (**pairs unverified**; the figure is an image) is:

| Width (m) | What fits |
|---|---|
| 5.5 | Two large vehicles pass |
| 4.8 | A car and a large vehicle pass |
| 4.1 | Two cars pass |
| 2.75 | One vehicle |

**Other street notes ([MfS]):**
- Centre lines are not required on residential streets. Removing them can lower speeds ([MfS] 9.3.1–9.3.3).
- MfS recommends a design speed of **20 mph** for residential streets.

### 7c. Proposed Geodrive table for England (OSM class → cross-section)

- **Lanes** means both directions on two-way ways, and one direction on `oneway` ways.
- **Left** is the nearside hard shoulder or strip. **Right** is the offside strip on one-way ways; on two-way ways the "left" value is used on both sides.
- This mirrors the `FCrossSection {Lanes, LaneWidth, Shoulder, InnerShoulder}` struct.

| OSM class | Two-way: lanes × width, edge strip | One-way (one carriageway of a dual road): lanes × width, left, right | Basis |
|---|---|---|---|
| motorway | – (always one-way) | **3 × 3.65, 3.30, 0.70** (use `lanes` when tagged; 2 and 4 are common) | [CD127] D3M. Urban (limit 60 mph or less): left 2.75. |
| motorway_link | – | 1 × 3.70, 3.30, 0.70. Two lanes: 2 × 3.65, 3.30, 1.00. | [CD127] MG1A, MG2C |
| trunk (green-sign A road) | 2 × 3.65, **1.00** | **2 × 3.65, 1.00, 1.00** (urban: 0, 0) | [CD127] S2, D2AP, D2UAP |
| trunk_link | 1 × 3.70, 1.00 | 1 × 3.70, 1.00 (or 3.30), 0.70 | [CD127] DG1A |
| primary (other A road) | 2 × 3.5, 0.5 (rural), 0 (urban) | 2 × 3.65, 1.00, 0.70 | Geodrive choice. **Unverified** for local roads. |
| secondary (B road) | 2 × 3.25, 0.25 rural / 0 urban | 1 × 3.65, 0.5, 0.5 | Geodrive choice. **Unverified.** |
| tertiary | 2 × 3.0, 0 | 1 × 3.5, 0, 0 | Geodrive choice. **Unverified.** |
| unclassified | 2 × 2.75 (5.5 total; **no centre line under 5.5**); single track 1 × 3.5 when `lanes=1` or `width` < 4.5 | – | [TSM5] 2.2.6; [MfS] |
| residential | 2 × 2.75 (5.5 total), 0 | 1 × 3.5, 0 | [MfS] 4.8–5.5 |
| living_street | 1 × 4.5 shared, 0, no markings | – | [MfS] shared surface (**width unverified**) |
| service | 1 × 3.7 (fire-access minimum), 0. Driveways 1 × 3.0. | – | [MfS] 6.7.3 |

- **Use real data where it exists:** if `width` or `lanes` is tagged, use it. UK `width` coverage is unknown (**unverified**); check it the way we did for the Garden Route.

### 7d. UK OSM tagging conventions ([OSM] Roads in the United Kingdom; Key:maxspeed)

| UK road | OSM | ref |
|---|---|---|
| Motorway (blue signs) | `highway=motorway` / `motorway_link` | `M25`, `A1(M)` |
| A road, **primary route** (green signs) | `highway=trunk` / `trunk_link` | `A30` |
| A road, non-primary (white signs) | `highway=primary` | `A3052` |
| B road | `highway=secondary` | `B3174` |
| Minor through road (C road; signed C refs are rare) | `highway=tertiary`. Only on roads where two cars can pass. | none, or `official_ref` |
| Country lanes and other non-residential roads | `highway=unclassified` | – |
| Housing streets | `highway=residential` | – |

- **Trunk ≠ national trunk network.** In OSM, `trunk` means "primary route" (green signs), including council-run ones. The DMRB widths apply only to roads that National Highways runs. So treat trunk sizes as an upper bound.
- **Dual carriageways** are drawn as two `oneway=yes` ways. They often carry `dual_carriageway=yes`. This is the same as SA, so our one-way = divided rule carries over.
- **`maxspeed` uses mph:** for example `"30 mph"`, with a space before the unit ([OSM] Key:maxspeed).
- **`sidewalk=both|left|right|no|separate`** is common in towns. Use it for footway and kerb decisions.
- **`lit=yes`** matters for the 30 mph "restricted road" rule (section 8).

## 8. Speed limits

### 8a. The law ([HC] rule 124; Road Traffic Regulation Act 1984 s.81–86)

| Road | Cars and motorcycles | Cars towing, buses and coaches, goods vehicles up to 7.5 t | Goods vehicles over 7.5 t |
|---|---|---|---|
| Built-up area ("restricted road": street lights **183 m or less apart**) | **30 mph** (**20 mph in Wales** since Sept 2023) | 30 | 30 |
| Single carriageway, national limit | **60 mph** | 50 | 50 (England and Wales) |
| Dual carriageway, national limit | **70 mph** | 60 | 60 |
| Motorway | **70 mph** | 60 (buses 70) | 60 |

- **The national-limit sign:** a white disc with a black diagonal bar.
- **Other local limits:** 20, 40 and 50 mph are set by order and signed.
- **20 mph zones:** these are signed and use traffic calming.

### 8b. OSM tags and parse rules

| Tag value | Meaning | Use (km/h) |
|---|---|---|
| `maxspeed=30 mph` (any `N mph`) | Signed or implied limit | N × 1.609. **This already works.** |
| `maxspeed:type=GB:nsl_single` (with `maxspeed=60 mph`) | National limit, single carriageway | 96.6 |
| `maxspeed:type=GB:nsl_dual` (with `70 mph`) | National limit, dual carriageway | 112.7 |
| `maxspeed:type=GB:motorway` (with `70 mph`) | Motorway | 112.7 |
| `maxspeed:type=GB:nsl_restricted` (with `30 mph`, `lit=yes`) | Restricted road | 48.3 |
| `maxspeed:type=GB:zone20` / `GB:zone40` | 20 or 40 zone | 32.2 / 64.4 |
| `GB-WLS:nsl_restricted` | Wales restricted road | 32.2 |
| `maxspeed=national` (legacy; "possible tagging mistake" in [OSM]) | National limit | Motorway class: 70. Dual (oneway trunk or primary, or `dual_carriageway=yes`): 70. Otherwise: 60 mph. |
| `maxspeed=GB:nsl_single` etc. (type written into maxspeed; seen in old data) | As above | As above (**unverified** how often it occurs) |
| `source:maxspeed=GB:*` | An older alternative to `maxspeed:type` | As above |

**Parse rules:**
1. Read `maxspeed`. If it is a number with `mph`, convert to km/h.
2. If `maxspeed` is missing, `national`, or a `GB:` code, read `maxspeed:type`, then `source:maxspeed`, and map them as above.
3. If there is still nothing, use the class default:

| Class | Default limit | km/h |
|---|---|---|
| motorway, motorway_link | 70 mph | 113 |
| trunk or primary, one-way (dual) | 70 mph | 113 |
| trunk, primary, secondary, tertiary, unclassified: two-way, outside towns | 60 mph | 97 |
| The same inside a built-up area (`lit=yes`, or within a `place=town|city|village` area) | 30 mph | 48 |
| residential, living_street, service | 30 mph | 48 |

  Reality is often 20 mph; **in Wales, use 20 mph**.

**Design speed for geometry ([CD109] Table 2.5, Table 2.10):**
- Urban roads use **speed limit + a margin**:

| Limit (mph) | 30 | 40 | 50 | 60 |
|---|---|---|---|---|
| Design speed (km/h) | 60 | 70 | 85 | 100 |

- Rural design speed in CD 109 comes from alignment and layout constraint. For the sim, use **100 km/h** for 60 mph single carriageways and **120 km/h** for 70 mph roads.

**CD 109 sight distances, if we want them:**

| Design speed (km/h) | 120 | 100 | 85 | 70 | 60 | 50 |
|---|---|---|---|---|---|---|
| Stopping sight distance, desirable (m) | 295 | 215 | 160 | 120 | 90 | 70 |
| Full overtaking sight distance FOSD (m) | – | 580 | 490 | 410 | 345 | 290 |

- Stopping sight distance is measured from an eye at 1.05–2.0 m to an object at 0.26–2.0 m.
- FOSD is measured from 1.05 m to 1.05 m ([CD109] 3.1, 3.3).
- **Markings use Table 1 (V and W), not FOSD.**

## 9. Everything else that changes from SA

| Topic | SA (current code) | UK | Carries over? |
|---|---|---|---|
| Side of the road | Left | Left | **Yes**: lane order, roundabouts clockwise, left-hand bends limit sight |
| Speed units | km/h | **mph** in law and on signs. Widths are metric. | Parse and convert. The HUD could show mph. |
| Left edge line colour | **Yellow** | **White** | No |
| Right (median) edge line | White | White (amber studs) | Yes |
| Centre-line module | 12 m rural (4 / 8), 9 m urban (3 / 6) | **9 m over 40 mph (3 / 6), 6 m at 40 mph or less (2 / 4)** | Similar idea, different numbers |
| Lane lines | 2 / 4 rural, 1.5 / 3 urban | **2 / 7 over 40 mph, 1 / 5 at 40 or less** | No |
| Hazard warning line | None (SA has WM8 arrows only) | **1004 / 1004.1**: long marks, short gaps, before every hazard and junction | New |
| No overtaking | RM1 (one solid line) or RM2. Barrier sight distance with eye 1.05 m and object 1.30 m. Join gaps under 120 m. | **Always a double line** (solid plus broken, or solid plus solid). V / W table with eye and object both 1.05 m. Gaps under V are closed. Systems under V/2 apart are joined. | Same sweep method, new table and rules |
| Warning before a solid line | WM8 arrows at 24 / 60 / 108 m (rural) | 1014 deflection arrows at about 1, 3 and 6 s (Table 3-1) | Same idea |
| Junction control line | Stop line 300 / 500 mm solid; yield line 600 / 300 broken | Give-way line: **two** broken lines, 200 mm each, 300 mm apart, 600 / 300. Stop line only with a STOP sign. | Different shape |
| Painted islands | RM5: yellow bars, white border | **White** hatching (1040) and **white** chevrons (1041, 1042) | Colour change |
| Studs | Red = prohibition, yellow = warning, white = guidance | Red on the left edge, amber on the right edge, white for lanes, green for crossable edges | Different scheme |
| Roundabouts | Fairly rare; "traffic circles" | **Very common**. Give way to the right. Every entry has a give-way line, and approach lines become warning lines. | Rules the same, frequency very different |
| Mini-roundabouts | None (SA uses 4-way stops) | Painted white disc, 1–4 m, with 3 arrows. `highway=mini_roundabout`. | New |
| 4-way stops | Common | **None**. Never give-way on every approach. | Remove for UK |
| Shoulders | Wide surfaced shoulders (3 m on the N2). "Yellow-line driving". | No shoulders on single carriageways, only **1 m hard strips** on trunk roads. Hard shoulders only on motorways. | No |
| Country lanes | Rare | Very common: single track, no markings, hedges right at the edge. Sight is blocked by hedges, not shoulders. | New |
| Kerbs | Mostly urban | Most urban and village roads are kerbed with footways (`sidewalk=*`) | – |

## What the code needs to change

These map onto `RoadMarkings.cpp`, `OsmData.cpp` (`CrossSection`) and `RoadDesignRules.cpp`. Make each one a per-country rule set (for example a `FMarkingRules` struct chosen by a region setting or by the OSM country), not a replacement of the SA values.

1. **`ParseMaxSpeed` / `DefaultSpeed` (RoadDesignRules.cpp):**
   - Add `national` and the `GB:nsl_single|nsl_dual|motorway|nsl_restricted|zone20|zone40` and `GB-WLS:*` codes.
   - Read `maxspeed:type` and `source:maxspeed` when `maxspeed` is missing.
   - UK class defaults: 70 / 60 / 30 mph (section 8b).
   - mph is already handled.
2. **`CrossSection` (OsmData.cpp):** use the UK table (section 7c).
   - motorway {3, 3.65, 3.30, 0.70}
   - trunk two-way {2, 3.65, 1.0}
   - trunk one-way {2, 3.65, 1.0, 1.0}
   - residential {2, 2.75, 0}
   - The SA 3.0 m trunk shoulders are much too wide for the UK.
3. **Edge-line colour (`LocalYellow` for the left edge):**
   - UK: **white** on both sides.
   - Paint it only where there is a hard strip or hard shoulder, or on unlit rural classified roads. Not on kerbed urban streets.
   - Widths per Table 2-5: 100 / 150 / 200 mm. Motorway 200.
   - Replaces `SurfacedShoulder` and the yellow `RM4.1`.
4. **Centre-line pattern (lines 631–632, `IsRural`):** pick the module by **limit over 40 mph** (64 km/h), not by "80 km/h or national road".
   - 1008: 2 m mark, 4 m gap.
   - 1008.1: 3 m mark, 6 m gap.
   - 100 mm wide; 150 mm on roads 10 m or more wide, or with 4 lanes.
   - **No centre line under 5.5 m** carriageway.
   - Replaces the SA WM3 4/8, 3/6 and the multi-lane 6/6, 4.5/4.5 patterns.
5. **Lane lines (lines 658–659, 684):**
   - 1005: 1 m mark, 5 m gap (40 mph or less).
   - 1005.1: 2 m mark, 7 m gap.
   - 100 mm; 150 mm on 70 mph duals and motorways.
   - Replaces the SA GM1 2/4 and 1.5/3.
6. **New: hazard warning lines.** 1004 (4 m mark, 2 m gap) or 1004.1 (6 m mark, 3 m gap) replace the centre line:
   - where visibility is less than W;
   - before junctions on the main road, with at least 7 marks (5 at 30 mph);
   - on minor-road approaches;
   - before roundabouts and signals, for both centre and lane lines;
   - between double-white-line systems.
   - There is no SA equivalent.
7. **No-overtaking sweep (`BarrierSightDistance`, `EyeHeight`, `ObjectHeight`, `JoinGap`):**
   - `ObjectHeight` 130 → **105 cm**.
   - Replace the barrier-sight-distance table with **V** (75 / 95 / 120 / 150 / 175 m by mph band, no interpolation) and add **W** (115 / 160 / 195 / 240 / 275 m).
   - Replace the fixed 120 m `JoinGap` with the TSM steps:
     - drop isolated segments under V/4;
     - extend at the exit of left-hand bends to W;
     - close gaps under V;
     - join systems under V/2;
     - fill broken elements under V inside a system;
     - put warning lines in gaps between V/2 and W.
   - Skip double white lines on carriageways under 6.1 m and in built-up areas.
8. **Double-line geometry (`PairOffset`, the paired solid line):**
   - The UK always paints a **pair**: solid plus solid, or solid plus broken (broken = 1 m / 5 m).
   - Lines are 150 mm, 175 mm apart, centred on the road centre.
   - There is no single solid "RM1" in the UK. The SA 3-line system is closest. Replace the 15 cm `PairOffset` with 16.25 cm each side of centre (175 / 2 + 150 / 2).
9. **WM8 arrows → 1014 deflection arrows:** at Table 3-1 distances (for example 21 / 66 / 138 m at 50 mph), 4.5–6 m long (9 m at 70 mph before hatching), in the centre line.
10. **Stop and yield lines (stop-line painter, lines 700–745):**
    - The default UK control is a **give-way line**: two broken lines, 200 mm wide, 300 mm apart, 600 mm mark and 300 mm gap.
    - Run it to the centre, or across the full width if the half would be under 2.75 m.
    - Add the 1023A triangle 2.1–2.75 m back.
    - Use the solid 300 mm stop line only with `highway=stop`.
    - Signal stop line: 200 / 300 mm.
    - **The approach line before the stop line** (now an SA solid line) becomes a 1004 warning line on the minor road, with at least 5 or 7 marks.
    - On the main road, use warning lines **instead of** the SA RM1 with a 20 m break.
11. **New: roundabouts.**
    - A give-way line (1003.1, single broken, 200 or 300 mm) across every entry of `junction=roundabout`.
    - Warning lines on the approaches.
    - **Mini-roundabout:** a painted white disc (1–4 m) and three arrows at `highway=mini_roundabout` nodes.
12. **Painted islands and gores (SA RM5):**
    - UK hatching is **white**: 1040 diagonals between opposing flows and at ghost islands; 1042 chevrons at slip-road gores.
    - Borders: broken 4 / 2 or 6 / 3, or solid 200 mm in gores.
    - Tapers 1:40 to 1:55 (Table 2-6).
13. **Merges and diverges:** the edge line becomes **1010** (1 m / 1 m, 100–200 mm) across slip-road mouths and lay-bys. This replaces SA WM2.
14. **Studs (if we draw them):**
    - Red on the left edge, amber on the median edge.
    - White on centre and lane lines (at 12 or 18 m by module, 4.5 m in double white lines).
    - Green across slip roads.
15. **Region switch:** country from OSM `ISO3166-1` or the region config.
    - Things that stay the same for both: drive on the left, roundabouts clockwise, dual carriageway = two one-way ways, eye height 1.05 m.
