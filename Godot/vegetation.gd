# Plants round the camera (vegetation_placed.gdshader): rings of copies of plants made in Core
# (GeodriveWorld.make_plant_mesh), each ring's plants worked out on a worker from the world's plant map (where trees,
# shrubs and grass grow round the camera, from the satellite's land cover: GeodriveWorld.place_plants), only those that
# grow sent to the graphics card, in tiles so those out of view aren't drawn, and placed again as the camera moves (the
# near rings every few metres, the far every 25). (They had been placed by the graphics card square by square, every
# square sending its plant's every corner whether one grew there or not: ~3.5 ms a frame on Ultra; now ~0.2.) Beyond
# the trees the woods are one lumpy blanket at the treetops (canopy.gdshader). The Vegetation setting:
#            full trees   every tree (the far one past 300 m)   thinned   bushes   (High, Ultra: trees and near bushes
#                                                                                   cast shadows as far as the sun's do)
#   1 Low      100 m        300 m                                   -        60 m
#   2 Medium   140 m        450 m                                 900 m      80 m
#   3 High     150 m        650 m                                1400 m     100 m     yes
#   4 Ultra    110 m        650 m                                1800 m     140 m     yes
# Past the near trees a single broadleaf (lighter, then a ball on a stick, ~26 triangles) stands for every kind, the
# shader bending it into a cone or a gum.
class_name VegetationLook
extends Node3D

const NAMES := ["Off", "Low", "Medium", "High", "Ultra"]
const FINE := 7.0	# m: the near trees' square
const LIGHT := 300.0	# m: past this, every tree is the far one
const BUSH := 4.5	# m: the bushes' square
const NEAR_BLANKET := 1950.0	# m: the near blanket's edge (the near plant map reaches ~2 km round the camera)
const FAR_BLANKET := 9600.0	# m: the far one's (the far map ~9-10 km)
const KINDS := 3	# tree kinds (broadleaf, conifer, eucalyptus); 3 is the bush

var world: Node
var level := 3
var shadow_reach := 600.0	# m: how far the sun's shadows go (the Shadows setting): the plants that near cast them
var _rings: Array[GeometryInstance3D] = []
var _near_sheet: MeshInstance3D	# (the near blanket: from under the camera when high)
var _horizon_sheet: MeshInstance3D	# (the horizon's blanket: only when high, flying: driving, the land that far is hidden
	# behind hills and towns anyway, and it costs the graphics card ~0.7 ms)
var _horizon_shown := 0.0	# how far it's faded in (0-1)
const HORIZON_FADE := 2.0	# s to fade in or out
var _inner := 0.0	# its inner edge, low down
var high := false	# the camera high over the ground: no single plants, the blanket from right under it
const HIGH := 350.0	# m over the ground: the single plants go...
const LOW := 280.0	# ...and come back
# the rings, each placed on the processor (GeodriveWorld.place_plants: only the plants that grow there are drawn); each:
# {index, kind, channel, cell, fine_cell, inner, outer, grow, tile, every, shadows, mesh, material, eye (where it was
# last placed round), nodes {tile: MultiMeshInstance3D}}
var _specs: Array = []
var _placed: Node3D	# (their tiles' nodes)
var _placed_map: Texture2D	# (the plant map they were placed from: a new one, placed again)
var _region := -1.0
var _horizon_reach := 0.0	# m: how far the horizon's blanket was made to reach (the view distance)
static var _globals_added := false	# (the shaders' shared values, added once a run)


