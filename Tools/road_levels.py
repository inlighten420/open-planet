"""The road diagram: an area's roads as the game solved them, on one HTML page, by level.

    Open-Planet.exe -- --lat=43.7392 --lon=7.4277 --km=3 --dumproads=roads.json
    python Tools/road_levels.py roads.json roads.html

The page draws every road from above at its real width, coloured by its height; a slider shows one band of heights at a
time; "Spread the levels" pulls each OSM layer apart up the screen, so roads stacked on each other separate. Hover a road
for its name, level and height; shift-click (or ctrl-click) roads to collect several, then "Copy for Claude" puts their ids, names,
heights, ends, how they sit over each other and the problems found on them on the clipboard, to paste into the chat; click one to light up the roads joined to it and see it from the side (the ground under it, and every road
within 15 m at its height). The list on the right is what looks wrong:
levels crossing too close, roads lying on each other, ends that stop just short of another road, roads deep in the ground
that aren't tunnels or high in the air that aren't bridges, and roads that don't meet their junction's height, roads steeper than their class allows, and junctions that tilt
against the ground.
"""

import json
import math
import sys
from collections import defaultdict

CLEARANCE = 6.5  # metres one level must clear another by (the game's BridgeClearance)
NEAR_END = 3.0  # metres: a road's loose end this close to another road it doesn't join
BURIED = 5.0  # metres under the ground for a road that isn't a tunnel
FLYING = 8.0  # metres over the ground for a road that isn't a bridge
JOIN_STEP = 0.5  # metres between a road's end and its junction's height
TILT = 6.0  # % more or less than the ground, in a road's first 10 m out of a junction
# the steepest a road of each class may be (%), as the game's design rules have them roughly (as Tools/road_truth.py)
MAX_GRADE = {"motorway": 6, "trunk": 8, "primary": 10, "secondary": 12, "tertiary": 14, "residential": 18,
             "unclassified": 18, "living_street": 20, "service": 22, "track": 25}


def main(source, target):
    with open(source, encoding="utf-8") as f:
        data = json.load(f)
    roads, junctions = [], []
    for piece in data["pieces"]:
        roads += piece["roads"]
        junctions += piece["junctions"]

    # metres east and north of the area's middle (flat: a few km)
    lats = [p[0] for r in roads for p in r["points"]]
    lons = [p[1] for r in roads for p in r["points"]]
    lat0, lon0 = (min(lats) + max(lats)) / 2, (min(lons) + max(lons)) / 2
    kx = 111320.0 * math.cos(math.radians(lat0))
    ky = 111320.0

    def xy(lat, lon):
        return ((lon - lon0) * kx, (lat - lat0) * ky)

    for r in roads:
        r["xy"] = [xy(p[0], p[1]) for p in r["points"]]
        r["z"] = [p[2] for p in r["points"]]
        r["g"] = [p[3] for p in r["points"]]
        r["m"] = [p[4] if len(p) > 4 else 0 for p in r["points"]]
    for j in junctions:
        j["xy"] = xy(j["lat"], j["lon"])

    problems = find_problems(roads, junctions)
    # the pretend drive (Core/GeoRoads/RoadSweep): where a hatchback or a truck would meet the road
    for piece in data["pieces"]:
        for s in piece.get("sweep", []):
            problems.append({"kind": "car: " + s["kind"] + " (" + s["car"].split()[0].lower() + ")", "at": list(xy(s["lat"], s["lon"])),
                             "roads": [i for i in (s["edge"], s["other"]) if i >= 0], "text": s["text"]})

    page = PAGE.replace("/*DATA*/", json.dumps({
        "roads": [{
            "id": r["id"], "name": r["name"], "highway": r["highway"], "layer": r["layer"], "bridge": r["bridge"],
            "tunnel": r["tunnel"], "oneway": r["oneway"], "half": r["half"], "from": r["from"], "to": r["to"],
            "xy": [[round(x, 2), round(y, 2)] for x, y in r["xy"]], "z": r["z"], "g": r["g"],
        } for r in roads],
        "junctions": [{"id": j["id"], "xy": [round(j["xy"][0], 2), round(j["xy"][1], 2)], "z": j["height"], "roads": j["roads"]}
                      for j in junctions],
        "problems": problems,
        "origin": [lat0, lon0],
    }, separators=(",", ":")))
    with open(target, "w", encoding="utf-8") as f:
        f.write(page)
    kinds = defaultdict(int)
    for p in problems:
        kinds[p["kind"]] += 1
    print(f"{len(roads)} roads, {len(junctions)} junctions; {len(problems)} things to look at: " +
          ", ".join(f"{n} {k}" for k, n in sorted(kinds.items())))
    print(f"written to {target}")
    if "--list" in sys.argv:
        names = {r["id"]: f'{r["name"] or "unnamed"} {r["highway"]} (layer {r["layer"]}{", bridge" if r["bridge"] else ""}{", tunnel" if r["tunnel"] else ""})' for r in roads}
        for p in problems:
            x, y = p["at"]
            lat, lon = lat0 + y / ky, lon0 + x / kx
            print(f'  {p["kind"]}: {p.get("text", "")} at {lat:.5f},{lon:.5f}: ' + "; ".join(names.get(i, str(i)) for i in p["roads"]))


def segments(roads):
    for r in roads:
        for i in range(len(r["xy"]) - 1):
            yield r, i


def cell_index(roads, size=20.0):
    grid = defaultdict(list)
    for r, i in segments(roads):
        (x1, y1), (x2, y2) = r["xy"][i], r["xy"][i + 1]
        for cx in range(int(math.floor(min(x1, x2) / size)), int(math.floor(max(x1, x2) / size)) + 1):
            for cy in range(int(math.floor(min(y1, y2) / size)), int(math.floor(max(y1, y2) / size)) + 1):
                grid[(cx, cy)].append((r, i))
    return grid, size


def closest_on(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 < 1e-6 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2))
    q = (a[0] + dx * t, a[1] + dy * t)
    return t, q, math.hypot(p[0] - q[0], p[1] - q[1])


def rank(r):
    return r["layer"] * 4 + r["bridge"] - r["tunnel"]


def label(r):
    kind = ", bridge" if r["bridge"] else (", tunnel" if r["tunnel"] else "")
    return f"{r['name'] or 'unnamed'} ({r['highway']}, layer {r['layer']}{kind})"


