#include "engine/render/mesh/MeshBounds.h"

#include <algorithm>
#include <cmath>

namespace Concord {

bool ComputeMeshBounds(const MeshData& data, Collision::Aabb& outBounds) noexcept
{
    bool any = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;

    for (const Vector3& position : data.positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y)
            || !std::isfinite(position.z)) {
            continue;
        }
        if (!any) {
            minX = maxX = position.x;
            minY = maxY = position.y;
            minZ = maxZ = position.z;
            any = true;
            continue;
        }
        minX = std::min(minX, position.x);
        minY = std::min(minY, position.y);
        minZ = std::min(minZ, position.z);
        maxX = std::max(maxX, position.x);
        maxY = std::max(maxY, position.y);
        maxZ = std::max(maxZ, position.z);
    }

    if (!any) {
        return false;
    }
    outBounds = Collision::Aabb{Vector3{minX, minY, minZ}, Vector3{maxX, maxY, maxZ}};
    return true;
}

} // namespace Concord
