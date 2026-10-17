# What's on screen, as in the Unreal game: the speedometer (bottom right), the street you're on (top), a status line
# (bottom left), the debug numbers (F3), all hidden with H; and the performance overlay (top right: FPS, memory). The
# menu is menu.gd.
class_name Hud
extends CanvasLayer

var shown := true
var debug := false
var show_fps := false
var show_memory := false
var speed_label: Label
var gear_label: Label
var street_label: Label
var status_label: Label
var debug_label: Label
var perf_label: Label
var diff_label: Label		# DIFF LOCK, above the speed
var on_foot := false	# (the car's lines put away)
var flight_label: Label	# flying: the instruments (aviation.gd), bottom right
var stalled_label: Label	# STALLED, mid-screen
var info_label: Label		# robot / traffic, above the status line
var hints_label: Label		# the keys that do things now (build, phone, map...), so a newcomer knows they're there
var game_label: Label		# a game's line (its score, its time left), under the street's name
var banner_label: Label		# a game won or lost: big, mid-screen, a while
var _banner_until := 0.0
var quality_name := ""
var _frame_times: Array[float] = []
var _status_until := 0.0
var _perf_timer := 0.0


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	speed_label = _label(64, HORIZONTAL_ALIGNMENT_RIGHT)
	flight_label = _label(24, HORIZONTAL_ALIGNMENT_RIGHT)
	gear_label = _label(22, HORIZONTAL_ALIGNMENT_RIGHT)
	street_label = _label(26, HORIZONTAL_ALIGNMENT_CENTER)
	status_label = _label(20, HORIZONTAL_ALIGNMENT_LEFT)
	debug_label = _label(16, HORIZONTAL_ALIGNMENT_LEFT)
	debug_label.visible = false
	perf_label = _label(16, HORIZONTAL_ALIGNMENT_RIGHT)
	perf_label.visible = false
	diff_label = _label(20, HORIZONTAL_ALIGNMENT_RIGHT)
	diff_label.text = "DIFF LOCK"
	diff_label.add_theme_color_override("font_color", Color(1.0, 0.6, 0.2))
	diff_label.visible = false
	stalled_label = _label(30, HORIZONTAL_ALIGNMENT_CENTER)
	stalled_label.text = "STALLED - push the clutch in to restart"
	stalled_label.add_theme_color_override("font_color", Color(1.0, 0.3, 0.25))
	stalled_label.visible = false
	info_label = _label(18, HORIZONTAL_ALIGNMENT_LEFT)
	info_label.add_theme_color_override("font_color", Color(1.0, 0.8, 0.45))
	hints_label = _label(14, HORIZONTAL_ALIGNMENT_LEFT)
	hints_label.add_theme_color_override("font_color", Color(0.9, 0.9, 0.9, 0.85))
	hints_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	game_label = _label(24, HORIZONTAL_ALIGNMENT_CENTER)
	game_label.add_theme_color_override("font_color", Color(1.0, 0.85, 0.35))
	banner_label = _label(56, HORIZONTAL_ALIGNMENT_CENTER)
	banner_label.add_theme_constant_override("outline_size", 10)
	banner_label.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.85))
	banner_label.visible = false
	get_viewport().size_changed.connect(_layout)
	_layout()


func _label(font_size: int, align: HorizontalAlignment) -> Label:
	var label := Label.new()
	label.add_theme_font_size_override("font_size", font_size)
	label.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
	label.add_theme_constant_override("shadow_offset_x", 2)
	label.add_theme_constant_override("shadow_offset_y", 2)
	label.horizontal_alignment = align
	add_child(label)
	return label


func _layout() -> void:
	var size := get_viewport().get_visible_rect().size
	speed_label.position = Vector2(size.x - 340, size.y - 140)
	speed_label.size = Vector2(300, 80)
	gear_label.position = Vector2(size.x - 340, size.y - 60)
	gear_label.size = Vector2(300, 30)
	flight_label.position = Vector2(size.x - 840, size.y - 190)
	flight_label.size = Vector2(800, 160)
	street_label.position = Vector2(0, 16)
	street_label.size = Vector2(size.x, 40)
	status_label.position = Vector2(20, size.y - 70)
	status_label.size = Vector2(size.x * 0.6, 30)
	debug_label.position = Vector2(16, 70)
	perf_label.position = Vector2(size.x - 820, 12)
	perf_label.size = Vector2(800, 50)	# (wide enough for the memory and work lines)
	diff_label.position = Vector2(size.x - 340, size.y - 168)
	diff_label.size = Vector2(300, 30)
	stalled_label.position = Vector2(0, size.y * 0.3)
	stalled_label.size = Vector2(size.x, 40)
	info_label.position = Vector2(20, size.y - 98)
	info_label.size = Vector2(size.x * 0.7, 26)
	hints_label.position = Vector2(20, size.y - 36)
	hints_label.size = Vector2(size.x * 0.72, 34)
	game_label.position = Vector2(0, 56)
	game_label.size = Vector2(size.x, 34)
	banner_label.position = Vector2(0, size.y * 0.36)
	banner_label.size = Vector2(size.x, 80)


# A game's line ("" puts it away)
func set_game_line(text: String) -> void:
	game_label.text = text
	game_label.visible = text != ""


# A game's end, big in the middle a while (won: gold, lost: red)
func show_banner(text: String, won: bool, seconds: float = 6.0) -> void:
	banner_label.text = text
	banner_label.add_theme_color_override("font_color", Color(1.0, 0.85, 0.3) if won else Color(1.0, 0.35, 0.3))
	banner_label.visible = true
	_banner_until = Time.get_ticks_msec() / 1000.0 + seconds