def find_problems(roads, junctions):
    grid, size = cell_index(roads)
    out = []
    seen = set()

    # 1. levels crossing too close, and 2. roads lying on each other at one height
    stacks = defaultdict(float)
    stack_at = {}
    for cell, items in grid.items():
        for a in range(len(items)):
            for b in range(a + 1, len(items)):
                (r1, i), (r2, k) = items[a], items[b]
                if r1["id"] == r2["id"]:
                    continue
                key = (min((r1["id"], i), (r2["id"], k)), max((r1["id"], i), (r2["id"], k)))
                if key in seen:
                    continue
                seen.add(key)
                if {r1["from"], r1["to"]} & {r2["from"], r2["to"]} and (i in (0, len(r1["xy"]) - 2) or k in (0, len(r2["xy"]) - 2)):
                    shared = True
                else:
                    shared = False
                A, B = r1["xy"][i], r1["xy"][i + 1]
                C, D = r2["xy"][k], r2["xy"][k + 1]
                ab = (B[0] - A[0], B[1] - A[1])
                cd = (D[0] - C[0], D[1] - C[1])
                den = ab[0] * cd[1] - ab[1] * cd[0]
                la, lc = math.hypot(*ab), math.hypot(*cd)
                if abs(den) > 1e-6 and not shared:
                    t = ((C[0] - A[0]) * cd[1] - (C[1] - A[1]) * cd[0]) / den
                    u = ((C[0] - A[0]) * ab[1] - (C[1] - A[1]) * ab[0]) / den
                    if 0.01 < t < 0.99 and 0.01 < u < 0.99 and rank(r1) != rank(r2):
                        h1 = r1["z"][i] + (r1["z"][i + 1] - r1["z"][i]) * t
                        h2 = r2["z"][k] + (r2["z"][k + 1] - r2["z"][k]) * u
                        upper, lower, hu, hl = (r1, r2, h1, h2) if rank(r1) > rank(r2) else (r2, r1, h2, h1)
                        gap = hu - hl
                        if gap < CLEARANCE - 0.5:
                            out.append({"kind": "levels too close", "at": [A[0] + ab[0] * t, A[1] + ab[1] * t],
                                        "roads": [upper["id"], lower["id"]],
                                        "text": f"{label(upper)} only {gap:.1f} m over {label(lower)}"})
                # side by side on one line, about one height
                if la > 1 and lc > 1 and abs(ab[0] * cd[0] + ab[1] * cd[1]) / (la * lc) > 0.97:
                    ux, uy = ab[0] / la, ab[1] / la
                    across = abs(ux * (C[1] - A[1]) - uy * (C[0] - A[0]))
                    if across < 0.5 * (r1["half"] + r2["half"]):
                        s0 = (C[0] - A[0]) * ux + (C[1] - A[1]) * uy
                        s1 = (D[0] - A[0]) * ux + (D[1] - A[1]) * uy
                        overlap = min(la, max(s0, s1)) - max(0.0, min(s0, s1))
                        dz = abs((r1["z"][i] + r1["z"][i + 1]) / 2 - (r2["z"][k] + r2["z"][k + 1]) / 2)
                        if overlap > 3 and dz < 3:
                            pair = (min(r1["id"], r2["id"]), max(r1["id"], r2["id"]))
                            stacks[pair] += overlap
                            stack_at.setdefault(pair, [A[0], A[1], dz])
    by_id = {r["id"]: r for r in roads}
    for pair, length in sorted(stacks.items(), key=lambda kv: -kv[1]):
        if length < 5:
            continue
        r1, r2 = by_id[pair[0]], by_id[pair[1]]
        x, y, dz = stack_at[pair]
        out.append({"kind": "on top of each other", "at": [x, y], "roads": list(pair),
                    "text": f"{label(r1)} and {label(r2)}: {length:.0f} m along one line, {dz:.1f} m apart in height"})

    # 3. loose ends just short of another road
    degree = defaultdict(int)
    for r in roads:
        degree[r["from"]] += 1
        degree[r["to"]] += 1
    for r in roads:
        for end, node in ((0, r["from"]), (-1, r["to"])):
            if degree[node] != 1:
                continue
            p, z = r["xy"][end], r["z"][end]
            cx, cy = int(math.floor(p[0] / size)), int(math.floor(p[1] / size))
            best = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for other, k in grid.get((cx + dx, cy + dy), []):
                        if other["id"] == r["id"]:
                            continue
                        t, q, d = closest_on(p, other["xy"][k], other["xy"][k + 1])
                        oz = other["z"][k] + (other["z"][k + 1] - other["z"][k]) * t
                        gap = d - other["half"]
                        if gap < NEAR_END and abs(oz - z) < 3 and (best is None or gap < best[0]):
                            best = (gap, other, oz)
            if best:
                out.append({"kind": "stops short of a road", "at": list(p), "roads": [r["id"], best[1]["id"]],
                            "text": f"{label(r)} ends {max(best[0], 0):.1f} m from {label(best[1])} without joining it"})

    # 4. buried or flying without being a tunnel or a bridge
    for r in roads:
        worst = None
        for i, (z, g) in enumerate(zip(r["z"], r["g"])):
            if not r["tunnel"] and g - z > BURIED and (worst is None or g - z > worst[0]):
                worst = (g - z, i, "under the ground")
            if not r["bridge"] and z - g > FLYING and (worst is None or z - g > worst[0]):
                worst = (z - g, i, "over the ground")
        if worst:
            out.append({"kind": "in the ground or the air", "at": list(r["xy"][worst[1]]), "roads": [r["id"]],
                        "text": f"{label(r)} {worst[0]:.1f} m {worst[2]}"})

    # 5. a road's end not at its junction's height
    jz = {j["id"]: j["height"] for j in junctions}
    for r in roads:
        for end, node in ((0, r["from"]), (-1, r["to"])):
            if node in jz and abs(r["z"][end] - jz[node]) > JOIN_STEP:
                out.append({"kind": "step at a junction", "at": list(r["xy"][end]), "roads": [r["id"]],
                            "text": f"{label(r)} meets its junction {r['z'][end] - jz[node]:+.1f} m off its height"})

    # 6. too steep for its class (a piece 5 m or longer: a node's rounding doesn't count), tunnels and bridges too
    for r in roads:
        cls = r["highway"].replace("_link", "")
        worst = None
        for i in range(len(r["xy"]) - 1):
            run = math.hypot(r["xy"][i + 1][0] - r["xy"][i][0], r["xy"][i + 1][1] - r["xy"][i][1])
            if run < 5.0:
                continue
            grade = abs(r["z"][i + 1] - r["z"][i]) / run * 100
            if grade > MAX_GRADE.get(cls, 20) + 2 and (worst is None or grade > worst[0]):
                worst = (grade, i, run)
        if worst:
            out.append({"kind": "too steep", "at": list(r["xy"][worst[1]]), "roads": [r["id"]],
                        "text": f"{label(r)} climbs {worst[0]:.0f}% over {worst[2]:.0f} m (its class: at most {MAX_GRADE.get(cls, 20)}%)"})

    # 7. a tilted junction: a road climbing more than TILT % more or less than the ground in its first 10 m out of it
    by_id = {r["id"]: r for r in roads}
    for j in junctions:
        if len(j["roads"]) < 3:
            continue
        worst = None
        for rid in j["roads"]:
            r = by_id.get(rid)
            if not r or r["bridge"] or r["tunnel"] or len(r["xy"]) < 2:
                continue
            order_ = range(len(r["xy"])) if r["from"] == j["id"] else range(len(r["xy"]) - 1, -1, -1)
            idx = list(order_)
            start = r["xy"][idx[0]]
            for k in idx[1:]:
                d = math.hypot(r["xy"][k][0] - start[0], r["xy"][k][1] - start[1])
                if d >= 10.0:
                    ground = r["g"][k] - r["g"][idx[0]]
                    tilt = abs((r["z"][k] - j["height"]) - ground) / d * 100
                    climb = abs(r["z"][k] - j["height"]) / d * 100
                    # (only where the road itself climbs steeply, and the ground doesn't jump: a bridge or a wall)
                    if climb > TILT and abs(ground) / d * 100 < 15.0 and (worst is None or tilt > worst[0]):
                        worst = (tilt, r)
                    break
        if worst and worst[0] > TILT:
            out.append({"kind": "tilted junction", "at": list(j["xy"]), "roads": [worst[1]["id"]],
                        "text": f"Junction at {j['height']:.1f} m: {label(worst[1])} climbs {worst[0]:.0f}% more or less than the ground in its first 10 m"})

    order = ["levels too close", "too steep", "tilted junction", "stops short of a road", "step at a junction", "on top of each other", "in the ground or the air"]
    out.sort(key=lambda p: order.index(p["kind"]))
    for p in out:
        p["at"] = [round(p["at"][0], 1), round(p["at"][1], 1)]
    return out


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Road Levels</title>
<style>
:root { --bg: #f4f2ee; --panel: #ffffff; --ink: #1d1d1b; --muted: #6b6860; --line: #d9d5cc; --accent: #c2410c; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { --bg: #16171a; --panel: #1f2125; --ink: #ecebe8; --muted: #9a978f; --line: #33363c; --accent: #fb923c; } }
:root[data-theme="dark"] { --bg: #16171a; --panel: #1f2125; --ink: #ecebe8; --muted: #9a978f; --line: #33363c; --accent: #fb923c; }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 14px/1.4 system-ui, sans-serif; display: grid; grid-template-columns: 1fr 360px; height: 100vh; overflow: hidden; }
#map { position: relative; overflow: hidden; }
canvas { display: block; width: 100%; height: 100%; cursor: grab; }
canvas.dragging { cursor: grabbing; }
#bar { position: absolute; left: 12px; top: 12px; right: 12px; display: flex; flex-wrap: wrap; gap: 10px; align-items: center; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 8px 12px; }
#bar label { display: flex; gap: 6px; align-items: center; color: var(--muted); }
#bar input[type=range] { width: 140px; }
#band { font-variant-numeric: tabular-nums; min-width: 120px; }
#legend { position: absolute; left: 12px; bottom: 12px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 8px 12px; color: var(--muted); font-size: 12px; }
#ramp { width: 180px; height: 10px; border-radius: 3px; margin: 4px 0; }
#tip { position: absolute; pointer-events: none; background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 6px 8px; font-size: 12px; display: none; max-width: 320px; }
aside { background: var(--panel); border-left: 1px solid var(--line); display: flex; flex-direction: column; min-height: 0; }
aside h1 { font-size: 16px; margin: 14px 16px 4px; }
aside p { margin: 0 16px 10px; color: var(--muted); font-size: 12px; }
#kinds { display: flex; flex-wrap: wrap; gap: 6px; padding: 0 16px 10px; }
#kinds button { border: 1px solid var(--line); background: transparent; color: var(--ink); border-radius: 999px; padding: 3px 10px; font-size: 12px; cursor: pointer; }
#kinds button.on { background: var(--accent); border-color: var(--accent); color: #fff; }
#list { overflow: auto; flex: 1; border-top: 1px solid var(--line); }
#list div { padding: 8px 16px; border-bottom: 1px solid var(--line); cursor: pointer; font-size: 13px; }
#list div:hover, #list div.sel { background: color-mix(in srgb, var(--accent) 12%, transparent); }
#list small { color: var(--muted); display: block; }
#chosen { padding: 10px 16px; border-top: 1px solid var(--line); font-size: 12px; }
#chosen button { border: 1px solid var(--line); background: transparent; color: var(--ink); border-radius: 6px; padding: 4px 10px; cursor: pointer; margin-right: 6px; }
#chosen button.go { background: var(--accent); border-color: var(--accent); color: #fff; }
#chosen textarea { width: 100%; box-sizing: border-box; margin-top: 6px; background: transparent; color: var(--ink); border: 1px solid var(--line); border-radius: 6px; font: 12px system-ui; }
#chosenList { margin: 6px 0; color: var(--muted); }
#bar button { border: 1px solid var(--line); background: transparent; color: var(--ink); border-radius: 6px; padding: 3px 10px; cursor: pointer; }
#editTools { display: inline-flex; gap: 8px; align-items: center; }
#editTools[hidden], #editHelp[hidden] { display: none; }
#editHelp { position: absolute; left: 12px; top: 64px; max-width: 520px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 8px 12px; font-size: 12px; color: var(--muted); }
#editInfo { margin: 6px 0 0; color: var(--muted); }
#picked { padding: 10px 16px; border-top: 1px solid var(--line); font-size: 12px; color: var(--muted); max-height: 30%; overflow: auto; }
#side { position: absolute; left: 12px; right: 12px; bottom: 12px; height: 230px; background: var(--panel); border: 1px solid var(--line); border-radius: 8px; display: none; }
#side canvas { cursor: default; }
#side .title { position: absolute; left: 12px; top: 6px; font-size: 12px; color: var(--muted); pointer-events: none; }
#side button { position: absolute; right: 8px; top: 6px; border: 1px solid var(--line); background: transparent; color: var(--ink); border-radius: 6px; cursor: pointer; }
@media (max-width: 800px) { body { grid-template-columns: 1fr; grid-template-rows: 60vh 40vh; } aside { border-left: 0; border-top: 1px solid var(--line); } }
</style>
</head>
<body>
<div id="map">
  <canvas id="c"></canvas>
  <div id="bar">
    <label>Lowest <input id="lo" type="range" min="0" max="1000" value="0"></label>
    <label>Highest <input id="hi" type="range" min="0" max="1000" value="1000"></label>
    <span id="band"></span>
    <label><input id="spread" type="checkbox"> Spread the levels</label>
    <label><input id="tunnels" type="checkbox" checked> Tunnels</label>
    <label><input id="dots" type="checkbox" checked> Junctions</label>
    <label><input id="edit" type="checkbox"> Edit</label>
    <span id="editTools" hidden>
      <label>Scale collected <input id="scale" type="range" min="50" max="250" value="100"> <span id="scaleVal">100%</span></label>
      <button id="undo">Undo</button> <button id="reset">Reset all</button>
    </span>
  </div>
  <div id="editHelp" hidden>Edit: drag a point to move it (a road's end moves its junction and every road there). Double-click a road to add a point, alt-click a point to remove it. Shift-click roads to collect them, then use Scale to widen or shrink them about their middle (a roundabout). Ctrl+Z undoes. The grey dashed line is the map's own line.</div>
  <div id="legend">Road height<div id="ramp"></div><span id="rampLo"></span> &nbsp;to&nbsp; <span id="rampHi"></span><br>dashed: tunnel &nbsp; white edge: bridge</div>
  <div id="tip"></div>
  <div id="side"><canvas id="sc"></canvas><div class="title" id="sideTitle"></div><button id="sideClose">Close</button></div>
</div>
<aside>
  <h1>Things to look at</h1>
  <p>Click one to go there. Click a road on the map to light up the roads joined to it.</p>
  <div id="kinds"></div>
  <div id="list"></div>
  <div id="chosen">
    <b>Collected roads</b> <span style="color:var(--muted)">(shift-click or ctrl-click to add or remove)</span>
    <div id="chosenList">None yet.</div>
    <div id="editInfo"></div>
    <textarea id="note" rows="2" placeholder="What is wrong here? (optional, goes in the copy)"></textarea>
    <div style="margin-top:6px"><button class="go" id="copy">Copy for Claude</button><button id="clear">Clear</button> <span id="copied" style="color:var(--muted)"></span></div>
  </div>
  <div id="picked">Nothing picked.</div>
</aside>
<script>
const D = /*DATA*/;
const cv = document.getElementById('c'), ctx = cv.getContext('2d');
const tip = document.getElementById('tip');
let W = 0, H = 0, dpr = 1;
let zMin = Infinity, zMax = -Infinity, xMin = Infinity, xMax = -Infinity, yMin = Infinity, yMax = -Infinity;
for (const r of D.roads) { for (const z of r.z) { zMin = Math.min(zMin, z); zMax = Math.max(zMax, z); } for (const [x, y] of r.xy) { xMin = Math.min(xMin, x); xMax = Math.max(xMax, x); yMin = Math.min(yMin, y); yMax = Math.max(yMax, y); } }
const layers = [...new Set(D.roads.map(r => r.layer))].sort((a, b) => a - b);
let view = { cx: (xMin + xMax) / 2, cy: (yMin + yMax) / 2, s: 1 };
let picked = null, joined = new Set(), flash = null, kindsOn = new Set(), chosen = [];
const byId = new Map(D.roads.map(r => [r.id, r]));
const atJunction = new Map(D.junctions.map(j => [j.id, j]));

function colour(z) {
  const t = Math.max(0, Math.min(1, (z - zMin) / Math.max(1, zMax - zMin)));
  const stops = [[37, 99, 235], [6, 182, 212], [34, 197, 94], [234, 179, 8], [239, 68, 68]];
  const f = t * (stops.length - 1), i = Math.min(stops.length - 2, Math.floor(f)), u = f - i;
  const c = stops[i].map((v, k) => Math.round(v + (stops[i + 1][k] - v) * u));
  return `rgb(${c[0]},${c[1]},${c[2]})`;
}
document.getElementById('ramp').style.background = 'linear-gradient(90deg,' + [0, .25, .5, .75, 1].map(t => colour(zMin + t * (zMax - zMin))).join(',') + ')';
document.getElementById('rampLo').textContent = zMin.toFixed(0) + ' m';
document.getElementById('rampHi').textContent = zMax.toFixed(0) + ' m';

function band() {
  const a = +document.getElementById('lo').value / 1000, b = +document.getElementById('hi').value / 1000;
  const lo = zMin + Math.min(a, b) * (zMax - zMin), hi = zMin + Math.max(a, b) * (zMax - zMin);
  document.getElementById('band').textContent = lo.toFixed(0) + ' to ' + hi.toFixed(0) + ' m';
  return [lo, hi];
}
function lift(r) { return document.getElementById('spread').checked ? (layers.indexOf(r.layer) - layers.indexOf(0)) * 60 : 0; }
function screen(x, y, dy) { return [W / 2 + (x - view.cx) * view.s, H / 2 - (y - view.cy) * view.s - (dy || 0)]; }
function world(px, py) { return [(px - W / 2) / view.s + view.cx, -(py - H / 2) / view.s + view.cy]; }

function fit() {
  const r = cv.getBoundingClientRect(); dpr = window.devicePixelRatio || 1;
  W = r.width; H = r.height; cv.width = W * dpr; cv.height = H * dpr;
  if (view.s === 1) view.s = Math.min(W / (xMax - xMin), H / (yMax - yMin)) * 0.9;
  draw();
}
function draw() {
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, W, H);
  const [lo, hi] = band();
  const showT = document.getElementById('tunnels').checked;
  const segs = [];
  for (const r of D.roads) {
    if (r.tunnel && !showT) continue;
    for (let i = 0; i + 1 < r.xy.length; i++) {
      const z = (r.z[i] + r.z[i + 1]) / 2;
      if (z < lo - 0.01 || z > hi + 0.01) continue;
      segs.push([z, r, i]);
    }
  }
  segs.sort((a, b) => a[0] - b[0]);
  const dim = picked !== null;
  for (const [z, r, i] of segs) {
    const dy = lift(r);
    const [x1, y1] = screen(r.xy[i][0], r.xy[i][1], dy), [x2, y2] = screen(r.xy[i + 1][0], r.xy[i + 1][1], dy);
    const w = Math.max(1.5, r.half * 2 * view.s);
    ctx.globalAlpha = dim && !joined.has(r.id) ? 0.15 : (r.tunnel ? 0.75 : 1);
    if (r.bridge) { ctx.strokeStyle = '#fff'; ctx.lineWidth = w + 3; ctx.setLineDash([]); ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); }
    ctx.strokeStyle = r.id === picked || chosen.includes(r.id) ? getComputedStyle(document.documentElement).getPropertyValue('--accent') : colour(z);
    ctx.lineWidth = w; ctx.lineCap = 'round';
    ctx.setLineDash(r.tunnel ? [Math.max(4, w), Math.max(3, w * 0.6)] : []);
    ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
  }
  ctx.setLineDash([]); ctx.globalAlpha = 1;
  if (editing) {
    const muted = getComputedStyle(document.documentElement).getPropertyValue('--muted');
    for (const r of D.roads) {
      if (!edited(r)) continue;
      const dy = lift(r);
      ctx.strokeStyle = muted; ctx.lineWidth = 1.5; ctx.setLineDash([5, 4]); ctx.beginPath();
      r.orig.forEach(([x, y], i) => { const [sx, sy] = screen(x, y, dy); i ? ctx.lineTo(sx, sy) : ctx.moveTo(sx, sy); });
      ctx.stroke(); ctx.setLineDash([]);
    }
    if (view.s > 0.8) {
      const [lo, hi] = band(); const showT = document.getElementById('tunnels').checked;
      for (const r of D.roads) {
        if (r.tunnel && !showT) continue;
        const dy = lift(r);
        r.xy.forEach(([x, y], i) => {
          if (r.z[i] < lo - 0.01 || r.z[i] > hi + 0.01) return;
          const [sx, sy] = screen(x, y, dy);
          if (sx < -5 || sy < -5 || sx > W + 5 || sy > H + 5) return;
          ctx.fillStyle = i === 0 || i === r.xy.length - 1 ? '#fff' : '#111'; ctx.strokeStyle = '#000'; ctx.lineWidth = 1;
          ctx.fillRect(sx - 2.5, sy - 2.5, 5, 5); ctx.strokeRect(sx - 2.5, sy - 2.5, 5, 5);
        });
      }
    }
  }
  chosen.forEach((id, k) => {
    const r = byId.get(id); if (!r) return;
    const m = r.xy[Math.floor(r.xy.length / 2)], [x, y] = screen(m[0], m[1], lift(r));
    ctx.fillStyle = '#000'; ctx.strokeStyle = '#fff'; ctx.lineWidth = 3; ctx.font = 'bold 13px system-ui';
    ctx.strokeText('#' + (k + 1), x + 6, y - 6); ctx.fillText('#' + (k + 1), x + 6, y - 6);
  });
  if (document.getElementById('dots').checked && view.s > 0.5) {
    for (const j of D.junctions) {
      if (j.z < lo - 0.01 || j.z > hi + 0.01) continue;
      const r0 = byId.get(j.roads[0]);
      const [x, y] = screen(j.xy[0], j.xy[1], r0 ? lift(r0) : 0);
      ctx.fillStyle = j.roads.length === 1 ? '#888' : '#fff';
      ctx.strokeStyle = '#000'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(x, y, j.roads.length > 2 ? 3.5 : 2.2, 0, 7); ctx.fill(); ctx.stroke();
    }
  }
  const accent = getComputedStyle(document.documentElement).getPropertyValue('--accent');
  for (const p of shownProblems()) {
    const [x, y] = screen(p.at[0], p.at[1], 0);
    ctx.strokeStyle = accent; ctx.lineWidth = p === flash ? 3 : 1.5;
    ctx.beginPath(); ctx.arc(x, y, p === flash ? 14 : 6, 0, 7); ctx.stroke();
  }
}
function shownProblems() { return D.problems.filter(p => kindsOn.has(p.kind)); }

function nearest(px, py) {
  const [lo, hi] = band(); const showT = document.getElementById('tunnels').checked;
  let best = null, bd = 8;
  for (const r of D.roads) {
    if (r.tunnel && !showT) continue;
    const dy = lift(r);
    for (let i = 0; i + 1 < r.xy.length; i++) {
      const z = (r.z[i] + r.z[i + 1]) / 2; if (z < lo - 0.01 || z > hi + 0.01) continue;
      const [x1, y1] = screen(r.xy[i][0], r.xy[i][1], dy), [x2, y2] = screen(r.xy[i + 1][0], r.xy[i + 1][1], dy);
      const dx = x2 - x1, dyy = y2 - y1, l2 = dx * dx + dyy * dyy;
      const t = l2 ? Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dyy) / l2)) : 0;
      const d = Math.hypot(px - x1 - dx * t, py - y1 - dyy * t) - Math.max(1.5, r.half * 2 * view.s) / 2;
      if (d < bd || (best && Math.abs(d - bd) < 1 && z > best.z)) { bd = d; best = { r, z: r.z[i] + (r.z[i + 1] - r.z[i]) * t, g: r.g[i] + (r.g[i + 1] - r.g[i]) * t }; }
    }
  }
  return best;
}
function describe(r) { return (r.name || 'unnamed') + ' (' + r.highway + ')'; }
function kind(r) { return 'layer ' + r.layer + (r.bridge ? ', bridge' : '') + (r.tunnel ? ', tunnel' : '') + (r.oneway ? ', one way' : ''); }