func _ready() -> void:
	# the shaders' shared values, set here every frame
	for global in [["plant_map", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null], ["plant_heights", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null],
			["plant_origin", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO], ["plant_size", RenderingServer.GLOBAL_VAR_TYPE_FLOAT, 5120.0],
			["plant_offset", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO], ["plant_eye", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO],
			["plant_region", RenderingServer.GLOBAL_VAR_TYPE_FLOAT, 0.0],
			["plant_map_far", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null], ["plant_heights_far", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null],
			["plant_origin_far", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO], ["plant_size_far", RenderingServer.GLOBAL_VAR_TYPE_FLOAT, 10240.0],
			["plant_map_horizon", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null], ["plant_heights_horizon", RenderingServer.GLOBAL_VAR_TYPE_SAMPLER2D, null],
			["plant_origin_horizon", RenderingServer.GLOBAL_VAR_TYPE_VEC3, Vector3.ZERO], ["plant_size_horizon", RenderingServer.GLOBAL_VAR_TYPE_FLOAT, 128000.0]]:
		if not _globals_added:
			RenderingServer.global_shader_parameter_add(global[0], global[1], global[2])
	_globals_added = true
	_build()


func set_shadow_reach(metres: float) -> void:
	if metres != shadow_reach:
		shadow_reach = metres
		if is_inside_tree():
			_build()


# Inside a game farm: its bushes drawn far out too (thinned, to WIDE_BUSHES m), so the whole farm's seen as it is (an
# animal 1 km off stands among the bushes it really stands among). The ring's added last and taken off again (the
# rest kept: no plants vanishing and coming back)
const WIDE_BUSHES := 1500.0	# m
var wide := false

func set_wide(on: bool) -> void:
	if on == wide:
		return
	wide = on
	if not is_inside_tree() or level <= 0 or _specs.is_empty():
		return
	if on:
		_wide_ring()
	elif _specs.back().get("wide", false):
		var spec: Dictionary = _specs.pop_back()
		for node in spec["nodes"].values():
			node.queue_free()


func _wide_ring() -> void:
	var bushes: float = [0.0, 60.0, 80.0, 100.0, 140.0][level]
	_ring(3, 0, BUSH * 4.0, bushes * 3.0 if level >= 3 else bushes, WIDE_BUSHES, 1, 1.4, false)
	_specs.back()["wide"] = true


func set_level(value: int) -> void:
	value = clampi(value, 0, 4)
	if value != level:
		level = value
		_build()


# The Cape (fynbos, pines and gums) or the temperate north (the UK)
func set_place(lat: float) -> void:
	var region := 1.0 if lat < -20.0 else 0.0
	RenderingServer.global_shader_parameter_set("plant_region", region)
	if region != _region:
		_region = region
		if is_inside_tree():
			_build()	# (the UK has no gums: their rings left out)


func _process(_delta: float) -> void:
	if world == null:
		return
	var map: Texture2D = world.get_plant_texture()
	visible = map != null and level > 0
	if not visible:
		return
	update_globals()
	if world.view_distance != _horizon_reach:
		_build()	# (the horizon's blanket reaches as far as the land)
	var camera := get_viewport().get_camera_3d()
	if camera:
		# high up (flying), the single plants give way to the blanket: they'd be specks, and cost the same
		var over: float = camera.global_position.y - world.ground_height(camera.global_position)
		var now_high := over > (LOW if high else HIGH)
		if now_high != high:
			_set_high(now_high)
		if not high:
			_place(camera.global_position, map)
	if _horizon_sheet:
		var wanted := 1.0 if high else 0.0
		if _horizon_shown != wanted:
			_horizon_shown = move_toward(_horizon_shown, wanted, _delta / HORIZON_FADE)
			(_horizon_sheet.material_override as ShaderMaterial).set_shader_parameter("shown", _horizon_shown)
		_horizon_sheet.visible = _horizon_shown > 0.0


