#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RW(s_pos, vec4, 0);
BUFFER_RW(s_vel, vec4, 1);
BUFFER_RO(s_sorted, uint, 5);
BUFFER_RO(s_cellStart, uint, 6);
BUFFER_RO(s_cellCount, uint, 7);

uniform vec4 u_fluidParams[16];

/*
 * DFSPH density + factor pass. rho_i sums the cubic-spline kernel over fluid
 * and static boundary particles alike, so wall-adjacent water measures full
 * density instead of dipping (Akinci boundary). alpha_i is the DFSPH
 * stiffness factor of Bender & Koschier (2015) eq. 8; it is recomputed only
 * once per substep (FLUID_FLAG_ALPHA) while rho_i refreshes every iteration.
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
	float mass = u_fluidParams[1].w;
	float h = u_fluidParams[1].y;
	vec3 gridOrigin = u_fluidParams[3].xyz;
	float cell = u_fluidParams[3].w;
	ivec3 dims = ivec3(u_fluidParams[4].xyz);
	uint flags = floatBitsToUint(u_fluidParams[4].w);

	vec3 p_i = s_pos[id].xyz;
	ivec3 home = ivec3(floor((p_i - gridOrigin) / cell));

	float density = mass * FluidKernelW(0.0, h);
	vec3 gradSum = vec3_splat(0.0);
	float gradSqSum = 0.0;

	for (int dz = -1; dz <= 1; ++dz)
	for (int dy = -1; dy <= 1; ++dy)
	for (int dx = -1; dx <= 1; ++dx)
	{
		ivec3 c = home + ivec3(dx, dy, dz);
		if (any(lessThan(c, ivec3(0, 0, 0))) || any(greaterThanEqual(c, dims)))
		{
			continue;
		}
		int ci = c.x + dims.x * (c.y + dims.y * c.z);
		uint begin = s_cellStart[ci];
		uint n = s_cellCount[ci];
		for (uint k = 0u; k < n; ++k)
		{
			uint j = s_sorted[begin + k];
			if (j == id) { continue; }
			vec3 rij = p_i - s_pos[j].xyz;
			float r = length(rij);
			if (r >= h) { continue; }
			density += mass * FluidKernelW(r, h);
			vec3 grad = mass * FluidKernelGrad(rij, h);
			gradSum += grad;
			gradSqSum += dot(grad, grad);
		}
	}

	s_pos[id].w = density;
	if ((flags & FLUID_FLAG_ALPHA) != 0u)
	{
		float denom = dot(gradSum, gradSum) + gradSqSum;
		float alpha = denom > 1.0e-6 ? density / denom : 0.0;
		s_vel[id].w = alpha;
	}
}
