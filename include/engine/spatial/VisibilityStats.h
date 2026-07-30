#ifndef CONCORD_VISIBILITYSTATS_H
#define CONCORD_VISIBILITYSTATS_H

#include <cstdint>

namespace Concord::Spatial {

/**
 * @brief Authored / extracted / culled / submitted counts for one cull pass.
 *
 * These counters let a frame compare tree culling against a brute-force oracle
 * and establish 10k/100k baselines without parsing logs.
 */
struct VisibilityStats {
    /** Proxies present in the spatial index at cull start. */
    std::uint32_t authored = 0;
    /** Proxies considered by the cull (same as authored for full-tree walks). */
    std::uint32_t extracted = 0;
    /** Proxies rejected by the frustum (or other visibility test). */
    std::uint32_t culled = 0;
    /** Proxies that survived and would be submitted to the renderer. */
    std::uint32_t submitted = 0;
    /** Internal BVH nodes visited during the cull walk. */
    std::uint32_t nodesVisited = 0;
};

} // namespace Concord::Spatial

#endif // CONCORD_VISIBILITYSTATS_H
