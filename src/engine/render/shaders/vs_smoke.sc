$input a_position
$output v_ray, v_uv

#include <bgfx_shader.sh>

// Reconstructs a world-space view ray from the fullscreen triangle so the
// local volumetric-smoke pass marches along the same rays the scene was
// rendered with (mirrors vs_volcloud). u_smCamera.w is the near-plane NDC z
// (0 on Vulkan) so the near point lands on the correct clip plane.
uniform mat4 u_smInvViewProj;
uniform vec4 u_smCamera;

void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	vec4 nearClip = mul(u_smInvViewProj, vec4(a_position.xy, u_smCamera.w, 1.0));
	vec4 farClip = mul(u_smInvViewProj, vec4(a_position.xy, 1.0, 1.0));
	vec3 nearWorld = nearClip.xyz / nearClip.w;
	vec3 farWorld = farClip.xyz / farClip.w;
	v_ray = farWorld - nearWorld;
	v_uv = a_position.xy * 0.5 + 0.5;
}
