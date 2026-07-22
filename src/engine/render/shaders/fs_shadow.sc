$input v_wpos

#include <bgfx_shader.sh>

// Depth-only fragment shader for the directional-light shadow pass. We render
// into a color+depth RT (R32F color + D24S8 depth) and write the hardware
// post-divide depth (gl_FragCoord.z, in [0,1] for homogeneous-depth backends)
// into the R channel. The scene pass samples this color texture as a regular
// SAMPLER2D, which every backend supports — sampling a depth-stencil texture
// directly is not portable across all bgfx backends. The hardware still
// writes the real depth to the depth attachment for z-testing against the
// shadow map's own geometry.
void main()
{
	gl_FragColor = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
