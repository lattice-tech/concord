$input v_texcoord0, v_worldPosition, v_color

#include <bgfx_shader.sh>

uniform vec4 u_gpuParticleVisual;
uniform vec4 u_gpuParticleDraw;
uniform vec4 u_gpuParticleClipPlane;

void main()
{
	if (dot(vec4(v_worldPosition, 1.0), u_gpuParticleClipPlane) < 0.0)
	{
		discard;
	}
	vec2 p = v_texcoord0 * 2.0 - 1.0;
	float radiusSquared = dot(p, p);
	float radius = sqrt(radiusSquared);
	float edgeWidth = max(fwidth(radius), 0.0015);
	float core = exp(-radiusSquared * 42.0);
	float body = exp(-radiusSquared * 9.5);
	float halo = exp(-radiusSquared * 2.1);
	float rim = 1.0 - smoothstep(0.72 - edgeWidth, 1.0, radius);
	float profile = clamp(core * 1.35 + body * 0.72 + halo * 0.42,
		0.0, 2.5) * rim;

	vec3 authored = pow(max(v_color.rgb * u_gpuParticleVisual.w,
		vec3_splat(0.0)), vec3_splat(2.2));
	float heat = clamp(core * 1.8 + body * 0.35, 0.0, 2.0);
	vec3 hot = authored * vec3(1.15, 1.05, 0.92);
	vec3 cool = authored * vec3(0.55, 0.65, 1.15);
	vec3 energy = mix(cool, hot, clamp(heat, 0.0, 1.0))
		* (0.55 + heat * 1.65);
	float alpha = v_color.a * clamp(profile, 0.0, 1.0);
	if (alpha <= 1.0 / 255.0)
	{
		discard;
	}

	bool opaque = u_gpuParticleDraw.y < 0.5;
	vec3 color = opaque ? energy * profile : energy;
	if (opaque)
	{
		color = pow(max(color, vec3_splat(0.0)), vec3_splat(1.0 / 2.2));
	}
	gl_FragColor = vec4(color, alpha);
}