# The shaders' shared values: every frame, and at once when Godot's 0 moves (main: origin_shifted), so the plants
# never draw a frame a world's step out of place (the pattern jumped and came back)
func update_globals() -> void:
	var map: Texture2D = world.get_plant_texture()
	if map == null:
		return
	RenderingServer.global_shader_parameter_set("plant_map", map)
	RenderingServer.global_shader_parameter_set("plant_heights", world.get_plant_heights())
	RenderingServer.global_shader_parameter_set("plant_origin", world.get_plant_origin())
	RenderingServer.global_shader_parameter_set("plant_size", world.get_plant_size())
	RenderingServer.global_shader_parameter_set("plant_offset", world.get_render_offset())
	var far_map: Texture2D = world.get_plant_texture(true)
	if far_map:
		RenderingServer.global_shader_parameter_set("plant_map_far", far_map)
		RenderingServer.global_shader_parameter_set("plant_heights_far", world.get_plant_heights(true))
		RenderingServer.global_shader_parameter_set("plant_origin_far", world.get_plant_origin(true))
		RenderingServer.global_shader_parameter_set("plant_size_far", world.get_plant_size(true))
	var horizon_map: Texture2D = world.get_horizon_plants()
	if horizon_map:
		RenderingServer.global_shader_parameter_set("plant_map_horizon", horizon_map)
		RenderingServer.global_shader_parameter_set("plant_heights_horizon", world.get_horizon_plants(true))
		RenderingServer.global_shader_parameter_set("plant_origin_horizon", world.get_horizon_plant_origin())
		RenderingServer.global_shader_parameter_set("plant_size_horizon", world.get_horizon_plant_size())
	var camera := get_viewport().get_camera_3d()
	if camera:
		RenderingServer.global_shader_parameter_set("plant_eye", camera.global_position)


func _set_high(on: bool) -> void:
	high = on
	if _placed:
		_placed.visible = not on
	if _near_sheet:
		(_near_sheet.material_override as ShaderMaterial).set_shader_parameter("inner", 0.0 if on else _inner)


func _build() -> void:
	for ring in _rings:
		ring.queue_free()
	_rings.clear()
	_specs.clear()
	if _placed:
		_placed.queue_free()
	_placed = Node3D.new()
	_placed.name = "Placed"
	add_child(_placed)
	_placed_map = null
	_near_sheet = null
	_horizon_sheet = null
	if level <= 0:
		return
	var near_trees: float = [0.0, 100.0, 140.0, 150.0, 110.0][level]
	var mid_trees: float = [0.0, 300.0, 450.0, 650.0, 650.0][level]
	var far_trees: float = [0.0, 0.0, 900.0, 1400.0, 1800.0][level]
	var bushes: float = [0.0, 60.0, 80.0, 100.0, 140.0][level]
	# (at High and Ultra the plants cast shadows as far as the sun's go: only the near trees' had, to 60 m, when the
	# graphics card placed them, and the shadows' edge was close and plain to see; placed on the processor only those
	# that grow are drawn, and their shadows cost ~0.1 ms)
	var shadowed: float = shadow_reach if level >= 3 else 0.0
	for kind in (KINDS if _region != 0.0 else 2):	# (the UK: broadleaf and conifers only, as the shader chooses)
		_ring(kind, 1, FINE, 0.0, near_trees, 0, 1.0, shadowed > 0.0)
	# further: one ring for every kind (a broadleaf the shader bends into the square's kind), every tree to mid_trees
	# (the light one to LIGHT m, then the far one: a ball on a stick), then thinned (one of each 2 x 2 near squares'
	# trees to 600 m, one of 4 x 4 beyond)
	var light := maxf(minf(mid_trees, LIGHT), near_trees)
	if light > near_trees:
		_ring(-1, 0, FINE, near_trees, light, 0, 1.0, near_trees < shadowed)
	if mid_trees > light:
		_ring(-1, -1, FINE, light, mid_trees, 0, 1.0, light < shadowed)
	if far_trees > mid_trees:
		if mid_trees < 600.0:
			_ring(-1, -1, FINE * 2.0, mid_trees, minf(far_trees, 600.0), 0, 1.25, mid_trees < shadowed)
		if far_trees > 600.0:
			_ring(-1, -1, FINE * 4.0, maxf(600.0, mid_trees), far_trees, 0, 1.5, maxf(600.0, mid_trees) < shadowed)
	_ring(3, 1, BUSH, 0.0, bushes, 1, 1.0, shadowed > 0.0)
	if level >= 3:
		_ring(3, 0, BUSH * 2.0, bushes, bushes * 3.0, 1, 1.2, false)
	_blanket(mid_trees * 0.9)
	if wide:
		_wide_ring()
	if high:
		_set_high(true)	# (still high up: the new plants hidden too)


