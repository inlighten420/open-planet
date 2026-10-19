# Fetches France's 1 m ground heights (IGN RGE ALTI) round a place, for a height pack: 1 km squares on the UTM 32 grid
# (the one the game's fine heights use, as Germany's), asked for from IGN's map service already in that grid, as plain
# 32-bit float GeoTIFFs with their world files: what geotile dgm reads. IGN's data covers Monaco too.
# The data is IGN's (Geoplateforme, RGE ALTI, Licence Ouverte Etalab 2.0): credit it.
#   python Tools/trackpack/ign_fetch.py <name> <lat> <lon> <km> [out folder]
#   geotile dgm <Cache/heights/<name>.pmtiles> <out folder> --cell=1 --credit="Altimetrie: (c) IGN RGE ALTI, Licence Ouverte Etalab 2.0"
import math, os, re, struct, sys, time, urllib.request
import numpy
from PIL import Image

NAME, LAT, LON, KM = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
OUT = sys.argv[5] if len(sys.argv) > 5 else 'D:/GeodriveData/heights/' + re.sub(r'[^a-z0-9]+', '_', NAME.lower()).strip('_')
WMS = 'https://data.geopf.fr/wms-r/wms'
LAYER = 'ELEVATION.ELEVATIONGRIDCOVERAGE.HIGHRES'	# RGE ALTI 1 m
AGENT = {'User-Agent': 'Geodrive/0.1 (height packs; open source game)'}
SEA = -10.0	# m: lower than this is the sea (no data), or a sea-bed value from another source: left out


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


def square(e, n, folder):
	"""One 1 km square (its south-west corner in m): fetched, written; False if it is all sea"""
	name = 'ign_%d_%d' % (e // 1000, n // 1000)
	target = os.path.join(folder, name + '.tif')
	if os.path.exists(target):
		return True
	url = (WMS + '?SERVICE=WMS&VERSION=1.3.0&REQUEST=GetMap&LAYERS=' + LAYER + '&STYLES=&CRS=EPSG:32632'
		+ '&BBOX=%d,%d,%d,%d&WIDTH=1000&HEIGHT=1000&FORMAT=image/x-bil;bits=32' % (e, n, e + 1000, n + 1000))
	for attempt in range(4):
		try:
			data = urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=180).read()
			if len(data) == 4000000:
				break
		except Exception as error:
			print('  %s: %s, trying again' % (name, error))
		time.sleep(5 * (attempt + 1))
	else:
		raise RuntimeError('%s: no heights from IGN' % name)
	heights = numpy.frombuffer(data, dtype='<f4').reshape(1000, 1000)
	heights = numpy.where(heights < SEA, numpy.nan, heights).astype(numpy.float32)
	if numpy.isnan(heights).all():
		return False
	Image.fromarray(heights, mode='F').save(target, compression=None)
	# the world file: a pixel's size, no turn, the top left pixel's middle
	with open(os.path.join(folder, name + '.tfw'), 'w') as f:
		f.write('1.0\n0.0\n0.0\n-1.0\n%.1f\n%.1f\n' % (e + 0.5, n + 1000 - 0.5))
	return True


def main():
	os.makedirs(OUT, exist_ok=True)
	# the squares covering a KM wide square round the place
	half = KM * 1000 / 2
	corners = [utm32(LAT + dy / 111320.0, LON + dx / (111320.0 * math.cos(math.radians(LAT)))) for dx in (-half, half) for dy in (-half, half)]
	e0, e1 = int(min(c[0] for c in corners) // 1000), int(max(c[0] for c in corners) // 1000)
	n0, n1 = int(min(c[1] for c in corners) // 1000), int(max(c[1] for c in corners) // 1000)
	total = (e1 - e0 + 1) * (n1 - n0 + 1)
	print('%s: %d squares of 1 km (UTM 32 %d-%d E, %d-%d N) into %s' % (NAME, total, e0, e1, n0, n1, OUT))
	land = done = 0
	for n in range(n0, n1 + 1):
		for e in range(e0, e1 + 1):
			land += 1 if square(e * 1000, n * 1000, OUT) else 0
			done += 1
			print('  %d of %d (%d with land)' % (done, total, land), flush=True)
	print('done: %d squares with land' % land)


main()
