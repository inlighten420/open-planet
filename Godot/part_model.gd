# A thing's look made of shapes (a gun's first; a prop's, a car's, clothing's after): its file's "look", turned into a
# model. Each part a shape of 1 m sized to it, turned and coloured; a cut part takes its shape out of all the others
# (a box's window, a hollow barrel). Godot's CSG joins them.
#   "look": {"parts": [{"shape": "box", "at": [x, y, z], "size": [w, h, d], "turn": [x, y, z],
#                       "colour": "#rrggbb", "finish": "metal", "cut": false}, ...],
#            "sight_at": [x, y, z], "eye_relief": 0.08}
# In metres; x right, y up, z towards you (a gun's muzzle the -z way); turn in degrees. Shapes: box, wedge (a triangle
# end on, its slope across the top: high at the back), cylinder (along y; turn it to lie along z), cone (its point up
# y), sphere, tube (a cylinder with a hole down it: "wall" its thickness, a share of its radius). Finishes: matte,
# plastic, metal, wood. sight_at: the point looked over when aiming (a rear sight's top); eye_relief: how far in front
# of the eye it's held then.
class_name PartModel
extends RefCounted

const SHAPES := ["box", "wedge", "cylinder", "cone", "sphere", "tube"]
const FINISHES := ["matte", "plastic", "metal", "wood"]
# the palette: what most things are made of, and bright ones
const PALETTE := ["#0d0d0e", "#2b2b2e", "#55575c", "#8c8f94", "#c9ccd1", "#f2f2f2", "#47260f", "#7a4a26", "#b07a45", "#d9b98c",
	"#3d4a2a", "#6b7a45", "#8a7d5a", "#a6302a", "#d9822b", "#e8c547", "#2f6b3a", "#2a5d8f", "#5a3d8a", "#d9558f"]

static var _materials := {}	# (colour and finish -> its material, shared)
static var _wedge: ArrayMesh = null


# The model: a CSG tree of the parts (cuts last, taking from all), its shadows as asked
static func build(look: Dictionary, shadows: bool = false) -> CSGCombiner3D:
	var root := CSGCombiner3D.new()
	root.name = "Look"
	root.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON if shadows else GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var parts: Array = look.get("parts", [])
	for part in parts.filter(func(p): return not p.get("cut", false)) + parts.filter(func(p): return p.get("cut", false)):
		var node := shape(part)
		if node:
			root.add_child(node)
	return root


# One part: its shape, sized, turned, placed, coloured; a cut subtracts
static func shape(part: Dictionary) -> CSGShape3D:
	var node: CSGShape3D
	match String(part.get("shape", "box")):
		"box":
			var box := CSGBox3D.new()
			box.size = Vector3.ONE
			node = box
		"wedge":
			var mesh := CSGMesh3D.new()
			mesh.mesh = _wedge_mesh()
			node = mesh
		"cylinder", "cone":
			var cylinder := CSGCylinder3D.new()
			cylinder.radius = 0.5
			cylinder.height = 1.0
			cylinder.sides = 16
			cylinder.cone = part.get("shape") == "cone"
			node = cylinder
		"sphere":
			var sphere := CSGSphere3D.new()
			sphere.radius = 0.5
			sphere.radial_segments = 16
			sphere.rings = 10
			node = sphere
		"tube":
			# (a cylinder less a narrower, longer one down its middle)
			var tube := CSGCombiner3D.new()
			var outer := CSGCylinder3D.new()
			outer.radius = 0.5
			outer.height = 1.0
			outer.sides = 16
			var inner := CSGCylinder3D.new()
			inner.radius = 0.5 * (1.0 - clampf(float(part.get("wall", 0.25)), 0.05, 0.95))
			inner.height = 1.02
			inner.sides = 16
			inner.operation = CSGShape3D.OPERATION_SUBTRACTION
			tube.add_child(outer)
			tube.add_child(inner)
			node = tube
		_:
			return null
	var size := _vec(part.get("size", [0.1, 0.1, 0.1]), Vector3(0.1, 0.1, 0.1))
	var turn := _vec(part.get("turn", [0, 0, 0]), Vector3.ZERO)
	node.transform = Transform3D(Basis.from_euler(Vector3(deg_to_rad(turn.x), deg_to_rad(turn.y), deg_to_rad(turn.z))).scaled_local(size),
		_vec(part.get("at", [0, 0, 0]), Vector3.ZERO))
	if part.get("cut", false):
		node.operation = CSGShape3D.OPERATION_SUBTRACTION
	else:
		var material := _material(String(part.get("colour", "#55575c")), String(part.get("finish", "matte")))
		if node is CSGPrimitive3D:
			node.material = material
		for child in node.get_children():
			if child is CSGPrimitive3D:
				child.material = material
	return node


static func _vec(value, fallback: Vector3) -> Vector3:
	return Vector3(float(value[0]), float(value[1]), float(value[2])) if value is Array and value.size() == 3 else fallback


static func _material(colour: String, finish: String) -> StandardMaterial3D:
	var key := colour + finish
	if not _materials.has(key):
		var m := StandardMaterial3D.new()
		m.albedo_color = Color.html(colour) if Color.html_is_valid(colour) else Color(0.3, 0.3, 0.3)
		m.roughness = {"matte": 0.9, "plastic": 0.5, "metal": 0.35, "wood": 0.7}.get(finish, 0.9)
		m.metallic = 0.8 if finish == "metal" else 0.0
		_materials[key] = m
	return _materials[key]


