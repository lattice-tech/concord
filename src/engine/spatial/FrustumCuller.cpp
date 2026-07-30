#include "engine/spatial/FrustumCuller.h"

#include <algorithm>
#include <vector>

namespace Concord::Spatial {
namespace {

bool PassesFilter(const SpatialProxy& proxy, SpatialQueryFilter filter) noexcept
{
    return (proxy.layer & filter.layerMask) != 0u;
}

} // namespace

VisibilityStats CullFrustum(
    const DynamicAabbTree& tree, const Frustum& frustum, SpatialQueryFilter filter,
    const std::function<void(SpatialId, const SpatialProxy&)>& visitor)
{
    VisibilityStats stats;
    stats.authored = static_cast<std::uint32_t>(tree.Size());
    stats.extracted = stats.authored;

    std::uint32_t submitted = 0;
    tree.QueryFrustum(
        [&](const Collision::Aabb& bounds) {
            return FrustumIntersectsAabb(frustum, bounds);
        },
        filter, stats.nodesVisited,
        [&](SpatialId id, const SpatialProxy& proxy) {
            ++submitted;
            if (visitor) {
                visitor(id, proxy);
            }
        });
    stats.submitted = submitted;
    stats.culled = stats.extracted >= stats.submitted
        ? stats.extracted - stats.submitted : 0;
    return stats;
}

VisibilityStats CullFrustumBruteForce(
    const DynamicAabbTree& tree, const Frustum& frustum, SpatialQueryFilter filter,
    const std::function<void(SpatialId, const SpatialProxy&)>& visitor)
{
    VisibilityStats stats;
    stats.authored = static_cast<std::uint32_t>(tree.Size());
    stats.extracted = stats.authored;

    Collision::Aabb world{};
    if (!tree.WorldBounds(world)) {
        return stats;
    }

    // Full leaf scan via world-overlap query (visits every live leaf).
    tree.QueryOverlap(world, SpatialQueryFilter{},
                      [&](SpatialId id, const SpatialProxy& proxy) {
                          ++stats.nodesVisited;
                          if (!PassesFilter(proxy, filter)
                              || !FrustumIntersectsAabb(frustum, proxy.bounds)) {
                              ++stats.culled;
                              return;
                          }
                          ++stats.submitted;
                          if (visitor) {
                              visitor(id, proxy);
                          }
                      });
    return stats;
}

void CollectVisibleIds(const DynamicAabbTree& tree, const Frustum& frustum,
                       SpatialQueryFilter filter, std::vector<SpatialId>& out)
{
    out.clear();
    CullFrustum(tree, frustum, filter, [&](SpatialId id, const SpatialProxy&) {
        out.push_back(id);
    });
    std::sort(out.begin(), out.end(), [](SpatialId a, SpatialId b) {
        if (a.slot != b.slot) {
            return a.slot < b.slot;
        }
        return a.generation < b.generation;
    });
}

} // namespace Concord::Spatial