// --- edit mode: drag a road's points (an end drags its junction and every road meeting there), add and remove points,
// scale the collected roads about their middle; the copy for Claude then says what changed, as each edited road's new line ---
let editing = false, grab = null, undo = [], scaleBase = null;
D.roads.forEach(r => { r.orig = r.xy.map(p => p.slice()); });
D.junctions.forEach(j => { j.orig = j.xy.slice(); });
const roadsAt = new Map();	// junction id -> [road, which end (0: start, 1: end)]
D.roads.forEach(r => { [[r.from, 0], [r.to, 1]].forEach(([j, e]) => { if (!roadsAt.has(j)) roadsAt.set(j, []); roadsAt.get(j).push([r, e]); }); });
function edited(r) { return r.xy.length !== r.orig.length || r.xy.some((p, i) => Math.hypot(p[0] - r.orig[i][0], p[1] - r.orig[i][1]) > 0.05); }
function snapshot() {
  undo.push(D.roads.map(r => ({ xy: r.xy.map(p => p.slice()), z: r.z.slice(), g: r.g.slice() })).concat(D.junctions.map(j => j.xy.slice())));
  if (undo.length > 100) undo.shift();
}
function restore(s) {
  D.roads.forEach((r, k) => { r.xy = s[k].xy.map(p => p.slice()); r.z = s[k].z.slice(); r.g = s[k].g.slice(); });
  D.junctions.forEach((j, k) => { j.xy = s[D.roads.length + k].slice(); });
}
function showEdits() {
  const n = D.roads.filter(edited).length;
  document.getElementById('editInfo').textContent = n ? n + ' road' + (n > 1 ? 's' : '') + ' edited (goes in the copy).' : '';
}
function nearestPoint(px, py) {
  const [lo, hi] = band(); const showT = document.getElementById('tunnels').checked;
  let best = null, bd = 8;
  for (const r of D.roads) {
    if (r.tunnel && !showT) continue;
    const dy = lift(r);
    r.xy.forEach(([x, y], i) => {
      if (r.z[i] < lo - 0.01 || r.z[i] > hi + 0.01) return;
      const [sx, sy] = screen(x, y, dy), d = Math.hypot(px - sx, py - sy);
      if (d < bd) { bd = d; best = { r, i }; }
    });
  }
  return best;
}
function movePoint(r, i, x, y) {
  const end = i === 0 ? r.from : i === r.xy.length - 1 ? r.to : null;
  if (end === null) { r.xy[i] = [x, y]; return; }
  const j = atJunction.get(end); if (j) j.xy = [x, y];
  (roadsAt.get(end) || []).forEach(([o, e]) => { o.xy[e === 0 ? 0 : o.xy.length - 1] = [x, y]; });
  r.xy[i] = [x, y];
}
function insertPoint(px, py) {
  const n = nearest(px, py); if (!n) return;
  const r = n.r, dy = lift(r);
  let bi = -1, bd = Infinity, bt = 0;
  for (let i = 0; i + 1 < r.xy.length; i++) {
    const [x1, y1] = screen(r.xy[i][0], r.xy[i][1], dy), [x2, y2] = screen(r.xy[i + 1][0], r.xy[i + 1][1], dy);
    const dx = x2 - x1, dyy = y2 - y1, l2 = dx * dx + dyy * dyy, t = l2 ? Math.max(0, Math.min(1, ((px - x1) * dx + (py - y1) * dyy) / l2)) : 0;
    const d = Math.hypot(px - x1 - dx * t, py - y1 - dyy * t);
    if (d < bd) { bd = d; bi = i; bt = t; }
  }
  if (bi < 0 || bt <= 0.02 || bt >= 0.98) return;
  snapshot();
  const a = r.xy[bi], b = r.xy[bi + 1];
  r.xy.splice(bi + 1, 0, [a[0] + (b[0] - a[0]) * bt, a[1] + (b[1] - a[1]) * bt]);
  r.z.splice(bi + 1, 0, r.z[bi] + (r.z[bi + 1] - r.z[bi]) * bt);
  r.g.splice(bi + 1, 0, r.g[bi] + (r.g[bi + 1] - r.g[bi]) * bt);
  showEdits(); draw();
}
function scaleChosen(f) {
  if (!scaleBase) return;
  restore(scaleBase);
  const rs = chosen.map(id => byId.get(id)).filter(Boolean); if (!rs.length) return;
  let cx = 0, cy = 0, n = 0;
  rs.forEach(r => r.xy.forEach(([x, y]) => { cx += x; cy += y; n++; }));
  cx /= n; cy /= n;
  rs.forEach(r => r.xy.map(p => p.slice()).forEach(([x, y], i) => movePoint(r, i, cx + (x - cx) * f, cy + (y - cy) * f)));
  showEdits(); draw();
}
document.getElementById('edit').addEventListener('change', e => {
  editing = e.target.checked;
  document.getElementById('editTools').hidden = !editing; document.getElementById('editHelp').hidden = !editing;
  draw();
});
const scale = document.getElementById('scale');
scale.addEventListener('input', () => {
  document.getElementById('scaleVal').textContent = scale.value + '%';
  if (!scaleBase) { snapshot(); scaleBase = undo[undo.length - 1]; }
  scaleChosen(scale.value / 100);
});
scale.addEventListener('change', () => { scaleBase = null; scale.value = 100; document.getElementById('scaleVal').textContent = '100%'; });
document.getElementById('undo').onclick = () => { if (undo.length) { restore(undo.pop()); showEdits(); draw(); } };
document.getElementById('reset').onclick = () => {
  snapshot();
  D.roads.forEach(r => {
    const n = r.orig.length;
    if (r.xy.length !== n) { r.z = r.orig.map((p, i) => r.z[Math.round(i * (r.z.length - 1) / Math.max(1, n - 1))]); r.g = r.orig.map((p, i) => r.g[Math.round(i * (r.g.length - 1) / Math.max(1, n - 1))]); }
    r.xy = r.orig.map(p => p.slice());
  });
  D.junctions.forEach(j => { j.xy = j.orig.slice(); });
  showEdits(); draw();
};
window.addEventListener('keydown', e => { if (editing && (e.ctrlKey || e.metaKey) && e.key === 'z' && undo.length) { e.preventDefault(); restore(undo.pop()); showEdits(); draw(); } });
cv.addEventListener('dblclick', e => { if (!editing) return; const b = cv.getBoundingClientRect(); insertPoint(e.clientX - b.left, e.clientY - b.top); });

