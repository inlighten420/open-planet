# The buildings' look (building.gdshader): brick, plaster, concrete, stone and glass walls with windows on every floor,
# flat roofs; textures in textures/buildings (CC0, from ambientCG). Which walls a building gets comes from the world
# (its kind, its own number, the map's building:material), more brick in the UK. The Textures setting says how far the
# detail goes (half again as far as the ground's: buildings are big); the windows go further.
class_name BuildingLook

const FOLDER := "res://textures/buildings/"
const KINDS := ["brick", "plaster", "concrete", "stone", "roof"]

static var material: ShaderMaterial


static func make() -> ShaderMaterial:
	if material:
		return material
	material = ShaderMaterial.new()
	material.shader = load("res://building.gdshader")
	for kind in KINDS:
		var colour: Texture2D = load(FOLDER + kind + "_color.jpg")
		material.set_shader_parameter(kind + "_color", colour)
		material.set_shader_parameter(kind + "_normal", load(FOLDER + kind + "_normal.jpg"))
		if kind in ["plaster", "concrete", "roof"]:
			material.set_shader_parameter(kind + "_mean", GroundLook.mean_colour(colour))
	return material


static func apply(level: int) -> void:
	var detail: float = GroundLook.DETAIL_METRES[clampi(level, 0, 3)] * 1.5
	make().set_shader_parameter("detail_far", detail)
	make().set_shader_parameter("windows_far", maxf(detail, 600.0) * 2.5)


# Where the area is: in the UK most houses and flats are brick, elsewhere (South Africa) most are plastered
static func set_place(in_uk: bool) -> void:
	make().set_shader_parameter("brick_share", 0.65 if in_uk else 0.15)
