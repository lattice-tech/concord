#include <bgfx_compute.sh>
#include "fluid_common.sh"

BUFFER_RW(s_pos, vec4, 0);
BUFFER_RW(s_vel, vec4, 1);
BUFFER_RW(s_prev, vec4, 2);

uniform vec4 u_fluidParams[16];

/*
 * Position integration with the DFSPH-corrected velocities, plus the seed
 * path (FLUID_FLAG_RESET) that lays fluid particles out on the fill lattice
 * and writes the two-layer static wall sampling after them. A hard tank
 * clamp backs up the boundary particles: a particle that still tunneled is
 * put back inside with its wall-normal velocity removed, never deleted, so
 * volume cannot silently leak.
 *
 * When the authored obstacle box is enabled the same treatment applies to
 * it: a two-layer shell sampled outward on the box (appended after the six
 * tank walls), fill-lattice cells mirrored out of the box at seed time, and
 * a least-penetration push-out on every integrate.
 */

/** Two-layer boundary shell on an AABB. layerDir < 0 layers inward (tank
 *  walls), layerDir > 0 layers outward (inner obstacle). */
vec3 FluidShellPoint(uint bid, vec3 lo, vec3 hi, float spacing, float layerDir)
{
    vec3 size = hi - lo;
    uvec3 wall = uvec3(max(vec3_splat(2.0), floor(size / spacing)));
    uint planeX = wall.y * wall.z;
    uint planeY = wall.x * wall.z;
    uint planeZ = wall.x * wall.y;
    uint segX = 2u * planeX;
    uint segY = 2u * planeY;
    uint segZ = 2u * planeZ;
    // Faces in order (x-, x+, y-, y+, z-, z+), inner-most layer first.
    uint axis = 0u;
    uint side = 0u;
    uint rem = bid;
    if (rem >= 2u * segX + 2u * segY + segZ) { axis = 2u; side = 1u; rem -= 2u * segX + 2u * segY + segZ; }
    else if (rem >= 2u * segX + 2u * segY) { axis = 2u; side = 0u; rem -= 2u * segX + 2u * segY; }
    else if (rem >= 2u * segX + segY) { axis = 1u; side = 1u; rem -= 2u * segX + segY; }
    else if (rem >= 2u * segX) { axis = 1u; side = 0u; rem -= 2u * segX; }
    else if (rem >= segX) { axis = 0u; side = 1u; rem -= segX; }
    uint plane = axis == 0u ? planeX : (axis == 1u ? planeY : planeZ);
    uint layer = rem / plane;
    uint cellId = rem - layer * plane;
    uint a1 = axis == 0u ? 1u : 0u;
    uint a2 = axis == 2u ? 1u : 2u;
    uint n1 = axis == 0u ? wall.y : wall.x;
    uint n2 = axis == 2u ? wall.y : wall.z;
    uint i1 = cellId / n2;
    uint i2 = cellId - i1 * n2;
    float c1 = lo[a1] + (float(i1) + 0.5) * spacing;
    float c2 = lo[a2] + (float(i2) + 0.5) * spacing;
    float wallPos = side == 0u ? lo[axis] - layerDir * float(layer) * spacing
                               : hi[axis] + layerDir * float(layer) * spacing;
    vec3 p;
    p[axis] = wallPos;
    p[a1] = c1;
    p[a2] = c2;
    return p;
}