let drag = null;
cv.addEventListener('mousedown', e => {
  if (editing) {
    const b = cv.getBoundingClientRect(), hit = nearestPoint(e.clientX - b.left, e.clientY - b.top);
    if (hit && e.altKey && hit.i > 0 && hit.i < hit.r.xy.length - 1) {
      snapshot(); hit.r.xy.splice(hit.i, 1); hit.r.z.splice(hit.i, 1); hit.r.g.splice(hit.i, 1);
      showEdits(); draw(); return;
    }
    if (hit && !e.shiftKey && !e.ctrlKey) { snapshot(); grab = hit; return; }
  }
  drag = { x: e.clientX, y: e.clientY, cx: view.cx, cy: view.cy, moved: false }; cv.classList.add('dragging');
});
window.addEventListener('mouseup', e => {
  cv.classList.remove('dragging');
  if (grab) { grab = null; showEdits(); return; }
  if (drag && !drag.moved) pick(e);
  drag = null;
});
window.addEventListener('mousemove', e => {
  const r = cv.getBoundingClientRect(), px = e.clientX - r.left, py = e.clientY - r.top;
  if (grab) {
    const [x, y] = world(px, py + lift(grab.r));
    movePoint(grab.r, grab.i, x, y); draw(); return;
  }
  if (drag) {
    if (Math.abs(e.clientX - drag.x) + Math.abs(e.clientY - drag.y) > 3) drag.moved = true;
    view.cx = drag.cx - (e.clientX - drag.x) / view.s; view.cy = drag.cy + (e.clientY - drag.y) / view.s; draw(); return;
  }
  if (px < 0 || py < 0 || px > W || py > H) { tip.style.display = 'none'; return; }
  const n = nearest(px, py);
  if (!n) { tip.style.display = 'none'; return; }
  tip.innerHTML = '<b>' + describe(n.r) + '</b><br>' + kind(n.r) + '<br>road ' + n.z.toFixed(1) + ' m, ground ' + n.g.toFixed(1) + ' m (' + (n.z - n.g >= 0 ? '+' : '') + (n.z - n.g).toFixed(1) + ')<br>road ' + n.r.id;
  tip.style.display = 'block';
  tip.style.left = Math.min(W - 330, px + 14) + 'px'; tip.style.top = (py + 14) + 'px';
});
cv.addEventListener('wheel', e => {
  e.preventDefault();
  const r = cv.getBoundingClientRect(), [wx, wy] = world(e.clientX - r.left, e.clientY - r.top);
  view.s *= Math.exp(-e.deltaY * 0.0015);
  const [nx, ny] = world(e.clientX - r.left, e.clientY - r.top);
  view.cx += wx - nx; view.cy += wy - ny; draw();
}, { passive: false });

