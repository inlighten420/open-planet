# Fetches Rheinland-Pfalz's open data along a race track, for a track pack: the 1 m ground heights (DGM1, 1 km
# tiles) and the 20 cm aerial photos (DOP20 RGB, 2 km tiles, JPEG 2000) within Margin of its line. The data is the
# state survey's (LVermGeo Rheinland-Pfalz, geobasis-rlp.de, Datenlizenz Deutschland - Namensnennung 2.0): credit it.
#   python Tools/trackpack/rlp_fetch.py "Nuerburgring Nordschleife" [out folder]
# The track's line comes from the region's places (Cache/regions/rheinland_pfalz.games.json, geotile games).
import json, math, os, re, sys, urllib.request

NAME = sys.argv[1] if len(sys.argv) > 1 else 'Nürburgring Nordschleife'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'D:/GeodriveData/tracks/' + re.sub(r'[^a-z0-9]+', '_', NAME.lower()).strip('_')
PLACES = os.path.join(os.environ['LOCALAPPDATA'], 'Geodrive', 'Cache', 'regions', 'rheinland_pfalz.games.json')
BASE = 'https://geobasis-rlp.de/data/'
MARGIN = 60.0	# m either side of the line
AGENT = {'User-Agent': 'Geodrive/0.1 (track packs; open source game)'}


def utm32(lat, lon):
	"""WGS84 latitude, longitude to UTM zone 32 north (m): easting, northing"""
	a, f, k0 = 6378137.0, 1 / 298.257223563, 0.9996
	e2 = f * (2 - f)
	ep2 = e2 / (1 - e2)
	phi, lam, lam0 = math.radians(lat), math.radians(lon), math.radians(9.0)
	n = a / math.sqrt(1 - e2 * math.sin(phi) ** 2)
	t = math.tan(phi) ** 2
	c = ep2 * math.cos(phi) ** 2
	aa = math.cos(phi) * (lam - lam0)
	m = a * ((1 - e2 / 4 - 3 * e2 ** 2 / 64 - 5 * e2 ** 3 / 256) * phi - (3 * e2 / 8 + 3 * e2 ** 2 / 32 + 45 * e2 ** 3 / 1024) * math.sin(2 * phi)
		+ (15 * e2 ** 2 / 256 + 45 * e2 ** 3 / 1024) * math.sin(4 * phi) - (35 * e2 ** 3 / 3072) * math.sin(6 * phi))
	x = k0 * n * (aa + (1 - t + c) * aa ** 3 / 6 + (5 - 18 * t + t ** 2 + 72 * c - 58 * ep2) * aa ** 5 / 120) + 500000.0
	y = k0 * (m + n * math.tan(phi) * (aa ** 2 / 2 + (5 - t + 9 * c + 4 * c ** 2) * aa ** 4 / 24 + (61 - 58 * t + t ** 2 + 600 * c - 330 * ep2) * aa ** 6 / 720))
	return x, y


def listing(path):
	"""A folder's files on the server: {(east km, north km): name}"""
	html = urllib.request.urlopen(urllib.request.Request(BASE + path, headers=AGENT), timeout=120).read().decode('utf-8', 'replace')
	out = {}
	for name in re.findall(r'href="([^"]+\.(?:tif|jp2))"', html):
		parts = name.split('_')
		out[(int(parts[2]), int(parts[3]))] = name
	return out


def fetch(path, name, folder):
	os.makedirs(folder, exist_ok=True)
	target = os.path.join(folder, name)
	if os.path.exists(target) and os.path.getsize(target) > 0:
		return os.path.getsize(target), False
	with urllib.request.urlopen(urllib.request.Request(BASE + path + name, headers=AGENT), timeout=600) as r, open(target + '.part', 'wb') as f:
		while True:
			block = r.read(1 << 20)
			if not block:
				break
			f.write(block)
	os.replace(target + '.part', target)
	return os.path.getsize(target), True


def main():
	places = json.load(open(PLACES, encoding='utf-8'))['places']
	track = next((p for p in places if p['kind'] == 'track' and p['name'] == NAME), None)
	if track is None:
		sys.exit('no track called %r in %s' % (NAME, PLACES))
	line = [utm32(lat, lon) for lat, lon in track['course']]
	# the tiles within MARGIN of the line (every 10 m along it, and either side)
	dgm, dop = set(), set()
	for (x0, y0), (x1, y1) in zip(line, line[1:]):
		steps = max(1, int(math.hypot(x1 - x0, y1 - y0) / 10))
		for s in range(steps + 1):
			x, y = x0 + (x1 - x0) * s / steps, y0 + (y1 - y0) * s / steps
			for dx in (-MARGIN, 0, MARGIN):
				for dy in (-MARGIN, 0, MARGIN):
					e, n = (x + dx) / 1000.0, (y + dy) / 1000.0
					dgm.add((int(math.floor(e)), int(math.floor(n))))
					dop.add((2 * int(math.floor(e / 2)), 2 * int(math.floor(n / 2))))
	length = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(line, line[1:]))
	print('%s: %.1f km; %d height tiles (1 km), %d photo tiles (2 km)' % (NAME, length / 1000, len(dgm), len(dop)))
	os.makedirs(OUT, exist_ok=True)
	json.dump({'name': NAME, 'course': track['course'], 'utm32': line, 'source': 'LVermGeo Rheinland-Pfalz (geobasis-rlp.de), dl-de/by-2-0'},
		open(os.path.join(OUT, 'track.json'), 'w', encoding='utf-8'))
	for kind, path, wanted in (('heights', 'dgm1/current/tif/', dgm), ('photos', 'dop20rgb/current/jp2/', dop)):
		files = listing(path)
		total, got, missing = 0, 0, 0
		for key in sorted(wanted):
			if key not in files:
				missing += 1
				continue
			size, new = fetch(path, files[key], os.path.join(OUT, kind))
			total += size
			got += 1 if new else 0
			# (and its world file: where it sits)
			world = files[key].rsplit('.', 1)[0] + ('.tfw' if kind == 'heights' else '.j2w')
			fetch(path, world, os.path.join(OUT, kind))
		print('  %s: %d tiles (%d new, %d not on the server), %.0f MB' % (kind, len(wanted) - missing, got, missing, total / 1e6))
	print('in', OUT)


main()
