/*
 * SPDX-License-Identifier: MIT
 * Reference SMAA algorithm by Jorge Jimenez et al. (2013), ported to bgfx.
 */
$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
uniform vec4 u_smaaTexel;
uniform vec4 u_smaaConfig;

float Luma(vec3 color)
{
	return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
	vec2 texel = u_smaaTexel.xy;
	vec2 uv = v_texcoord0;
	float center = Luma(texture2D(s_color, uv).rgb);
	float left = Luma(texture2D(s_color, uv - vec2(texel.x, 0.0)).rgb);
	float top = Luma(texture2D(s_color, uv - vec2(0.0, texel.y)).rgb);
	vec4 delta;
	delta.xy = abs(vec2(center, center) - vec2(left, top));
	vec2 edges = step(vec2(u_smaaConfig.x, u_smaaConfig.x), delta.xy);
	if (dot(edges, vec2(1.0, 1.0)) == 0.0) {
		discard;
	}

	float right = Luma(texture2D(s_color, uv + vec2(texel.x, 0.0)).rgb);
	float bottom = Luma(texture2D(s_color, uv + vec2(0.0, texel.y)).rgb);
	delta.zw = abs(vec2(center, center) - vec2(right, bottom));
	vec2 maxDelta = max(delta.xy, delta.zw);
	float leftLeft = Luma(texture2D(s_color, uv - vec2(2.0 * texel.x, 0.0)).rgb);
	float topTop = Luma(texture2D(s_color, uv - vec2(0.0, 2.0 * texel.y)).rgb);
	delta.zw = abs(vec2(left, top) - vec2(leftLeft, topTop));
	maxDelta = max(maxDelta, delta.zw);
	float finalDelta = max(maxDelta.x, maxDelta.y);
	edges *= step(vec2(finalDelta, finalDelta), 2.0 * delta.xy);
	gl_FragColor = vec4(edges, 0.0, 1.0);
}