# The forests beyond the near trees as one lumpy blanket (canopy.gdshader): a sheet of the near plant map's 10 m
# cells, 4 km across round the camera, to 1.95 km; then a sheet of 40 m squares (the far map's cells: blocky hexagons
# showed at 80) to 9.6 km; then one of the horizon's plant map's cells (200 m; more for a view past ~55 km, as
# GeodriveWorld's HorizonPlantCell) to the view distance, as far as the land
func _blanket(inner: float) -> void:
	_inner = inner
	_near_sheet = _sheet(false, 4000.0, 10.0, inner, NEAR_BLANKET)
	_sheet(true, 19600.0, 40.0, NEAR_BLANKET, FAR_BLANKET)
	_horizon_reach = world.view_distance if world else 0.0
	if _horizon_reach > FAR_BLANKET:
		var cell := maxf(200.0, ceil((2.0 * _horizon_reach * 100.0 + 2500000.0) / 640.0 / 1000.0) * 10.0)
		_horizon_sheet = _sheet(true, ceil(2.0 * _horizon_reach / cell) * cell, cell, FAR_BLANKET, _horizon_reach)
		(_horizon_sheet.material_override as ShaderMaterial).set_shader_parameter("horizon", true)
		(_horizon_sheet.material_override as ShaderMaterial).set_shader_parameter("shown", _horizon_shown)
		_horizon_sheet.visible = _horizon_shown > 0.0


func _sheet(far: bool, size: float, cell: float, inner: float, outer: float) -> MeshInstance3D:
	var sheet := PlaneMesh.new()
	sheet.size = Vector2(size, size)
	sheet.subdivide_width = int(size / cell) - 1
	sheet.subdivide_depth = int(size / cell) - 1
	var node := MeshInstance3D.new()
	node.layers = 1 << 18	# (the canopy's render layer)
	node.mesh = sheet
	node.custom_aabb = AABB(Vector3(-1.0e6, -1.0e4, -1.0e6), Vector3(2.0e6, 2.0e4, 2.0e6))
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF	# (its lumps shade it; a shadow would cost ~10 ms)
	var material := ShaderMaterial.new()
	material.shader = preload("res://canopy.gdshader")
	material.set_shader_parameter("far", far)
	material.set_shader_parameter("cell", cell)
	material.set_shader_parameter("inner", inner)
	material.set_shader_parameter("outer", outer)
	node.material_override = material
	add_child(node)
	_rings.append(node)
	return node


# A ring: plant Kind (Detail 1 full, 0 light, -1 far) on Cell squares from Inner to Outer m round the camera; its plants
# placed on the processor (only those that grow), in tiles (so those out of view aren't drawn), placed again once the
# camera has moved a little (the near rings more often than the far)
func _ring(kind: int, detail: int, cell: float, inner: float, outer: float, channel: int, grow: float, shadows: bool) -> void:
	var material := ShaderMaterial.new()
	material.shader = preload("res://vegetation_placed.gdshader")
	material.set_shader_parameter("bark", preload("res://textures/plants/bark_color.jpg"))
	material.set_shader_parameter("leaves", _leaf_textures()[0])
	material.set_shader_parameter("leaf_bumps", _leaf_textures()[1])
	material.set_shader_parameter("kind", kind)
	material.set_shader_parameter("fine_cell", FINE if channel == 0 else BUSH)
	material.set_shader_parameter("inner", inner)
	material.set_shader_parameter("outer", outer)
	_specs.append({"index": _specs.size(), "kind": kind, "channel": channel, "cell": cell, "fine_cell": FINE if channel == 0 else BUSH,
		"inner": inner, "outer": outer, "grow": grow, "tile": clampf(outer * 0.35, 40.0, 400.0), "every": clampf(outer * 0.02, 2.0, 25.0),
		"shadows": shadows, "material": material, "eye": Vector3(INF, 0.0, INF), "nodes": {}, "label": "%s %s, %.0f m squares, %.0f-%.0f m%s" % [
			["broadleaf", "conifer", "gum", "bush"][maxi(kind, 0)] if kind >= 0 else "any tree", ["far", "light", "full"][detail + 1], cell, inner, outer,
			", shadows" if shadows else ""],
		"mesh": ClassDB.class_call_static("GeodriveWorld", "make_plant_mesh", maxi(kind, 0), 1, detail)})


