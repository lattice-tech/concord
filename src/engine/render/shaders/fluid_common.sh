#ifndef CONCORD_FLUID_COMMON_SH
#define CONCORD_FLUID_COMMON_SH

/*
 * Shared constants and helpers for the DFSPH fluid compute pipeline.
 *
 * Uniform contract (u_fluidParams, vec4 x 16) — written by
 * BgfxFluidRenderer before every dispatch; all simulation-space values are
 * in the fluid body's local frame:
 *   [0]  x=dt  y=dt^2  z=particleCount  w=totalPoints (fluid + boundary)
 *   [1]  x=spacing  y=kernelRadius h  z=restDensity  w=particleMass
 *   [2]  xyz=gravity (local)  w=viscosity (XSPH epsilon)
 *   [3]  xyz=gridOrigin  w=grid cell size (= h)
 *   [4]  xyz=gridDims  w=modeFlags (bit0 reset/seed, bit1 computeAlpha,
 *         bit2 firstFieldFrame)
 *   [5]  xyz=tankMin (local)  w=maxSpeed
 *   [6]  xyz=tankMax (local)  w=fieldSmoothing
 *   [7]  xyz=fluidLattice counts  w=boundaryCount
 *   [8]  xyz=wall particle counts per axis (wallX, wallY, wallZ)  w=unused
 *   [9]  xyz=fillOrigin (local)  w=unused
 *   [10] xyz=fieldOrigin  w=fieldCell
 *   [11] xyz=fieldDims (cells)  w=isoLevel
 *   [12] x=fieldFixedScale  y=fieldFixedInvScale  z=maxActiveVoxels  w=maxVerts
 *   [13] xyz=obstacleMin (local)  w=obstacleBoundaryCount (0 = disabled)
 *   [14] xyz=obstacleMax (local)  w=unused
 *   [15] unused
 *
 * The optional obstacle is a static inner box (e.g. a land mass) sampled with
 * a two-layer boundary shell appended after the six tank walls, layered
 * outward. Fill-lattice cells inside it are mirrored to the opposite side at
 * seed time; fluid particles are pushed out of it on every integrate.
 *
 * Buffer binding slots (steady across every cs_fluid_* pass):
 *   0 s_pos      vec4  (xyz local position, w = density rho_i)
 *   1 s_vel      vec4  (xyz velocity,       w = DFSPH factor alpha_i)
 *   2 s_prev     vec4  (previous position for the velocity update)
 *   3 s_factors  float (per-particle pressure/divergence scalar k)
 *   4 s_cellOf   uint  (point -> linear neighbor-cell index)
 *   5 s_sorted   uint  (cell-sorted point indices)
 *   6 s_cellStart uint
 *   7 s_cellCount uint (count after count pass; scatter cursor after scan)
 *   8 s_counters uint  ([0]=active voxels, [1]=emitted vertices)
 *   9 s_fieldAccum uint (fixed-point density splat target)
 *   10 s_voxels   uint (active voxel linear cell indices)
 *   11 s_voxelOffsets uint (first vertex per active voxel)
 *   12 s_meshVerts float (MC vertex stream: pos xyz + normal xyz per vertex)
 *   13 s_triTable int  (256*16 canonical Marching Cubes table)
 *
 * Textures:
 *   sampler 0 s_fieldTex  (smoothed density field, R32F 3D)
 *   image   1 s_fieldImg  (field write target this frame, r32f 3D)
 *   image   2 s_counterImg (drawn vertex count for the VS, r32ui 2D 1x1)
 */

#define FLUID_THREADS 64

#define FLUID_FLAG_RESET 1u
#define FLUID_FLAG_ALPHA 2u
#define FLUID_FLAG_FIRST_FIELD 4u

float FluidKernelW(float r, float h)
{
	float q = 2.0 * r / h;
	if (q >= 2.0) { return 0.0; }
	float sigma = 64.0 / (3.14159265359 * h * h * h);
	if (q >= 1.0)
	{
		float t = 2.0 - q;
		return sigma * 0.25 * t * t * t;
	}
	return sigma * (1.0 - 1.5 * q * q + 0.75 * q * q * q);
}

/** Gradient of the cubic-spline kernel along rij (r = |rij| < h). */
vec3 FluidKernelGrad(vec3 rij, float h)
{
	float r = length(rij);
	if (r < 1.0e-6 || r >= h) { return vec3_splat(0.0); }
	float q = 2.0 * r / h;
	float sigma = 64.0 / (3.14159265359 * h * h * h);
	float dWdr;
	if (q >= 1.0)
	{
		float t = 2.0 - q;
		dWdr = sigma * (-0.75 * t * t);
	}
	else
	{
		dWdr = sigma * (-3.0 * q + 2.25 * q * q);
	}
	return dWdr * (2.0 / h) * (rij / r);
}

/** Linear neighbor-cell index of a local position, or -1 when outside. */
int FluidCellIndex(vec3 p, vec3 gridOrigin, float cell, ivec3 dims)
{
	ivec3 c = ivec3(floor((p - gridOrigin) / cell));
	if (any(lessThan(c, ivec3(0, 0, 0))) || any(greaterThanEqual(c, dims)))
	{
		return -1;
	}
	return c.x + dims.x * (c.y + dims.y * c.z);
}

/** Field texel coordinate of a local position on the (dims+1) vertex grid. */
ivec3 FluidFieldTexel(vec3 p, vec3 fieldOrigin, float fieldCell, ivec3 dims)
{
	ivec3 c = ivec3(floor((p - fieldOrigin) / fieldCell));
	return clamp(c, ivec3(0, 0, 0), dims);
}

#endif // CONCORD_FLUID_COMMON_SH
