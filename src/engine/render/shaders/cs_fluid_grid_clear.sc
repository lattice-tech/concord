#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RW(s_cellCount, uint, 7);
BUFFER_RW(s_counters, uint, 8);
BUFFER_RW(s_fieldAccum, uint, 9);

uniform vec4 u_fluidParams[16];

NUM_THREADS(FLUID_THREADS, 1, 1)
void main()
{
	uint id = gl_GlobalInvocationID.x;
	uvec3 gridDims = uvec3(u_fluidParams[4].xyz);
	uint numCells = gridDims.x * gridDims.y * gridDims.z;
	uvec3 fieldDims = uvec3(u_fluidParams[11].xyz);
	uvec3 fieldVerts = fieldDims + uvec3(1u, 1u, 1u);
	uint numVerts = fieldVerts.x * fieldVerts.y * fieldVerts.z;

	if (id < numCells)
	{
		s_cellCount[id] = 0u;
	}
	if (id < numVerts)
	{
		s_fieldAccum[id] = 0u;
	}
	if (id < 4u)
	{
		s_counters[id] = 0u;
	}
}
