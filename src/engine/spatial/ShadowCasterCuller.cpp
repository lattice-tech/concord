#include "engine/spatial/ShadowCasterCuller.h"

#include "engine/collision/AabbOps.h"
#include "engine/spatial/WorldAabbFromMatrix.h"

#include <algorithm>
#include <cmath>

namespace Concord::Spatial {
namespace {

bool NormalizeDirection(const float direction[3], float out[3]) noexcept
{
    if (direction == nullptr) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(direction[axis])) {
            return false;
        }
    }
    const float length = std::sqrt(direction[0] * direction[0]
                                   + direction[1] * direction[1]
                                   + direction[2] * direction[2]);
    if (length <= 1.0e-6f) {
        return false;
    }
    const float inverse = 1.0f / length;
    out[0] = direction[0] * inverse;
    out[1] = direction[1] * inverse;
    out[2] = direction[2] * inverse;
    return true;
}

} // namespace

bool ShadowCasterTouchesFrustum(const Frustum& frustum, const Collision::Aabb& bounds,
                                const float lightDirection[3], float extrusion) noexcept
{
    if (!Collision::IsValidAabb(bounds) || !std::isfinite(extrusion) || extrusion < 0.0f) {
        return false;
    }
    float direction[3]{};
    if (!NormalizeDirection(lightDirection, direction)) {
        return false;
    }

    // Union of the caster box and the same box pushed along the light: every
    // point its shadow can reach lies inside this swept volume.
    Collision::Aabb swept = bounds;
    const float offset[3] = {direction[0] * extrusion, direction[1] * extrusion,
                             direction[2] * extrusion};
    swept.min.x = std::min(swept.min.x, bounds.min.x + offset[0]);
    swept.min.y = std::min(swept.min.y, bounds.min.y + offset[1]);
    swept.min.z = std::min(swept.min.z, bounds.min.z + offset[2]);
    swept.max.x = std::max(swept.max.x, bounds.max.x + offset[0]);
    swept.max.y = std::max(swept.max.y, bounds.max.y + offset[1]);
    swept.max.z = std::max(swept.max.z, bounds.max.z + offset[2]);
    return FrustumIntersectsAabb(frustum, swept);
}

std::uint32_t SelectShadowCasters(const std::vector<RenderInstance>& authored,
                                  const std::vector<std::size_t>& culledIndices,
                                  const Frustum& frustum, const float lightDirection[3],
                                  float extrusion, std::vector<RenderInstance>& out)
{
    out.clear();
    float direction[3]{};
    if (!NormalizeDirection(lightDirection, direction)) {
        return 0;
    }
    for (std::size_t index : culledIndices) {
        if (index >= authored.size()) {
            continue;
        }
        const RenderInstance& instance = authored[index];
        const Collision::Aabb bounds = WorldAabbForInstance(
            instance.world, instance.hasLocalBounds, instance.localMin, instance.localMax);
        if (ShadowCasterTouchesFrustum(frustum, bounds, direction, extrusion)) {
            out.push_back(instance);
        }
    }
    return static_cast<std::uint32_t>(out.size());
}

} // namespace Concord::Spatial
