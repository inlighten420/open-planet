// The forests far off, as one blanket: a sheet over the land round the camera, a vertex on each of the plant map's
// 10 m cells, lifted to the treetops where the map has trees (as thick as they are: thin woods lower, the edges of
// woods sloping down into the ground like a wood's edge does) and sunk under the ground where it has none, so only
// the woods show. Its top is lumpy with crowns (a pattern of round domes about 6 m across, each its own green),
// fading to a plain dark green far off where they'd only shimmer. Near the camera it fades out (each crown at its own
// distance) and the single trees (vegetation_placed.gdshader) take over. A second, coarser sheet (40 m squares, from the far
// plant map) carries on from the near one's edge (2 km) to 9.6 km, and a third (200 m squares, from the horizon's
// plant map) from there to the view distance, so the woods reach as far as the land. vegetation.gd sets them up.
shader_type spatial;
render_mode cull_back, depth_draw_opaque, diffuse_burley, specular_schlick_ggx;

global uniform sampler2D plant_map : filter_linear;
global uniform sampler2D plant_heights : filter_linear;
global uniform vec3 plant_origin;
global uniform float plant_size;
global uniform vec3 plant_offset;
global uniform vec3 plant_eye;
global uniform float plant_region;
global uniform sampler2D plant_map_far : filter_linear;	// (the far map: 20 m cells, 10 km)
global uniform sampler2D plant_heights_far : filter_linear;
global uniform vec3 plant_origin_far;
global uniform float plant_size_far;
global uniform sampler2D plant_map_horizon : filter_linear;	// (the horizon's: 200 m cells, to the view distance)
global uniform sampler2D plant_heights_horizon : filter_linear;
global uniform vec3 plant_origin_horizon;
global uniform float plant_size_horizon;

uniform bool far = false;	// the far sheet (from the far map), past the near one
uniform bool horizon = false;	// (with far) the horizon's sheet, from its map, past the far one
uniform float shown = 1.0;	// how much of it is in (0-1): vegetation.gd fades it in and out, crown by crown
uniform float cell = 10.0;	// m: the sheet's squares (the near map's cells; the far sheet's twice its map's)
uniform float inner = 140.0;	// m: the blanket starts here (the single trees inside; the far sheet: where the near one ends)
uniform float outer = 2000.0;	// m: and ends here (the plant map's edge)
uniform float crown = 6.0;	// m: the crowns' size

varying float cover;
varying vec3 fixed_position;	// world-fixed (the crowns stay put as the camera moves)

float hash(vec2 p) {
	p = fract(p * vec2(0.1031, 0.1030));
	p += dot(p, p.yx + 33.33);
	return fract((p.x + p.y) * p.x);
}

// this sheet's map: where (its north-west corner), how wide (m), how many cells across
vec2 map_origin() { return horizon ? plant_origin_horizon.xz : (far ? plant_origin_far.xz : plant_origin.xz); }
float map_size() { return horizon ? plant_size_horizon : (far ? plant_size_far : plant_size); }
float map_cells() {
	return float(horizon ? textureSize(plant_heights_horizon, 0).x : (far ? textureSize(plant_heights_far, 0).x : textureSize(plant_heights, 0).x));
}
vec4 map_at(vec2 uv) { return horizon ? texture(plant_map_horizon, uv) : (far ? texture(plant_map_far, uv) : texture(plant_map, uv)); }
vec2 heights_at(vec2 uv) {
	return horizon ? texture(plant_heights_horizon, uv).rg : (far ? texture(plant_heights_far, uv).rg : texture(plant_heights, uv).rg);
}

