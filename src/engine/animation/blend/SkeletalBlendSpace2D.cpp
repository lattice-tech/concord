#include "engine/animation/blend/SkeletalBlendSpace2D.h"

#include "engine/animation/blend/SkeletalBlend.h"
#include "engine/animation/blend/detail/BlendSpace2DGeometry.h"
#include "engine/animation/clip/SkeletalClip.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace Concord::Animation {
namespace {

/** 1D-style bracket lookup shared with the collinear fallback below. */
float BlendParameter(const std::vector<float>& parameters, float query,
                     std::size_t& outIndex)
{
    outIndex = 0;
    while (outIndex + 1 < parameters.size()
           && parameters[outIndex + 1] <= query) {
        ++outIndex;
    }
    const float span = parameters[outIndex + 1] - parameters[outIndex];
    return span > 1.0e-6f ? (query - parameters[outIndex]) / span : 0.0f;
}

} // namespace

void SkeletalBlendSpace2D::AddClip(Vector2 position, const SkeletalClip* clip)
{
    Entry entry{position, clip};
    m_entries.push_back(entry);
    m_triangulationValid = false;
}

float SkeletalBlendSpace2D::Duration() const noexcept
{
    float duration = 0.0f;
    for (const Entry& entry : m_entries) {
        if (entry.clip != nullptr) {
            duration = std::max(duration, entry.clip->Duration());
        }
    }
    return duration;
}

void SkeletalBlendSpace2D::RebuildTriangulation() const
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

void SkeletalBlendSpace2D::SampleEntry(const Entry& entry, float phase,
                                       const Skeleton& skeleton, SkeletonPose& out)
{
    if (entry.clip == nullptr) {
        out = skeleton.BindPose();
        return;
    }
    const float clamped = phase < 0.0f ? 0.0f : (phase > 1.0f ? 1.0f : phase);
    entry.clip->Sample(clamped * entry.clip->Duration(), skeleton, out);
}

void SkeletalBlendSpace2D::Sample(float x, float y, float phase,
                                  const Skeleton& skeleton, SkeletonPose& out) const
{
    if (m_entries.empty()) {
        out = skeleton.BindPose();
        return;
    }
    if (m_entries.size() == 1) {
        SampleEntry(m_entries.front(), phase, skeleton, out);
        return;
    }
    if (!m_triangulationValid) {
        RebuildTriangulation();
    }

    if (!m_triangles.empty()) {
        const std::vector<std::uint32_t> dedupToEntry =
            Detail::BuildDedupToEntry(m_remap, m_points.size());
        const Vector2 query{x, y};

        // Inside a triangle: bone-by-bone barycentric blend of three clips.
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
            SkeletonPose poseA;
            SkeletonPose poseB;
            SkeletonPose poseC;
            SampleEntry(entryA, phase, skeleton, poseA);
            SampleEntry(entryB, phase, skeleton, poseB);
            SampleEntry(entryC, phase, skeleton, poseC);
            const float abWeight = wA + wB > 1.0e-6f ? wB / (wA + wB) : 0.0f;
            SkeletonPose poseAB;
            BlendSkeletonPose(poseA, poseB, abWeight, poseAB);
            BlendSkeletonPose(poseAB, poseC, wC, out);
            return;
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
            SkeletonPose poseA;
            SkeletonPose poseB;
            SampleEntry(*bestA, phase, skeleton, poseA);
            SampleEntry(*bestB, phase, skeleton, poseB);
            BlendSkeletonPose(poseA, poseB, bestT, out);
            return;
        }
    }

    // Degenerate layout (collinear or fewer than three distinct points):
    // project onto the longest axis and blend exactly like a 1D blend space.
    const Detail::CollinearAxis axis = Detail::BuildCollinearAxis(m_points);
    if (axis.order.empty()) {
        SampleEntry(m_entries.front(), phase, skeleton, out);
        return;
    }
    const Vector2& origin = m_points[axis.order.front()];
    const float query = (x - origin.x) * axis.direction.x
        + (y - origin.y) * axis.direction.y;
    if (query <= axis.parameters.front()) {
        SampleEntry(m_entries[axis.order.front()], phase, skeleton, out);
        return;
    }
    if (query >= axis.parameters.back()) {
        SampleEntry(m_entries[axis.order.back()], phase, skeleton, out);
        return;
    }
    std::size_t index = 0;
    const float t = BlendParameter(axis.parameters, query, index);
    SkeletonPose poseA;
    SkeletonPose poseB;
    SampleEntry(m_entries[axis.order[index]], phase, skeleton, poseA);
    SampleEntry(m_entries[axis.order[index + 1]], phase, skeleton, poseB);
    BlendSkeletonPose(poseA, poseB, t, out);
}

} // namespace Concord::Animation
