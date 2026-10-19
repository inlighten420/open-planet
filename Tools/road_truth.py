"""The truth test: how far the game's roads are from the real ground, where there is LiDAR to say.

    Open-Planet.exe -- --lat=.. --lon=.. --km=2 --truth --dumproads=area.json      (roads solved as if there were no LiDAR)
    python Tools/road_truth.py area.json [more.json ...] [--csv out.csv]

Bare-earth LiDAR is the real road surface (a road is the ground there), so for every road point on measured ground that
isn't a bridge or a tunnel, the error is the solved height minus the LiDAR height. It is told by road class, away from and
near junctions (within 25 m), with the 20 m grade error beside it. Then what no LiDAR can measure but the rules can:
road pieces steeper than their class allows (tunnels included), and junctions that tilt (the roads round them climbing
6% more or less than the real ground in their first 10 m). One line at the end is the score to beat: the 95th percentile error of every
measured point, all classes together.
"""

import json
import math
import sys
from collections import defaultdict

NEAR_JUNCTION = 25.0  # m
GRADE_SPAN = 20.0  # m
# the steepest a road of each class may be (%), as the game's design rules have them roughly; steeper is a fault
MAX_GRADE = {"motorway": 6, "trunk": 8, "primary": 10, "secondary": 12, "tertiary": 14, "residential": 18,
             "unclassified": 18, "living_street": 20, "service": 22, "track": 25}
TILT = 6.0  # % in the first 10 m out of a junction


