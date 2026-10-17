// The buildings: walls of brick, plaster, concrete, stone or glass with a window to every bay of every floor, and
// flat roofs. What each building is comes from the world (Core's Buildings.cpp) in its corners' colour: R its kind
// (0.1 a house, 0.3 industrial, 0.5 flats, 0.7 offices and shops, 0.9 a tower), G a number of its own (its colour,
// its windows), B its walls when the map says (0 not said, 0.2 brick, 0.4 stone, 0.6 concrete, 0.8 glass,
// 1 plaster). Its texture coordinates are in 3 m: along a wall from its corner, and up it in floors from the
// building's foot (so the windows sit on its floors); on the roof, across it. Where the map doesn't say the walls,
// the kind and the building's own number choose them, more brick in the UK (brick_share). Far off the windows blend
// into the wall's colour (they would only shimmer) and the textures' detail fades. buildings.gd sets it up.
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform sampler2D brick_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D brick_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D plaster_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D plaster_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D concrete_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D concrete_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D stone_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D stone_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D roof_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D roof_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 plaster_mean = vec3(0.4);
uniform vec3 concrete_mean = vec3(0.4);
uniform vec3 roof_mean = vec3(0.1);
// the share of houses and flats of brick where the map doesn't say (the UK: most; elsewhere: few)
uniform float brick_share = 0.2;
// how far the textures' detail goes (m) and the windows (m: they blend into the wall by here)
uniform float detail_far = 600.0;
uniform float windows_far = 1500.0;

varying vec3 world_position;
varying vec3 world_normal;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// a rectangle in a cell (0-1 each way), its edges softened by how big a pixel is, so it doesn't shimmer
float box(vec2 cell, vec4 rect, vec2 pixel) {
	vec2 low = smoothstep(rect.xy - pixel, rect.xy + pixel, cell);
	vec2 high = 1.0 - smoothstep(rect.zw - pixel, rect.zw + pixel, cell);
	return low.x * low.y * high.x * high.y;
}

// plaster and painted concrete: the colours walls are painted (linear), one each by the building's own number
vec3 paint(float own) {
	float k = fract(own * 7.0);
	if (k < 0.3) return vec3(0.62, 0.6, 0.55);	// off-white
	if (k < 0.5) return vec3(0.6, 0.52, 0.36);	// cream
	if (k < 0.62) return vec3(0.58, 0.47, 0.3);	// sand
	if (k < 0.72) return vec3(0.5, 0.33, 0.24);	// terracotta
	if (k < 0.85) return vec3(0.46, 0.46, 0.45);	// light grey
	return vec3(0.42, 0.45, 0.48);	// cool grey
}