function pick(e) {
  const r = cv.getBoundingClientRect(); const n = nearest(e.clientX - r.left, e.clientY - r.top);
  const box = document.getElementById('picked');
  if (!n) { picked = null; joined = new Set(); if (!(e.shiftKey || e.ctrlKey || e.metaKey)) { chosen = []; showChosen(); } box.textContent = 'Nothing picked.'; draw(); return; }
  if (e.shiftKey || e.ctrlKey || e.metaKey) { chosen = chosen.includes(n.r.id) ? chosen.filter(i => i !== n.r.id) : [...chosen, n.r.id]; } else { chosen = [n.r.id]; }
  showChosen();
  picked = n.r.id; joined = new Set([picked]);
  const lines = [];
  for (const end of [n.r.from, n.r.to]) {
    const j = atJunction.get(end); if (!j) continue;
    const others = j.roads.filter(id => id !== picked).map(id => byId.get(id)).filter(Boolean);
    others.forEach(o => joined.add(o.id));
    lines.push('<b>' + (end === n.r.from ? 'Start' : 'End') + '</b> (junction ' + end + ', ' + j.z.toFixed(1) + ' m): ' + (others.length ? others.map(o => describe(o) + ' [' + kind(o) + ']').join('; ') : 'a dead end'));
  }
  box.innerHTML = '<b>' + describe(n.r) + '</b>, ' + kind(n.r) + '<br>' + lines.join('<br>');
  side(n.r);
  draw();
}

