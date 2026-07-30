#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_pos, vec4, 0);
BUFFER_RW(s_vel, vec4, 1);
BUFFER_RO(s_factors, float, 3);
BUFFER_RO(s_sorted, uint, 5);
BUFFER_RO(s_cellStart, uint, 6);
BUFFER_RO(s_cellCount, uint, 7);

uniform vec4 u_fluidParams[16];

/*
 * Shared correction half of both DFSPH solvers: applies the velocity change
 * implied by the per-particle stiffness values stored by the divergence or
 * pressure error pass. Boundary particles contribute through k_i only (they
 * carry no pressure factor of their own and do not move).
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
	float dt = u_fluidParams[0].x;
	float mass = u_fluidParams[1].w;
	float h = u_fluidParams[1].y;
	float rho0 = u_fluidParams[1].z;
	vec3 gridOrigin = u_fluidParams[3].xyz;
	float cell = u_fluidParams[3].w;
	ivec3 dims = ivec3(u_fluidParams[4].xyz);

	vec3 p_i = s_pos[id].xyz;
	float k_i = s_factors[id];
	float rho_i = max(s_pos[id].w, 0.5 * rho0);
	float kOverRho_i = k_i / rho_i;
	ivec3 home = ivec3(floor((p_i - gridOrigin) / cell));

	vec3 delta = vec3_splat(0.0);
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
			float kOverRhoSum = kOverRho_i;
			if (j < count)
			{
				float rho_j = max(s_pos[j].w, 0.5 * rho0);
				kOverRhoSum += s_factors[j] / rho_j;
			}
			delta += mass * kOverRhoSum * FluidKernelGrad(rij, h);
		}
	}

	s_vel[id].xyz = s_vel[id].xyz - dt * delta;
}
