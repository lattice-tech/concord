#include <bgfx_compute.sh>
#include "fluid_common.sh"

SAMPLER3D(s_fieldTex, 0);
BUFFER_RW(s_counters, uint, 8);
BUFFER_RW(s_voxels, uint, 10);
BUFFER_RW(s_voxelOffsets, uint, 11);
BUFFER_RO(s_triTable, int, 13);

uniform vec4 u_fluidParams[16];

/*
 * Sparse Marching Cubes, classification half: only cells whose corner field
 * values straddle the iso level ever touch geometry — the overwhelming
 * majority of the field is empty or solid and is discarded here, which is
 * what makes this "sparse" MC rather than a dense mesh sweep. Surviving
 * voxels reserve their vertex range in the shared counter so the emission
 * pass needs no second synchronization.
 */
NUM_THREADS(FLUID_THREADS, 1, 1)
void main()
{
	uint id = gl_GlobalInvocationID.x;
	ivec3 dims = ivec3(u_fluidParams[11].xyz);
	uint numCells = uint(dims.x) * uint(dims.y) * uint(dims.z);
	if (id >= numCells)
	{
		return;
	}
	float iso = u_fluidParams[11].w;
	uint maxVoxels = floatBitsToUint(u_fluidParams[12].z);

	ivec3 c = ivec3(int(id % uint(dims.x)),
	                int((id / uint(dims.x)) % uint(dims.y)),
	                int(id / (uint(dims.x) * uint(dims.y))));
	uint caseIndex = 0u;
	for (int corner = 0; corner < 8; ++corner)
	{
		ivec3 offset = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
		float value = texelFetch(s_fieldTex, c + offset, 0).r;
		if (value > iso)
		{
			caseIndex |= 1u << corner;
		}
	}
	if (caseIndex == 0u || caseIndex == 255u)
	{
		return;
	}

	int triangles = 0;
	for (int i = 0; i < 16; ++i)
	{
		if (s_triTable[int(caseIndex) * 16 + i] < 0) { break; }
		++triangles;
	}
	triangles /= 3;
	if (triangles == 0)
	{
		return;
	}

	uint slot = atomicAdd(s_counters[0], 1u);
	if (slot >= maxVoxels)
	{
		return;
	}
	s_voxels[slot] = id;
	s_voxelOffsets[slot] = atomicAdd(s_counters[1], uint(triangles) * 3u);
}
