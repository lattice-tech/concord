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
 * DFSPH constant-density solver, error half: predicts rho* from the measured
 * base density (pos.w, refreshed after integration) plus the divergence of
 * the current velocity field, and stores k_i for the paired apply pass.
 * Only positive errors are corrected (compression), which avoids artificial
 * particle attraction under tension — standard DFSPH practice.
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
	float dt2 = u_fluidParams[0].y;
	float mass = u_fluidParams[1].w;
	float h = u_fluidParams[1].y;
	float rho0 = u_fluidParams[1].z;
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

	float predicted = s_pos[id].w + dt * divergence;
	float error = max(predicted - rho0, 0.0);
	float alpha = s_vel[id].w;
	s_factors[id] = alpha * error / max(dt2, 1.0e-8);
}
