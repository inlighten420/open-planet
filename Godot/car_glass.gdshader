// A car's glass: tinted and see-through looking straight through it, turning into a mirror of the sky and the world
// round it at a glancing angle (Fresnel), as real glass does: so from outside a car's windows read as glass, not
// holes, and from inside the windscreen is there but clear. GeodriveCar gives its glass this (its tint from the
// corners' colour).
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_never, diffuse_burley, specular_schlick_ggx;

void fragment() {
	float facing = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);	// (Schlick's, glass's 4% head on)
	ALBEDO = COLOR.rgb;
	// from outside the cabin's dark behind it and the sky's in it: most of what's seen is the reflection; from inside
	// (the back face) the world's bright beyond it, so it's nearly clear
	float base = FRONT_FACING ? 0.78 : 0.12;	// (its outside: the front face)
	ALPHA = clamp(base + 0.25 * (1.0 - facing) + 0.6 * fresnel, 0.0, 0.95);
	ROUGHNESS = 0.03;
	METALLIC = 0.0;
	SPECULAR = 1.0;
}
