// The ground: its colours as the world builds them (the vertex colours: grass, forest, sand along the shore, rock on
// steep slopes, the seabed) with real textures' detail on top. Each texture is used as a change to that colour (its
// own colour over its average), so the land keeps the colours it has, gains grass blades, sand grains and rock, and
// the light picks out their bumps. Grass and sand are seen from above; rock wraps round steep slopes (three ways, so
// it doesn't stretch).
// Soft noise at three sizes (about 900, 220 and 55 m) keeps the land from being one flat colour, as the Garden Route
// looks from above: grass goes between dark bush and bright pasture (bushier on slopes), now and then dry, lighter
// and darker; on slopes bare earth shows in patches (cuttings, hillsides,
// from ~20 degrees), and rock on steeper ones (from ~30 degrees) before they're steep enough for bare rock. Past the near detail, the same textures at eight times the size give the
// middle distance some grain; far off only the noise's colours are left. ground.gd sets it up.
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform sampler2D grass_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D grass_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D sand_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D sand_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D rock_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D rock_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
// soft seamless noise, 0-1 (ground.gd makes it)
uniform sampler2D noise : filter_linear_mipmap, repeat_enable;
// each texture's average colour (linear): the detail is the texture over this
uniform vec3 grass_mean = vec3(0.1);
uniform vec3 sand_mean = vec3(0.3);
uniform vec3 rock_mean = vec3(0.2);
// metres one texture covers
uniform float grass_size = 3.0;
uniform float sand_size = 4.0;
uniform float rock_size = 8.0;
// how far the detail goes (m): full to 40% of it, gone by it; 0 = plain colours. The coarse grain goes 4 times as far
uniform float detail_far = 400.0;
// how strongly the detail changes the colour (1 = as the texture), the bumps, and the coarse grain
uniform float colour_strength = 0.8;
uniform float bump_strength = 1.0;
uniform float grain_strength = 0.5;
// grass: the world's colour times these (dark bush, bright pasture, dry), and how much lighter and darker it goes
uniform vec3 bush_tint = vec3(0.33, 0.55, 0.4);
uniform vec3 pasture_tint = vec3(1.1, 1.2, 0.6);
uniform vec3 dry_tint = vec3(1.7, 1.15, 1.3);
uniform float shade_strength = 0.45;
// bare earth's and rock's colours (linear; the world's rock is darker, for the plain look), and where earth and
// rock start on slopes (lower = more)
uniform vec3 earth_tint = vec3(0.055, 0.04, 0.026);
uniform vec3 rock_tint = vec3(0.075, 0.068, 0.058);
uniform float bare_from = 0.55;
uniform float rock_from = 0.55;

// past the blankets (canopy.gdshader, to 9.6 km), wooded ground is coloured as their treetops: the world gives each
// point's tree and shrub cover in the colour's alpha (1 bare, 0 closed forest); this far it fades to that green
uniform vec3 canopy_tint = vec3(0.034, 0.055, 0.026);
uniform float canopy_from = 9100.0;	// m: starts here (under the far blanket's edge)
uniform float canopy_full = 9600.0;	// m: all of it by here

// the roads painted into the ground far off (GeodriveWorld's road map, 51 km round the camera: each 25 m cell the
// offset to the nearest road and its half width, so its edges are drawn crisp at any distance; and where it's built
// up): from ~2 km out, where the 3D roads thin out, so from the air the roads and towns are always there
global uniform sampler2D road_map : filter_linear;	// (no smaller versions: its offsets don't average)
global uniform vec3 road_origin;	// its north-west corner (Godot)
global uniform float road_size;	// m across
global uniform float road_from;	// m: painted in from here, fully by twice as far (main.gd: 1,800 driving, none flying high,
	// where no 3D roads come in under you)
uniform vec3 road_tint = vec3(0.035, 0.035, 0.04);
uniform vec3 town_tint = vec3(0.13, 0.12, 0.105);

