#include <bgfx_compute.sh>
#include "fluid_common.sh"

SAMPLER3D(s_fieldTex, 0);
UIMAGE2D_RW(s_counterImg, r32ui, 2);
BUFFER_RO(s_counters, uint, 8);
BUFFER_RO(s_voxels, uint, 10);
BUFFER_RO(s_voxelOffsets, uint, 11);
BUFFER_RW(s_meshVerts, float, 12);
BUFFER_RO(s_triTable, int, 13);

uniform vec4 u_fluidParams[16];

/*
 * Sparse Marching Cubes, emission half: for each active voxel the canonical
 * Lorensen & Cline triangulation (uploaded as a table, validated by a CPU
 * regression) is instantiated with linear edge interpolation. Vertex normals
 * come from the field gradient — the same smoothed field the mesh is cut
 * from — so lighting and the refraction march agree on the surface shape.
 * Vertices are (pos xyz, normal xyz) float pairs in local space.
 */
const ivec3 kCornerOffset[8] = {
	ivec3(0, 0, 0), ivec3(1, 0, 0), ivec3(1, 1, 0), ivec3(0, 1, 0),
	ivec3(0, 0, 1), ivec3(1, 0, 1), ivec3(1, 1, 1), ivec3(0, 1, 1)
};
const int kEdgeCorners[24] = {
	0, 1, 1, 2, 2, 3, 3, 0,
	4, 5, 5, 6, 6, 7, 7, 4,
	0, 4, 1, 5, 2, 6, 3, 7
};

NUM_THREADS(FLUID_THREADS, 1, 1)
void main()
{
	uint slot = gl_GlobalInvocationID.x;
	uint active = s_counters[0];
	uint maxVerts = floatBitsToUint(u_fluidParams[12].w);
	if (slot == 0u)
	{
		imageStore(s_counterImg, ivec2(0, 0), uvec4(min(s_counters[1], maxVerts), 0u, 0u, 0u));
	}
	if (slot >= active)
	{
		return;
	}

	ivec3 dims = ivec3(u_fluidParams[11].xyz);
	uint cellId = s_voxels[slot];
	ivec3 c = ivec3(int(cellId % uint(dims.x)),
	                int((cellId / uint(dims.x)) % uint(dims.y)),
	                int(cellId / (uint(dims.x) * uint(dims.y))));
	vec3 fieldOrigin = u_fluidParams[10].xyz;
	float fieldCell = u_fluidParams[10].w;
	float iso = u_fluidParams[11].w;

	float cornerValue[8];
	uint caseIndex = 0u;
	for (int corner = 0; corner < 8; ++corner)
	{
		float value = texelFetch(s_fieldTex, c + kCornerOffset[corner], 0).r;
		cornerValue[corner] = value;
		if (value > iso)
		{
			caseIndex |= 1u << corner;
		}
	}

	uint base = s_voxelOffsets[slot];
	for (int i = 0; i < 15; ++i)
	{
		int edge = s_triTable[int(caseIndex) * 16 + i];
		if (edge < 0) { break; }
		int c0 = kEdgeCorners[edge * 2];
		int c1 = kEdgeCorners[edge * 2 + 1];
		float f0 = cornerValue[c0];
		float f1 = cornerValue[c1];
		float t = clamp((iso - f0) / (f1 - f0), 0.0, 1.0);
		vec3 p0 = fieldOrigin + vec3(c + kCornerOffset[c0]) * fieldCell;
		vec3 p1 = fieldOrigin + vec3(c + kCornerOffset[c1]) * fieldCell;
		vec3 pos = mix(p0, p1, t);

		// Gradient by central differences; outward = direction of decrease.
		ivec3 vc = ivec3(floor((pos - fieldOrigin) / fieldCell));
		ivec3 lo = clamp(vc - ivec3(1, 1, 1), ivec3(0, 0, 0), dims);
		ivec3 hi = clamp(vc + ivec3(1, 1, 1), ivec3(0, 0, 0), dims);
		vec3 grad = vec3(
			texelFetch(s_fieldTex, ivec3(hi.x, vc.y, vc.z), 0).r
				- texelFetch(s_fieldTex, ivec3(lo.x, vc.y, vc.z), 0).r,
			texelFetch(s_fieldTex, ivec3(vc.x, hi.y, vc.z), 0).r
				- texelFetch(s_fieldTex, ivec3(vc.x, lo.y, vc.z), 0).r,
			texelFetch(s_fieldTex, ivec3(vc.x, vc.y, hi.z), 0).r
				- texelFetch(s_fieldTex, ivec3(vc.x, vc.y, lo.z), 0).r);
		vec3 normal = length(grad) > 1.0e-6 ? -normalize(grad) : vec3(0.0, 1.0, 0.0);

		uint vert = base + uint(i);
		if (vert < maxVerts)
		{
			uint o = vert * 6u;
			s_meshVerts[o + 0u] = pos.x;
			s_meshVerts[o + 1u] = pos.y;
			s_meshVerts[o + 2u] = pos.z;
			s_meshVerts[o + 3u] = normal.x;
			s_meshVerts[o + 4u] = normal.y;
			s_meshVerts[o + 5u] = normal.z;
		}
	}
}
