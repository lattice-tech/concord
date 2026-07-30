#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_pos, vec4, 0);
BUFFER_RW(s_cellOf, uint, 4);
BUFFER_RW(s_cellCount, uint, 7);

uniform vec4 u_fluidParams[16];

NUM_THREADS(FLUID_THREADS, 1, 1)
void main()
{
	uint id = gl_GlobalInvocationID.x;
	uint total = floatBitsToUint(u_fluidParams[0].w);
	if (id >= total)
	{
		return;
	}
	vec3 gridOrigin = u_fluidParams[3].xyz;
	float cell = u_fluidParams[3].w;
	ivec3 dims = ivec3(u_fluidParams[4].xyz);
	int index = FluidCellIndex(s_pos[id].xyz, gridOrigin, cell, dims);
	uint stored = index >= 0 ? uint(index) : 0xffffffffu;
	s_cellOf[id] = stored;
	if (index >= 0)
	{
		atomicAdd(s_cellCount[uint(index)], 1u);
	}
}
