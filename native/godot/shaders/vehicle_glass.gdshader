// vehicle_glass.gdshader - tinted glass: alpha blend, no depth write, both sides.
// CUSTOM0.a is the pane's opacity.
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_burley, specular_schlick_ggx;

varying float v_alpha;

void vertex() {
	v_alpha = CUSTOM0.a;
}

void fragment() {
	ALBEDO = COLOR.rgb;
	ALPHA = v_alpha;
	ROUGHNESS = 0.05;
	METALLIC = 0.0;
	SPECULAR = 0.6;
}
