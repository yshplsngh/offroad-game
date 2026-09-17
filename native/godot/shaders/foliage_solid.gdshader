// foliage_solid.gdshader - rocks, snags and deadfall. Static: no time or wind
// uniforms, no shadows (PLAN.md rules 2 and 3). COLOR is the baked prototype
// colour already multiplied by the MultiMesh per-instance tint.
shader_type spatial;
render_mode cull_back, diffuse_lambert, specular_disabled, shadows_disabled;

void fragment() {
	ALBEDO = COLOR.rgb;
	ROUGHNESS = 0.92;
}
