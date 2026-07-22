$input v_uv

#include <bgfx_shader.sh>

// Upsamples the half-resolution premultiplied cloud march result and lets the
// (ONE, INV_SRC_ALPHA) blend composite it over the full-resolution scene color.
// Hardware bilinear filtering on the low-res target does the smooth upscale.
SAMPLER2D(s_vcCloud, 0);

void main()
{
	// The low-res cloud target is an offscreen RT (top-left origin on Vulkan,
	// this engine's only backend); flip V when sampling so the upsampled clouds
	// land at the same screen position as the scene, not mirrored below it.
	gl_FragColor = texture2D(s_vcCloud, vec2(v_uv.x, 1.0 - v_uv.y));
}