def metres(points):
    lat0 = points[0][0]
    kx = 111320.0 * math.cos(math.radians(lat0))
    xy = [((p[1] - points[0][1]) * kx, (p[0] - lat0) * 111320.0) for p in points]
    along = [0.0]
    for a, b in zip(xy, xy[1:]):
        along.append(along[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    return xy, along


def stats(values):
    if not values:
        return None
    v = sorted(abs(x) for x in values)
    n = len(v)
    return {"n": n, "mean": sum(v) / n, "median": v[n // 2], "p95": v[min(n - 1, int(n * 0.95))], "worst": v[-1],
            "bias": sum(values) / n}


def base_class(highway):
    return highway.replace("_link", "")


def main(files, csv_path):
    errors = defaultdict(list)  # (class, near) -> [road - lidar]
    grade_errors = defaultdict(list)
    all_errors = []
    steep = defaultdict(int)
    steep_tunnel = 0
    pieces = 0
    tilted = []
    areas = []
    for path in files:
        data = json.load(open(path, encoding="utf-8"))
        roads, junctions = [], []
        for piece in data["pieces"]:
            roads += piece["roads"]
            junctions += piece["junctions"]
        jxy = {}
        lat0 = roads[0]["points"][0][0]
        kx = 111320.0 * math.cos(math.radians(lat0))
        for j in junctions:
            jxy[j["id"]] = (j["lon"] * kx, j["lat"] * 111320.0, len(j["roads"]))
        measured = 0
        area_errors = []
        for r in roads:
            pts = r["points"]
            if len(pts) < 2:
                continue
            xy, along = metres(pts)
            cls = base_class(r["highway"])
            # how far each point is from the nearest junction of 3 or more roads at either end (along the road)
            ends = []
            for node, at in ((r["from"], 0.0), (r["to"], along[-1])):
                if node in jxy and jxy[node][2] >= 3:
                    ends.append(at)
            # steep pieces (5 m or longer, so a node's rounding doesn't count)
            for i in range(len(pts) - 1):
                run = along[i + 1] - along[i]
                if run < 5.0:
                    continue
                pieces += 1
                g = abs(pts[i + 1][2] - pts[i][2]) / run * 100
                if g > MAX_GRADE.get(cls, 20) + 2:
                    steep[cls] += 1
                    if r["tunnel"]:
                        steep_tunnel += 1
            if r["bridge"] or r["tunnel"]:
                continue
            for i, p in enumerate(pts):
                if len(p) < 5 or not p[4]:
                    continue
                measured += 1
                near = any(abs(along[i] - e) < NEAR_JUNCTION for e in ends)
                err = p[2] - p[3]
                errors[(cls, near)].append(err)
                all_errors.append(err)
                area_errors.append(err)
            # the grade over 20 m, road against ground
            for i in range(len(pts)):
                k = i
                while k < len(pts) - 1 and along[k] - along[i] < GRADE_SPAN:
                    k += 1
                if along[k] - along[i] < GRADE_SPAN * 0.75 or not (pts[i][4] and pts[k][4]):
                    continue
                run = along[k] - along[i]
                grade_errors[cls].append(((pts[k][2] - pts[i][2]) - (pts[k][3] - pts[i][3])) / run * 100)
        # tilted junctions: a road's climb in its first 10 m out of a junction, against the real ground's
        byid = {r["id"]: r for r in roads}
        for j in junctions:
            if len(j["roads"]) < 3:
                continue
            worst = 0.0
            for rid in j["roads"]:
                r = byid.get(rid)
                if not r or r["bridge"] or r["tunnel"] or len(r["points"]) < 2:
                    continue
                pts = r["points"] if r["from"] == j["id"] else list(reversed(r["points"]))
                xy, along = metres(pts)
                for i in range(1, len(pts)):
                    if along[i] >= 10.0:
                        # (against the real ground's own climb there, where it is measured: a junction on a hill tilts too)
                        ground = (pts[i][3] - pts[0][3]) if (pts[i][4] and pts[0][4]) else 0.0
                        climb = abs(pts[i][2] - j["height"]) / along[i] * 100
                        # (only where the road itself climbs steeply, and the ground doesn't jump: a bridge the bare-earth
                        # LiDAR has taken away, or a wall, is not the road's fault)
                        if climb > TILT and abs(ground) / along[i] * 100 < 15.0:
                            worst = max(worst, abs((pts[i][2] - j["height"]) - ground) / along[i] * 100)
                        break
            if worst > TILT:
                tilted.append((worst, j["lat"], j["lon"]))
        areas.append((path, len(roads), measured, stats(area_errors)))

    lines = []
    for path, n, m, a in areas:
        lines.append(f"{path}: {n} roads, {m} road points on measured ground" + (f"; 95% within {a['p95']:.2f} m, median {a['median']:.2f} m" if a else ""))
    lines.append("")
    lines.append("height error, road minus LiDAR (m): class, where -> mean / median / 95% / worst, bias (points)")
    rows = []
    for (cls, near), values in sorted(errors.items(), key=lambda kv: (list(MAX_GRADE).index(kv[0][0]) if kv[0][0] in MAX_GRADE else 99, kv[0][1])):
        s = stats(values)
        if s["n"] < 30:
            continue
        where = "junction" if near else "open"
        lines.append(f"  {cls:13} {where:8} {s['mean']:.2f} / {s['median']:.2f} / {s['p95']:.2f} / {s['worst']:.2f}, bias {s['bias']:+.2f} ({s['n']})")
        rows.append(("height", cls, where, s))
    lines.append("")
    lines.append("grade error over 20 m, road minus LiDAR (%): class -> median / 95% (spans)")
    for cls, values in sorted(grade_errors.items(), key=lambda kv: list(MAX_GRADE).index(kv[0]) if kv[0] in MAX_GRADE else 99):
        s = stats(values)
        if s["n"] < 30:
            continue
        lines.append(f"  {cls:13} {s['median']:.1f} / {s['p95']:.1f} ({s['n']})")
        rows.append(("grade", cls, "all", s))
    lines.append("")
    total_steep = sum(steep.values())
    lines.append(f"too steep for their class: {total_steep} of {pieces} pieces ({steep_tunnel} in tunnels): " +
                 ", ".join(f"{c} {n}" for c, n in sorted(steep.items(), key=lambda kv: -kv[1])))
    tilted.sort(reverse=True)
    lines.append(f"tilted junctions (a road climbing {TILT:.0f}% more or less than the ground in its first 10 m): {len(tilted)}" +
                 ("; worst " + ", ".join(f"{t:.0f}% at {la:.5f},{lo:.5f}" for t, la, lo in tilted[:5]) if tilted else ""))
    s = stats(all_errors)
    lines.append("")
    if s:
        lines.append(f"SCORE: 95% of measured road points within {s['p95']:.2f} m of the real ground (median {s['median']:.2f} m, "
                     f"{s['n']} points); {total_steep} too-steep pieces; {len(tilted)} tilted junctions")
    print("\n".join(lines))
    if csv_path:
        with open(csv_path, "w", encoding="utf-8") as f:
            f.write("measure,class,where,mean,median,p95,worst,bias,n\n")
            for measure, cls, where, s in rows:
                f.write(f"{measure},{cls},{where},{s['mean']:.3f},{s['median']:.3f},{s['p95']:.3f},{s['worst']:.3f},{s['bias']:.3f},{s['n']}\n")


if __name__ == "__main__":
    args = sys.argv[1:]
    csv_path = None
    if "--csv" in args:
        i = args.index("--csv")
        csv_path = args[i + 1]
        args = args[:i] + args[i + 2:]
    if not args:
        print(__doc__)
        sys.exit(1)
    main(args, csv_path)
