# The ground's look (ground.gdshader): grass, sand and rock textures (textures/ground, CC0 from ambientCG) over the
# colours the world gives the ground. The Textures setting says how far the detail goes:
#   Low     none (the plain colours, still varied by the noise)
#   Medium  250 m      High  400 m      Ultra  600 m   (and a coarse grain 4 times as far)
# The soft noise that varies the land (bush/pasture/dry grass, lighter/darker, bare earth and rock on slopes) is made
# here: 256 x 256, one channel, about 85 KB on the graphics card.
class_name GroundLook

const DETAIL_METRES := [0.0, 250.0, 400.0, 600.0]
const FOLDER := "res://textures/ground/"

static var material: ShaderMaterial


# The material (made once; the setting changes it in place, so the ground already built changes too)
static func make() -> ShaderMaterial:
	if material:
		return material
	material = ShaderMaterial.new()
	material.shader = load("res://ground.gdshader")
	for kind in ["grass", "sand", "rock"]:
		var colour: Texture2D = load(FOLDER + kind + "_color.jpg")
		material.set_shader_parameter(kind + "_color", colour)
		material.set_shader_parameter(kind + "_normal", load(FOLDER + kind + "_normal.jpg"))
		material.set_shader_parameter(kind + "_mean", mean_colour(colour))
	material.set_shader_parameter("noise", _noise())
	return material


static func apply(level: int) -> void:
	make().set_shader_parameter("detail_far", DETAIL_METRES[clampi(level, 0, 3)])


# Soft noise that wraps round (the same every time: a fixed seed), stretched to fill 0-1
static func _noise() -> ImageTexture:
	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.seed = 7
	noise.frequency = 1.0 / 48.0
	noise.fractal_octaves = 3
	var image := noise.get_seamless_image(256, 256, false, false, 0.1, true)
	image.generate_mipmaps()
	return ImageTexture.create_from_image(image)


# A texture's average colour, in linear light (as the shader reads the texture)
static func mean_colour(texture: Texture2D) -> Vector3:
	var image := texture.get_image()
	if image == null:
		return Vector3(0.2, 0.2, 0.2)
	if image.is_compressed():
		image.decompress()
	image.clear_mipmaps()
	while image.get_width() > 64 or image.get_height() > 64:
		# halving with bilinear is a 2x2 average (in sRGB: close enough this far)
		image.resize(maxi(image.get_width() / 2, 1), maxi(image.get_height() / 2, 1), Image.INTERPOLATE_BILINEAR)
	var total := Vector3.ZERO
	for y in image.get_height():
		for x in image.get_width():
			var pixel := image.get_pixel(x, y).srgb_to_linear()
			total += Vector3(pixel.r, pixel.g, pixel.b)
	return total / float(image.get_width() * image.get_height())
