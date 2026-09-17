// foliage_leaf.gdshader - needles, leaves, ferns and grass: thin cards seen from
// both sides, so back faces are lit with a flipped normal. Static, no shadows.
shader_type spatial;
render_mode cull_disabled, diffuse_lambert, specular_disabled, shadows_disabled;

void fragment() {
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 0.84;
	if (!FRONT_FACING) {
		NORMAL = -NORMAL;
	}
}
