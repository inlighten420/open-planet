# A track's width from its aerial photos (a track pack's: DOP20, 20 cm a pixel), measured by the computer, nothing
# looked at by hand: every Step m along its line, the pixels across it out to Reach m each side, each one tarmac (grey,
# no colour to it), a line or kerb (white, red), grass (green) or gravel (beige); from the line out, the track goes on
# till a metre of grass or gravel (a shadow or a patch in between doesn't end it). Out: widths.csv (along m, east,
# north, left m, right m) and the numbers: how much of it was measured, its typical width, its spread.
#   python Tools/trackpack/width.py <track folder> [step m]
import csv, glob, math, os, sys
import numpy
from PIL import Image

FOLDER = sys.argv[1]
STEP = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
REACH = 15.0	# m each side
PIXEL = 0.2	# m
END_RUN = 1.0	# m of grass or gravel: the track's edge
Image.MAX_IMAGE_PIXELS = None

track = __import__('json').load(open(os.path.join(FOLDER, 'track.json'), encoding='utf-8'))
line = track['utm32']

# the photo tiles (those downloaded so far)
tiles = []
for path in sorted(glob.glob(os.path.join(FOLDER, 'photos', '*.jp2'))):
	w = [float(v) for v in open(path[:-4] + '.j2w').read().split()]
	tiles.append({'path': path, 'size': w[0], 'left': w[4] - w[0] / 2, 'top': w[5] - w[3] / 2, 'pixels': None})
print('%d photo tiles' % len(tiles))


def tile_at(x, y):
	for t in tiles:
		if t['left'] <= x < t['left'] + 2000 and t['top'] - 2000 < y <= t['top']:
			return t
	return None


def classify(rgb):
	"""rgb: (n, 3) floats -> 0 tarmac or line or kerb (track), 1 grass or gravel (not), 2 unsure (deep shadow)"""
	r, g, b = rgb[:, 0], rgb[:, 1], rgb[:, 2]
	bright = rgb.mean(axis=1)
	spread = rgb.max(axis=1) - rgb.min(axis=1)
	kind = numpy.full(len(rgb), 1, dtype=numpy.int8)
	grey = spread < 28	# tarmac, concrete, white lines
	red = (r > g + 50) & (r > b + 50)	# a kerb's red
	green = (g > r + 6) & (g > b + 4)
	kind[grey | red] = 0
	kind[green] = 1
	kind[bright < 35] = 2
	return kind


samples = []
along = 0.0
next_at = 0.0
for (x0, y0), (x1, y1) in zip(line, line[1:]):
	seg = math.hypot(x1 - x0, y1 - y0)
	if seg <= 0:
		continue
	dx, dy = (x1 - x0) / seg, (y1 - y0) / seg
	while next_at <= along + seg:
		t = next_at - along
		samples.append((next_at, x0 + dx * t, y0 + dy * t, dx, dy))
		next_at += STEP
	along += seg
print('%d places along %.1f km' % (len(samples), along / 1000))

offsets = numpy.arange(-REACH, REACH + PIXEL / 2, PIXEL)
rows = []
for at, x, y, dx, dy in samples:
	t = tile_at(x, y)
	if t is None:
		continue
	if t['pixels'] is None:
		image = Image.open(t['path']).convert('RGB')
		t['pixels'] = numpy.asarray(image, dtype=numpy.float32)
		print('  read %s' % os.path.basename(t['path']))
	# across the line (its left: -dy, dx)
	px = (x - dy * offsets - t['left']) / t['size']
	py = (t['top'] - (y + dx * offsets)) / t['size']
	ix, iy = px.astype(int), py.astype(int)
	inside = (ix >= 0) & (iy >= 0) & (ix < t['pixels'].shape[1]) & (iy < t['pixels'].shape[0])
	if not inside.all():
		continue
	kind = classify(t['pixels'][iy, ix])
	middle = len(offsets) // 2
	if kind[middle - 2:middle + 3].tolist().count(0) < 3:
		continue	# (the line isn't on tarmac here: under trees, a bridge, the line off the track)
	widths = []
	for direction in (1, -1):
		edge, run = 0, 0
		i = middle
		while 0 <= i + direction < len(offsets):
			i += direction
			if kind[i] == 1:
				run += 1
				if run * PIXEL >= END_RUN:
					break
			else:
				run = 0
				if kind[i] == 0:
					edge = abs(i - middle)
		widths.append(edge * PIXEL if edge * PIXEL < REACH - 0.5 else float('nan'))
	rows.append((at, x, y, widths[0], widths[1]))

with open(os.path.join(FOLDER, 'widths.csv'), 'w', newline='') as f:
	out = csv.writer(f)
	out.writerow(['along_m', 'east', 'north', 'left_m', 'right_m'])
	for r in rows:
		out.writerow(['%.1f' % r[0], '%.1f' % r[1], '%.1f' % r[2], '%.2f' % r[3], '%.2f' % r[4]])

total = numpy.array([r[3] + r[4] for r in rows if not math.isnan(r[3]) and not math.isnan(r[4])])
print('measured at %d of %d places (%.0f%%)' % (len(total), len(samples), 100.0 * len(total) / max(1, len(samples))))
if len(total):
	print('width: typical %.1f m; middle half %.1f to %.1f m; 10%% under %.1f, 10%% over %.1f' % (
		numpy.median(total), numpy.percentile(total, 25), numpy.percentile(total, 75), numpy.percentile(total, 10), numpy.percentile(total, 90)))
	# by kilometre: how it changes round the lap
	by_km = {}
	for r in rows:
		if not math.isnan(r[3]) and not math.isnan(r[4]):
			by_km.setdefault(int(r[0] // 1000), []).append(r[3] + r[4])
	print('by km: ' + ', '.join('%d: %.1f' % (k, numpy.median(v)) for k, v in sorted(by_km.items())))
