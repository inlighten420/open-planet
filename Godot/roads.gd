# The road surfaces' look (road.gdshader): asphalt, paving (setts, cobbles, paving stones) and gravel (dirt roads),
# real textures (textures/roads, CC0 from ambientCG) over the colours the world gives them. The Textures setting says
# how far the detail goes, as for the ground (ground.gd).
class_name RoadLook

const FOLDER := "res://textures/roads/"
# each surface: its colour (linear, as the world's plain ones were), metres one texture covers, roughness
const SURFACES := {
	"asphalt": [Vector3(0.025, 0.025, 0.028), 3.0, 0.9],
	"paving": [Vector3(0.13, 0.11, 0.09), 2.5, 0.85],
	"gravel": [Vector3(0.25, 0.17, 0.09), 3.0, 0.95],
}

static var materials := {}


# The three materials, made once (the setting changes them in place)
static func make() -> Dictionary:
	if not materials.is_empty():
		return materials
	var shader: Shader = load("res://road.gdshader")
	for kind in SURFACES:
		var material := ShaderMaterial.new()
		material.shader = shader
		var colour: Texture2D = load(FOLDER + kind + "_color.jpg")
		material.set_shader_parameter("base_colour", SURFACES[kind][0])
		material.set_shader_parameter("detail_color", colour)
		material.set_shader_parameter("detail_normal", load(FOLDER + kind + "_normal.jpg"))
		material.set_shader_parameter("detail_mean", GroundLook.mean_colour(colour))
		material.set_shader_parameter("detail_size", SURFACES[kind][1])
		material.set_shader_parameter("roughness", SURFACES[kind][2])
		materials[kind] = material
	return materials


static func apply(level: int) -> void:
	for material: ShaderMaterial in make().values():
		material.set_shader_parameter("detail_far", GroundLook.DETAIL_METRES[clampi(level, 0, 3)])
