$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Single-channel (R8) glyph atlas: the red channel is anti-aliased coverage.
SAMPLER2D(s_font, 0);

void main()
{
	float coverage = texture2D(s_font, v_texcoord0).r;
	gl_FragColor = vec4(v_color0.rgb, v_color0.a * coverage);
}