# The rings placed again where the camera's moved far enough (or the plant map is new), and what's been placed taken
func _place(eye: Vector3, map: Texture2D) -> void:
	var placed: Dictionary = world.take_plants()
	if not placed.is_empty():
		_take(placed)
	var renew := map != _placed_map
	var asked := []
	for spec in _specs:
		if renew or Vector2(eye.x - spec["eye"].x, eye.z - spec["eye"].z).length() > spec["every"]:
			asked.append(spec)
	if asked.is_empty():
		return
	# (a little more than the ring round each edge: the shader cuts it where the camera is now, fading across the edge;
	# so enough for the hand-over's width, each plant's edge (up to 20 m in), and the camera moving on till it's placed again)
	var rings := []
	for spec in asked:
		var inner: float = spec["inner"]
		var outer: float = spec["outer"]
		var more: float = spec["every"] + 1.0
		rings.append({"index": spec["index"], "kind": spec["kind"], "channel": spec["channel"], "cell": spec["cell"], "fine_cell": spec["fine_cell"],
			"inner": maxf(inner - _band(inner) - 20.0 - more, 0.0) if inner > 0.0 else 0.0, "outer": outer + _band(outer) + more,
			"grow": spec["grow"], "tile": spec["tile"]})
	if world.place_plants(rings, eye, _region > 0.5):
		_placed_map = map
		for spec in asked:
			spec["eye"] = eye


# The plant drawn nearest a line (from From along Way, Reach m): a tree or bush in its sights, its trunk within its
# crown's reach of the line; {} if none. {channel (0 trees, 1 bushes), fx, fz (its fine square), position, kind}
func plant_along(from: Vector3, way: Vector3, reach: float) -> Dictionary:
	var best := {}
	var nearest := INF
	way = way.normalized()
	for spec in _specs:
		if float(spec["inner"]) > reach + 30.0:
			continue	# (a far ring: nothing of it within reach)
		for node in spec["nodes"].values():
			var origin: Vector3 = node.position
			var buffer: PackedFloat32Array = node.multimesh.buffer
			for k in node.multimesh.instance_count:
				var i: int = k * 16
				var at := origin + Vector3(buffer[i + 3], buffer[i + 7], buffer[i + 11])
				var along := (at - from).dot(way)
				if along < 0.0 or along > reach:
					continue
				# (how far its trunk is from the line, across: a tree's crown ~1.5 m round it, a bush's ~0.8 m)
				var across := Vector2(at.x - (from.x + way.x * along), at.z - (from.z + way.z * along)).length()
				var crown := 0.8 if int(spec["channel"]) == 1 else 1.5
				if across < crown and along < nearest:
					nearest = along
					best = {"channel": int(spec["channel"]), "fx": int(buffer[i + 14]), "fz": int(buffer[i + 15]), "position": at,
						"kind": int(buffer[i + 12]), "fine": float(spec["fine_cell"])}
	return best


# Placed again now (after a plant's cut down, so it's gone from every ring at once)
func replace_now() -> void:
	_placed_map = null


# How wide (m) a ring's hand-over to the next is across an edge Radius m out (as vegetation_placed.gdshader's band)
static func _band(radius: float) -> float:
	return clampf(radius * 0.08, 3.0, 40.0)