func toggle_shown() -> void:
	set_shown(not shown)


func set_shown(on: bool) -> void:
	shown = on
	for label in [speed_label, gear_label, street_label, status_label, info_label, hints_label]:
		label.visible = shown
	speed_label.visible = shown and not on_foot
	gear_label.visible = shown and not on_foot
	if not shown:
		diff_label.visible = false
		stalled_label.visible = false
	debug_label.visible = shown and debug


func toggle_debug() -> void:
	set_debug(not debug)


func set_debug(on: bool) -> void:
	debug = on
	debug_label.visible = shown and debug


func set_status(text: String, seconds: float = 4.0) -> void:
	status_label.text = text
	_status_until = Time.get_ticks_msec() / 1000.0 + seconds


func update(speed_kmh: float, gear: String, street: String, trip_km: float, debug_text: String, mph: bool = false) -> void:
	if mph:
		speed_label.text = "%d" % roundi(abs(speed_kmh) / 1.609344)
		gear_label.text = "mph   gear %s   trip %.1f mi" % [gear, trip_km / 1.609344]
	else:
		speed_label.text = "%d" % roundi(abs(speed_kmh))
		gear_label.text = "km/h   gear %s   trip %.1f km" % [gear, trip_km]
	street_label.text = street
	if debug_label.visible:
		debug_label.text = debug_text
	if _status_until > 0.0 and Time.get_ticks_msec() / 1000.0 > _status_until:
		status_label.text = ""
		_status_until = 0.0


# The car's own lines: DIFF LOCK above the speed (orange), STALLED mid-screen (red), as in the Unreal game
func set_car_flags(diff_lock: bool, stalled: bool) -> void:
	diff_label.visible = shown and diff_lock and not on_foot
	stalled_label.visible = shown and stalled and not on_foot


# Flying: the instruments (aviation.gd), in the speed's place; "" puts them away
func set_flight(text: String) -> void:
	flight_label.text = text
	flight_label.visible = shown and text != ""
	if text.ends_with("STALL"):
		flight_label.add_theme_color_override("font_color", Color(1.0, 0.35, 0.3))
	else:
		flight_label.remove_theme_color_override("font_color")


# On foot (walker.gd): the car's speed, gear and trip are put away until they're back in it
func set_on_foot(on: bool) -> void:
	if on != on_foot:
		on_foot = on
		set_shown(shown)


# A line above the status for what's running: the robot driver, traffic
func set_info(text: String) -> void:
	info_label.text = text


func set_hints(text: String) -> void:
	hints_label.text = text


# The performance overlay, as the Unreal game's: "N FPS (x ms, worst y ms)" over the last second, green at 59+, yellow at
# 45+, red below, and the graphics preset; with memory, the PC's and the graphics card's in use. Twice a second.
func _process(delta: float) -> void:
	if banner_label.visible and Time.get_ticks_msec() / 1000.0 > _banner_until:
		banner_label.visible = false
	perf_label.visible = show_fps and shown
	_frame_times.append(delta)
	if _frame_times.size() > 240:
		_frame_times.pop_front()
	if not perf_label.visible:
		return
	_perf_timer -= delta
	if _perf_timer > 0.0:
		return
	_perf_timer = 0.5
	# the last second's frames
	var total := 0.0
	var worst := 0.0
	var count := 0
	for i in range(_frame_times.size() - 1, -1, -1):
		total += _frame_times[i]
		worst = maxf(worst, _frame_times[i])
		count += 1
		if total >= 1.0:
			break
	var fps := count / maxf(total, 0.001)
	var text := "%d FPS (%.1f ms, worst %.1f ms)" % [roundi(fps), 1000.0 * total / maxi(count, 1), worst * 1000.0]
	if quality_name != "":
		text += "\nQuality: " + quality_name
	if show_memory:
		var memory: Dictionary = OS.get_memory_info()
		var used_gb := float(int(memory.get("physical", 0)) - int(memory.get("free", 0))) / float(1 << 30)
		var video_gb := Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / float(1 << 30)
		var game_gb: float = ClassDB.class_call_static("GeodriveWorld", "get_game_memory") / float(1 << 30)
		# the game's own memory, then the whole PC's (every program, the editor too), then the graphics card's
		text += "\nMemory: game %.1f GB (whole PC %.1f GB)   graphics card %.1f GB" % [game_gb, used_gb, video_gb]
		# the headroom: how long the graphics card works on each frame and its share of the frame (the frame rate is often
		# held at the screen's rate, so it doesn't tell), and the game's share of the whole processor (Godot's own process
		# time counts the wait for the screen too, so it isn't shown)
		var viewport := get_viewport().get_viewport_rid()
		RenderingServer.viewport_set_measure_render_time(viewport, true)
		var gpu_ms := RenderingServer.viewport_get_measured_render_time_gpu(viewport)
		var frame_ms := 1000.0 * total / maxi(count, 1)
		var cpu_share: float = ClassDB.class_call_static("GeodriveWorld", "get_game_cpu")
		text += "\nGraphics card busy %.1f ms a frame (%d%%)   game CPU %d%%" % [gpu_ms, roundi(100.0 * gpu_ms / maxf(frame_ms, 0.1)), roundi(cpu_share)]
	perf_label.text = text
	perf_label.add_theme_color_override("font_color", Color(0.4, 1.0, 0.45) if fps >= 59.0 else (Color(1.0, 0.85, 0.3) if fps >= 45.0 else Color(1.0, 0.4, 0.35)))
