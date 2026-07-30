$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// RGBA UI image atlas/texture. Unlike fs_debugtext (single-channel coverage),
// the sampled color passes through and the vertex color acts as a tint, so a
// white tint draws the image unchanged.
SAMPLER2D(s_font, 0);

void main()
{
	gl_FragColor = texture2D(s_font, v_texcoord0) * v_color0;
}
