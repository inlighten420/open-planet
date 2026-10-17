# Multiplayer, as in the Unreal game: one player hosts (port 7777), friends join by address (over Tailscale, or on the
# same network). Each player drives their own car here; about 20 times a second it sends where it is (latitude,
# longitude and height, so the players' own world origins don't matter), how it's turned, how fast it's going, its
# steering and its body. Everyone else's car is shown a little in the past, moving smoothly between the two updates
# around that moment, and carried on along its velocity for a moment if updates are late. The host's area goes to
# whoever joins (and again whenever the host goes somewhere new), and the joiner is put on the road beside the host.
# On foot the same: where each player stands, which way they look, how fast they go (their body walks or runs to
# match), whether they carry a rifle, whether they're down. A shot's heard by everyone (later and quieter the farther
# off), and stops on whoever's in its way: they're told who shot them and where, go down, and get up again by their car.
# Godot's own networking (ENet); it can't join an Unreal game or the other way round.
class_name GameNet
extends Node

signal status_changed(text: String)

const PORT := 7777
const SEND_EVERY := 0.05
const SHOW_DELAY := 0.12		# s in the past the others' cars are shown, so there are two updates to move between
const MAX_AHEAD := 0.3		# s a car is carried on along its velocity when updates are late

var game: Node				# main: the world, the car, starting areas, putting the car somewhere
var player_name := "Driver"
var status := ""
var _send_timer := 0.0
var _cars := {}				# peer id -> { node, label, name, body, updates: [[time, lat, lon, height, rotation, velocity, steer, speed]] }
var _area := []				# the host's area: lat, lon, name
var _join_pending := false	# just joined: put the car beside the host once the area is in
var _people := {}			# peer id -> { node (a puppet Walker), label, name, height, updates: [[time, lat, lon, above the ground, yaw, speed, rifle, down]] }
const PERSON_GONE := 0.6	# s without an update: back in their car (their body hidden)
const DOWN_FOR := 6.0		# s down when shot, then up again by your car
var _down_until := 0.0		# (down by a shot or a car: up again then; 0: not down by those)
# a car meeting someone on foot (each player checks their own body against the cars they see: a friend's, the traffic,
# their own rolling): slower than this it pushes them aside; faster, they're knocked down and the car loses their share
const KNOCKED_AT := 2.0		# m/s (7 km/h)
const TOUCH := 0.15		# m: a car this near you is touching you
const PERSON_KG := 80.0
const ANIMALS_EVERY := 0.1	# s: the host's animals sent this often (~4.5 KB a farm)
var _animals_timer := 0.0
# what's made: the host's own packs (its Saved folder's: the game's own everyone has), sent to whoever joins, and again
# when the host makes something; the props on the host's map, as they're put and taken away (a joiner's put or taken
# by the host, so everyone's map is the same)
const SHARE_KINDS := ["weapons", "creatures", "props", "reserves"]	# (in this order: a game's creatures before it)
const SHARE_MOST := 512 * 1024	# characters a file (a bigger one isn't sent, nor taken)
var _from_host := {}	# "kind/pack/file" -> its text (what's come from the host, this time online)


func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS	# (the menu doesn't pause a networked game, but just in case)
	multiplayer.peer_connected.connect(_on_peer_connected)
	multiplayer.peer_disconnected.connect(_on_peer_disconnected)
	multiplayer.connected_to_server.connect(func(): _set_status("Joined: waiting for the host's area..."))
	# (the peer closed and replaced after its signal's done: not from inside it, while it's still polling)
	multiplayer.connection_failed.connect(func():
		_set_status("Couldn't reach the host. Check the address, and that you both have Tailscale on.")
		_close.call_deferred()
		game.join_failed.call_deferred())
	multiplayer.server_disconnected.connect(func():
		_set_status("The host left the game.")
		_close.call_deferred()
		game.join_failed.call_deferred())


func is_online() -> bool:
	var peer := multiplayer.multiplayer_peer
	return peer != null and not (peer is OfflineMultiplayerPeer) and peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTED


# Still reaching a host (not online yet: the game goes on as before)
func is_connecting() -> bool:
	var peer := multiplayer.multiplayer_peer
	return peer != null and not (peer is OfflineMultiplayerPeer) and peer.get_connection_status() == MultiplayerPeer.CONNECTION_CONNECTING


