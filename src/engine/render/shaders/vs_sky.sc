$input a_position
$output v_skyDirection

#include <bgfx_shader.sh>

uniform mat4 u_skyInvViewProj;
uniform vec4 u_skyCamera;

void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	vec4 nearClip = mul(u_skyInvViewProj, vec4(a_position.xy, u_skyCamera.w, 1.0));
	vec4 farClip = mul(u_skyInvViewProj, vec4(a_position.xy, 1.0, 1.0));
	vec3 nearWorld = nearClip.xyz / nearClip.w;
	vec3 farWorld = farClip.xyz / farClip.w;
	v_skyDirection = farWorld - nearWorld;
}
