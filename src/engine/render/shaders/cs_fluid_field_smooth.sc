#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_fieldAccum, uint, 9);
SAMPLER3D(s_fieldTex, 0);
IMAGE3D_RW(s_fieldImg, r32f, 1);

uniform vec4 u_fluidParams[16];

/*
 * Field normalization + temporal smoothing. The raw splat is converted back
 * to a normalized density, then exponentially blended with last frame's
 * field. This temporal filter is what stops the free surface from jittering
 * between frames — particles move every substep, but the presented surface
 * only glides. The first frame after a reset takes the raw field verbatim.
 */
NUM_THREADS(4, 4, 4)
void main()
{
	ivec3 v = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 dims = ivec3(u_fluidParams[11].xyz);
	if (any(greaterThan(v, dims)))
	{
		return;
	}
	float fixedInv = u_fluidParams[12].y;
	float smoothing = u_fluidParams[6].w;
	uint flags = floatBitsToUint(u_fluidParams[4].w);

	uint vi = uint(v.x) + uint(dims.x + 1)
	    * (uint(v.y) + uint(dims.y + 1) * uint(v.z));
	float fresh = float(s_fieldAccum[vi]) * fixedInv;
	float previous = texelFetch(s_fieldTex, v, 0).r;
	float value = (flags & FLUID_FLAG_FIRST_FIELD) != 0u
	    ? fresh
	    : mix(fresh, previous, clamp(smoothing, 0.0, 0.95));
	imageStore(s_fieldImg, v, vec4(value, 0.0, 0.0, 0.0));
}
