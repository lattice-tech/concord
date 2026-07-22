$input v_texcoord0

#include <bgfx_shader.sh>

// Progressive downsample for the COD:AW / Jimenez bloom mip chain. A 13-tap
// filter (four inner + eight outer taps + centre) whose weights sum to 1, so
// each mip is a stable, well-antialiased half-resolution of the one above with
// no shimmering. The first pass (mip 0, u_bloomParams.z > 0) also applies a
// soft threshold so only HDR-bright energy enters the chain.
SAMPLER2D(s_tex, 0);

// xy: source texel size (1/srcW, 1/srcH); z: mip0 prefilter flag; w: threshold.
uniform vec4 u_bloomParams;

vec3 prefilter(vec3 color)
{
	float threshold = u_bloomParams.w;
	float knee = max(threshold * 0.2, 1e-4);
	float brightness = max(color.r, max(color.g, color.b));
	float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
	soft = soft * soft / (4.0 * knee + 1e-4);
	float contribution = max(brightness - threshold, soft) / max(brightness, 1e-4);
	return color * contribution;
}

void main()
{
	float x = u_bloomParams.x;
	float y = u_bloomParams.y;
	vec2 uv = v_texcoord0;

	vec3 a = texture2D(s_tex, uv + vec2(-2.0 * x,  2.0 * y)).rgb;
	vec3 b = texture2D(s_tex, uv + vec2( 0.0,      2.0 * y)).rgb;
	vec3 c = texture2D(s_tex, uv + vec2( 2.0 * x,  2.0 * y)).rgb;
	vec3 d = texture2D(s_tex, uv + vec2(-2.0 * x,  0.0)).rgb;
	vec3 e = texture2D(s_tex, uv).rgb;
	vec3 f = texture2D(s_tex, uv + vec2( 2.0 * x,  0.0)).rgb;
	vec3 g = texture2D(s_tex, uv + vec2(-2.0 * x, -2.0 * y)).rgb;
	vec3 h = texture2D(s_tex, uv + vec2( 0.0,     -2.0 * y)).rgb;
	vec3 i = texture2D(s_tex, uv + vec2( 2.0 * x, -2.0 * y)).rgb;
	vec3 j = texture2D(s_tex, uv + vec2(-x,  y)).rgb;
	vec3 k = texture2D(s_tex, uv + vec2( x,  y)).rgb;
	vec3 l = texture2D(s_tex, uv + vec2(-x, -y)).rgb;
	vec3 m = texture2D(s_tex, uv + vec2( x, -y)).rgb;
	if (u_bloomParams.z > 0.5) {
		a = prefilter(a); b = prefilter(b); c = prefilter(c);
		d = prefilter(d); e = prefilter(e); f = prefilter(f);
		g = prefilter(g); h = prefilter(h); i = prefilter(i);
		j = prefilter(j); k = prefilter(k); l = prefilter(l); m = prefilter(m);
	}

	vec3 down = e * 0.125;
	down += (a + c + g + i) * 0.03125;
	down += (b + d + f + h) * 0.0625;
	down += (j + k + l + m) * 0.125;

	gl_FragColor = vec4(max(down, vec3_splat(0.0)), 1.0);
}