varying vec3 world_position;
// where Godot's 0 is in the world (GeodriveWorld.get_render_offset): the patterns are laid by the world's own
// position, so they stay put when Godot's 0 moves (by Godot's they jumped every 2 km)
global uniform vec3 world_offset;
varying vec3 pattern_position;
varying vec3 world_normal;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	pattern_position = world_position + world_offset;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

// a normal map's tilt: they're stored as two channels (x, y), so z is worked out from them
vec3 unpack(vec4 map) {
	vec2 xy = map.xy * 2.0 - 1.0;
	return vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
}

// a texture's bump from above (the whiteout blend: its tilt added to the ground's)
vec3 bump_from_above(vec3 map, vec3 n) {
	return vec3(map.x + n.x, abs(map.z) * n.y, map.y + n.z);
}

// the textures' colour over their averages, at a scale (1 = near, bigger = coarser), as much of each as is there
// (sand's is earth's too)
vec3 detail_at(float scale, float grass, float sand, float rock, vec3 w) {
	vec3 detail = vec3(0.0);
	vec2 top = pattern_position.xz / scale;
	if (grass > 0.0) {
		detail += grass * texture(grass_color, top / grass_size).rgb / grass_mean;
	}
	if (sand > 0.0) {
		detail += sand * texture(sand_color, top / sand_size).rgb / sand_mean;
	}
	if (rock > 0.0) {
		vec3 p = pattern_position / (rock_size * scale);
		vec3 c = texture(rock_color, p.zy).rgb * w.x + texture(rock_color, p.xz).rgb * w.y + texture(rock_color, p.xy).rgb * w.z;
		detail += rock * c / rock_mean;
	}
	return detail;
}

