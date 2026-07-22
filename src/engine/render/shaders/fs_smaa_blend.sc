/*
 * SPDX-License-Identifier: MIT
 * Reference SMAA algorithm by Jorge Jimenez et al. (2013), ported to bgfx.
 */
$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
SAMPLER2D(s_weights, 1);
uniform vec4 u_smaaTexel;

void main()
{
	vec2 texel = u_smaaTexel.xy;
	vec2 uv = v_texcoord0;
	vec4 weights;
	weights.x = texture2D(s_weights, uv + vec2(texel.x, 0.0)).a;
	weights.y = texture2D(s_weights, uv + vec2(0.0, texel.y)).g;
	weights.wz = texture2D(s_weights, uv).xz;
	if (dot(weights, vec4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
		gl_FragColor = texture2DLod(s_color, uv, 0.0);
		return;
	}

	bool horizontal = max(weights.x, weights.z) > max(weights.y, weights.w);
	vec4 offset = horizontal
		? vec4(weights.x, 0.0, weights.z, 0.0)
		: vec4(0.0, weights.y, 0.0, weights.w);
	vec2 blend = horizontal ? weights.xz : weights.yw;
	blend /= dot(blend, vec2(1.0, 1.0));
	vec4 coordinates = uv.xyxy + offset * vec4(texel, -texel);
	gl_FragColor = blend.x * texture2DLod(s_color, coordinates.xy, 0.0)
		+ blend.y * texture2DLod(s_color, coordinates.zw, 0.0);
}
