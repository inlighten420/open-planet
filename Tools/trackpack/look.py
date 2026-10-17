# A look at a track pack's aerial photos: 80 m squares round points along its line (every Every m), the line drawn on
# them, in one sheet: is the tarmac's edge clear, are its kerbs there to find?
#   python Tools/trackpack/look.py <track folder> <out.png> [every m] [count]
import glob, json, math, os, sys
from PIL import Image, ImageDraw

FOLDER = sys.argv[1]
OUT = sys.argv[2]
EVERY = float(sys.argv[3]) if len(sys.argv) > 3 else 1000.0
COUNT = int(sys.argv[4]) if len(sys.argv) > 4 else 20
HALF = 40.0	# m: half a square's side
PIXEL = 0.2	# m: the photos' pixel (DOP20)
SIDE = int(2 * HALF / PIXEL)	# px

Image.MAX_IMAGE_PIXELS = None
track = json.load(open(os.path.join(FOLDER, 'track.json'), encoding='utf-8'))
line = track['utm32']

# the photo tiles: where each sits (its world file: the top left pixel's middle, the pixel size)
tiles = []
for path in glob.glob(os.path.join(FOLDER, 'photos', '*.jp2')):
	w = [float(v) for v in open(path[:-4] + '.j2w').read().split()]
	tiles.append({'path': path, 'size': w[0], 'left': w[4] - w[0] / 2, 'top': w[5] - w[3] / 2, 'image': None})


def tile_at(x, y):
	for t in tiles:
		if t['left'] <= x < t['left'] + 2000 and t['top'] - 2000 < y <= t['top']:
			return t
	return None


def crop(x, y):
	t = tile_at(x, y)
	if t is None:
		return None
	if t['image'] is None:
		t['image'] = Image.open(t['path'])
		t['image'].load()
	px, py = (x - HALF - t['left']) / t['size'], (t['top'] - (y + HALF)) / t['size']
	return t['image'].crop((int(px), int(py), int(px) + SIDE, int(py) + SIDE)), (x - HALF, y + HALF)


# points every EVERY m along the line
points, along, next_at = [], 0.0, 0.0
for (x0, y0), (x1, y1) in zip(line, line[1:]):
	seg = math.hypot(x1 - x0, y1 - y0)
	while next_at <= along + seg and len(points) < COUNT:
		t = (next_at - along) / seg if seg > 0 else 0
		points.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, next_at))
		next_at += EVERY
	along += seg

cols = 5
rows = (len(points) + cols - 1) // cols
sheet = Image.new('RGB', (cols * SIDE, rows * SIDE), (40, 40, 40))
for i, (x, y, at) in enumerate(points):
	got = crop(x, y)
	if got is None:
		continue
	image, (left, top) = got
	image = image.convert('RGB')
	draw = ImageDraw.Draw(image)
	pix = [((lx - left) / PIXEL, (top - ly) / PIXEL) for lx, ly in line if abs(lx - x) < HALF * 1.5 and abs(ly - y) < HALF * 1.5]
	if len(pix) > 1:
		draw.line(pix, fill=(0, 200, 255), width=2)
	draw.text((6, 6), '%.1f km' % (at / 1000), fill=(255, 255, 0))
	sheet.paste(image, ((i % cols) * SIDE, (i // cols) * SIDE))
sheet = sheet.resize((sheet.width // 2, sheet.height // 2))
sheet.save(OUT)
print('%d squares in %s' % (len(points), OUT))