func is_host() -> bool:
	return is_online() and multiplayer.is_server()


func is_client() -> bool:
	return is_online() and not multiplayer.is_server()


func player_count() -> int:
	return _cars.size() + 1 if is_online() else 1


# This world becomes the host where it is: nothing reloads, the area stays
func host() -> bool:
	_close()
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_server(PORT, 16)
	if error != OK:
		_set_status("Couldn't host (port %d in use?)" % PORT)
		return false
	multiplayer.multiplayer_peer = peer
	_set_status("Hosting: friends join you at %s" % ", ".join(addresses()))
	return true


func join(address: String) -> bool:
	_close()
	var peer := ENetMultiplayerPeer.new()
	var error := peer.create_client(address.strip_edges(), PORT)
	if error != OK:
		_set_status("Couldn't join %s" % address)
		return false
	multiplayer.multiplayer_peer = peer
	_set_status("Joining %s..." % address)
	return true


func leave() -> void:
	_close()
	_set_status("")


func _close() -> void:
	if not _from_host.is_empty():
		_forget_shared()
	if multiplayer.multiplayer_peer and not (multiplayer.multiplayer_peer is OfflineMultiplayerPeer):
		multiplayer.multiplayer_peer.close()
	multiplayer.multiplayer_peer = OfflineMultiplayerPeer.new()
	for id in _cars.keys():
		_remove_car(id)
	for id in _people.keys():
		_remove_person(id)
	_join_pending = false


func _set_status(text: String) -> void:
	status = text
	if text != "":
		print("Geodrive net: ", text)
	status_changed.emit(text)


# This PC's addresses friends can use: Tailscale's (100.x) first, then the local network's
func addresses() -> PackedStringArray:
	var tailscale := PackedStringArray()
	var local := PackedStringArray()
	for address in IP.get_local_addresses():
		if address.contains(":"):
			continue	# (IPv6)
		if address.begins_with("100."):
			tailscale.append(address)
		elif address.begins_with("192.168.") or address.begins_with("10."):
			local.append(address)
	return tailscale + local if not (tailscale + local).is_empty() else PackedStringArray(["this PC's address"])


# --- the area ---

func _on_peer_connected(id: int) -> void:
	# the area round where the host is now (it may have driven far from where it started)
	if multiplayer.is_server() and game.world and game.car:
		var at: PackedFloat64Array = game.world.to_lat_lon(game.car.global_position)
		_receive_area.rpc_id(id, at[0], at[1], player_name)
	elif multiplayer.is_server() and not _area.is_empty():
		_receive_area.rpc_id(id, _area[0], _area[1], _area[2])
	if multiplayer.is_server():
		share_packs(id)
		_set_status("Hosting: %d friend%s joined. Friends join you at %s" % [multiplayer.get_peers().size(), "" if multiplayer.get_peers().size() == 1 else "s", ", ".join(addresses())])


func _on_peer_disconnected(id: int) -> void:
	_remove_car(id)
	_remove_person(id)
	if multiplayer.is_server():
		_set_status("Hosting: %d friend%s here. Friends join you at %s" % [multiplayer.get_peers().size(), "" if multiplayer.get_peers().size() == 1 else "s", ", ".join(addresses())])


# The host's area: kept, and sent to everyone (when the host goes somewhere new)
func set_area(lat: float, lon: float, place_name: String) -> void:
	_area = [lat, lon, place_name]
	if is_host():
		_receive_area.rpc(lat, lon, place_name)


@rpc("authority", "call_remote", "reliable")
func _receive_area(lat: float, lon: float, place_name: String) -> void:
	var moved := not _area.is_empty() and game.car != null
	_area = [lat, lon, place_name]
	_join_pending = true
	_set_status(("The host moved to %s: going there..." if moved else "Joined: building the host's area (%s)...") % place_name)
	game.join_area(lat, lon, place_name)


# --- what's made: shared ---

