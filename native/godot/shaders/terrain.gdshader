// terrain.gdshader - static ground shader. Port of src/world/terrain/material.js
// (browser reference, git a59773d), plus baked grass detail.
// Vertex COLOR is the classifier's linear albedo; CUSTOM0 = (rock, wet, snow).
// No time uniform, no animation, no shadows received (PLAN: static world).
shader_type spatial;
render_mode cull_back, diffuse_lambert, specular_disabled, shadows_disabled;

uniform float detail_fade = 260.0;
// Grass detail from worldcore_bake (generated/ground.bin): an RGB multiplier
// around 1.0 and its normal map, sampled in world space at two scales.
uniform sampler2D grass_albedo : hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D grass_normal : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float grass_scale = 0.25;       // texture repeats per metre (a 4 m tile)
uniform float grass_strength = 1.0;
uniform float grass_fade_end = 140.0;   // metres: detail gone by here

varying vec3 v_mask;
varying vec3 v_world;

float t_hash(vec2 p) {
	p = fract(p * vec2(123.34, 345.45));
	p += dot(p, p + 34.345);
	return fract(p.x * p.y);
}

float t_noise(vec2 p) {
	vec2 i = floor(p), f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(t_hash(i), t_hash(i + vec2(1.0, 0.0)), f.x),
			mix(t_hash(i + vec2(0.0, 1.0)), t_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void vertex() {
	v_mask = CUSTOM0.rgb;
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	// World-aligned texture axes for the normal map: U along +X, V along +Z.
	TANGENT = normalize(vec3(1.0, 0.0, 0.0) - NORMAL * NORMAL.x);
	BINORMAL = normalize(cross(NORMAL, TANGENT));
}

void fragment() {
	vec3 albedo = COLOR.rgb;
	float dist = length(v_world - CAMERA_POSITION_WORLD);
	float fade = 1.0 - smoothstep(detail_fade * 0.45, detail_fade, dist);
	float m = t_noise(v_world.xz * 0.11) * 0.62 + t_noise(v_world.xz * 0.71) * 0.38;
	albedo *= 1.0 + (m - 0.5) * 0.46 * fade;

	float rough = 0.97;
	float rock = v_mask.x;
	float wet = v_mask.y;
	float snow = v_mask.z;

	// Grass detail: vegetated ground only (not rock, snow or standing wet), and
	// only near the camera, where individual blades can be resolved at all.
	float grassy = clamp((1.0 - rock * 1.6) * (1.0 - snow) * (1.0 - wet * 0.8), 0.0, 1.0);
	float near = 1.0 - smoothstep(grass_fade_end * 0.35, grass_fade_end, dist);
	float gw = grassy * near * grass_strength;
	if (gw > 0.001) {
		// One albedo and one normal read. The macro noise above (m) already breaks
		// up the 4 m repeat, so a second, coarser read was measured and dropped.
		vec2 uv = v_world.xz * grass_scale;
		albedo *= mix(vec3(1.0), texture(grass_albedo, uv).rgb * 2.0, gw);
		NORMAL_MAP = mix(vec3(0.5, 0.5, 1.0), texture(grass_normal, uv).rgb, gw);
		NORMAL_MAP_DEPTH = 1.0;
	}

	if (rock > 0.001) {
		float rn = t_noise(v_world.xz * 0.33 + v_world.y * 0.21) * 0.6 + t_noise(v_world.xz * 1.9) * 0.4;
		vec3 stone = vec3(0.132, 0.126, 0.118) * (0.62 + rn * 0.85);
		albedo = mix(albedo, stone, rock * (0.55 + 0.45 * fade));
		rough = mix(rough, 0.82, rock);
	}
	// Wetness telegraphs risk (REALISM.md R2): darker and glossier = wetter =
	// more viscous, before the wheels commit.
	albedo *= 1.0 - wet * 0.55;
	rough = mix(rough, 0.28, wet);

	ALBEDO = albedo;
	ROUGHNESS = rough;
}
