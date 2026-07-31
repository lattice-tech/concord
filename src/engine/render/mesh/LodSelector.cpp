#include "engine/render/mesh/LodSelector.h"

#include <algorithm>
#include <cmath>

namespace Concord {

MeshHandle SelectLodMesh(const RenderInstance& instance, const float eye[3]) noexcept
{
    if (instance.lodCount == 0 || eye == nullptr) {
        return instance.mesh;
    }

    // Measure to the instance's world origin (translation row of the world
    // matrix). The origin is what authors reason about when they choose the
    // switch distances, and it is stable under rotation, unlike a projected
    // bound.
    const float dx = instance.world[12] - eye[0];
    const float dy = instance.world[13] - eye[1];
    const float dz = instance.world[14] - eye[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    const std::uint32_t count = std::min(instance.lodCount,
                                         RenderInstance::kMaxLodLevels);
    // Coarsest level the camera has passed. Start distances are ordered
    // ascending by construction (level 0 starts at 0).
    std::uint32_t selected = 0;
    for (std::uint32_t level = 1; level < count; ++level) {
        if (distance >= instance.lodStartDistances[level]) {
            selected = level;
        }
    }
    // Fall back toward finer levels when the chosen mesh is not resident yet
    // (still uploading), so a LOD switch can never blank the model.
    for (std::uint32_t level = selected + 1; level-- > 0;) {
        if (instance.lodMeshes[level].IsValid()) {
            return instance.lodMeshes[level];
        }
    }
    return instance.mesh;
}

} // namespace Concord
