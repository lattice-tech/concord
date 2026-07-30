#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_pos, vec4, 0);
BUFFER_RW(s_fieldAccum, uint, 9);

uniform vec4 u_fluidParams[16];

/*
 * Free-surface scalar field construction: every fluid particle splats its
 * kernel weight into the surrounding field vertices (fixed-point atomics).
 * The field is the normalized SPH number density, so it hovers near 1 inside
 * the liquid and the iso level (default 0.5) tracks the free surface. This
 * is NOT a metaball sum — it is the same kernel density the solver measures,
 * which is what keeps the reconstructed volume honest.
 */
NUM_THREADS(FLUID_THREADS, 1, 1)
void main()
{
	uint id = gl_GlobalInvocationID.x;
	uint count = floatBitsToUint(u_fluidParams[0].z);
	if (id >= count)
	{
		return;
	}
	float spacing = u_fluidParams[1].x;
	float h = u_fluidParams[1].y;
	vec3 fieldOrigin = u_fluidParams[10].xyz;
	float fieldCell = u_fluidParams[10].w;
	ivec3 dims = ivec3(u_fluidParams[11].xyz);
	float fixedScale = u_fluidParams[12].x;

	vec3 p = s_pos[id].xyz;
	vec3 base = (p - fieldOrigin) / fieldCell;
	ivec3 center = ivec3(floor(base));
	float weight = spacing * spacing * spacing * fixedScale;

	for (int dz = -2; dz <= 2; ++dz)
	for (int dy = -2; dy <= 2; ++dy)
	for (int dx = -2; dx <= 2; ++dx)
	{
		ivec3 v = center + ivec3(dx, dy, dz);
		if (any(lessThan(v, ivec3(0, 0, 0))) || any(greaterThan(v, dims)))
		{
			continue;
		}
		vec3 vp = fieldOrigin + vec3(v) * fieldCell;
		float w = FluidKernelW(length(p - vp), h) * weight;
		uint vi = uint(v.x) + uint(dims.x + 1)
		    * (uint(v.y) + uint(dims.y + 1) * uint(v.z));
		atomicAdd(s_fieldAccum[vi], uint(w));
	}
}
