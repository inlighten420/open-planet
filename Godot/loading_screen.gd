# The loading screen, as in the Unreal game: the turning globe with a pin on the place, the name, and what the world
# is building with dots that keep moving so it never looks stuck. Shown while a new area is built (main sets loading);
# once it's in, a moment for the ground under the car, then it fades out over half a second. Along the bottom, the
# world's data's credits (what its licences ask for: shown here rather than over the game).
class_name LoadingScreen
extends CanvasLayer

const BACKGROUND := Color(0.004, 0.008, 0.02)
const CREDITS := "Map data (c) OpenStreetMap contributors, ODbL - openstreetmap.org/copyright.   Heights: SRTM courtesy of " 	+ "the U.S. Geological Survey; ETOPO1, NOAA; Terrain Tiles via AWS Open Data. Produced using Copernicus WorldDEM-30 " 	+ "(c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European " 	+ "Union and ESA. Contains Environment Agency information (c) Environment Agency and/or database right (OGL v3). " 	+ "Heights (c) GeoBasis-DE / LVermGeoRP, dl-de/by-2-0. Heights: IGN RGE ALTI (c) IGN, Licence Ouverte Etalab 2.0. Land cover (c) ESA WorldCover project 2021 / Contains modified " 	+ "Copernicus Sentinel data (2021) processed by ESA WorldCover consortium (CC BY 4.0). World map: Natural Earth. " 	+ "Open-Planet is free software (AGPL-3.0)."

var loading := false
var status := ""
var error := false
var _root: ColorRect
var _globe: Globe
var _status_label: Label
var _time := 0.0
var _since_done := 0.0


func _ready() -> void:
	layer = 100	# above the HUD and the menu
	process_mode = Node.PROCESS_MODE_ALWAYS
	_root = ColorRect.new()
	_root.color = BACKGROUND
	_root.set_anchors_preset(Control.PRESET_FULL_RECT)
	_root.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(_root)
	var centre := CenterContainer.new()
	centre.set_anchors_preset(Control.PRESET_FULL_RECT)
	_root.add_child(centre)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 0)
	centre.add_child(column)
	_globe = Globe.new()
	_globe.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	column.add_child(_globe)
	column.add_child(_spacer(28))
	column.add_child(_text("O P E N - P L A N E T", 34, Color(0.92, 0.96, 1.0), true))
	column.add_child(_spacer(6))
	column.add_child(_text("the real world, open to everyone", 13, Globe.LINE, false))
	column.add_child(_spacer(34))
	_status_label = _text("", 14, Color(0.8, 0.82, 0.86), false)
	_status_label.custom_minimum_size = Vector2(700, 0)
	_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	column.add_child(_status_label)
	var credits := _text(CREDITS, 11, Color(0.55, 0.58, 0.64), false)
	credits.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	credits.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	credits.offset_left = 40
	credits.offset_right = -40
	credits.offset_top = -64
	credits.offset_bottom = -16
	credits.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	_root.add_child(credits)
	visible = false


func _spacer(height: int) -> Control:
	var spacer := Control.new()
	spacer.custom_minimum_size = Vector2(0, height)
	return spacer


func _text(text: String, font_size: int, colour: Color, bold: bool) -> Label:
	var label := Label.new()
	label.text = text
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_color", colour)
	if bold:
		var font := SystemFont.new()
		font.font_weight = 700
		label.add_theme_font_override("font", font)
	return label


func set_target(lat: float, lon: float) -> void:
	_globe.has_target = true
	_globe.target_lat = lat
	_globe.target_lon = lon


func is_showing() -> bool:
	return visible


func _process(delta: float) -> void:
	if loading:
		_since_done = 0.0
		if not visible:
			_time = 0.0
		visible = true
		_root.modulate.a = 1.0
	elif visible:
		# a moment for the ground under the car to stream in, then fade out over half a second
		_since_done += delta
		_root.modulate.a = 1.0 - clampf((_since_done - 0.8) / 0.5, 0.0, 1.0)
		if _since_done > 1.3:
			visible = false
	if not visible:
		return
	_time += delta
	var text := status if status != "" else "Getting ready"
	if error:
		_status_label.text = text
	else:
		text = text.trim_suffix("...")
		var dots := int(_time * 2.5) % 4
		# the stage, and how long it has been going: so slow is easy to tell from stuck
		var seconds := int(_time)
		_status_label.text = text + ".".repeat(dots) + " ".repeat(3 - dots) + "   %d:%02d" % [seconds / 60, seconds % 60]
		if _time > 20.0:
			_status_label.text += "\nStill working: a big, dense city takes a while to build the first time. Nothing is stuck."
	_status_label.add_theme_color_override("font_color", Color(1.0, 0.4, 0.35) if error else Color(0.8, 0.82, 0.86))
