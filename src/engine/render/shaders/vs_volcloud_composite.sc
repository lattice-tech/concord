$input a_position
$output v_uv

#include <bgfx_shader.sh>

// Trivial fullscreen-triangle pass-through for the cloud upsample composite:
// the low-resolution cloud march result is sampled with hardware bilinear
// filtering and blended into the scene, so no view ray or uniforms are needed.
void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	v_uv = a_position.xy * 0.5 + 0.5;
}
