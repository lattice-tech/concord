#ifndef CONCORD_BLENDSPACE2DGEOMETRY_H
#define CONCORD_BLENDSPACE2DGEOMETRY_H

#include "math/Vector2.h"

#include <cstdint>
#include <vector>

namespace Concord::Animation::Detail {

/**
 * One triangle of a 2D blend-space triangulation: three indices into the
 * *deduplicated* point list handed to BuildDelaunay. Counter-clockwise.
 */
struct BlendTriangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

/**
 * @brief Deterministic Delaunay triangulation of a blend-space sample set.
 *
 * Implements incremental Bowyer-Watson over a bounding super-triangle with
 * double-precision circumcircle tests. Given the same points in the same
 * order, the triangle list is identical every call — the 2D blend spaces rely
 * on that for reproducible poses.
 *
 * Coincident points (within a small epsilon) are collapsed: `outRemap` maps
 * every input index to the deduplicated index that stands for it (the first
 * occurrence wins), and triangle vertices name deduplicated indices.
 *
 * @param points  Sample positions, in insertion order (may repeat).
 * @param outTriangles  Receives the triangulation on success.
 * @param outRemap  Receives input index -> deduplicated index.
 * @return false when the points cannot form a triangle: fewer than three
 *         distinct points, or every distinct point collinear.
 */
bool BuildDelaunay(const std::vector<Vector2>& points,
                   std::vector<BlendTriangle>& outTriangles,
                   std::vector<std::uint32_t>& outRemap);

/**
 * @brief Barycentric weights of `point` inside `triangle`.
 *
 * The caller already tested containment, so this computes positive weights
 * that sum to one. Returns false when the triangle is degenerate (zero area),
 * which the caller must treat as "no triangle".
 */
bool BarycentricWeights(const Vector2& point, const BlendTriangle& triangle,
                        const std::vector<Vector2>& points, float& outA,
                        float& outB, float& outC);

/**
 * @brief Contains a point in `triangle` (including its edges).
 *
 * Exact-area test in double precision, so points that sit exactly on an edge
 * or vertex belong to the triangle; shared edges therefore never "lose" a
 * query point.
 */
bool ContainsPoint(const Vector2& point, const BlendTriangle& triangle,
                   const std::vector<Vector2>& points) noexcept;

/**
 * @brief Builds the inverse of a deduplication remap.
 *
 * `remap` maps original sample indices to deduplicated indices (the first
 * occurrence wins). This returns, for every deduplicated index, the original
 * index that represents it — so triangle vertices (deduplicated indices) can
 * be translated back to the caller's clip list.
 */
std::vector<std::uint32_t> BuildDedupToEntry(const std::vector<std::uint32_t>& remap,
                                             std::size_t dedupCount);

/**
 * @brief Computes the blend-space axes for the collinear fallback.
 *
 * When a 2D blend space degenerates to a line (all samples collinear), the
 * samples are sorted along the longest point-pair axis and the control value
 * is interpolated over that axis exactly like a 1D blend space.
 *
 * @param points  Deduplicated sample positions.
 * @param outAxis  Normalised line direction (doubles as the projection axis).
 * @return Normalised parameters `t` per sample in *sorted* order, plus the
 *         matching sample indices. Empty when there are fewer than two points.
 */
struct CollinearAxis {
    Vector2 direction{};
    std::vector<float> parameters;
    std::vector<std::uint32_t> order;
};
CollinearAxis BuildCollinearAxis(const std::vector<Vector2>& points);

} // namespace Concord::Animation::Detail

#endif // CONCORD_BLENDSPACE2DGEOMETRY_H
