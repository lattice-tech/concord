$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_worldPosition, v_color

#include <bgfx_shader.sh>

uniform mat4 u_gpuParticleWorld;
uniform mat4 u_gpuParticleRotation;
uniform vec4 u_gpuParticleVisual;
uniform vec4 u_gpuParticleDraw;
uniform vec4 u_gpuParticleColorStart;
uniform vec4 u_gpuParticleColorMid;
uniform vec4 u_gpuParticleColorEnd;

float SampleCurve(float startValue, float midValue, float endValue, float t)
{
	return t < 0.5
		? mix(startValue, midValue, t * 2.0)
		: mix(midValue, endValue, (t - 0.5) * 2.0);
}

vec4 SampleCurve(vec4 startValue, vec4 midValue, vec4 endValue, float t)
{
	return t < 0.5
		? mix(startValue, midValue, t * 2.0)
		: mix(midValue, endValue, (t - 0.5) * 2.0);
}

void main()
{
	bool alive = i_data3.w > 0.5 && i_data1.w > 0.0 && i_data0.w < i_data1.w;
	if (!alive)
	{
		gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
		v_texcoord0 = a_texcoord0;
		v_worldPosition = vec3_splat(0.0);
		v_color = vec4_splat(0.0);
		return;
	}

	vec3 worldPosition = i_data0.xyz;
	vec3 worldVelocity = i_data1.xyz;
	if (u_gpuParticleDraw.x > 0.5)
	{
		worldPosition = mul(u_gpuParticleWorld, vec4(i_data0.xyz, 1.0)).xyz;
		worldVelocity = mul(u_gpuParticleRotation, vec4(i_data1.xyz, 0.0)).xyz;
	}

	float t = clamp(i_data0.w / i_data1.w, 0.0, 1.0);
	float size = max(0.0, SampleCurve(u_gpuParticleVisual.x,
		u_gpuParticleVisual.y, u_gpuParticleVisual.z, t));
	vec4 viewCenter = mul(u_view, vec4(worldPosition, 1.0));
	vec2 viewVelocity = mul(u_view, vec4(worldVelocity, 0.0)).xy;
	float speed = length(worldVelocity);
	vec2 along = length(viewVelocity) > 1e-5 ? normalize(viewVelocity) : vec2(1.0, 0.0);
	vec2 across = vec2(-along.y, along.x);
	float angle = i_data2.z * 0.01745329252;
	vec2 corner = vec2(
		a_position.x * cos(angle) - a_position.y * sin(angle),
		a_position.x * sin(angle) + a_position.y * cos(angle));
	float stretch = 1.0 + min(speed * 0.12, 2.2);
	viewCenter.xy += (along * corner.x * stretch + across * corner.y) * (size * 0.5);

	gl_Position = mul(u_proj, viewCenter);
	v_texcoord0 = a_texcoord0;
	v_worldPosition = worldPosition;
	v_color = SampleCurve(u_gpuParticleColorStart, u_gpuParticleColorMid,
		u_gpuParticleColorEnd, t);
}
