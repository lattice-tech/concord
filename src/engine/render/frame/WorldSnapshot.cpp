#include "engine/render/frame/WorldSnapshot.h"

#include "engine/render/backend/IRenderBackend.h"

#include <cstddef>

namespace Concord {

void WorldSnapshot::RebindBonePalettes() noexcept
{
    std::size_t offset = 0;
    for (RenderInstance& instance : instances) {
        if (instance.boneCount == 0) {
            instance.bonePalette = nullptr;
            continue;
        }
        const std::size_t matrixFloats = static_cast<std::size_t>(instance.boneCount) * 16;
        if (offset + matrixFloats > boneMatrices.size()) {
            instance.bonePalette = nullptr;
            instance.boneCount = 0;
            continue;
        }
        instance.bonePalette = boneMatrices.data() + offset;
        offset += matrixFloats;
    }
}

} // namespace Concord