# The host's own packs' files: to one who's joined (to), or everyone (something new made); then its props on the map
func share_packs(to: int = 0) -> void:
	if not is_host():
		return
	for kind in SHARE_KINDS:
		for path in Packs.files(kind):
			if path.begins_with(Packs.OWN):
				continue	# (the game's own: everyone has them)
			var text := FileAccess.get_file_as_string(path)
			var name := path.get_base_dir().get_base_dir().get_file() + "/" + path.get_file()
			if text.length() > SHARE_MOST:
				continue
			if to > 0:
				_receive_pack_file.rpc_id(to, kind, name, text)
			else:
				_receive_pack_file.rpc(kind, name, text)
	share_props(to)


@rpc("authority", "call_remote", "reliable")
func _receive_pack_file(kind: String, name: String, text: String) -> void:
	var key := kind + "/" + name
	if not SHARE_KINDS.has(kind) or text.length() > SHARE_MOST or _from_host.get(key, "") == text:
		return
	_from_host[key] = text
	var shared_by := "the host's " + name
	match kind:
		"weapons", "creatures":
			if ClassDB.class_exists("GeodriveHunt"):
				GeodriveHunt.read_file(text, kind, shared_by)	# (Core checks each: one that can't be is turned away)
		"props":
			var parsed = JSON.parse_string(text)
			for entry in (parsed if parsed is Array else [parsed]):
				if entry is Dictionary and entry.get("id") is String and entry.get("look") is Dictionary and game.builder:
					game.builder.props.add_kind(entry)
		"reserves":
			var parsed = JSON.parse_string(text)
			if parsed is Dictionary and game.hunt and game.hunt.share_reserve(parsed):
				print("Geodrive net: the host's game %s" % parsed["name"])


# The props on the host's map (each with its height above the ground: each player's ground is their own, a little apart)
func share_props(to: int = 0) -> void:
	if not is_host() or game.builder == null or game.world == null:
		return
	var props: PropKeeper = game.builder.props
	var list := []
	var kinds := {}
	for entry in props.placed:
		if entry.is_empty():
			list.append({})
			continue
		list.append(props.shared_entry(entry))
		if props.kinds.has(entry["id"]):
			kinds[entry["id"]] = props.kinds[entry["id"]]
	var text := JSON.stringify({"kinds": kinds, "placed": list})
	if to > 0:
		_receive_props.rpc_id(to, text)
	else:
		_receive_props.rpc(text)


@rpc("authority", "call_remote", "reliable")
func _receive_props(text: String) -> void:
	var parsed = JSON.parse_string(text)
	if not (parsed is Dictionary) or game.builder == null:
		return
	_from_host["props_placed"] = "1"
	var props: PropKeeper = game.builder.props
	var kinds = parsed.get("kinds", {})
	if kinds is Dictionary:
		for id in kinds:
			if kinds[id] is Dictionary and kinds[id].get("look") is Dictionary:
				props.add_kind(kinds[id])
	var list = parsed.get("placed", [])
	if list is Array:
		props.use_shared(list)


# A joiner put a prop (its kind with it: the host may not have it) or took one away: the host does it, and tells everyone
func prop_put(entry: Dictionary, kind: Dictionary) -> void:
	_prop_put.rpc_id(1, JSON.stringify(entry), JSON.stringify(kind))


func prop_take(index: int) -> void:
	_prop_take.rpc_id(1, index)


@rpc("any_peer", "call_remote", "reliable")
func _prop_put(entry_text: String, kind_text: String) -> void:
	if not is_host() or game.builder == null or entry_text.length() + kind_text.length() > SHARE_MOST:
		return
	var entry = JSON.parse_string(entry_text)
	var kind = JSON.parse_string(kind_text)
	if entry is Dictionary and kind is Dictionary and game.builder.props.put_shared(entry, kind):
		share_props()


@rpc("any_peer", "call_remote", "reliable")
func _prop_take(index: int) -> void:
	if is_host() and game.builder and game.builder.props.take_index(index):
		share_props()


# Out of the host's game: what came from it goes, yours back as it was
func _forget_shared() -> void:
	_from_host = {}
	if game.builder:
		game.builder.props.stop_sharing()
	if game.hunt:
		game.hunt.forget_shared()
	if ClassDB.class_exists("GeodriveHunt"):
		for kind in ["weapons", "creatures"]:
			for path in Packs.files(kind):
				if not path.begins_with(Packs.OWN):
					GeodriveHunt.read_file(FileAccess.get_file_as_string(path), kind, path)


# --- the cars ---

