// Plants placed on the processor (GeodriveWorld.place_plants: which grow round the camera, ring by ring, as the
// graphics card had chosen them square by square; now only those that grow are drawn). Each copy
// is a plant: its multimesh transform its place, turn and size; its own numbers (INSTANCE_CUSTOM) its kind and its
// square, from which come its tint, its leaves' patch and its sway. The far one (kind -1) is bent into the square's
// kind here: a cone for a conifer, a tall trunk for a gum. The fragment is vegetation.gdshader's.
// Its ring's edges are cut here, from where the camera is this frame (the processor sends a little more round each
// edge): across an edge a plant fades out in a fine screen dither as the next ring's plant there fades in on the
// pixels it leaves, so one ring hands over to the next with no gap, no plant drawn twice, and no pop.
shader_type spatial;
render_mode cull_back, depth_draw_opaque, diffuse_burley, specular_schlick_ggx;

uniform sampler2D bark : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D leaves : filter_linear_mipmap, repeat_enable;
uniform sampler2D leaf_bumps : hint_normal, filter_linear_mipmap, repeat_enable;
uniform float leaf_tile = 1.6;
uniform float leaf_far = 90.0;
uniform float leaf_strength = 0.55;
uniform float leaf_bump = 0.9;
uniform int kind = 0;	// this ring's plant: 0 broadleaf, 1 conifer, 2 eucalyptus, 3 bush; -1 any tree (bent here)
uniform float fine_cell = 7.0;	// m: the near squares (the sway's phase from where it stands)
uniform float inner = 0.0;	// m: this ring's edges round the camera
uniform float outer = 100.0;
global uniform vec3 plant_eye;	// the camera (set each frame: in the shadows' pass too, where CAMERA_POSITION_WORLD is the sun's)

varying float leaf;
varying vec3 tint;
varying vec3 leaf_at;
varying vec3 leaf_normal;
varying vec2 leaf_turn;
varying float leaf_away;
varying flat float fade_in;	// drawn where the dither is under this (at the inner edge)...
varying flat float fade_out;	// ...and not under this (at the outer)

// How wide (m) the hand-over across an edge at Radius m is: the same for both rings there
float band(float radius) {
	return clamp(radius * 0.08, 3.0, 40.0);
}

float hash(vec2 p) {
	p = fract(p * vec2(0.1031, 0.1030));
	p += dot(p, p.yx + 33.33);
	return fract((p.x + p.y) * p.x);
}

void vertex() {
	leaf = COLOR.r;
	int chosen = int(INSTANCE_CUSTOM.x + 0.5);
	vec2 fine = INSTANCE_CUSTOM.zw;
	vec3 v = VERTEX;
	if (kind < 0 && chosen == 1) {
		// a conifer from the far broadleaf (its crown from ~3 to ~8.8 m up): a cone from 2 m to 15.5 m
		float rise = clamp((v.y - 3.0) / 5.8, 0.0, 1.0);
		v.y = leaf > 0.5 ? 2.0 + rise * 13.5 : v.y;
		v.xz *= leaf > 0.5 ? mix(1.15, 0.05, rise) : 1.0;
	} else if (kind < 0 && chosen == 2) {
		// a gum: the trunk to 12 m, the crown on it, wider
		v.y = leaf > 0.5 ? 12.0 + (v.y - 5.9) * 1.2 : v.y * 2.6;
		v.xz *= leaf > 0.5 ? 1.25 : 1.0;
	}
	leaf_at = v + vec3(hash(fine + 7.1), hash(fine + 8.3), hash(fine + 9.7)) * 20.0;	// (each plant its own patch)
	leaf_normal = NORMAL;
	float turn = hash(fine + 61.3) * 6.2832;
	leaf_turn = vec2(cos(turn), sin(turn));
	vec3 placed = (MODEL_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	leaf_away = length(placed - CAMERA_POSITION_WORLD);
	// where it stands across the ring (each plant's edge a little in or out, so the edge isn't a circle)
	float across = length(placed.xz - plant_eye.xz) + 20.0 * hash(fine + 5.13);
	fade_in = inner > 0.0 ? smoothstep(inner - band(inner), inner + band(inner), across) : 1.0;
	fade_out = smoothstep(outer - band(outer), outer + band(outer), across);
	// the wind: the top sways most, each plant at its own time
	float up = COLOR.g;
	vec2 spot = fine * fine_cell;
	float t = TIME * 1.3 + spot.x * 0.07 + spot.y * 0.05;
	v.xz += vec2(sin(t), cos(t * 0.8)) * up * up * 0.12;
	VERTEX = v;
	// each plant its own green: lighter or darker, yellower or bluer
	float h = hash(fine + 77.7);
	tint = mix(vec3(0.75, 0.88, 0.7), vec3(1.08, 1.02, 0.78), h);
	if (chosen == 1) tint *= vec3(0.75, 0.85, 0.8);	// conifers darker
	if (chosen == 2) tint *= vec3(0.85, 0.95, 0.95);	// gums a grey-blue green
	if (chosen == 3) tint *= mix(vec3(0.85, 0.9, 0.75), vec3(1.0, 0.95, 0.7), hash(fine + 3.3));	// bushes duller
}

void fragment() {
	// (interleaved gradient noise: an even dither, fixed to the screen)
	float dither = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
	if (dither >= fade_in || dither < fade_out) {
		discard;
	}
	if (leaf > 0.5) {
		// leaves: the plant's green, in clumps of leaves near (lighter and darker, bumped), plain far off
		vec3 colour = vec3(0.06, 0.1, 0.035) * tint;
		float near = 1.0 - smoothstep(leaf_far * 0.5, leaf_far, leaf_away);
		if (near > 0.0) {
			vec3 p = leaf_at / leaf_tile;
			vec3 ln = normalize(leaf_normal);
			vec3 w = pow(abs(ln), vec3(4.0));
			w /= w.x + w.y + w.z;
			float clumps = texture(leaves, p.zy).r * w.x + texture(leaves, p.xz).r * w.y + texture(leaves, p.xy).r * w.z;
			colour *= mix(1.0, mix(1.0 - leaf_strength, 1.0 + leaf_strength * 0.6, clumps), near);
			// the bumps: each way's tilt along its two axes, on the plant, then turned as it is
			vec2 bx = texture(leaf_bumps, p.zy).xy * 2.0 - 1.0;
			vec2 by = texture(leaf_bumps, p.xz).xy * 2.0 - 1.0;
			vec2 bz = texture(leaf_bumps, p.xy).xy * 2.0 - 1.0;
			vec3 tilt = w.x * vec3(0.0, bx.y, bx.x) + w.y * vec3(by.x, 0.0, by.y) + w.z * vec3(bz.x, bz.y, 0.0);
			vec3 bumped = normalize(ln + tilt * leaf_bump * near);
			bumped.xz = mat2(vec2(leaf_turn.x, leaf_turn.y), vec2(-leaf_turn.y, leaf_turn.x)) * bumped.xz;
			NORMAL = normalize((VIEW_MATRIX * vec4(bumped, 0.0)).xyz);
		}
		ALBEDO = colour;
		ROUGHNESS = 0.8;
		SPECULAR = 0.25;
		BACKLIGHT = vec3(0.2, 0.25, 0.1);	// (sun through the leaves)
	} else {
		ALBEDO = texture(bark, UV).rgb * 0.8;
		ROUGHNESS = 0.95;
	}
}