// the side view: the picked road straightened out, left to right along it, by height; the ground under it, and every other
// road within 15 m of it at the height it passes (a road over it, under it, beside it), so stacked levels and steps show
function side(r) {
  const box = document.getElementById('side'), sc = document.getElementById('sc');
  box.style.display = 'block';
  document.getElementById('legend').style.display = 'none';
  const rect = sc.getBoundingClientRect(), w = rect.width, h = rect.height, k = window.devicePixelRatio || 1;
  sc.width = w * k; sc.height = h * k;
  const g = sc.getContext('2d'); g.setTransform(k, 0, 0, k, 0, 0); g.clearRect(0, 0, w, h);
  const along = [0];
  for (let i = 1; i < r.xy.length; i++) along.push(along[i - 1] + Math.hypot(r.xy[i][0] - r.xy[i - 1][0], r.xy[i][1] - r.xy[i - 1][1]));
  const total = along[along.length - 1];
  // where a point lies along the road and how far off it
  function place(x, y) {
    let best = null;
    for (let i = 0; i + 1 < r.xy.length; i++) {
      const [ax, ay] = r.xy[i], [bx, by] = r.xy[i + 1], dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
      const t = l2 ? Math.max(0, Math.min(1, ((x - ax) * dx + (y - ay) * dy) / l2)) : 0;
      const d = Math.hypot(x - ax - dx * t, y - ay - dy * t);
      if (!best || d < best.d) best = { d, s: along[i] + t * Math.sqrt(l2) };
    }
    return best;
  }
  const others = [];
  for (const o of D.roads) {
    if (o.id === r.id) continue;
    let run = [];
    for (let i = 0; i < o.xy.length; i++) {
      const p = place(o.xy[i][0], o.xy[i][1]);
      if (p.d < 15 + o.half && p.s > 0.5 && p.s < total - 0.5) run.push([p.s, o.z[i]]);
      else if (run.length) { others.push([o, run]); run = []; }
    }
    if (run.length) others.push([o, run]);
  }
  let lo = Math.min(...r.z, ...r.g), hi = Math.max(...r.z, ...r.g);
  for (const [, run] of others) for (const [, z] of run) { lo = Math.min(lo, z); hi = Math.max(hi, z); }
  lo -= 3; hi += 3;
  const X = s => 40 + (s / Math.max(1, total)) * (w - 60), Y = z => h - 24 - (z - lo) / (hi - lo) * (h - 50);
  const ink = getComputedStyle(document.documentElement).getPropertyValue('--muted'), accent = getComputedStyle(document.documentElement).getPropertyValue('--accent');
  g.fillStyle = ink; g.font = '11px system-ui'; g.strokeStyle = ink; g.globalAlpha = 0.25;
  for (let z = Math.ceil(lo / 5) * 5; z <= hi; z += 5) { g.beginPath(); g.moveTo(40, Y(z)); g.lineTo(w - 20, Y(z)); g.stroke(); }
  g.globalAlpha = 1;
  for (let z = Math.ceil(lo / 10) * 10; z <= hi; z += 10) g.fillText(z + ' m', 4, Y(z) + 4);
  g.fillText('0 m', 40, h - 6); g.fillText(total.toFixed(0) + ' m along', w - 90, h - 6);
  // the ground
  g.strokeStyle = '#8b6b3d'; g.lineWidth = 2; g.beginPath();
  r.g.forEach((z, i) => i ? g.lineTo(X(along[i]), Y(z)) : g.moveTo(X(along[i]), Y(z))); g.stroke();
  // the others, labelled once
  const labelled = [];
  for (const [o, run] of others) {
    g.strokeStyle = colour(run[0][1]); g.lineWidth = 3; g.setLineDash(o.tunnel ? [6, 4] : []);
    g.beginPath(); run.forEach(([s, z], i) => i ? g.lineTo(X(s), Y(z)) : g.moveTo(X(s), Y(z))); g.stroke();
    if (run.length === 1) { g.fillStyle = colour(run[0][1]); g.beginPath(); g.arc(X(run[0][0]), Y(run[0][1]), 4, 0, 7); g.fill(); }
    // (a name once per spot: where several roads meet they would write over each other)
    const lx = X(run[0][0]), ly = Y(run[0][1]);
    if (!labelled.some(([x, y]) => Math.abs(x - lx) < 120 && Math.abs(y - ly) < 14)) {
      labelled.push([lx, ly]);
      g.fillStyle = ink; g.fillText((o.name || 'unnamed') + ' (layer ' + o.layer + ')', lx + 4, ly - 6);
    }
  }
  // the road itself
  g.setLineDash(r.tunnel ? [8, 5] : []); g.strokeStyle = accent; g.lineWidth = 4; g.beginPath();
  r.z.forEach((z, i) => i ? g.lineTo(X(along[i]), Y(z)) : g.moveTo(X(along[i]), Y(z))); g.stroke(); g.setLineDash([]);
  document.getElementById('sideTitle').textContent = 'Side view: ' + describe(r) + ', ' + kind(r) + '. Brown: the ground. Others within 15 m at their heights.';
}
document.getElementById('sideClose').onclick = () => { document.getElementById('side').style.display = 'none'; document.getElementById('legend').style.display = 'block'; };