void fragment() {
	vec3 base = COLOR.rgb;
	vec3 n = normalize(world_normal);
	float distance_away = length(world_position - CAMERA_POSITION_WORLD);
	float near = detail_far > 0.0 ? 1.0 - smoothstep(detail_far * 0.4, detail_far, distance_away) : 0.0;
	float grain = detail_far > 0.0 ? grain_strength * (1.0 - near) * (1.0 - smoothstep(detail_far * 1.5, detail_far * 4.0, distance_away)) : 0.0;

	// the noise, about 900, 220 and 55 m across (moved apart so they don't line up), from -0.5 to 0.5
	vec2 xz = pattern_position.xz;
	float big = texture(noise, xz / 900.0).r - 0.5;
	float mid = texture(noise, xz / 220.0 + vec2(0.37, 0.61)).r - 0.5;
	float small = texture(noise, xz / 55.0 + vec2(0.71, 0.13)).r - 0.5;
	float steep = 1.0 - n.y;

	// which is which: rock where the world put it (as Terrain.cpp: over ~35 degrees) and in patches from ~30;
	// bare earth in patches from ~20 (the noise's edges, so they're broken and don't follow the slope's lines); sand
	// by its red-yellow colour; the rest grass (forest too, darker)
	float rock = max(clamp((0.86 - n.y) / 0.1, 0.0, 1.0), smoothstep(rock_from, rock_from + 0.12, steep * 3.5 + mid * 0.8));
	float earth = smoothstep(bare_from, bare_from + 0.15, steep * 3.5 + mid * 0.6 + small * 0.6) * (1.0 - rock);
	float sand = smoothstep(0.6, 1.1, base.r / max(base.g, 0.0005)) * (1.0 - rock - earth);
	float grass = max(1.0 - rock - earth - sand, 0.0);

	// the colours: grass bush or pasture (the big and middle noise, bushier on slopes), dry here and there (the big
	// noise's highs), everything a little lighter or darker
	float pasture = clamp(0.5 + big * 1.2 + mid * 1.0 - steep * 5.0, 0.0, 1.0);
	float dry = smoothstep(0.22, 0.4, big + small * 0.3) * 0.6;
	vec3 grass_tint = mix(mix(bush_tint, pasture_tint, pasture), dry_tint, dry);
	float shade = 1.0 + (mid * 0.6 + small) * shade_strength;
	vec3 colour = (grass * base * grass_tint + sand * base + earth * earth_tint + rock * rock_tint) * shade;

	// the rock's three ways: onto the slopes facing east-west, up-down, north-south, weighted by which way it faces
	vec3 w = pow(abs(n), vec3(4.0));
	w /= (w.x + w.y + w.z);
	vec3 bumped = n;

	if (near > 0.0) {
		colour *= mix(vec3(1.0), detail_at(1.0, grass, sand + earth, rock, w), near * colour_strength);
		vec3 tilt = vec3(0.0);
		vec2 top = pattern_position.xz;
		if (grass > 0.0) {
			tilt += grass * bump_from_above(unpack(texture(grass_normal, top / grass_size)), n);
		}
		if (sand + earth > 0.0) {
			tilt += (sand + earth) * bump_from_above(unpack(texture(sand_normal, top / sand_size)), n);
		}
		if (rock > 0.0) {
			vec3 p = pattern_position / rock_size;
			vec3 tx = unpack(texture(rock_normal, p.zy));
			vec3 ty = unpack(texture(rock_normal, p.xz));
			vec3 tz = unpack(texture(rock_normal, p.xy));
			tx = vec3(tx.xy + n.zy, abs(tx.z) * n.x);
			ty = vec3(ty.xy + n.xz, abs(ty.z) * n.y);
			tz = vec3(tz.xy + n.xy, abs(tz.z) * n.z);
			tilt += rock * normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
		}
		bumped = normalize(mix(n, normalize(tilt), near * bump_strength));
	}
	if (grain > 0.0) {
		colour *= mix(vec3(1.0), detail_at(8.0, grass, sand + earth, rock, w), grain);
	}

	// the far woods: past the blankets, the ground where the trees are takes their green
	float wooded = (1.0 - COLOR.a) * smoothstep(canopy_from, canopy_full, distance_away);
	if (wooded > 0.0) {
		colour = mix(colour, canopy_tint, wooded);
	}

	// the far roads and towns, painted (the 3D ones fade into these)
	vec2 road_uv = (world_position.xz - road_origin.xz) / road_size;
	if (road_uv.x > 0.0 && road_uv.y > 0.0 && road_uv.x < 1.0 && road_uv.y < 1.0) {
		float far_off = road_from < 1.0 ? 1.0 : smoothstep(road_from, road_from * 1.95, distance_away);
		if (far_off > 0.0) {
			vec4 painted = texture(road_map, road_uv);
			vec2 to_road = (painted.rg * 255.0 - 128.0) * 0.5;	// m, east and south
			float half_width = painted.b * 255.0 * 0.25;	// m
			// the road's edge a pixel soft; one thinner than a pixel still a line a pixel wide, a little fainter (as the eye
			// still picks out a thin road from the air)
			float pixel = max(length(fwidth(pattern_position.xz)), 0.05);
			float seen = max(half_width, 0.5 * pixel);
			float road = clamp((seen + 0.5 * pixel - length(to_road)) / pixel, 0.0, 1.0) * mix(0.55, 1.0, clamp(2.0 * half_width / pixel, 0.0, 1.0));
			road *= 1.0 - smoothstep(14000.0, 22000.0, distance_away);	// (past ~20 km a 25 m cell is under a pixel: it would shimmer)
			colour = mix(colour, town_tint, painted.a * 0.55 * far_off);
			colour = mix(colour, road_tint, road * far_off);
		}
	}

	ALBEDO = clamp(colour, 0.0, 1.0);
	ROUGHNESS = mix(0.95, 0.85, rock);
	SPECULAR = 0.35;	// (grass, earth and rock hardly shine)
	NORMAL = normalize((VIEW_MATRIX * vec4(bumped, 0.0)).xyz);
}