func _process(delta: float) -> void:
	_cars_on_foot()
	var walker_now: Node3D = game.walker
	if walker_now and walker_now.down_wanted and _down_until > 0.0 and Time.get_ticks_msec() / 1000.0 >= _down_until:
		_get_up()
	if not is_online():
		return
	var world: Node = game.world
	var car: Node3D = game.car
	if world == null:
		return
	# this player's car, about 20 times a second
	_send_timer -= delta
	if car and game.waiting == "" and _send_timer <= 0.0:
		_send_timer = SEND_EVERY
		var at: PackedFloat64Array = world.to_lat_lon(car.global_position)
		_receive_car.rpc(at[0], at[1], car.global_position.y, car.global_transform.basis.get_rotation_quaternion(),
			car.linear_velocity, car.get_steer_degrees(), car.get_speed_kmh() / 3.6, car.body, player_name)
	# just joined, the area in: beside the host
	if _join_pending and car and game.waiting == "" and _cars.has(1) and not _cars[1]["updates"].is_empty():
		_join_pending = false
		var last: Array = _cars[1]["updates"][-1]
		var host_at: Vector3 = world.from_lat_lon(last[1], last[2])
		var facing: Vector3 = (last[4] as Quaternion) * Vector3.FORWARD
		game.place_car(host_at + (last[4] as Quaternion) * Vector3.RIGHT * 4.0, facing, "Joined %s's game" % _cars[1]["name"])
		_set_status("Joined %s's game" % _cars[1]["name"])
	# on foot: where this player stands (sent with the car's, as often)
	var walker: Node3D = game.walker
	if walker and _send_timer == SEND_EVERY:
		var here: PackedFloat64Array = world.to_lat_lon(walker.global_position)
		var rifle: bool = game.hunt != null and game.hunt.weapon != null and not game.hunt.weapon.load.get("arrow", false)
		# (their height above the ground, not Godot's: each player's ground is built round their own area, a metre or
		# two apart in places)
		_receive_person.rpc(here[0], here[1], walker.global_position.y - world.ground_height(walker.global_position), walker.rotation.y, walker.ground_speed, walker.height_cm, rifle, walker.down_wanted, player_name)
	# the host: its farm's animals, to everyone
	_animals_timer -= delta
	if is_host() and game.hunt and game.hunt.living >= 0 and game.hunt.hunter and _animals_timer <= 0.0:
		_animals_timer = ANIMALS_EVERY
		_receive_animals.rpc(game.hunt.living, game.hunt.hunter.pack())
	# everyone else's: a little in the past, between the two updates round that moment
	var now := Time.get_ticks_msec() / 1000.0
	for id in _cars:
		_show_car(_cars[id], now - SHOW_DELAY, world)
	for id in _people:
		_show_person(_people[id], now, world)


@rpc("any_peer", "call_remote", "unreliable_ordered")
func _receive_car(lat: float, lon: float, height: float, rotation: Quaternion, velocity: Vector3, steer: float, speed: float, body: int, driver: String) -> void:
	var id := multiplayer.get_remote_sender_id()
	if not _cars.has(id) or _cars[id]["body"] != body:
		_remove_car(id)
		_make_car(id, body)
	var entry: Dictionary = _cars[id]
	entry["name"] = driver
	var updates: Array = entry["updates"]
	var now := Time.get_ticks_msec() / 1000.0
	updates.append([now, lat, lon, height, rotation, velocity, steer, speed])
	while updates.size() > 0 and updates[0][0] < now - 1.5:
		updates.pop_front()


func _make_car(id: int, body: int) -> void:
	var node: RigidBody3D = ClassDB.instantiate("GeodriveCar")
	node.body = body
	node.name = "Player%d" % id
	game.add_child(node)
	node.set_remote(true)
	var label := Label3D.new()
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.fixed_size = true
	label.pixel_size = 0.0015
	label.font_size = 28
	label.outline_size = 8
	label.modulate = Color(0.4, 0.85, 1.0)
	label.position = Vector3(0.0, 2.4, 0.0)
	node.add_child(label)
	_cars[id] = { "node": node, "label": label, "name": "", "body": body, "updates": [] }


func _remove_car(id: int) -> void:
	if _cars.has(id):
		var node: Node = _cars[id]["node"]
		if is_instance_valid(node):
			node.queue_free()
		_cars.erase(id)