// the trees' height (m) over the ground at a place (Godot), and how covered it is (0-1)
float top_at(vec2 at, out float thick) {
	vec2 uv = (at - map_origin()) / map_size();
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
		thick = 0.0;	// (off the map: none, not the map's far side wrapped round)
		return -1.0e4;
	}
	vec4 map = map_at(uv);
	vec2 ground_room = heights_at(uv);
	// a notch where roads and buildings are (they're cleared on the map, but only in whole cells)
	thick = smoothstep(0.2, 0.7, map.r) * smoothstep(-1.0, 4.0, ground_room.g);
	// none over rock faces (as the single trees: thinning from ~30 degrees, gone by ~38)
	float texel = 1.0 / map_cells();
	float across = map_size() * texel * 2.0;
	vec4 sides = vec4(heights_at(uv + vec2(texel, 0.0)).r, heights_at(uv - vec2(texel, 0.0)).r,
		heights_at(uv + vec2(0.0, texel)).r, heights_at(uv - vec2(0.0, texel)).r);
	thick *= 1.0 - smoothstep(0.58, 0.78, length(vec2(sides.x - sides.y, sides.z - sides.w)) / across);
	float tall = plant_region > 0.5 ? 13.0 : 11.0;	// (the Cape's pines and gums taller than the UK's oaks)
	return ground_room.r - 3.0 + thick * (tall + 3.0);
}

void vertex() {
	// on the map's cells' middles, round the camera
	vec2 origin = map_origin();
	float map_cell = map_size() / map_cells();
	vec2 base = origin + map_cell * 0.5 + floor((plant_eye.xz - origin) / cell) * cell;	// (on the map's cells' middles)
	vec2 at = base + VERTEX.xz;
	float thick;
	float y = top_at(at, thick);
	float dummy;
	float east = top_at(at + vec2(cell, 0.0), dummy) - top_at(at - vec2(cell, 0.0), dummy);
	float south = top_at(at + vec2(0.0, cell), dummy) - top_at(at - vec2(0.0, cell), dummy);
	VERTEX = vec3(at.x, y, at.y);
	NORMAL = normalize(vec3(-east, 2.0 * cell, -south));
	cover = thick;
	fixed_position = VERTEX + plant_offset;
}

void fragment() {
	float d = length(fixed_position.xz - plant_offset.xz - plant_eye.xz);
	// the crowns: the nearest of a scatter of points, one in each crown-sized square
	vec2 p = fixed_position.xz / crown;
	vec2 square = floor(p);
	float nearest = 9.0;
	vec2 middle = p;
	float own = hash(square + 5.13);
	// the lumps fade where a crown is under a few pixels (far off they're left out, but for the two sheets' seam,
	// where each crown says which sheet it's on)
	float lumps = 1.0 - smoothstep(0.15, 0.5, length(fwidth(p)));
	bool seam = abs(d - (far ? inner : outer)) < 200.0;
	for (int i = -1; i <= 1 && (lumps > 0.0 || seam); i++) {
		for (int k = -1; k <= 1; k++) {
			vec2 s = square + vec2(float(i), float(k));
			vec2 m = s + 0.2 + 0.6 * vec2(hash(s), hash(s + 17.31));
			float e = length(p - m);
			if (e < nearest) {
				nearest = e;
				middle = m;
				own = hash(s + 5.13);
			}
		}
	}
	// near: fade out (each crown at its own distance), so the single trees take over; far: the map's edge (the near
	// sheet's edge is the far sheet's start, crown by crown)
	float start = far ? inner - 150.0 * own : inner + 40.0 * own;
	if (cover < 0.08 || d < start || d > outer - 150.0 * own || own >= shown) {
		discard;
	}
	vec2 off = (p - middle) / 0.75;
	float r = min(length(off), 1.0);
	vec3 dome = normalize(vec3(off.x, sqrt(1.0 - r * r) + 0.3, off.y));
	vec3 n = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 bumped = normalize(mix(n, normalize(n + vec3(dome.x, 0.0, dome.z) * 1.2), lumps));
	// each crown its own green, darker in the gaps between crowns
	vec3 green = plant_region > 0.5 ? vec3(0.035, 0.058, 0.03) : vec3(0.04, 0.065, 0.025);
	green *= mix(vec3(0.75, 0.85, 0.85), vec3(1.25, 1.15, 0.9), own);
	float gap = mix(1.0, 1.0 - 0.55 * smoothstep(0.55, 1.0, r), lumps);
	ALBEDO = green * gap;
	ROUGHNESS = 0.85;
	SPECULAR = 0.2;
	BACKLIGHT = vec3(0.1, 0.12, 0.05);
	NORMAL = normalize((VIEW_MATRIX * vec4(bumped, 0.0)).xyz);
}
