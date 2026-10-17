# Draws the world map's pictures (the game's map screen): the whole world from Natural Earth (public domain): land and
# sea, ice, towns' built-up areas, lakes, rivers, country and province borders, main roads; at 7 zoom levels, in 256 px
# tiles on a plain latitude/longitude grid (as the map screen draws: it squeezes them sideways by the latitude, as it
# does everything). Only tiles with land in them are kept (the sea is the map's own colour) and packed into one file,
# worldmap.bin, for the game to read a tile at a time:
#   "GDWM", count (int32), then count x (key int32: level << 24 | x << 12 | y, offset int64, length int32), then the
#   tiles (WebP)
# and the places for its labels, worldmap_places.json ([name, lat, lon, min level, population] each).
#   python Tools/make_worldmap.py [Natural Earth GeoJSON folder] [out folder]
import json, math, os, struct, sys, time
from io import BytesIO
from PIL import Image, ImageDraw
from shapely.geometry import shape, box
from shapely.strtree import STRtree

SRC = sys.argv[1] if len(sys.argv) > 1 else 'D:/GeodriveData/naturalearth'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'D:/GeodriveData/worldmap'
LEVELS = 7		# 0-6: 1.4 to 91 px a degree
TILE = 256
SS = 2			# (drawn twice the size, then shrunk: smooth edges)
BLOCK = 8		# tiles a side drawn at once

SEA = (166, 198, 222)
LAND = (238, 235, 226)
ICE = (250, 251, 252)
TOWN = (224, 214, 202)
RIVER = (136, 178, 214)
BORDER = (142, 118, 150)
PROVINCE = (196, 184, 200)
ROAD = (214, 132, 52)
MINOR_ROAD = (222, 176, 112)


def load(name, keep=None):
    path = os.path.join(SRC, name + '.geojson')
    data = json.load(open(path, encoding='utf-8'))
    out = []
    for f in data['features']:
        if f.get('geometry') is None:
            continue
        p = f.get('properties') or {}
        if keep and not keep(p):
            continue
        out.append((shape(f['geometry']), p))
    print('  %s: %d' % (name, len(out)))
    return out


def tree(items):
    return STRtree([g for g, _ in items]), items


def polys(g):
    if g.geom_type == 'Polygon':
        return [g]
    if g.geom_type == 'MultiPolygon':
        return list(g.geoms)
    if g.geom_type == 'GeometryCollection':
        return [x for x in g.geoms if x.geom_type == 'Polygon']
    return []


def lines(g):
    if g.geom_type == 'LineString':
        return [g]
    if g.geom_type == 'MultiLineString':
        return list(g.geoms)
    return []