func _take(placed: Dictionary) -> void:
	var started := Time.get_ticks_usec()
	var count := 0
	var tiles := 0
	_take_rings(placed)
	for index in placed["rings"]:
		for entry in placed["rings"][index]:
			count += int(entry["count"])
			tiles += 1
	if OS.get_cmdline_user_args().has("--verbose"):
		print("Geodrive: [Plants] placed %d rings (%d tiles, %d plants) in %.0f ms on a worker; taken in %.1f ms" % [placed["rings"].size(), tiles, count, placed["ms"], (Time.get_ticks_usec() - started) / 1000.0])


func _take_rings(placed: Dictionary) -> void:
	var anchor: Vector3 = placed["anchor"]
	var offset: Vector3 = world.get_render_offset()
	var rings: Dictionary = placed["rings"]
	for index in rings:
		if index >= _specs.size():
			continue
		var spec: Dictionary = _specs[index]
		var nodes: Dictionary = spec["nodes"]
		var kept := {}
		for entry in rings[index]:
			var key: Vector2i = entry["tile"]
			kept[key] = true
			var node: MultiMeshInstance3D = nodes.get(key)
			if node == null:
				node = MultiMeshInstance3D.new()
				node.layers = 1 << 17	# (the plants' render layer)
				node.set_meta("ring", spec["label"])
				var multimesh := MultiMesh.new()
				multimesh.transform_format = MultiMesh.TRANSFORM_3D
				multimesh.use_custom_data = true
				multimesh.mesh = spec["mesh"]
				node.multimesh = multimesh
				node.material_override = spec["material"]
				node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if spec["shadows"] else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				_placed.add_child(node)
				nodes[key] = node
			node.multimesh.instance_count = entry["count"]
			node.multimesh.buffer = entry["buffer"]
			node.position = anchor
			# its box (for culling): its tile's square, from its plants' lowest to above its tallest tree
			var tile: float = spec["tile"]
			var low := Vector3(key.x * tile - (anchor.x + offset.x) - 15.0, float(entry["low"]) - 2.0, key.y * tile - (anchor.z + offset.z) - 15.0)
			node.custom_aabb = AABB(low, Vector3(tile + 30.0, float(entry["high"]) - float(entry["low"]) + 45.0, tile + 30.0))
		for key in nodes.keys():
			if not kept.has(key):
				nodes[key].queue_free()
				nodes.erase(key)


# Godot's 0 moved: the placed plants with it (their next placing is in the new frame)
func shift(offset: Vector3) -> void:
	for spec in _specs:
		for node in spec["nodes"].values():
			node.position += offset
		spec["eye"] += offset


# The leaves' clumps, made once a run (no image files): soft cells, bright in the middle of each clump of leaves and
# darker in the gaps, two sizes over each other, seamless; and a bump map from them. 256 x 256, ~0.4 MB together
static var _leaf_pair: Array = []
static func _leaf_textures() -> Array:
	if _leaf_pair.is_empty():
		var noise := FastNoiseLite.new()
		noise.noise_type = FastNoiseLite.TYPE_CELLULAR
		noise.cellular_return_type = FastNoiseLite.RETURN_DISTANCE
		noise.seed = 11
		noise.frequency = 1.0 / 22.0	# (about 12 clumps across a repeat)
		noise.fractal_type = FastNoiseLite.FRACTAL_FBM
		noise.fractal_octaves = 2
		noise.fractal_gain = 0.45
		var clumps := noise.get_seamless_image(256, 256, false, false, 0.1, true)
		clumps.convert(Image.FORMAT_L8)
		# (the cells' distance is low in their middles: turned round, the clumps are bright)
		for y in 256:
			for x in 256:
				var v := clumps.get_pixel(x, y).r
				clumps.set_pixel(x, y, Color(1.0 - v, 1.0 - v, 1.0 - v))
		var bumps := clumps.duplicate()
		bumps.bump_map_to_normal_map(6.0)
		clumps.generate_mipmaps()
		bumps.generate_mipmaps()
		_leaf_pair = [ImageTexture.create_from_image(clumps), ImageTexture.create_from_image(bumps)]
	return _leaf_pair
