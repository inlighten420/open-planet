"""How close the game's roads come to the road edits drawn in its editor (Tools/road_edits): the edit truth test.

    Open-Planet.exe -- --lat=43.7392 --lon=7.4277 --km=3 --dumproads=roads.json
    python Tools/edit_score.py roads.json [Tools/road_edits/*.txt]

Each edited road is found again among the game's roads by its name and class (the ids change from build to build), and
every point of the drawn line is measured to the nearest of them: sideways (m) and in height (m). The drawing is a
guideline by hand, so the numbers say "closer" or "further", not right or wrong.
"""

import glob
import json
import math
import os
import re
import sys


def load_edits(paths):
    edits = []
    for path in paths:
        current = None
        for line in open(path, encoding="utf-8"):
            if line.startswith("#"):
                continue
            head = re.match(r"road (\d+) (.+?) \(([a-z_]+)\), layer (-?\d+)", line)
            if head:
                name = head.group(2)
                current = {"file": os.path.basename(path), "id": int(head.group(1)), "name": "" if name == "unnamed road" else name,
                           "highway": head.group(3), "layer": int(head.group(4)), "points": []}
                continue
            if current and line.startswith("    "):
                for part in line.strip().split(";"):
                    lat, lon, h = (float(v) for v in part.split(","))
                    current["points"].append((lat, lon, h))
                # a later drawing of the same road (same name, class and first point's place) replaces an earlier one
                edits[:] = [e for e in edits if not (e["name"] == current["name"] and e["highway"] == current["highway"]
                                                    and [q[:2] for q in e["points"][:1]] == [q[:2] for q in current["points"][:1]])]
                edits.append(current)
                current = None
    return edits


def main(dump, paths):
    data = json.load(open(dump, encoding="utf-8"))
    roads = [r for p in data["pieces"] for r in p["roads"]]
    edits = load_edits(paths)
    lat0 = sum(p[0] for e in edits for p in e["points"]) / max(1, sum(len(e["points"]) for e in edits))
    kx, ky = 111320.0 * math.cos(math.radians(lat0)), 111320.0

    def xy(lat, lon):
        return (lon * kx, lat * ky)

    total_side = total_up = 0.0
    count = 0
    for e in edits:
        same = [r for r in roads if (r["name"] or "") == e["name"] and r["highway"] == e["highway"]]
        if not same:
            print(f"  {e['name'] or 'unnamed'} ({e['highway']}): not found in the game's roads")
            continue
        segs = []
        for r in same:
            pts = [(xy(p[0], p[1]), p[2]) for p in r["points"]]
            segs += [(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
        sides, ups = [], []
        for lat, lon, h in e["points"]:
            q = xy(lat, lon)
            best = None
            for (a, za), (b, zb) in segs:
                dx, dy = b[0] - a[0], b[1] - a[1]
                l2 = dx * dx + dy * dy
                t = max(0.0, min(1.0, ((q[0] - a[0]) * dx + (q[1] - a[1]) * dy) / l2)) if l2 > 1e-6 else 0.0
                d = math.hypot(q[0] - a[0] - dx * t, q[1] - a[1] - dy * t)
                if best is None or d < best[0]:
                    best = (d, za + (zb - za) * t)
            sides.append(best[0])
            ups.append(best[1] - h)
        mean_side = sum(sides) / len(sides)
        mean_up = sum(abs(u) for u in ups) / len(ups)
        total_side += sum(sides)
        total_up += sum(abs(u) for u in ups)
        count += len(sides)
        print(f"  {e['name'] or 'unnamed'} ({e['highway']}, {e['file']}): {len(sides)} points, sideways mean {mean_side:.1f} m (most {max(sides):.1f}),"
              f" height mean {mean_up:.1f} m (game {'higher' if sum(ups) > 0 else 'lower'} on the whole)")
    if count:
        print(f"EDIT SCORE: the game's roads are {total_side / count:.1f} m sideways and {total_up / count:.1f} m in height from the drawn lines, on average ({count} points)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    files = sys.argv[2:] or glob.glob(os.path.join(os.path.dirname(os.path.abspath(__file__)), "road_edits", "*.txt"))
    main(sys.argv[1], files)