function showChosen() {
  document.getElementById('chosenList').innerHTML = chosen.length ? chosen.map((id, k) => { const r = byId.get(id); return '#' + (k + 1) + ' road ' + id + ': ' + describe(r) + ', ' + kind(r); }).join('<br>') : 'None yet.';
  document.getElementById('copied').textContent = '';
}
// the text for the chat: everything about the collected roads that is worth knowing, in as few words as it takes
function report() {
  const [lat0, lon0] = D.origin, ky = 111320, kx = 111320 * Math.cos(lat0 * Math.PI / 180);
  const ll = p => (lat0 + p[1] / ky).toFixed(6) + ', ' + (lon0 + p[0] / kx).toFixed(6);
  const out = ['Open-Planet roads picked (' + chosen.length + ')'];
  const note = document.getElementById('note').value.trim();
  if (note) out.push('Note: ' + note);
  const rs = chosen.map(id => byId.get(id)).filter(Boolean);
  rs.forEach((r, k) => {
    let len = 0, steep = 0;
    for (let i = 0; i + 1 < r.xy.length; i++) {
      const l = Math.hypot(r.xy[i + 1][0] - r.xy[i][0], r.xy[i + 1][1] - r.xy[i][1]); len += l;
      if (l > 1) steep = Math.max(steep, Math.abs(r.z[i + 1] - r.z[i]) / l * 100);
    }
    const n = r.xy.length - 1;
    out.push('#' + (k + 1) + ' road ' + r.id + ' ' + describe(r) + ', ' + kind(r) + ', ' + (r.half * 2).toFixed(1) + ' m wide, ' + len.toFixed(0) + ' m long, junctions ' + r.from + ' -> ' + r.to);
    out.push('   start ' + ll(r.xy[0]) + ' road ' + r.z[0].toFixed(1) + ' m (ground ' + r.g[0].toFixed(1) + '); end ' + ll(r.xy[n]) + ' road ' + r.z[n].toFixed(1) + ' m (ground ' + r.g[n].toFixed(1) + '); steepest ' + steep.toFixed(0) + '%');
  });
  // how each pair sit on each other: where they come closest, and the room between them there
  for (let a = 0; a < rs.length; a++) for (let b = a + 1; b < rs.length; b++) {
    let best = null;
    for (let i = 0; i < rs[a].xy.length; i++) {
      for (let j = 0; j + 1 < rs[b].xy.length; j++) {
        const [px, py] = rs[a].xy[i], [ax, ay] = rs[b].xy[j], [bx, by] = rs[b].xy[j + 1], dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        const t = l2 ? Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / l2)) : 0;
        const d = Math.hypot(px - ax - dx * t, py - ay - dy * t);
        if (!best || d < best.d) best = { d, za: rs[a].z[i], zb: rs[b].z[j] + (rs[b].z[j + 1] - rs[b].z[j]) * t, at: rs[a].xy[i] };
      }
    }
    if (best) out.push('#' + (a + 1) + ' and #' + (b + 1) + ' come closest at ' + ll(best.at) + ': ' + best.d.toFixed(1) + ' m apart sideways, #' + (a + 1) + ' at ' + best.za.toFixed(1) + ' m, #' + (b + 1) + ' at ' + best.zb.toFixed(1) + ' m (' + Math.abs(best.za - best.zb).toFixed(1) + ' m between their surfaces)');
  }
  // what the checks found on them
  const ids = new Set(chosen);
  D.problems.filter(p => p.roads.some(i => ids.has(i))).slice(0, 12).forEach(p => out.push('check: ' + p.kind + ': ' + p.text));
  // what was drawn differently: each edited road's new line, from its start, and how far it moved from the map's
  const changed = D.roads.filter(edited);
  if (changed.length) {
    out.push('Edits drawn (' + changed.length + ' roads; each road\'s new line from its start):');
    changed.forEach(r => {
      let most = 0;
      r.xy.forEach(p => { let d = Infinity; for (const q of r.orig) d = Math.min(d, Math.hypot(p[0] - q[0], p[1] - q[1])); most = Math.max(most, d); });
      out.push('  road ' + r.id + ' ' + describe(r) + ', ' + kind(r) + ': ' + r.orig.length + ' -> ' + r.xy.length + ' points, up to ' + most.toFixed(1) + ' m from the map');
      out.push('    ' + r.xy.map(ll).join('; '));
    });
  }
  return out.join('\n');
}
document.getElementById('copy').onclick = () => {
  const text = report(), done = () => { document.getElementById('copied').textContent = chosen.length || D.roads.some(edited) ? 'Copied: paste it in the chat.' : 'Pick roads first (shift-click to add more).'; };
  if (navigator.clipboard && window.isSecureContext) { navigator.clipboard.writeText(text).then(done, () => fallback(text, done)); } else fallback(text, done);
};
function fallback(text, done) {
  const t = document.createElement('textarea'); t.value = text; document.body.appendChild(t); t.select();
  try { document.execCommand('copy'); } catch (e) {} t.remove(); done();
}
document.getElementById('clear').onclick = () => { chosen = []; showChosen(); draw(); };