def main():
    os.makedirs(OUT, exist_ok=True)
    print('reading Natural Earth')
    land = {0: tree(load('ne_110m_land')), 2: tree(load('ne_50m_land')), 4: tree(load('ne_10m_land') + load('ne_10m_minor_islands'))}
    ice = tree(load('ne_10m_glaciated_areas'))
    towns = tree(load('ne_10m_urban_areas'))
    lakes = tree(load('ne_10m_lakes'))
    rivers = tree(load('ne_10m_rivers_lake_centerlines'))
    borders = tree(load('ne_10m_admin_0_boundary_lines_land'))
    provinces = tree(load('ne_10m_admin_1_states_provinces_lines'))
    roads = tree(load('ne_10m_roads', lambda p: p.get('type') in ('Major Highway', 'Secondary Highway', 'Road', 'Beltway', 'Bypass', 'Track')))

    entries = []
    blob = BytesIO()
    started = time.time()
    for level in range(LEVELS):
        cols, rows = 2 ** (level + 1), 2 ** level
        ppd = TILE * cols / 360.0	# px a degree
        kept = 0
        land_tree = land[max(k for k in land if k <= level)]
        for by in range(0, rows, BLOCK):
            for bx in range(0, cols, BLOCK):
                nx, ny = min(BLOCK, cols - bx), min(BLOCK, rows - by)
                west = -180.0 + bx * TILE / ppd
                north = 90.0 - by * TILE / ppd
                east = west + nx * TILE / ppd
                south = north - ny * TILE / ppd
                area = box(west, south, east, north)
                w, h = nx * TILE * SS, ny * TILE * SS
                scale = ppd * SS

                def xy(coords):
                    return [((x - west) * scale, (north - y) * scale) for x, y in coords]

                def fill(tr, colour, draw, test=None):
                    t, items = tr
                    hits = t.query(area)
                    for i in hits:
                        g, p = items[i]
                        if test and not test(p):
                            continue
                        for poly in polys(g):
                            draw.polygon(xy(poly.exterior.coords), fill=colour)
                            for hole in poly.interiors:
                                draw.polygon(xy(hole.coords), fill=SEA if colour == LAND else LAND)
                    return len(hits)

                def stroke(tr, colour, width, draw, test=None):
                    t, items = tr
                    for i in t.query(area):
                        g, p = items[i]
                        if test and not test(p):
                            continue
                        for line in lines(g):
                            draw.line(xy(line.coords), fill=colour, width=max(1, int(round(width * SS))), joint='curve')

                image = Image.new('RGB', (w, h), SEA)
                draw = ImageDraw.Draw(image)
                if fill(land_tree, LAND, draw) == 0:
                    continue	# (all sea)
                if level >= 2:
                    fill(ice, ICE, draw)
                if level >= 4:
                    fill(towns, TOWN, draw)
                if level >= 2:
                    fill(lakes, SEA, draw, lambda p: (p.get('scalerank') or 9) <= level + 2)
                if level >= 3:
                    stroke(rivers, RIVER, 0.6 + 0.25 * (level - 3), draw, lambda p: (p.get('scalerank') or 9) <= level + 3)
                if level >= 4:
                    stroke(provinces, PROVINCE, 0.6, draw)
                if level >= 4:
                    stroke(roads, MINOR_ROAD, 0.9 + 0.2 * (level - 4), draw, lambda p: p.get('type') != 'Major Highway' and (p.get('scalerank') or 9) <= level + 2)
                    stroke(roads, ROAD, 1.3 + 0.4 * (level - 4), draw, lambda p: p.get('type') == 'Major Highway')
                stroke(borders, BORDER, 0.6 + 0.2 * max(0, level - 2), draw)
                image = image.resize((nx * TILE, ny * TILE), Image.LANCZOS)
                for ty in range(ny):
                    for tx in range(nx):
                        tile = image.crop((tx * TILE, ty * TILE, (tx + 1) * TILE, (ty + 1) * TILE))
                        lo, hi = tile.getextrema()[0], tile.getextrema()[2]
                        if tile.getcolors(1) is not None and tile.getpixel((0, 0)) == SEA:
                            continue	# (sea only)
                        out = BytesIO()
                        tile.save(out, 'WEBP', quality=82, method=4)
                        data = out.getvalue()
                        key = (level << 24) | ((bx + tx) << 12) | (by + ty)
                        entries.append((key, blob.tell(), len(data)))
                        blob.write(data)
                        kept += 1
        print('level %d: %d x %d tiles, %d with land, %.1f px a degree; %.0f s' % (level, cols, rows, kept, ppd, time.time() - started))

    path = os.path.join(OUT, 'worldmap.bin')
    with open(path, 'wb') as f:
        head = 4 + 4 + len(entries) * 16
        f.write(b'GDWM')
        f.write(struct.pack('<i', len(entries)))
        for key, offset, length in entries:
            f.write(struct.pack('<iqi', key, head + offset, length))
        f.write(blob.getvalue())
    print('%s: %d tiles, %.1f MB' % (path, len(entries), os.path.getsize(path) / 1e6))

    # the places for the labels: the bigger the place, the farther out it shows
    places = []
    for g, p in load('ne_10m_populated_places_simple'):
        rank = p.get('scalerank', 10) or 10
        places.append([p.get('name') or '', round(g.y, 4), round(g.x, 4), min(int(rank), 10), int(p.get('pop_max') or 0), 1 if p.get('adm0cap') else 0])
    places.sort(key=lambda r: (r[3], -r[4]))
    json.dump(places, open(os.path.join(OUT, 'worldmap_places.json'), 'w', encoding='utf-8'), ensure_ascii=False, separators=(',', ':'))
    print('places: %d' % len(places))


if __name__ == '__main__':
    main()
