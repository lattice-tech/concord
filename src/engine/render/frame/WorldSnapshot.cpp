#include "engine/render/frame/WorldSnapshot.h"

#include "engine/render/backend/IRenderBackend.h"

#include <cstddef>

namespace Concord {

void WorldSnapshot::RebindBonePalettes() noexcept
{
    // Palettes were appended in this exact order (visible draws, then shadow
    // casters), so one running offset rebinds both lists.
    std::size_t offset = 0;
    const auto rebind = [this, &offset](std::vector<RenderInstance>& list) {
        for (RenderInstance& instance : list) {
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
    };
    rebind(instances);
    rebind(shadowCasters);
}

} // namespace Concord
