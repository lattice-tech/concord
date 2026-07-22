$input v_texcoord0, v_wpos

#include <bgfx_shader.sh>

uniform vec4 u_albedo;
uniform vec4 u_emissive;
uniform vec4 u_texFlags; // z: Material::BlendMode (Opaque = 0)
uniform vec4 u_clipPlane;

void main()
{
	if (dot(u_clipPlane.xyz, u_clipPlane.xyz) > 0.5
		&& dot(u_clipPlane.xyz, v_wpos) + u_clipPlane.w < 0.0)
	{
		discard;
	}
	// Radial distance from sprite centre in billboard UV space.
	vec2 p = v_texcoord0 * 2.0 - 1.0;
	float r2 = dot(p, p);
	float radius = sqrt(r2);
	float edgeWidth = max(fwidth(radius), 0.0015);

	// Three Gaussian scales preserve a bright center while removing a hard edge.
	float core = exp(-r2 * 42.0);
	float body = exp(-r2 * 9.5);
	float halo = exp(-r2 * 2.1);
	float rim = 1.0 - smoothstep(0.72 - edgeWidth, 1.0, radius);
	float profile = clamp(core * 1.35 + body * 0.72 + halo * 0.42, 0.0, 2.5) * rim;

	// Authored colour is sRGB; lift to a warm linear-ish energy so additive HDR
	// blooms cleanly without a muddy mid-grey look under gamma present.
	vec3 authoredSrgb = u_albedo.rgb + u_emissive.rgb * u_emissive.w;
	vec3 authored = pow(max(authoredSrgb, vec3_splat(0.0)), vec3_splat(2.2));

	// Hot core / cool edge: cores drive bloom harder without washing the whole disc.
	float heat = clamp(core * 1.8 + body * 0.35, 0.0, 2.0);
	vec3 hot = authored * vec3(1.15, 1.05, 0.92);
	vec3 cool = authored * vec3(0.55, 0.65, 1.15);
	vec3 lit = mix(cool, hot, clamp(heat, 0.0, 1.0));

	// Boost core energy into HDR (>1) so the bloom knee actually catches sparks.
	vec3 energy = lit * (0.55 + heat * 1.65);

	float alpha = u_albedo.a * clamp(profile, 0.0, 1.0);
	if (alpha <= 1.0 / 255.0)
	{
		discard;
	}

	// Opaque: premultiply by profile. Additive: full energy, alpha weights src.
	bool opaque = u_texFlags.z < 0.5;
	vec3 color = opaque ? energy * profile : energy;
	// Output stays linear-ish HDR; present path / bloom handle display.
	// Slight gamma encode only for opaque so unlit solids still look filled.
	if (opaque)
	{
		color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
	}
	gl_FragColor = vec4(color, alpha);
}
