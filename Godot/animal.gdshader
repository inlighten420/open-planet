// The animals' coats (GeodriveHunt: every part of every animal one copy of a rounded mesh; its colour the coat's, its
// own numbers (INSTANCE_CUSTOM) the pattern: r the pattern (0 plain, 1 paler underneath, 2 stripes, 3 patches, 4 a
// gemsbok's mask and band, 5 a kudu's thin stripes), g how the part lies (0 across: the body and head; 1 along: the neck, legs, tail; 2 no
// pattern: hooves, horns, manes), b 1 on the head, a the animal's own (a little lighter or darker)). The
// patterns are drawn in the part's own metres, so they stay on it as it moves.
shader_type spatial;
render_mode cull_back, depth_draw_opaque, diffuse_burley;

varying vec3 at;	// m: the part's own (its long axis y)
varying float down;	// its surface facing the ground (-1 up, 1 down)
varying flat vec4 custom;

void vertex() {
	vec3 scale = vec3(length(MODEL_MATRIX[0].xyz), length(MODEL_MATRIX[1].xyz), length(MODEL_MATRIX[2].xyz));
	at = VERTEX * scale;
	down = -normalize((MODEL_MATRIX * vec4(NORMAL / (scale * scale), 0.0)).xyz).y;
	custom = INSTANCE_CUSTOM;
}

// a soft-edged 0-1 for a value, its edge as wide as a pixel (stripes too fine to see fade to their mix)
float band(float v, float edge) {
	float w = max(fwidth(v), 1e-4);
	return smoothstep(edge - w, edge + w, v);
}

vec2 hash2(vec2 p) {
	p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
	return fract(sin(p) * 43758.5453);
}

void fragment() {
	int pattern = int(custom.r + 0.5);
	int lies = int(custom.g + 0.5);
	bool head = custom.b > 0.5;
	vec3 coat = COLOR.rgb * (0.92 + 0.16 * custom.a);
	if (lies < 2) {
		if (pattern == 1 || pattern == 4 || pattern == 5) {
			// paler underneath (the belly, under the neck, the insides of the legs)
			coat = mix(coat, vec3(0.78, 0.74, 0.66), band(down, 0.45) * 0.85);
		}
		if (pattern == 2) {
			// a zebra's stripes: across the body (bending back over the rump), round the neck and legs; ~0.2 m apart,
			// the black a little narrower than the white
			float v = lies == 0 ? at.x * 5.0 + at.y * 1.2 * sign(at.x) : at.y * 6.0;
			v += sin(at.y * 7.0 + at.x * 3.0) * 0.15;
			float s = abs(fract(v) - 0.5) * 2.0;
			float black = 1.0 - band(s, 0.45);
			coat = mix(vec3(0.62, 0.6, 0.56), vec3(0.015, 0.014, 0.013), black);
		}
		if (pattern == 3) {
			// a giraffe's patches: brown cells ~0.3 m with cream lines between (the legs paler, plain low down)
			vec2 p = (lies == 0 ? at.xy + at.zz * 0.7 : at.yz + at.xx * 0.7) / 0.3;
			vec2 i = floor(p);
			float d1 = 9.0, d2 = 9.0;
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					vec2 c = i + vec2(float(x), float(y));
					float d = length(p - c - hash2(c) * 0.8);
					if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) { d2 = d; }
				}
			}
			float line = 1.0 - band(d2 - d1, 0.18);
			coat = mix(coat, vec3(0.80, 0.72, 0.55), line);
		}
		if (pattern == 4) {
			// a gemsbok: a black band low along its flank and a black face with a white nose
			if (lies == 0 && !head) {
				coat = mix(coat, vec3(0.02), band(down, 0.12) * (1.0 - band(down, 0.3)));
			}
			if (head) {
				coat = mix(vec3(0.85, 0.83, 0.8), vec3(0.02), band(-down, -0.2));
			}
		}
		if (pattern == 5 && lies == 0 && !head) {
			// a kudu's thin white stripes down its sides (~0.25 m apart, from the spine to halfway down)
			float s = abs(fract(at.x * 4.0 + 0.5) - 0.5) * 2.0;
			coat = mix(coat, vec3(0.82, 0.8, 0.76), (1.0 - band(s, 0.1)) * (1.0 - band(down, 0.0)));
		}
	}
	ALBEDO = coat;
	ROUGHNESS = 0.92;
	SPECULAR = 0.25;
}
