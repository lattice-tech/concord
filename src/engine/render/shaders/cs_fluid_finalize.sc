#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_pos, vec4, 0);
BUFFER_RW(s_vel, vec4, 1);
BUFFER_RO(s_prev, vec4, 2);

uniform vec4 u_fluidParams[16];

/*
 * DFSPH velocity update: after the density solve the positions have already
 * moved, so the true substep velocity is recovered from the position delta.
 * This closes the DFSPH step (Bender & Koschier 2015, algorithm 1).
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
	vec3 velocity = (s_pos[id].xyz - s_prev[id].xyz) / max(dt, 1.0e-6);
	float alpha = s_vel[id].w;
	s_vel[id] = vec4(velocity, alpha);
}