# The wedge, 1 m: a triangle end on (seen from its side, x): its back high, its front at the bottom (a stock's slope)
static func _wedge_mesh() -> ArrayMesh:
	if _wedge == null:
		var tool := SurfaceTool.new()
		tool.begin(Mesh.PRIMITIVE_TRIANGLES)
		# (each end: the bottom front, the bottom back, the top back)
		var ends := []
		for x in [-0.5, 0.5]:
			ends.append([Vector3(x, -0.5, -0.5), Vector3(x, -0.5, 0.5), Vector3(x, 0.5, 0.5)])
		var l: Array = ends[0]
		var r: Array = ends[1]
		var faces := [[l[0], l[2], l[1]], [r[0], r[1], r[2]],	# (the ends)
			[l[0], l[1], r[1]], [l[0], r[1], r[0]],			# (the bottom)
			[l[1], l[2], r[2]], [l[1], r[2], r[1]],			# (the back)
			[l[0], r[0], r[2]], [l[0], r[2], l[2]]]			# (the slope)
		for face in faces:
			var normal: Vector3 = (face[1] - face[0]).cross(face[2] - face[0]).normalized()
			for v in face:
				tool.set_normal(normal)
				tool.add_vertex(v)
		_wedge = tool.commit()
	return _wedge


# The guns' own looks, as parts (the hunting pack's rifles, a submachine gun): what a weapon with no look of its own has
static func default_look(model: String) -> Dictionary:
	var wood := "#47260f"
	var steel := "#0a0a0b"
	if model == "smg":
		var black := "#0d0d0e"
		return {"parts": [
			{"shape": "box", "at": [0, 0, -0.05], "size": [0.05, 0.07, 0.3], "colour": black, "finish": "plastic"},
			{"shape": "box", "at": [0, -0.11, -0.12], "size": [0.03, 0.17, 0.035], "turn": [8.6, 0, 0], "colour": steel, "finish": "metal"},
			{"shape": "box", "at": [0, -0.08, 0.06], "size": [0.035, 0.1, 0.045], "turn": [-14.3, 0, 0], "colour": black, "finish": "plastic"},
			{"shape": "cylinder", "at": [0, 0.01, -0.27], "size": [0.022, 0.14, 0.022], "turn": [90, 0, 0], "colour": steel, "finish": "metal"},
			{"shape": "box", "at": [0, 0, 0.22], "size": [0.012, 0.02, 0.26], "colour": steel, "finish": "metal"},
			{"shape": "box", "at": [0, 0.045, -0.18], "size": [0.008, 0.025, 0.008], "colour": steel, "finish": "metal"},
			{"shape": "box", "at": [0, 0.045, 0.07], "size": [0.025, 0.02, 0.012], "colour": steel, "finish": "metal"},
			{"shape": "box", "at": [0, 0.05, 0.07], "size": [0.008, 0.012, 0.014], "cut": true}],
			"sight_at": [0, 0.052, 0.07], "eye_relief": 0.22}
	return {"parts": [
		{"shape": "box", "at": [0, -0.035, 0.2], "size": [0.045, 0.1, 0.34], "turn": [-4.6, 0, 0], "colour": wood, "finish": "wood"},
		{"shape": "box", "at": [0, -0.01, -0.15], "size": [0.04, 0.05, 0.4], "colour": wood, "finish": "wood"},
		{"shape": "box", "at": [0, 0.02, 0], "size": [0.035, 0.045, 0.22], "colour": steel, "finish": "metal"},
		{"shape": "cylinder", "at": [0, 0.02, -0.4], "size": [0.018, 0.6, 0.018], "turn": [90, 0, 0], "colour": steel, "finish": "metal"},
		{"shape": "tube", "at": [0, 0.075, -0.02], "size": [0.04, 0.3, 0.04], "turn": [90, 0, 0], "colour": steel, "finish": "metal", "wall": 0.15},
		{"shape": "tube", "at": [0, 0.075, -0.18], "size": [0.052, 0.08, 0.052], "turn": [90, 0, 0], "colour": steel, "finish": "metal", "wall": 0.12},
		{"shape": "tube", "at": [0, 0.075, 0.13], "size": [0.046, 0.06, 0.046], "turn": [90, 0, 0], "colour": steel, "finish": "metal", "wall": 0.12},
		{"shape": "box", "at": [0, 0.05, -0.02], "size": [0.02, 0.012, 0.06], "colour": steel, "finish": "metal"},
		{"shape": "cylinder", "at": [0.03, 0.03, 0.06], "size": [0.012, 0.05, 0.012], "turn": [0, 0, 90], "colour": steel, "finish": "metal"}],
		"sight_at": [0, 0.075, 0.16], "eye_relief": 0.07}


# Where a look's sight is (the point looked over), and how far in front of the eye it's held aiming
static func sight_of(look: Dictionary) -> Vector3:
	return _vec(look.get("sight_at", [0, 0.05, 0]), Vector3(0, 0.05, 0))
