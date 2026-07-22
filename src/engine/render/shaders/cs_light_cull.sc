#include <bgfx_compute.sh>

// Forward+ GPU light culler: one thread per cluster. Reads the packed light
// data texture (same layout the CPU ClusteredLightCuller produces and fs_mesh
// samples) and writes the same two outputs the CPU path builds: a per-cluster
// (offset,count) range and a flat local-light index list. Because the output
// layout matches the CPU culler exactly, fs_mesh needs no changes to consume
// either backend's result (see ClusteredLightCuller.h / BgfxSceneUniforms).
//
// Per-cluster capacity and grid dimensions mirror Concord::ClusterGrid.

#define CLUSTER_DIM_X 16
#define CLUSTER_DIM_Y 9
#define CLUSTER_DIM_Z 24
#define CLUSTER_MAX_LIGHTS 64
#define CLUSTER_INDEX_TEX_W 1024
#define MAX_LIGHTS 256

SAMPLER2D(s_lightData, 0);
IMAGE2D_WO(s_outRanges, rgba32f, 1);
IMAGE2D_WO(s_outIndices, r32f, 2);

// x: light count, y: directional count, z: near, w: far.
uniform vec4 u_cullParams;
// Screen dims (xy) and tan(fovY/2), aspect (zw) — reconstructs each cluster's
// view-space frustum slice on the fly instead of receiving a CPU-built AABB.
uniform vec4 u_cullScreen;
// Column-major view matrix (camera world -> view space), for light positions.
uniform mat4 u_cullView;
// Column-major view-projection, for screen-space light footprint.
uniform mat4 u_cullViewProj;

int ClusterSlice(float viewZ, float nearP, float farP)
{
	float z = max(viewZ, nearP);
	float lg = log(farP / nearP);
	if (lg <= 0.0) { return 0; }
	int s = int(log(z / nearP) / lg * float(CLUSTER_DIM_Z));
	return clamp(s, 0, CLUSTER_DIM_Z - 1);
}

// Near-plane depth of a given slice (matches ClusterGrid::SliceNearDepth).
float SliceNearDepth(int slice, float nearP, float farP)
{
	float t = float(slice) / float(CLUSTER_DIM_Z);
	return nearP * pow(farP / nearP, t);
}

vec4 FetchLight(int base, int row)
{
	return texelFetch(s_lightData, ivec2(base, row), 0);
}

NUM_THREADS(CLUSTER_DIM_X, CLUSTER_DIM_Y, 1)
void main()
{
	int tileX = int(gl_GlobalInvocationID.x);
	int tileY = int(gl_GlobalInvocationID.y);
	int slice = int(gl_WorkGroupID.z);
	if (tileX >= CLUSTER_DIM_X || tileY >= CLUSTER_DIM_Y || slice >= CLUSTER_DIM_Z)
	{
		return;
	}

	int lightCount = int(u_cullParams.x);
	int dirCount = int(u_cullParams.y);
	float nearP = u_cullParams.z;
	float farP = u_cullParams.w;

	// This cluster's screen-space tile rectangle, in [0,1] UV.
	float u0 = float(tileX) / float(CLUSTER_DIM_X);
	float u1 = float(tileX + 1) / float(CLUSTER_DIM_X);
	float v0 = float(tileY) / float(CLUSTER_DIM_Y);
	float v1 = float(tileY + 1) / float(CLUSTER_DIM_Y);
	float sliceNear = SliceNearDepth(slice, nearP, farP);
	float sliceFar = SliceNearDepth(slice + 1, nearP, farP);

	int found[CLUSTER_MAX_LIGHTS];
	int count = 0;

	// Local lights start right after the directional ones in the packed list.
	for (int li = dirCount; li < lightCount; ++li)
	{
		if (count >= CLUSTER_MAX_LIGHTS) { break; }
		vec4 posType = FetchLight(0, li);
		vec4 dirRange = FetchLight(1, li);
		vec3 worldPos = posType.xyz;
		float range = dirRange.w;

		// View-space depth span test against the slice's near/far.
		vec4 viewPos = mul(u_cullView, vec4(worldPos, 1.0));
		float vz = viewPos.z;
		if (vz + range < sliceNear || vz - range > sliceFar)
		{
			continue;
		}

		// Screen-space AABB test (sphere corners projected), matching the CPU
		// culler's approach so both backends agree on assignment.
		vec4 clipCenter = mul(u_cullViewProj, vec4(worldPos, 1.0));
		if (clipCenter.w <= 1e-4)
		{
			// Conservative: a light center behind/at the eye touches every tile.
			found[count] = li;
			++count;
			continue;
		}
		float minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
		bool full = false;
		for (int c = 0; c < 8; ++c)
		{
			vec3 corner = worldPos + vec3(
				((c & 1) != 0) ? range : -range,
				((c & 2) != 0) ? range : -range,
				((c & 4) != 0) ? range : -range);
			vec4 clip = mul(u_cullViewProj, vec4(corner, 1.0));
			if (clip.w <= 1e-4) { full = true; break; }
			float ndcX = clip.x / clip.w;
			float ndcY = clip.y / clip.w;
			float sx = ndcX * 0.5 + 0.5;
			float sy = 1.0 - (ndcY * 0.5 + 0.5);
			minX = min(minX, sx); maxX = max(maxX, sx);
			minY = min(minY, sy); maxY = max(maxY, sy);
		}
		if (!full)
		{
			if (maxX < u0 || minX > u1 || maxY < v0 || minY > v1)
			{
				continue;
			}
		}
		found[count] = li;
		++count;
	}

	int clusterFlat = slice * (CLUSTER_DIM_X * CLUSTER_DIM_Y) + tileY * CLUSTER_DIM_X + tileX;
	int offset = clusterFlat * CLUSTER_MAX_LIGHTS;
	imageStore(s_outRanges, ivec2(tileY * CLUSTER_DIM_X + tileX, slice),
		vec4(float(offset), float(count), 0.0, 0.0));
	for (int k = 0; k < CLUSTER_MAX_LIGHTS; ++k)
	{
		float value = (k < count) ? float(found[k]) : 0.0;
		int flat = offset + k;
		imageStore(s_outIndices, ivec2(flat % CLUSTER_INDEX_TEX_W, flat / CLUSTER_INDEX_TEX_W),
			vec4_splat(value));
	}
}
