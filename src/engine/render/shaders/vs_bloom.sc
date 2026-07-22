$input a_position
$output v_texcoord0

#include <bgfx_shader.sh>

// Fullscreen-triangle vertex shader for the bloom passes. Unlike vs_fullscreen
// it never flips V: every bloom pass reads and writes offscreen RGBA16F targets
// that share one orientation, so the flip only belongs in the final present.
void main()
{
	gl_Position = vec4(a_position.xy, 0.0, 1.0);
	v_texcoord0 = a_position.xy * 0.5 + 0.5;
}
