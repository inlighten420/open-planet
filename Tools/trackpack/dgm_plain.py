# A state survey's 1 m heights tiles (compressed GeoTIFFs) as plain ones, 32-bit floats uncompressed, for geotile dgm
# (its reader takes those only); their world files beside them.
#   python Tools/trackpack/dgm_plain.py <folder of heights .tif> <out folder>
import glob, os, shutil, sys
import numpy
from PIL import Image

IN, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)
done = 0
for path in sorted(glob.glob(os.path.join(IN, '*.tif'))):
	image = Image.open(path)
	heights = numpy.asarray(image, dtype=numpy.float32)
	heights = numpy.where(heights < -1000, numpy.nan, heights).astype(numpy.float32)	# (no data: none)
	name = os.path.basename(path)
	Image.fromarray(heights, mode='F').save(os.path.join(OUT, name), compression=None)
	shutil.copy(path[:-4] + '.tfw', os.path.join(OUT, name[:-4] + '.tfw'))
	done += 1
print('%d tiles made plain in %s (%s: %.1f to %.1f m)' % (done, OUT, image.size, float(numpy.nanmin(heights)), float(numpy.nanmax(heights))))
