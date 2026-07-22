$input a_position
$output v_ray, v_uv

#include <bgfx_shader.sh>

// Reconstructs a world-space view ray from the fullscreen triangle, matching
// vs_sky so the volumetric cloud pass marches along the same rays the scene
// was rendered with. u_vcCamera.w is the near-plane NDC z (0 on Vulkan/D3D,
// -1 on GL) so the near point is reconstructed on the correct clip plane.
uniform mat4 u_vcInvViewProj;
uniform vec4 u_vcCamera;

void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	vec4 nearClip = mul(u_vcInvViewProj, vec4(a_position.xy, u_vcCamera.w, 1.0));
	vec4 farClip = mul(u_vcInvViewProj, vec4(a_position.xy, 1.0, 1.0));
	vec3 nearWorld = nearClip.xyz / nearClip.w;
	vec3 farWorld = farClip.xyz / farClip.w;
	v_ray = farWorld - nearWorld;
	v_uv = a_position.xy * 0.5 + 0.5;
}
