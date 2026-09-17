// vehicle_lamp.gdshader - lamp lenses. CUSTOM0.a is the lamp kind
// (1 head, 2 tail, 3 indicator, 4 aux light bar); each kind has an intensity.
shader_type spatial;
render_mode cull_back, diffuse_burley, specular_schlick_ggx;

uniform float head : hint_range(0.0, 8.0) = 0.0;
uniform float tail : hint_range(0.0, 8.0) = 0.0;
uniform float indicator : hint_range(0.0, 8.0) = 0.0;
uniform float aux : hint_range(0.0, 8.0) = 0.0;

varying float v_kind;

void vertex() {
	v_kind = CUSTOM0.a;
}

void fragment() {
	float k = floor(v_kind + 0.5);
	float on = k == 1.0 ? head : k == 2.0 ? tail : k == 3.0 ? indicator : aux;
	ALBEDO = COLOR.rgb * 0.6;
	ROUGHNESS = 0.12;
	EMISSION = COLOR.rgb * on;
}
