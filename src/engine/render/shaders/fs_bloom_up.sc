$input v_texcoord0

#include <bgfx_shader.sh>

// Progressive upsample for the COD:AW / Jimenez bloom mip chain. A 3x3 tent
// filter reads the smaller mip and is *additively* blended onto the next
// larger mip (blend state set by the backend), so each level accumulates the
// blur of all smaller levels — a wide, smooth, artefact-free glow. The filter
// radius is converted from source texels to UV by the backend for each level,
// keeping the blur footprint consistent and preventing recognizable mip ghosts.
SAMPLER2D(s_tex, 0);

// xy: per-level filter radius in source UV; zw unused.
uniform vec4 u_bloomParams;

void main()
{
	float rx = u_bloomParams.x;
	float ry = u_bloomParams.y;
	vec2 uv = v_texcoord0;

	vec3 a = texture2D(s_tex, uv + vec2(-rx,  ry)).rgb;
	vec3 b = texture2D(s_tex, uv + vec2( 0.0, ry)).rgb;
	vec3 c = texture2D(s_tex, uv + vec2( rx,  ry)).rgb;
	vec3 d = texture2D(s_tex, uv + vec2(-rx,  0.0)).rgb;
	vec3 e = texture2D(s_tex, uv).rgb;
	vec3 f = texture2D(s_tex, uv + vec2( rx,  0.0)).rgb;
	vec3 g = texture2D(s_tex, uv + vec2(-rx, -ry)).rgb;
	vec3 h = texture2D(s_tex, uv + vec2( 0.0, -ry)).rgb;
	vec3 i = texture2D(s_tex, uv + vec2( rx, -ry)).rgb;

	vec3 up = e * 4.0;
	up += (b + d + f + h) * 2.0;
	up += (a + c + g + i);
	up *= (1.0 / 16.0);

	gl_FragColor = vec4(up, 1.0);
}