func _show_car(entry: Dictionary, show_time: float, world: Node) -> void:
	var updates: Array = entry["updates"]
	var node: RigidBody3D = entry["node"]
	if updates.is_empty() or not is_instance_valid(node):
		return
	var a: Array = updates[0]
	var b: Array = a
	var t := 0.0
	if show_time >= updates[-1][0]:
		a = updates[-1]
		b = a
	else:
		for i in range(updates.size() - 1):
			if updates[i + 1][0] >= show_time:
				a = updates[i]
				b = updates[i + 1]
				t = clampf((show_time - a[0]) / maxf(b[0] - a[0], 0.001), 0.0, 1.0)
				break
	var pa := _position(a, world)
	var pb := _position(b, world)
	var at := pa.lerp(pb, t)
	if a == b and show_time > a[0]:
		at += (a[5] as Vector3) * minf(show_time - a[0], MAX_AHEAD)	# late: carried on along its velocity
	var rotation := (a[4] as Quaternion).slerp(b[4] as Quaternion, t)
	node.global_transform = Transform3D(Basis(rotation), at)
	# their name, and how far away they are (as in the Unreal game)
	var label: Label3D = entry["label"]
	var mine: Node3D = game.car
	if mine:
		var metres := mine.global_position.distance_to(at)
		label.text = "%s  %s" % [entry["name"], ("%d m" % roundi(metres)) if metres < 1000.0 else ("%.1f km" % (metres / 1000.0))]
	else:
		label.text = entry["name"]
	node.set_remote_state(lerpf(a[6], b[6], t), lerpf(a[7], b[7], t))


# --- on foot ---

@rpc("any_peer", "call_remote", "unreliable_ordered")
func _receive_person(lat: float, lon: float, above: float, yaw: float, speed: float, height_cm: float, rifle: bool, down: bool, who: String) -> void:
	var id := multiplayer.get_remote_sender_id()
	if not _people.has(id) or not is_equal_approx(_people[id]["height"], height_cm):
		_remove_person(id)
		_make_person(id, height_cm)
	var entry: Dictionary = _people[id]
	entry["name"] = who
	var updates: Array = entry["updates"]
	var now := Time.get_ticks_msec() / 1000.0
	updates.append([now, lat, lon, above, yaw, speed, rifle, down])
	while updates.size() > 0 and updates[0][0] < now - 1.5:
		updates.pop_front()


func _make_person(id: int, height_cm: float) -> void:
	var node: Walker = Walker.new(height_cm)
	node.name = "Person%d" % id
	node.make_puppet()
	node.set_meta("peer", id)
	game.add_child(node)
	var label := Label3D.new()
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.fixed_size = true
	label.pixel_size = 0.0015
	label.font_size = 26
	label.outline_size = 8
	label.modulate = Color(1.0, 0.85, 0.4)
	label.position = Vector3(0.0, height_cm / 100.0 + 0.35, 0.0)
	node.add_child(label)
	_people[id] = { "node": node, "label": label, "name": "", "height": height_cm, "updates": [] }


func _remove_person(id: int) -> void:
	if _people.has(id):
		var node: Node = _people[id]["node"]
		if is_instance_valid(node):
			node.queue_free()
		_people.erase(id)


func _show_person(entry: Dictionary, now: float, world: Node) -> void:
	var updates: Array = entry["updates"]
	var node: Walker = entry["node"]
	if not is_instance_valid(node):
		return
	# (no word from them a moment: they're in their car, or gone)
	node.visible = not updates.is_empty() and updates[-1][0] > now - PERSON_GONE
	if not node.visible:
		return
	var show_time := now - SHOW_DELAY
	var a: Array = updates[-1]
	var b: Array = a
	var t := 0.0
	for i in range(updates.size() - 1):
		if updates[i + 1][0] >= show_time:
			a = updates[i]
			b = updates[i + 1]
			t = clampf((show_time - a[0]) / maxf(b[0] - a[0], 0.001), 0.0, 1.0)
			break
	var at: Vector3 = world.from_lat_lon(lerpf(a[1], b[1], t), lerpf(a[2], b[2], t))
	node.global_position = Vector3(at.x, world.ground_height(at) + lerpf(a[3], b[3], t), at.z)
	node.rotation.y = lerp_angle(a[4], b[4], t)
	node.ground_speed = lerpf(a[5], b[5], t)
	node.carry(b[6])
	node.down_wanted = b[7]
	var label: Label3D = entry["label"]
	var mine: Node3D = game.walker if game.walker else game.car
	var metres := mine.global_position.distance_to(node.global_position) if mine else 0.0
	label.text = "%s  %d m" % [entry["name"], roundi(metres)] if mine else entry["name"]


