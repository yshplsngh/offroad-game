// vehicle.gdshader - baked procedural vehicle surfaces (opaque and paint layers).
// COLOR = linear albedo, CUSTOM0 = (roughness, metallic, clearcoat, emission).
// One material per layer keeps the whole truck at a handful of draw calls.
shader_type spatial;
render_mode cull_back, diffuse_burley, specular_schlick_ggx;

uniform bool is_paint = false;
uniform vec3 paint_color : source_color = vec3(1.0);
uniform float dirt : hint_range(0.0, 1.0) = 0.0;

varying vec4 v_mat;
varying vec3 v_local;

void vertex() {
	v_mat = CUSTOM0;
	v_local = VERTEX;
}

void fragment() {
	vec3 albedo = COLOR.rgb;
	float rough = v_mat.r;
	float metal = v_mat.g;
	if (is_paint) {
		albedo *= paint_color;
	}
	// Mud: heavier low on the truck, following vehicle-local height.
	float mud = dirt * clamp(1.2 - v_local.y * 0.9, 0.0, 1.0);
	albedo = mix(albedo, vec3(0.243, 0.180, 0.110), mud);
	ALBEDO = albedo;
	ROUGHNESS = mix(rough, 0.97, mud);
	METALLIC = mix(metal, 0.0, mud);
	CLEARCOAT = v_mat.b * (1.0 - mud);
	CLEARCOAT_ROUGHNESS = 0.08;
}
