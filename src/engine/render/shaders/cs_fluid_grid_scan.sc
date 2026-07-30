#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RW(s_cellStart, uint, 6);
BUFFER_RW(s_cellCount, uint, 7);

uniform vec4 u_fluidParams[16];

SHARED uint g_partials[1024];

/*
 * Single-workgroup exclusive prefix sum over the neighbor-cell counters.
 * Every thread serially sums a strided chunk (<= 256 cells), a shared-memory
 * Blelloch scan over the 1024 chunk sums produces each chunk's base, and a
 * second serial walk writes the exclusive prefixes. The counts are reset to
 * zero here so the scatter pass can reuse them as per-cell write cursors.
 * Valid while numCells <= 1024 * 256 (see FluidDesc's kMaxFluidGridCells).
 */
NUM_THREADS(1024, 1, 1)
void main()
{
	uvec3 dims = uvec3(u_fluidParams[4].xyz);
	uint numCells = dims.x * dims.y * dims.z;
	uint tid = gl_LocalInvocationIndex;
	uint chunk = (numCells + 1023u) / 1024u;
	uint begin = tid * chunk;
	uint end = min(begin + chunk, numCells);

	uint sum = 0u;
	for (uint i = begin; i < end; ++i)
	{
		sum += s_cellCount[i];
	}

	g_partials[tid] = sum;
	barrier();

	// Inclusive scan of the 1024 partial sums (Hillis-Steele).
	for (uint offset = 1u; offset < 1024u; offset <<= 1u)
	{
		uint value = tid >= offset ? g_partials[tid - offset] : 0u;
		barrier();
		g_partials[tid] += value;
		barrier();
	}

	// Exclusive base of this thread's chunk = inclusive scan of the previous.
	uint base = tid > 0u ? g_partials[tid - 1u] : 0u;
	for (uint i = begin; i < end; ++i)
	{
		uint count = s_cellCount[i];
		s_cellStart[i] = base;
		base += count;
		s_cellCount[i] = 0u;
	}
}