function goTo(p) {
  flash = p; view.cx = p.at[0]; view.cy = p.at[1]; view.s = Math.max(view.s, 2);
  picked = p.roads[0]; joined = new Set(p.roads);
  document.getElementById('picked').innerHTML = p.roads.map(id => byId.get(id)).filter(Boolean).map(r => '<b>' + describe(r) + '</b>, ' + kind(r)).join('<br>');
  if (byId.get(p.roads[0])) side(byId.get(p.roads[0]));
  draw();
}
function list() {
  const box = document.getElementById('list'); box.innerHTML = '';
  for (const p of shownProblems()) {
    const d = document.createElement('div');
    d.innerHTML = p.text + '<small>' + p.kind + '</small>';
    d.onclick = () => { document.querySelectorAll('#list div').forEach(x => x.classList.remove('sel')); d.classList.add('sel'); goTo(p); };
    box.appendChild(d);
  }
}
const kinds = [...new Set(D.problems.map(p => p.kind))];
kinds.forEach(k => kindsOn.add(k));
const kb = document.getElementById('kinds');
for (const k of kinds) {
  const b = document.createElement('button'); b.className = 'on';
  b.textContent = k + ' (' + D.problems.filter(p => p.kind === k).length + ')';
  b.onclick = () => { kindsOn.has(k) ? kindsOn.delete(k) : kindsOn.add(k); b.classList.toggle('on'); list(); draw(); };
  kb.appendChild(b);
}
['lo', 'hi', 'spread', 'tunnels', 'dots'].forEach(id => document.getElementById(id).addEventListener('input', draw));
window.addEventListener('resize', fit);
list(); fit();
// a link straight to a road: page.html#road=123
const asked = /road=(\d+)/.exec(location.hash);
if (asked && byId.get(+asked[1])) { const r = byId.get(+asked[1]); goTo({ at: r.xy[Math.floor(r.xy.length / 2)], roads: [r.id] }); }
</script>
</body>
</html>
"""

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
