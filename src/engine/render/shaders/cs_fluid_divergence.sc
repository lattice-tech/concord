#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_pos, vec4, 0);
BUFFER_RO(s_vel, vec4, 1);
BUFFER_RW(s_factors, float, 3);
BUFFER_RO(s_sorted, uint, 5);
BUFFER_RO(s_cellStart, uint, 6);
BUFFER_RO(s_cellCount, uint, 7);

uniform vec4 u_fluidParams[16];

/*
 * DFSPH divergence solver, error half: measures d rho / dt of the predicted
 * velocity field and stores the per-particle stiffness k^v_i. Boundary
 * particles contribute with zero velocity. The paired apply pass then makes
 * the field divergence-free; running this solver is half of what keeps the
 * fluid from pumping volume (the density solver is the other half).
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
	vec3 gridOrigin = u_fluidParams[3].xyz;
	float cell = u_fluidParams[3].w;
	ivec3 dims = ivec3(u_fluidParams[4].xyz);

	vec3 p_i = s_pos[id].xyz;
	vec3 v_i = s_vel[id].xyz;
	ivec3 home = ivec3(floor((p_i - gridOrigin) / cell));

	float divergence = 0.0;
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
			vec3 v_j = j < count ? s_vel[j].xyz : vec3_splat(0.0);
			divergence += mass * dot(v_i - v_j, FluidKernelGrad(rij, h));
		}
	}

	float alpha = s_vel[id].w;
	s_factors[id] = alpha * divergence / max(dt, 1.0e-6);
}