NUM_THREADS(FLUID_THREADS, 1, 1)
void main()
{
    uint id = gl_GlobalInvocationID.x;
    uint total = floatBitsToUint(u_fluidParams[0].w);
    if (id >= total)
    {
        return;
    }
    uint count = floatBitsToUint(u_fluidParams[0].z);
    float dt = u_fluidParams[0].x;
    float spacing = u_fluidParams[1].x;
    float rho0 = u_fluidParams[1].z;
    vec3 tankMin = u_fluidParams[5].xyz;
    vec3 tankMax = u_fluidParams[6].xyz;
    uint flags = floatBitsToUint(u_fluidParams[4].w);
    uint obstacleCount = floatBitsToUint(u_fluidParams[13].w);
    vec3 obstacleMin = u_fluidParams[13].xyz;
    vec3 obstacleMax = u_fluidParams[14].xyz;

    if ((flags & FLUID_FLAG_RESET) != 0u)
    {
        vec3 p;
        if (id < count)
        {
            uvec3 lattice = uvec3(u_fluidParams[7].xyz);
            uint ix = id % lattice.x;
            uint iy = (id / lattice.x) % lattice.y;
            uint iz = (id / (lattice.x * lattice.y));
            p = u_fluidParams[9].xyz + vec3(float(ix), float(iy), float(iz)) * spacing;
            if (obstacleCount > 0u)
            {
                // Fill cells inside the obstacle are mirrored to the opposite
                // side of the box, keeping the seeded count exact.
                vec3 elo = obstacleMin - vec3_splat(spacing);
                vec3 ehi = obstacleMax + vec3_splat(spacing);
                if (all(greaterThanEqual(p, elo)) && all(lessThanEqual(p, ehi)))
                {
                    vec3 center = (obstacleMin + obstacleMax) * 0.5;
                    p = center * 2.0 - p;
                    p = clamp(p, tankMin + vec3_splat(spacing),
                              tankMax - vec3_splat(spacing));
                }
            }
        }
        else
        {
            uint bid = id - count;
            vec3 size = tankMax - tankMin;
            uvec3 wall = uvec3(max(vec3_splat(2.0), floor(size / spacing)));
            uint wallTotal = 4u * (wall.x * wall.y + wall.x * wall.z + wall.y * wall.z);
            if (bid < wallTotal || obstacleCount == 0u)
            {
                p = FluidShellPoint(bid, tankMin, tankMax, spacing, -1.0);
            }
            else
            {
                p = FluidShellPoint(bid - wallTotal, obstacleMin, obstacleMax,
                                    spacing, 1.0);
            }
        }
        s_pos[id] = vec4(p, rho0);
        s_vel[id] = vec4_splat(0.0);
        s_prev[id] = vec4(p, rho0);
        return;
    }

    if (id >= count)
    {
        return;
    }
    vec4 pos = s_pos[id];
    vec3 vel = s_vel[id].xyz;
    s_prev[id] = pos;
    pos.xyz += vel * dt;

    // Safety clamp against tunneling (see header comment).
    float margin = 0.5 * spacing;
    vec3 lo = tankMin + vec3_splat(margin);
    vec3 hi = tankMax - vec3_splat(margin);
    if (pos.x < lo.x) { pos.x = lo.x; vel.x = max(vel.x, 0.0); }
    if (pos.y < lo.y) { pos.y = lo.y; vel.y = max(vel.y, 0.0); }
    if (pos.z < lo.z) { pos.z = lo.z; vel.z = max(vel.z, 0.0); }
    if (pos.x > hi.x) { pos.x = hi.x; vel.x = min(vel.x, 0.0); }
    if (pos.y > hi.y) { pos.y = hi.y; vel.y = min(vel.y, 0.0); }
    if (pos.z > hi.z) { pos.z = hi.z; vel.z = min(vel.z, 0.0); }

    if (obstacleCount > 0u)
    {
        // Least-penetration push-out, same never-delete policy as the walls.
        vec3 elo = obstacleMin - vec3_splat(margin);
        vec3 ehi = obstacleMax + vec3_splat(margin);
        if (all(greaterThan(pos.xyz, elo)) && all(lessThan(pos.xyz, ehi)))
        {
            vec3 dLo = pos.xyz - elo;
            vec3 dHi = ehi - pos.xyz;
            vec3 pen = min(dLo, dHi);
            if (pen.x <= pen.y && pen.x <= pen.z)
            {
                if (dLo.x < dHi.x) { pos.x = elo.x; vel.x = min(vel.x, 0.0); }
                else { pos.x = ehi.x; vel.x = max(vel.x, 0.0); }
            }
            else if (pen.y <= pen.z)
            {
                if (dLo.y < dHi.y) { pos.y = elo.y; vel.y = min(vel.y, 0.0); }
                else { pos.y = ehi.y; vel.y = max(vel.y, 0.0); }
            }
            else
            {
                if (dLo.z < dHi.z) { pos.z = elo.z; vel.z = min(vel.z, 0.0); }
                else { pos.z = ehi.z; vel.z = max(vel.z, 0.0); }
            }
        }
    }

    s_pos[id] = pos;
    s_vel[id].xyz = vel;
}
