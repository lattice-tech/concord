$input v_ray, v_uv

#include <bgfx_shader.sh>

// Upsamples the low-resolution premultiplied smoke march result with a
// **depth-aware (bilateral) 4-tap upsample**: the four nearest low-res texels
// are weighted both by their bilinear distance and by how closely their
// marched device depth matches the full-resolution surface at this pixel.
// A naive bilinear upsample blends smoke samples whose rays hit completely
// different depths (e.g. one that stopped at a foreground character and one
// that continued into the background smoke), which reads as a blurry halo
// around silhouettes; weighting by depth similarity instead favors the
// low-res texels that agree with the actual surface here, keeping edges crisp
// (mirrors the volumetric-cloud pipeline's plain bilinear where depth
// discontinuities are rare; smoke sits much closer to geometry so this extra
// step matters more). Falls back smoothly to plain bilinear when all four
// taps agree (flat depth region).
SAMPLER2D(s_smComposite, 0);
SAMPLER2D(s_smLowDepth, 1);
SAMPLER2D(s_smFullDepth, 2);

// x,y: low-res texel size in full-res UV units. z: depth similarity sharpness.
uniform vec4 u_smUpsample;

void main()
{
	// All three sources are offscreen RTs (top-left origin on Vulkan, this
	// engine's only backend); flip V so samples land at the same screen
	// position as the scene, not mirrored (mirrors fs_volcloud_composite).
	vec2 uv = vec2(v_uv.x, 1.0 - v_uv.y);
	float fullDepth = texture2D(s_smFullDepth, uv).x;

	vec2 lowTexel = u_smUpsample.xy;
	vec2 basePos = uv / lowTexel - 0.5;
	vec2 baseTexel = floor(basePos);
	vec2 frac = basePos - baseTexel;

	vec4 accum = vec4_splat(0.0);
	float weightSum = 0.0;
	float sharpness = u_smUpsample.z;
	for (int dy = 0; dy < 2; ++dy)
	{
		for (int dx = 0; dx < 2; ++dx)
		{
			vec2 tapUv = (baseTexel + vec2(float(dx), float(dy)) + 0.5) * lowTexel;
			float bilinearW = (dx == 0 ? (1.0 - frac.x) : frac.x)
				* (dy == 0 ? (1.0 - frac.y) : frac.y);
			float tapDepth = texture2D(s_smLowDepth, tapUv).x;
			float depthDelta = abs(tapDepth - fullDepth);
			float depthW = exp2(-sharpness * depthDelta * depthDelta);
			float w = bilinearW * depthW + 1e-5;
			accum += texture2D(s_smComposite, tapUv) * w;
			weightSum += w;
		}
	}

	gl_FragColor = accum / max(weightSum, 1e-5);
}
