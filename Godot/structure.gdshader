// The player's buildings (GeodriveStructure): each face its finish's texture (planks, brick, concrete, roof tiles; the
// corners' alpha says which: Core's Finish / 8), laid on in the building's own frame so it stays put on it (the face's
// main axis picks the plane: walls get it upright, floors flat), and tinted to the piece's colour (its corners' rgb:
// the material's mean tone). Glass, handles: plain colour.
shader_type spatial;
render_mode diffuse_burley, specular_schlick_ggx;

uniform sampler2D planks_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D brick_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D concrete_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D tiles_color : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
// each texture's mean colour (linear): dividing by it keeps the pattern, the piece's colour the tone
uniform vec3 planks_mean = vec3(0.3);
uniform vec3 brick_mean = vec3(0.2);
uniform vec3 concrete_mean = vec3(0.4);
uniform vec3 tiles_mean = vec3(0.3);

varying vec3 local_position;
varying vec3 local_normal;

void vertex() {
	local_position = VERTEX;
	local_normal = NORMAL;
}

void fragment() {
	int finish = int(round(COLOR.a * 8.0));
	vec3 albedo = COLOR.rgb;
	if (finish > 0) {
		// the plane it's laid on: across a wall's face (upright), or flat on a floor (m)
		vec3 n = abs(local_normal);
		vec2 metres = n.y > max(n.x, n.z) ? local_position.xz : (n.x > n.z ? vec2(local_position.z, local_position.y) : local_position.xy);
		vec3 tex;
		if (finish == 1) {
			tex = texture(planks_color, metres / vec2(1.5, 1.5)).rgb / planks_mean;	// (boards ~15 cm)
		} else if (finish == 2) {
			tex = texture(brick_color, metres / vec2(2.0, 1.0)).rgb / brick_mean;	// (the city's bricks' size)
		} else if (finish == 3) {
			tex = texture(concrete_color, metres / vec2(3.0, 1.5)).rgb / concrete_mean;
		} else {
			tex = texture(tiles_color, metres / vec2(2.0, 1.0)).rgb / tiles_mean;
		}
		albedo *= clamp(tex, vec3(0.0), vec3(2.5));
	}
	ALBEDO = albedo;
	ROUGHNESS = finish == 0 ? 0.3 : 0.85;
}
