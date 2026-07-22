$input v_texcoord0

#include <bgfx_shader.sh>

// Composites the compute ray tracer's output (cs_raytrace) into the scene view.
// RGB is the average display-referred color of the hit subsamples. Alpha packs
// `sampleCount * 2 + deviceDepth`; zero means no hit. The two-unit stride keeps
// count and depth exactly separable in RGBA32F.
SAMPLER2D(s_rtColor, 0);

void main()
{
	vec4 texel = texture2D(s_rtColor, v_texcoord0);
	if (texel.a < 1.5)
	{
		discard;
	}
	float sampleCount = floor(texel.a * 0.5);
	float coverage = sampleCount * 0.25;
	float depth = texel.a - sampleCount * 2.0;
	gl_FragColor = vec4(texel.rgb, coverage);
	gl_FragDepth = depth;
}
