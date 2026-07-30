#include "engine/render/backend/BgfxSceneAabb.h"

#include "engine/render/frame/ShadowCasterLight.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace Concord::RenderDetail {

void TransformAabbWorld(float outMin[3], float outMax[3],
                       const float localMin[3], const float localMax[3],
                       const float worldMatrix[16]) noexcept
{
    outMin[0] = outMin[1] = outMin[2] = std::numeric_limits<float>::max();
    outMax[0] = outMax[1] = outMax[2] = std::numeric_limits<float>::lowest();
    const float xs[2] = {localMin[0], localMax[0]};
    const float ys[2] = {localMin[1], localMax[1]};
    const float zs[2] = {localMin[2], localMax[2]};
    // The engine uses row-major matrices with a row-vector convention
    // (translation at indices 12/13/14), so a point transforms as `p * M`.
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                const float px = xs[i], py = ys[j], pz = zs[k];
                const float wx = px * worldMatrix[0]  + py * worldMatrix[4]  + pz * worldMatrix[8]  + worldMatrix[12];
                const float wy = px * worldMatrix[1]  + py * worldMatrix[5]  + pz * worldMatrix[9]  + worldMatrix[13];
                const float wz = px * worldMatrix[2]  + py * worldMatrix[6]  + pz * worldMatrix[10] + worldMatrix[14];
                outMin[0] = std::min(outMin[0], wx); outMax[0] = std::max(outMax[0], wx);
                outMin[1] = std::min(outMin[1], wy); outMax[1] = std::max(outMax[1], wy);
                outMin[2] = std::min(outMin[2], wz); outMax[2] = std::max(outMax[2], wz);
            }
        }
    }
}

void TransformSkinnedAabbWorld(float outMin[3], float outMax[3],
                               const std::array<float, 6>* boneAabbs,
                               std::uint32_t boneAabbCount,
                               const float* bonePalette,
                               std::uint32_t boneCount,
                               const float worldMatrix[16]) noexcept
{
    outMin[0] = outMin[1] = outMin[2] = std::numeric_limits<float>::max();
    outMax[0] = outMax[1] = outMax[2] = std::numeric_limits<float>::lowest();
    const std::uint32_t count = std::min(boneAabbCount, boneCount);
    for (std::uint32_t bone = 0; bone < count; ++bone) {
        const std::array<float, 6>& bounds = boneAabbs[bone];
        if (bounds[0] > bounds[3]) {
            continue;
        }
        const float* matrix = bonePalette + static_cast<std::size_t>(bone) * 16;
        const float xs[2] = {bounds[0], bounds[3]};
        const float ys[2] = {bounds[1], bounds[4]};
        const float zs[2] = {bounds[2], bounds[5]};
        for (int x = 0; x < 2; ++x) {
            for (int y = 0; y < 2; ++y) {
                for (int z = 0; z < 2; ++z) {
                    const float px = xs[x], py = ys[y], pz = zs[z];
                    const float sx = px * matrix[0] + py * matrix[4] + pz * matrix[8] + matrix[12];
                    const float sy = px * matrix[1] + py * matrix[5] + pz * matrix[9] + matrix[13];
                    const float sz = px * matrix[2] + py * matrix[6] + pz * matrix[10] + matrix[14];
                    const float wx = sx * worldMatrix[0] + sy * worldMatrix[4] + sz * worldMatrix[8] + worldMatrix[12];
                    const float wy = sx * worldMatrix[1] + sy * worldMatrix[5] + sz * worldMatrix[9] + worldMatrix[13];
                    const float wz = sx * worldMatrix[2] + sy * worldMatrix[6] + sz * worldMatrix[10] + worldMatrix[14];
                    outMin[0] = std::min(outMin[0], wx); outMax[0] = std::max(outMax[0], wx);
                    outMin[1] = std::min(outMin[1], wy); outMax[1] = std::max(outMax[1], wy);
                    outMin[2] = std::min(outMin[2], wz); outMax[2] = std::max(outMax[2], wz);
                }
            }
        }
    }
}

int FindShadowCaster(const RenderLight* lights, std::uint32_t count) noexcept
{
    // Shared with scene extraction, which selects shadow casters against the
    // same light (see engine/render/frame/ShadowCasterLight).
    return FindShadowCastingLight(lights, count);
}

} // namespace Concord::RenderDetail
