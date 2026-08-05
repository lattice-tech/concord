#include "engine/animation/blend/BlendSpace2D.h"

#include "engine/animation/blend/detail/BlendSpace2DGeometry.h"
#include "engine/animation/clip/AnimationClip.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Concord::Animation {

void BlendSpace2D::AddClip(Vector2 position, const AnimationClip* clip)
{
    Entry entry{position, clip};
    m_entries.push_back(entry);
    m_triangulationValid = false;
}

float BlendSpace2D::Duration() const noexcept
{
    float duration = 0.0f;
    for (const Entry& entry : m_entries) {
        if (entry.clip != nullptr) {
            duration = std::max(duration, entry.clip->Duration());
        }
    }
    return duration;
}

void BlendSpace2D::RebuildTriangulation() const
{
    m_points.clear();
    m_points.reserve(m_entries.size());
    for (const Entry& entry : m_entries) {
        m_points.push_back(entry.position);
    }
    m_triangles.clear();
    m_remap.clear();
    m_triangulationValid = true;
    Detail::BuildDelaunay(m_points, m_triangles, m_remap);
}

Pose BlendSpace2D::SampleEntry(const Entry& entry, float phase)
{
    if (entry.clip == nullptr) {
        return Pose{};
    }
    const float clamped = phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase);
    return entry.clip->SamplePose(clamped * entry.clip->Duration());
}

Pose BlendSpace2D::Sample(float x, float y, float phase) const
{
    if (m_entries.empty()) {
        return Pose{};
    }
    if (m_entries.size() == 1) {
        return SampleEntry(m_entries.front(), phase);
    }
    if (!m_triangulationValid) {
        RebuildTriangulation();
    }

    if (!m_triangles.empty()) {
        const std::vector<std::uint32_t> dedupToEntry =
            Detail::BuildDedupToEntry(m_remap, m_points.size());
        const Vector2 query{x, y};

        // Inside a triangle: barycentric blend of its three clips.
        for (const Detail::BlendTriangle& triangle : m_triangles) {
            if (!Detail::ContainsPoint(query, triangle, m_points)) {
                continue;
            }
            float wA = 0.0f;
            float wB = 0.0f;
            float wC = 0.0f;
            if (!Detail::BarycentricWeights(query, triangle, m_points, wA, wB, wC)) {
                continue;
            }
            const Entry& entryA = m_entries[dedupToEntry[triangle.a]];
            const Entry& entryB = m_entries[dedupToEntry[triangle.b]];
            const Entry& entryC = m_entries[dedupToEntry[triangle.c]];
            const float abWeight = wA + wB > 1.0e-6f ? wB / (wA + wB) : 0.0f;
            const Pose poseAB = BlendPose(SampleEntry(entryA, phase),
                                          SampleEntry(entryB, phase), abWeight);
            return BlendPose(poseAB, SampleEntry(entryC, phase), wC);
        }

        // Outside the convex hull: blend along the nearest triangle edge.
        const Entry* bestA = nullptr;
        const Entry* bestB = nullptr;
        float bestT = 0.0f;
        float bestDistance = std::numeric_limits<float>::max();
        for (const Detail::BlendTriangle& triangle : m_triangles) {
            const std::uint32_t edges[3][2] = {
                {triangle.a, triangle.b},
                {triangle.b, triangle.c},
                {triangle.c, triangle.a},
            };
            for (const auto& edge : edges) {
                const Vector2& p0 = m_points[edge[0]];
                const Vector2& p1 = m_points[edge[1]];
                const float dx = p1.x - p0.x;
                const float dy = p1.y - p0.y;
                const float lengthSquared = dx * dx + dy * dy;
                const float t = lengthSquared > 1.0e-12f
                    ? std::clamp(((x - p0.x) * dx + (y - p0.y) * dy) / lengthSquared,
                                 0.0f, 1.0f)
                    : 0.0f;
                const float projX = p0.x + t * dx - x;
                const float projY = p0.y + t * dy - y;
                const float distance = projX * projX + projY * projY;
                if (distance >= bestDistance) {
                    continue;
                }
                bestDistance = distance;
                bestT = t;
                bestA = &m_entries[dedupToEntry[edge[0]]];
                bestB = &m_entries[dedupToEntry[edge[1]]];
            }
        }
        if (bestA != nullptr && bestB != nullptr) {
            return BlendPose(SampleEntry(*bestA, phase), SampleEntry(*bestB, phase),
                             bestT);
        }
    }

    // Degenerate layout (collinear or fewer than three distinct points):
    // project onto the longest axis and blend exactly like a 1D blend space.
    const Detail::CollinearAxis axis = Detail::BuildCollinearAxis(m_points);
    if (axis.order.empty()) {
        return SampleEntry(m_entries.front(), phase);
    }
    const Vector2& origin = m_points[axis.order.front()];
    const float query = (x - origin.x) * axis.direction.x
        + (y - origin.y) * axis.direction.y;
    if (query <= axis.parameters.front()) {
        return SampleEntry(m_entries[axis.order.front()], phase);
    }
    if (query >= axis.parameters.back()) {
        return SampleEntry(m_entries[axis.order.back()], phase);
    }
    std::size_t i = 0;
    while (i + 1 < axis.order.size() && axis.parameters[i + 1] <= query) {
        ++i;
    }
    const float span = axis.parameters[i + 1] - axis.parameters[i];
    const float t = span > 1.0e-6f ? (query - axis.parameters[i]) / span : 0.0f;
    return BlendPose(SampleEntry(m_entries[axis.order[i]], phase),
                     SampleEntry(m_entries[axis.order[i + 1]], phase), t);
}

} // namespace Concord::Animation