void fragment() {
	float kind = COLOR.r;
	float own = COLOR.g;
	float said = COLOR.b;
	vec3 n = normalize(world_normal);
	float away = length(world_position - CAMERA_POSITION_WORLD);
	float near = detail_far > 0.0 ? 1.0 - smoothstep(detail_far * 0.4, detail_far, away) : 0.0;
	vec3 colour;
	vec3 bumped = n;
	float rough = 0.9;
	float shine = 0.5;

	if (n.y > 0.5) {
		// a flat roof: dark slabs, a little lighter or darker by the building
		vec2 uv = UV * 3.0 / 4.0;
		vec3 tex = texture(roof_color, uv).rgb / roof_mean;
		vec3 tint = vec3(0.09, 0.088, 0.085) * (0.7 + own * 0.6);
		colour = tint * mix(vec3(1.0), tex, near);
		vec3 map = texture(roof_normal, uv).xyz * 2.0 - 1.0;
		bumped = normalize(mix(n, normalize(vec3(map.x + n.x, abs(map.z) * n.y, map.y + n.z)), near * 0.6));
	} else if (n.y < -0.5) {
		colour = vec3(0.1);	// underneath (a canopy, a building over a street)
	} else {
		// the walls: which material
		float walls = said;	// 0 not said: chosen
		if (walls < 0.1) {
			if (kind < 0.2) walls = own < brick_share ? 0.2 : 1.0;	// houses: brick or plaster
			else if (kind < 0.4) walls = own < brick_share * 0.6 ? 0.2 : 0.6;	// industrial: concrete, some brick
			else if (kind < 0.6) walls = own < brick_share * 0.8 ? 0.2 : (own > 0.75 ? 0.6 : 1.0);	// flats
			else if (kind < 0.8) walls = own > 0.65 ? 0.8 : (own < brick_share * 0.4 ? 0.2 : 0.6);	// offices: glass or concrete
			else walls = own > 0.25 ? 0.8 : 0.6;	// towers: mostly glass
		}
		bool brick = walls < 0.3, stone = walls >= 0.3 && walls < 0.5, concrete = walls >= 0.5 && walls < 0.7;
		bool glass = walls >= 0.7 && walls < 0.9;

		// the wall's own surface (texture sizes: brick 2 x 1 m, stone 3 x 1.5, plaster 2 x 1, concrete 3 x 1.5)
		vec2 metres = UV * 3.0;
		vec3 tex;
		vec3 map;
		if (brick) {
			vec2 uv = metres / vec2(2.0, 1.0);
			tex = texture(brick_color, uv).rgb * (0.85 + own * 0.3);
			map = texture(brick_normal, uv).xyz;
		} else if (stone) {
			vec2 uv = metres / vec2(3.0, 1.5);
			tex = texture(stone_color, uv).rgb;
			map = texture(stone_normal, uv).xyz;
		} else if (concrete || glass) {
			vec2 uv = metres / vec2(3.0, 1.5);
			tex = paint(own) * 0.9 * mix(vec3(1.0), texture(concrete_color, uv).rgb / concrete_mean, near);
			map = texture(concrete_normal, uv).xyz;
		} else {
			vec2 uv = metres / vec2(2.0, 1.0);
			tex = paint(own) * mix(vec3(1.0), texture(plaster_color, uv).rgb / plaster_mean, near);
			map = texture(plaster_normal, uv).xyz;
		}
		colour = tex;
		// the bumps: across the wall (its length) and up it
		vec3 across = normalize(vec3(-n.z, 0.0, n.x));
		vec3 tilt = map * 2.0 - 1.0;
		bumped = normalize(mix(n, normalize(n * abs(tilt.z) + across * tilt.x + vec3(0.0, 1.0, 0.0) * tilt.y), near * 0.7));

		// the windows: one to each 3 m bay of each 3 m floor, sized by the kind (and a little by the building)
		vec2 cell = fract(UV);
		vec2 bay = floor(UV);
		vec4 rect;	// left, bottom, right, top in the cell
		if (glass) rect = vec4(0.04, 0.06, 0.96, 0.94);	// a glass wall, its frames
		else if (kind < 0.2) rect = vec4(0.3 - own * 0.08, 0.33, 0.7 + own * 0.08, 0.78);	// houses
		else if (kind < 0.4) rect = vec4(0.15, 0.62, 0.85, 0.82);	// industrial: high narrow windows
		else if (kind < 0.6) rect = vec4(0.22, 0.3, 0.78, 0.8);	// flats
		else rect = vec4(0.08, 0.28, 0.92, 0.86);	// offices: ribbon windows
		if (bay.y < 0.5 && kind >= 0.6 && kind < 0.8 && !glass) rect = vec4(0.05, 0.05, 0.95, 0.85);	// shopfronts
		if (kind >= 0.2 && kind < 0.4 && mod(bay.x, 2.0) > 0.5) rect = vec4(0.0);	// industrial: every other bay
		vec2 pixel = fwidth(UV) * 1.2;
		float window = box(cell, rect, pixel);
		// far off: the windows' share of the wall, as a shade (no pattern to shimmer)
		float coverage = (rect.z - rect.x) * (rect.w - rect.y);
		float far_off = smoothstep(windows_far * 0.4, windows_far, away);
		window = mix(window, coverage * 0.8, far_off);
		// glass: dark, the sky in it; some windows lighter (curtains, blinds), by the window
		float inside = hash(bay + vec2(own * 91.0, own * 17.0));
		vec3 pane = mix(vec3(0.012, 0.016, 0.022), vec3(0.06, 0.055, 0.05), step(0.75, inside));
		colour = mix(colour, pane, window);
		rough = mix(rough, 0.15, window);	// (glass, not a mirror: a mirror sparkles in the distance)
		shine = mix(shine, 0.9, window);
		bumped = normalize(mix(bumped, n, window));
	}

	if (kind < 0.02) {
		// infrastructure (Core's R 0): a tunnel's tube and portals, a gallery's roof: bare concrete, no windows, lit by its
		// own lights inside (its ceiling most: in the sun's shadow it was black)
		vec3 rough_concrete = texture(concrete_color, UV * vec2(3.0, 6.0)).rgb / concrete_mean;
		colour = vec3(0.42, 0.41, 0.39) * mix(vec3(1.0), rough_concrete, near);
		rough = 0.92;
		shine = 0.35;
		bumped = n;
		EMISSION = colour * (n.y < -0.5 ? 0.55 : 0.18);
	}
	ALBEDO = clamp(colour, 0.0, 1.0);
	ROUGHNESS = rough;
	SPECULAR = shine;
	NORMAL = normalize((VIEW_MATRIX * vec4(bumped, 0.0)).xyz);
}
