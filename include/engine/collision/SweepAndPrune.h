#ifndef CONCORD_SWEEPANDPRUNE_H
#define CONCORD_SWEEPANDPRUNE_H

#include "engine/collision/Aabb.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace Concord::Collision {

/** A collider-index pair in ascending spawn order. */
using OverlapPair = std::pair<std::size_t, std::size_t>;

/**
 * @brief Finds overlapping AABBs with a deterministic x-axis sweep.
 *
 * The returned pairs are lexicographically sorted, matching the order produced
 * by a brute-force `i`, `j` traversal without paying its cost for sparse scenes.
 */
inline std::vector<OverlapPair> SweepAndPrune(const std::vector<Aabb>& bounds)
{
    std::vector<std::size_t> order(bounds.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [&bounds](std::size_t left, std::size_t right) {
        const float leftMin = bounds[left].min.x;
        const float rightMin = bounds[right].min.x;
        const bool leftNan = std::isnan(leftMin);
        const bool rightNan = std::isnan(rightMin);
        if (leftNan != rightNan) {
            return !leftNan;
        }
        if (!leftNan && leftMin != rightMin) {
            return leftMin < rightMin;
        }
        return left < right;
    });

    std::vector<std::size_t> active;
    std::vector<OverlapPair> overlaps;
    for (const std::size_t index : order) {
        const float sweepMin = bounds[index].min.x;
        std::erase_if(active, [&bounds, sweepMin](std::size_t candidate) {
            return bounds[candidate].max.x < sweepMin;
        });
        for (const std::size_t candidate : active) {
            if (!bounds[index].Overlaps(bounds[candidate])) {
                continue;
            }
            overlaps.emplace_back(std::min(index, candidate), std::max(index, candidate));
        }
        active.push_back(index);
    }
    std::sort(overlaps.begin(), overlaps.end());
    return overlaps;
}

} // namespace Concord::Collision

#endif // CONCORD_SWEEPANDPRUNE_H
