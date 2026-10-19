# F6, "send to us": what you made last (a picture, a recording, a bug report, a road edit) as an issue on the game's
# GitHub page, the form filled in by the game (the version, where it is, the road edit's lines) and a box for why and what
# to look for; the file's folder opens beside it, to drag the file in. Nothing is sent by the game itself: the player's
# browser opens the page, and they post it (a free GitHub account). No key or password is in the game.
class_name SendToUs

const REPO := "https://github.com/inlighten420/open-planet"
const MAX_LINK := 7500	# (characters of a link the browser and GitHub take; longer text goes on the clipboard instead)


# The issue page for what was made (main.gd's last_made: {kind, path, text, lat, lon}); what to tell the player
static func send(made: Dictionary, dry_run: bool = false) -> String:
	if made.is_empty():
		return "Nothing to send yet: take a picture (%s), a recording (%s), or draw a road edit (%s, then Ctrl+C) first" % [
			Controls.text("screenshot"), Controls.text("record"), Controls.text("road_edit")]
	var kind: String = made["kind"]
	var where := "%.6f, %.6f" % [made["lat"], made["lon"]] if made.has("lat") else ""
	var fields := {"version": GameSettings.VERSION, "where": where}
	var template := "bug-report.yml"
	var title := "Bug: "
	var clipped := false
	match kind:
		"edit":
			template = "road-edit.yml"
			title = "Road edit near %s" % where
			fields["edit"] = made["text"]
		"picture":
			title = "Picture: near %s" % where
			fields["details"] = "A picture (F12), taken at %s. %s" % [where, made.get("text", "")]
		"recording":
			title = "Recording: "
			fields["details"] = "A recording (F4), stopped at %s." % where
		_:
			fields["details"] = "A bug report from the game, at %s. %s" % [where, made.get("text", "")]
	var link := _link(template, title, fields)
	if link.length() > MAX_LINK and fields.has("edit"):
		DisplayServer.clipboard_set(made["text"])
		fields["edit"] = "(too long for the link: it's on your clipboard, paste it here with Ctrl+V)"
		link = _link(template, title, fields)
		clipped = true
	if dry_run:
		return link	# (--sendtest: the link, not opened)
	OS.shell_open(link)
	var path: String = made.get("path", "")
	if path != "":
		OS.shell_show_in_file_manager(path)
	return "Your browser opens the form on GitHub: say why, then Submit%s%s" % [
		" (the road edit is on your clipboard: paste it in)" if clipped else "",
		"; drag in %s from the folder that opened" % path.get_file() if path != "" else ""]


static func _link(template: String, title: String, fields: Dictionary) -> String:
	var link := "%s/issues/new?template=%s&title=%s" % [REPO, template, title.uri_encode()]
	for key in fields:
		if String(fields[key]) != "":
			link += "&%s=%s" % [key, String(fields[key]).uri_encode()]
	return link
