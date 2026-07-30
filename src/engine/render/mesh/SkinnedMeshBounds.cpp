#include "engine/render/mesh/SkinnedMeshBounds.h"

#include "engine/collision/AabbOps.h"
#include "engine/spatial/WorldAabbFromMatrix.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Concord {
namespace {

void GrowBounds(Collision::Aabb& box, const Vector3& point, bool& any) noexcept
{
    if (!any) {
        box.min = point;
        box.max = point;
        any = true;
        return;
    }
    box.min.x = std::min(box.min.x, point.x);
    box.min.y = std::min(box.min.y, point.y);
    box.min.z = std::min(box.min.z, point.z);
    box.max.x = std::max(box.max.x, point.x);
    box.max.y = std::max(box.max.y, point.y);
    box.max.z = std::max(box.max.z, point.z);
}

bool IsFinitePoint(const Vector3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

} // namespace

bool ComputeSkinnedBoneBounds(const MeshData& data,
                              std::vector<SkinnedBoneBounds>& out,
                              bool& outHasUnweighted)
{
    out.clear();
    outHasUnweighted = false;
    if (!data.HasSkin() || data.boneWeights.size() != data.boneIndices.size()
        || data.boneIndices.size() != data.positions.size()) {
        return false;
    }

    std::unordered_map<std::uint16_t, Collision::Aabb> perBone;
    std::unordered_map<std::uint16_t, bool> seeded;
    for (std::size_t vertex = 0; vertex < data.positions.size(); ++vertex) {
        const Vector3& position = data.positions[vertex];
        if (!IsFinitePoint(position)) {
            continue;
        }
        bool weighted = false;
        for (std::size_t slot = 0; slot < 4; ++slot) {
            const float weight = data.boneWeights[vertex][slot];
            if (!std::isfinite(weight) || weight <= 0.0f) {
                continue;
            }
            weighted = true;
            const std::uint16_t bone = data.boneIndices[vertex][slot];
            bool& any = seeded[bone];
            GrowBounds(perBone[bone], position, any);
        }
        if (!weighted) {
            outHasUnweighted = true;
        }
    }

    out.reserve(perBone.size());
    for (const auto& [bone, box] : perBone) {
        out.push_back(SkinnedBoneBounds{bone, box});
    }
    // Ascending bone order keeps the cached data deterministic across runs,
    // which the hash map iteration order alone would not guarantee.
    std::sort(out.begin(), out.end(),
              [](const SkinnedBoneBounds& a, const SkinnedBoneBounds& b) {
                  return a.bone < b.bone;
              });
    return !out.empty() || outHasUnweighted;
}

bool ComputeSkinnedPoseBounds(const std::vector<SkinnedBoneBounds>& boneBounds,
                              const Matrix4* palette, std::size_t paletteCount,
                              bool includeOrigin, Collision::Aabb& out)
{
    Collision::Aabb result{};
    bool any = false;
    if (includeOrigin) {
        GrowBounds(result, Vector3{0.0f, 0.0f, 0.0f}, any);
    }
    if (palette != nullptr) {
        for (const SkinnedBoneBounds& entry : boneBounds) {
            if (entry.bone >= paletteCount) {
                continue;
            }
            const Collision::Aabb posed = Spatial::WorldAabbFromLocalBox(
                palette[entry.bone].m, entry.rest);
            if (!Collision::IsValidAabb(posed)) {
                continue;
            }
            GrowBounds(result, posed.min, any);
            GrowBounds(result, posed.max, any);
        }
    }
    if (!any) {
        return false;
    }
    out = result;
    return true;
}

} // namespace Concord