# Whose body (a peer id) a node is, if it's someone else's: -1 if not
func person_of(node: Object) -> int:
	return int(node.get_meta("peer", -1)) if node is Walker and node.puppet else -1


# --- the animals: the host's ---

@rpc("authority", "call_remote", "unreliable_ordered")
func _receive_animals(farm: int, bytes: PackedByteArray) -> void:
	if game.hunt:
		game.hunt.shared(farm, bytes)


# An animal this player hit (in someone else's game): the host's told, to do it to the real one
func animal_shot(farm: int, index: int, from: Vector3, down: bool, dies_after: float, wounded: bool) -> void:
	var here: PackedFloat64Array = game.world.to_lat_lon(from)
	_animal_shot.rpc_id(1, farm, index, here[0], here[1], down, dies_after, wounded)


@rpc("any_peer", "call_remote", "reliable")
func _animal_shot(farm: int, index: int, lat: float, lon: float, down: bool, dies_after: float, wounded: bool) -> void:
	if game.hunt and game.hunt.living == farm and game.hunt.hunter:
		game.hunt.hunter.apply_shot(index, game.world.from_lat_lon(lat, lon), down, dies_after, wounded)


# --- shots ---

# This player fired: everyone hears it (where it was, what it was)
func fired(at: Vector3, kind: String) -> void:
	if not is_online() or game.world == null:
		return
	var here: PackedFloat64Array = game.world.to_lat_lon(at)
	_heard_shot.rpc(here[0], here[1], at.y, kind)


@rpc("any_peer", "call_remote", "reliable")
func _heard_shot(lat: float, lon: float, height: float, kind: String) -> void:
	var world: Node = game.world
	var ear: Node3D = game.camera
	if world == null or ear == null:
		return
	var at: Vector3 = world.from_lat_lon(lat, lon)
	at.y = height
	var metres := ear.global_position.distance_to(at)
	var loud := clampf(-20.0 * log(maxf(metres, 5.0) / 5.0) / log(10.0), -60.0, 0.0)
	print("Geodrive net: heard a %s %d m off" % [kind, roundi(metres)])
	if game.hunt:
		game.hunt.heard_shot(at, kind, metres / 343.0, loud)


# This player's shot stopped on someone: they're told (who shot them, where, from how far)
func hit_person(id: int, zone: String, metres: float, energy: float) -> void:
	_you_were_shot.rpc_id(id, player_name, zone, metres, energy)


@rpc("any_peer", "call_remote", "reliable")
func _you_were_shot(by: String, zone: String, metres: float, energy: float) -> void:
	var walker: Walker = game.walker
	if walker == null or walker.down_wanted:
		return
	walker.down_wanted = true
	_down_until = Time.get_ticks_msec() / 1000.0 + DOWN_FOR
	game.hud.set_status("Shot by %s: %s, from %d m (%d ft-lb). Up again by your car in a few seconds" % [by, zone, roundi(metres), roundi(energy)], DOWN_FOR)
	print("Geodrive net: shot by %s (%s, %d m)" % [by, zone, roundi(metres)])


# Up again, beside the car (as on arriving)
func _get_up() -> void:
	var walker: Walker = game.walker
	_down_until = 0.0
	walker.down_wanted = false
	var car: Node3D = game.car
	if car:
		walker.global_position = car.global_position + car.global_transform.basis.x * -2.2 + Vector3.UP * 0.5
		walker.velocity = Vector3.ZERO


# --- cars and people ---

