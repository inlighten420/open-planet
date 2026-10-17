# The light and the sky: a sun high in a clear afternoon sky, the sky's own colours (Godot's physical sky: blue
# overhead, paler at the horizon, the sun's glow round it), the land lit by both and hazing into the sky with
# distance. The Lighting setting adds what costs the graphics card time:
#   Low     the sun and sky, plain haze
#   Medium  + the haze lit by the sun (brighter towards it), the sky's colour in the distance
#   High    + soft contact shadows (ambient occlusion: corners, kerbs, under the car, where buildings meet the ground)
#   Ultra   + a soft glow round bright things, and finer contact shadows
class_name GameLighting
extends Node

const LEVELS := ["Low", "Medium", "High", "Ultra"]

var environment: Environment
var sun: DirectionalLight3D
var _sky_material: PhysicalSkyMaterial


func _ready() -> void:
	_sky_material = PhysicalSkyMaterial.new()
	_sky_material.rayleigh_coefficient = 1.6
	_sky_material.rayleigh_color = Color(0.2, 0.4, 0.68)
	_sky_material.mie_coefficient = 0.003
	_sky_material.mie_eccentricity = 0.8
	_sky_material.mie_color = Color(0.69, 0.73, 0.81)
	_sky_material.turbidity = 2.5
	_sky_material.sun_disk_scale = 1.5
	_sky_material.ground_color = Color(0.1, 0.09, 0.08)
	_sky_material.energy_multiplier = 2.6
	var sky := Sky.new()
	sky.sky_material = _sky_material
	sky.radiance_size = Sky.RADIANCE_SIZE_128	# (the light the sky gives the land: soft, so small is enough)

	environment = Environment.new()
	environment.background_mode = Environment.BG_SKY
	environment.sky = sky
	environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.ambient_light_sky_contribution = 1.0
	environment.ambient_light_energy = 0.7
	environment.reflected_light_source = Environment.REFLECTION_SOURCE_SKY
	# AgX: bright skies and sunlit ground without the washed-out look, colours kept natural
	environment.tonemap_mode = Environment.TONE_MAPPER_AGX
	environment.tonemap_exposure = 1.0
	environment.adjustment_enabled = true
	environment.adjustment_saturation = 1.15	# (AgX keeps colours natural but a little pale: a touch more)
	environment.fog_enabled = true
	environment.fog_mode = Environment.FOG_MODE_EXPONENTIAL
	environment.fog_light_color = Color(0.62, 0.71, 0.82)
	environment.fog_density = 0.00005
	environment.fog_sky_affect = 0.0	# (the sky has its own haze)
	var node := WorldEnvironment.new()
	node.environment = environment
	add_child(node)

	sun = DirectionalLight3D.new()
	set_place(-34.0)
	sun.light_color = Color(1.0, 0.96, 0.9)
	sun.light_energy = 2.0
	sun.shadow_enabled = true
	sun.shadow_blur = 1.0
	sun.directional_shadow_max_distance = 600.0
	add_child(sun)


# The sun where it is at half past one on an equinox day at this latitude: high in the north in Cape Town (50 degrees
# up), lower in the south in London (35), a little to the west (Godot's axes: x east, y up, -z north)
func set_place(lat: float) -> void:
	var latitude := deg_to_rad(lat)
	var hour := deg_to_rad(22.5)	# (1.5 hours after noon)
	var to_sun := Vector3(-sin(hour), cos(latitude) * cos(hour), sin(latitude) * cos(hour)).normalized()
	sun.transform = Transform3D(Basis.looking_at(-to_sun, Vector3.UP), Vector3.ZERO)


# The Lighting setting (0-3), and the haze for the view distance (m)
func apply(level: int, view_metres: float) -> void:
	level = clampi(level, 0, 3)
	environment.fog_density = 1.5 / view_metres
	# the haze lit by the sun and taking the sky's colour far off (the land fades into the sky, not into grey)
	environment.fog_sun_scatter = 0.25 if level >= 1 else 0.0
	environment.fog_aerial_perspective = 0.6 if level >= 1 else 0.0
	# contact shadows
	environment.ssao_enabled = level >= 2
	environment.ssao_radius = 1.5
	environment.ssao_intensity = 1.6
	environment.ssao_power = 1.5
	environment.ssao_detail = 0.5
	environment.ssao_horizon = 0.06
	environment.ssao_light_affect = 0.1	# (a little in sunlight too, so cars sit on the road)
	RenderingServer.environment_set_ssao_quality(RenderingServer.ENV_SSAO_QUALITY_MEDIUM if level == 2 else RenderingServer.ENV_SSAO_QUALITY_HIGH, true, 0.5, 2, 50.0, 300.0)
	# a soft glow round the brightest things (the sun on paint and glass, the sky near the sun)
	environment.glow_enabled = level >= 3
	environment.glow_intensity = 0.4
	environment.glow_bloom = 0.02
	environment.glow_hdr_threshold = 1.2
