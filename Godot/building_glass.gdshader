// A building's glass (GeodriveStructure's panes): clear looking straight through, a little tinted, turning into a mirror
// of the sky and the world at a glancing angle (Fresnel), as a window does; from inside or out alike (each pane's two
// faces). It casts no shadow: the sun comes in through the windows.
shader_type spatial;
render_mode blend_mix, cull_back, depth_draw_never, diffuse_burley, specular_schlick_ggx;

void fragment() {
	float facing = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);	// (Schlick's, glass's 4% head on)
	ALBEDO = COLOR.rgb;
	ALPHA = clamp(0.1 + 0.15 * (1.0 - facing) + 0.7 * fresnel, 0.0, 0.9);
	ROUGHNESS = 0.03;
	METALLIC = 0.0;
	SPECULAR = 1.0;
}
