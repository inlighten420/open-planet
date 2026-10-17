# A wireframe Earth, as on the Unreal game's loading screen: lines of latitude and longitude, turning slowly, tilted
# towards you, with a pulsing pin on the place being loaded. It turns to bring that place round to face you, then sways
# a little either side of it.
class_name Globe
extends Control

const LINE := Color(0.25, 0.65, 1.0)
const ACCENT := Color(1.0, 0.6, 0.25)

var has_target := false
var target_lat := 0.0
var target_lon := 0.0
var _time := 0.0
var _spin := 0.0
var _tilt := -20.0


func _ready() -> void:
	custom_minimum_size = Vector2(420, 420)


func _process(delta: float) -> void:
	_time += delta
	if has_target:
		var wanted := -target_lon + 12.0 * sin(_time * 0.4)
		_spin += wrapf(wanted - _spin, -180.0, 180.0) * min(1.0, delta * 1.5)
		_tilt += (clampf(target_lat, -60.0, 60.0) - _tilt) * clampf(delta * 1.5, 0.0, 1.0)
	else:
		_spin += delta * 25.0
	queue_redraw()


# A point on the sphere seen from the front: x right, y up, z towards you (positive: the near side). Returns x, y, depth
func _project(lat: float, lon: float, centre: Vector2, radius: float) -> Vector3:
	var la := deg_to_rad(lat)
	var lo := deg_to_rad(lon + _spin)
	var p := Vector3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo))
	var t := deg_to_rad(_tilt)
	p = Vector3(p.x, p.y * cos(t) - p.z * sin(t), p.y * sin(t) + p.z * cos(t))
	return Vector3(centre.x + p.x * radius, centre.y - p.y * radius, p.z)


# A line round the globe, split into runs on the near side (bright) and the far side (faint)
func _line(points: Array[Vector3]) -> void:
	var run := PackedVector2Array()
	var front := true
	for p in points:
		var at := Vector2(p.x, p.y)
		if (p.z >= 0.0) != front and run.size() > 0:
			run.append(at)
			_flush(run, front)
			run = PackedVector2Array()
		front = p.z >= 0.0
		run.append(at)
	_flush(run, front)


func _flush(run: PackedVector2Array, front: bool) -> void:
	if run.size() > 1:
		draw_polyline(run, Color(LINE, 0.85 if front else 0.12), 1.6 if front else 1.0, true)


func _circle(centre: Vector2, radius: float, colour: Color, width: float, steps: int) -> void:
	var points := PackedVector2Array()
	for i in steps + 1:
		var a := TAU * i / steps
		points.append(centre + Vector2(cos(a), sin(a)) * radius)
	draw_polyline(points, colour, width, true)


func _draw() -> void:
	var centre := size * 0.5
	var radius: float = min(size.x, size.y) * 0.46
	for lat in range(-75, 76, 15):
		var points: Array[Vector3] = []
		for i in 73:
			points.append(_project(lat, i * 5.0, centre, radius))
		_line(points)
	for lon in range(0, 360, 20):
		var points: Array[Vector3] = []
		for i in 73:
			points.append(_project(-90.0 + i * 2.5, lon, centre, radius))
		_line(points)
	_circle(centre, radius, LINE, 2.5, 96)
	if has_target:
		var pin := _project(target_lat, target_lon, centre, radius)
		if pin.z > 0.0:
			var at := Vector2(pin.x, pin.y)
			_circle(at, 3.0, ACCENT, 4.0, 24)
			_circle(at, 6.0 + 5.0 * (0.5 + 0.5 * sin(_time * 4.0)), Color(ACCENT, 0.6), 2.0, 24)
