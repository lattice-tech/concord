#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RO(s_cellOf, uint, 4);
BUFFER_RW(s_sorted, uint, 5);
BUFFER_RO(s_cellStart, uint, 6);
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
	uint cell = s_cellOf[id];
	if (cell == 0xffffffffu)
	{
		return;
	}
	uint slot = s_cellStart[cell] + atomicAdd(s_cellCount[cell], 1u);
	s_sorted[slot] = id;
}
