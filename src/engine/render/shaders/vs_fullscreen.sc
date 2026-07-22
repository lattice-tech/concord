$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

// x: flip V (1 = invert V when sampling offscreen into the swap chain).
// y/z: texel size; w: FXAA quality.
// Vulkan/D3D: offscreen RT V is opposite the window — must flip (see Draw).
uniform vec4 u_presentParams;

void main()
{
	// Fullscreen triangle is already in clip space; UV from NDC, optional V flip.
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	vec2 uv = a_position.xy * 0.5 + 0.5;
	uv.y = mix(uv.y, 1.0 - uv.y, u_presentParams.x);
	v_texcoord0 = uv;
}
