#ifndef CONCORD_WORLDAABBFROMMATRIX_H
#define CONCORD_WORLDAABBFROMMATRIX_H

#include "engine/collision/Aabb.h"
#include "math/Vector3.h"

#include <algorithm>
#include <cmath>

namespace Concord::Spatial {
namespace {

inline bool MatrixIsFinite(const float worldMatrix[16]) noexcept
{
    if (worldMatrix == nullptr) {
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(worldMatrix[i])) {
            return false;
        }
    }
    return true;
}

inline Collision::Aabb InvalidAabb() noexcept
{
    return Collision::Aabb{Vector3{1, 1, 1}, Vector3{-1, -1, -1}};
}

/** Transforms 8 local-space corners of [min,max] by column-major worldMatrix. */
inline Collision::Aabb TransformLocalBox(const float worldMatrix[16],
                                         float minX, float minY, float minZ,
                                         float maxX, float maxY, float maxZ) noexcept
{
    if (!MatrixIsFinite(worldMatrix)
        || !std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(minZ)
        || !std::isfinite(maxX) || !std::isfinite(maxY) || !std::isfinite(maxZ)
        || minX > maxX || minY > maxY || minZ > maxZ) {
        return InvalidAabb();
    }

    const float ox = worldMatrix[12];
    const float oy = worldMatrix[13];
    const float oz = worldMatrix[14];
    float outMinX = ox;
    float outMinY = oy;
    float outMinZ = oz;
    float outMaxX = ox;
    float outMaxY = oy;
    float outMaxZ = oz;
    bool first = true;

    const float xs[2] = {minX, maxX};
    const float ys[2] = {minY, maxY};
    const float zs[2] = {minZ, maxZ};
    for (float lx : xs) {
        for (float ly : ys) {
            for (float lz : zs) {
                const float x = worldMatrix[0] * lx + worldMatrix[4] * ly
                    + worldMatrix[8] * lz + ox;
                const float y = worldMatrix[1] * lx + worldMatrix[5] * ly
                    + worldMatrix[9] * lz + oy;
                const float z = worldMatrix[2] * lx + worldMatrix[6] * ly
                    + worldMatrix[10] * lz + oz;
                if (first) {
                    outMinX = outMaxX = x;
                    outMinY = outMaxY = y;
                    outMinZ = outMaxZ = z;
                    first = false;
                } else {
                    outMinX = std::min(outMinX, x);
                    outMinY = std::min(outMinY, y);
                    outMinZ = std::min(outMinZ, z);
                    outMaxX = std::max(outMaxX, x);
                    outMaxY = std::max(outMaxY, y);
                    outMaxZ = std::max(outMaxZ, z);
                }
            }
        }
    }
    return Collision::Aabb{Vector3{outMinX, outMinY, outMinZ},
                           Vector3{outMaxX, outMaxY, outMaxZ}};
}

} // namespace

/**
 * @brief World-space AABB of the unit cube [-1,1]^3 under a column-major 4x4.
 *
 * Matches the baked primitive path (unit mesh * half-size scale already in the
 * world matrix). Used for frustum culling without reading GPU mesh bounds.
 * Non-finite matrices yield an invalid inverted box that cull tests reject.
 */
inline Collision::Aabb WorldAabbFromUnitCube(const float worldMatrix[16]) noexcept
{
    return TransformLocalBox(worldMatrix, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
}

/**
 * @brief World-space AABB of an arbitrary local AABB under a column-major 4x4.
 *
 * Used for imported meshes whose model-space bounds are not the unit cube.
 */
inline Collision::Aabb WorldAabbFromLocalBox(const float worldMatrix[16],
                                             const Collision::Aabb& local) noexcept
{
    return TransformLocalBox(worldMatrix,
                             local.min.x, local.min.y, local.min.z,
                             local.max.x, local.max.y, local.max.z);
}

/**
 * @brief Cull AABB for a RenderInstance: local box when present, else unit cube.
 *
 * @param hasLocalBounds When true, @p localMin/localMax are used; otherwise the
 *        unit cube [-1,1]^3 is transformed (built-in primitives).
 */
inline Collision::Aabb WorldAabbForInstance(const float worldMatrix[16],
                                            bool hasLocalBounds,
                                            const float localMin[3],
                                            const float localMax[3]) noexcept
{
    if (hasLocalBounds && localMin != nullptr && localMax != nullptr) {
        return WorldAabbFromLocalBox(
            worldMatrix,
            Collision::Aabb{Vector3{localMin[0], localMin[1], localMin[2]},
                            Vector3{localMax[0], localMax[1], localMax[2]}});
    }
    return WorldAabbFromUnitCube(worldMatrix);
}

} // namespace Concord::Spatial

#endif // CONCORD_WORLDAABBFROMMATRIX_H
