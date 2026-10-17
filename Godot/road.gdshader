// A road surface (asphalt, paving, gravel): its colour as the world gives it, with a real texture's detail on top
// (the texture over its own average, so the colour stays), seen from above, bumps the light picks out, big soft
// patches (resurfacing, wear, dust) against the repeating, the detail fading to the plain colour far off. The roads
// have no coordinates along them yet, so the texture is laid by the world's position (asphalt, paving and gravel
// look much the same whichever way they're laid). roads.gd sets it up, one material for each surface.
shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec3 base_colour = vec3(0.025);	// linear
uniform sampler2D detail_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D detail_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform vec3 detail_mean = vec3(0.2);
uniform float detail_size = 3.0;	// m one texture covers
uniform float detail_far = 400.0;	// m: full to 40% of it, gone by it; 0 = plain
uniform float colour_strength = 0.8;
uniform float bump_strength = 0.8;
uniform float patch_size = 35.0;	// m
uniform float patch_strength = 0.25;
uniform float roughness = 0.9;

varying vec3 world_position;
varying vec3 world_normal;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 n = normalize(world_normal);
	float near = detail_far > 0.0 ? 1.0 - smoothstep(detail_far * 0.4, detail_far, length(world_position - CAMERA_POSITION_WORLD)) : 0.0;
	vec2 top = world_position.xz;
	// big soft patches: the texture seen from far above, everywhere
	vec3 patches = texture(detail_color, top / patch_size).rgb / detail_mean;
	vec3 colour = base_colour * mix(1.0, dot(patches, vec3(0.3, 0.5, 0.2)), patch_strength);
	vec3 bumped = n;
	if (near > 0.0) {
		vec2 uv = top / detail_size;
		colour *= mix(vec3(1.0), texture(detail_color, uv).rgb / detail_mean, near * colour_strength);
		vec3 map = texture(detail_normal, uv).xyz * 2.0 - 1.0;
		vec3 tilted = normalize(vec3(map.x + n.x, abs(map.z) * n.y, map.y + n.z));
		bumped = normalize(mix(n, tilted, near * bump_strength));
	}
	ALBEDO = clamp(colour, 0.0, 1.0);
	ROUGHNESS = roughness;
	NORMAL = normalize((VIEW_MATRIX * vec4(bumped, 0.0)).xyz);
}