# This player on foot, and the cars round them: a friend's (its driver told), the traffic's, their own rolling away
func _cars_on_foot() -> void:
	var walker: Walker = game.walker
	var world: Node = game.world
	if walker == null or walker.down_wanted or walker.puppet or world == null:
		return
	var cars := []	# [node, velocity, its driver (a peer id; 0: none), who]
	for id in _cars:
		var entry: Dictionary = _cars[id]
		if is_instance_valid(entry["node"]) and not entry["updates"].is_empty():
			cars.append([entry["node"], entry["updates"][-1][5], id, "%s's car" % entry["name"]])
	for child in world.get_children():
		if child is RigidBody3D and String(child.name).begins_with("Traffic"):
			cars.append([child, (child as RigidBody3D).linear_velocity, 0, "a car"])
	var own: RigidBody3D = game.car
	if own and not own.freeze and own.linear_velocity.length() > 0.5:
		cars.append([own, own.linear_velocity, 0, "your own car (rolling)"])
	var middle: Vector3 = walker.global_position + Vector3.UP * float(walker.measures["height"]) * 0.5
	var radius := float(walker.measures["radius"])
	for c in cars:
		var car: Node3D = c[0]
		if car.global_position.distance_squared_to(middle) > 100.0:
			continue
		var box := _car_box(car)
		var frame: Transform3D = car.global_transform * box[1]
		var local: Vector3 = frame.affine_inverse() * middle
		var half: Vector3 = box[0] * 0.5
		# (touching counts: the physics keeps the body a few centimetres off the car's own shape, never in its box)
		var into := Vector3(half.x + radius + TOUCH - absf(local.x), half.y + 0.9 - absf(local.y), half.z + radius + TOUCH - absf(local.z))
		if into.x <= 0.0 or into.y <= 0.0 or into.z <= 0.0:
			continue
		if (frame.affine_inverse() * walker.global_position).y > -half.y + 0.2:
			continue	# (standing in it or on it: carried, not hit)
		var velocity: Vector3 = c[1]
		var closing: float = (velocity - walker.velocity).length()
		if closing < KNOCKED_AT:
			# (pushed aside: out by the nearer side or end)
			var out := Vector3(signf(local.x) * into.x, 0.0, 0.0) if into.x < into.z else Vector3(0.0, 0.0, signf(local.z) * into.z)
			walker.global_position += frame.basis * out
			continue
		walker.down_wanted = true
		_down_until = Time.get_ticks_msec() / 1000.0 + DOWN_FOR
		walker.velocity = Vector3.ZERO
		var kmh := roundi(closing * 3.6)
		game.hud.set_status("Hit by %s at %d km/h. Up again by your car in a few seconds" % [c[3], kmh], DOWN_FOR)
		print("Geodrive net: hit by %s at %d km/h" % [c[3], kmh])
		if int(c[2]) > 0 and is_online():
			_you_hit_someone.rpc_id(int(c[2]), player_name, closing)
		elif car == own:
			own.linear_velocity *= own.mass / (own.mass + PERSON_KG)
		return


# A car's box (its own, or a bakkie's) and where it sits on the car: kept on the car once found
static func _car_box(car: Node3D) -> Array:
	if car.has_meta("box"):
		return car.get_meta("box")
	var box := [Vector3(2.0, 1.5, 4.8), Transform3D(Basis.IDENTITY, Vector3(0.0, 0.75, 0.0))]
	for child in car.get_children():
		if child is CollisionShape3D and child.shape is BoxShape3D:
			box = [child.shape.size, child.transform]
			break
	car.set_meta("box", box)
	return box


# This player's car hit someone (they tell it): it loses their share of its speed
@rpc("any_peer", "call_remote", "reliable")
func _you_hit_someone(who: String, closing: float) -> void:
	var car: RigidBody3D = game.car
	if car and not car.freeze:
		car.linear_velocity *= car.mass / (car.mass + PERSON_KG)
	game.hud.set_status("You hit %s at %d km/h" % [who, roundi(closing * 3.6)], 4.0)
	print("Geodrive net: hit %s at %d km/h" % [who, roundi(closing * 3.6)])


func _position(update: Array, world: Node) -> Vector3:
	var on_ground: Vector3 = world.from_lat_lon(update[1], update[2])
	return Vector3(on_ground.x, update[3], on_ground.z)


# For the debug line and the tests: where the others' cars are
func describe() -> String:
	var parts := PackedStringArray()
	for id in _cars:
		var node: Node3D = _cars[id]["node"]
		parts.append("%s at %s" % [_cars[id]["name"], node.global_position if is_instance_valid(node) else "-"])
	return ", ".join(parts)
