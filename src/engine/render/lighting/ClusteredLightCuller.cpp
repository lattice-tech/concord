#include "engine/render/lighting/ClusteredLightCuller.h"

#include <algorithm>
#include <cmath>

namespace Concord {

namespace {

/** Column-major 4x4 (bx convention) times a (x,y,z,w) point; writes 4 floats. */
void MulPoint(const float m[16], float x, float y, float z, float w, float out[4]) noexcept
{
    out[0] = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
    out[1] = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
    out[2] = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
    out[3] = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
}

} // namespace

void ClusteredLightCuller::PackOnly(const RenderLight* lights, std::uint32_t count)
{
    m_lights.clear();
    m_ranges.clear();
    m_indices.clear();
    m_directionalCount = 0;
    m_cappedAssignments = 0;
    m_droppedLights = 0;
    if (lights == nullptr || count == 0) {
        return;
    }
    m_lights.reserve(std::min(count, ClusterGrid::kMaxPackedLights));
    for (std::uint32_t i = 0;
         i < count && m_lights.size() < ClusterGrid::kMaxPackedLights;
         ++i) {
        if (lights[i].type == LightType::Directional) {
            m_lights.push_back(GpuLight::Pack(lights[i], i));
            ++m_directionalCount;
        }
    }
    for (std::uint32_t i = 0;
         i < count && m_lights.size() < ClusterGrid::kMaxPackedLights;
         ++i) {
        if (lights[i].type != LightType::Directional) {
            m_lights.push_back(GpuLight::Pack(lights[i], i));
        }
    }
    m_droppedLights = count - static_cast<std::uint32_t>(m_lights.size());
}

void ClusteredLightCuller::Assign(const RenderLight* lights, std::uint32_t count,
                                  const float view[16], const float viewProj[16],
                                  const ClusterGrid& grid)
{
    m_lights.clear();
    m_indices.clear();
    m_directionalCount = 0;
    m_cappedAssignments = 0;
    m_droppedLights = 0;

    const std::size_t clusterCount = ClusterGrid::kClusterCount;
    m_ranges.assign(clusterCount, ClusterRange{});
    if (m_buckets.size() != clusterCount) {
        m_buckets.assign(clusterCount, {});
    }
    for (std::vector<std::uint32_t>& bucket : m_buckets) {
        bucket.clear();
    }

    if (lights == nullptr || count == 0) {
        return;
    }

    // Pack directional lights first (applied to every fragment), then locals.
    m_lights.reserve(std::min(count, ClusterGrid::kMaxPackedLights));
    for (std::uint32_t i = 0;
         i < count && m_lights.size() < ClusterGrid::kMaxPackedLights;
         ++i) {
        if (lights[i].type == LightType::Directional) {
            m_lights.push_back(GpuLight::Pack(lights[i], i));
            ++m_directionalCount;
        }
    }
    std::vector<std::uint32_t> localSource; // original index of each packed local light
    localSource.reserve(ClusterGrid::kMaxPackedLights - m_directionalCount);
    for (std::uint32_t i = 0;
         i < count && m_lights.size() < ClusterGrid::kMaxPackedLights;
         ++i) {
        if (lights[i].type != LightType::Directional) {
            m_lights.push_back(GpuLight::Pack(lights[i], i));
            localSource.push_back(i);
        }
    }
    m_droppedLights = count - static_cast<std::uint32_t>(m_lights.size());

    // Assign each local light to the clusters its bounding sphere touches.
    const float fw = static_cast<float>(grid.screenWidth);
    const float fh = static_cast<float>(grid.screenHeight);
    for (std::size_t local = 0; local < localSource.size(); ++local) {
        const RenderLight& light = lights[localSource[local]];
        const std::uint32_t packedIndex = m_directionalCount + static_cast<std::uint32_t>(local);
        const float range = light.range > 0.0f ? light.range : 0.0f;

        // View-space depth (left-handed: +Z in front) for the slice span.
        float viewPos[4];
        MulPoint(view, light.position[0], light.position[1], light.position[2], 1.0f, viewPos);
        const float vz = viewPos[2];
        if (vz + range < grid.nearPlane) {
            continue; // entirely behind the near plane
        }
        const std::uint32_t sliceMin = grid.SliceForViewDepth(vz - range);
        const std::uint32_t sliceMax = grid.SliceForViewDepth(vz + range);

        // Screen-space tile span from the sphere's world-AABB corners. Seed to
        // ±infinity so a sphere that projects entirely off one edge is rejected
        // rather than clamped back onto the screen border.
        float minX = 1e30f;
        float minY = 1e30f;
        float maxX = -1e30f;
        float maxY = -1e30f;
        bool fullScreen = false;
        for (int c = 0; c < 8 && !fullScreen; ++c) {
            const float cx = light.position[0] + ((c & 1) ? range : -range);
            const float cy = light.position[1] + ((c & 2) ? range : -range);
            const float cz = light.position[2] + ((c & 4) ? range : -range);
            float clip[4];
            MulPoint(viewProj, cx, cy, cz, 1.0f, clip);
            if (clip[3] <= 1e-4f) {
                fullScreen = true; // corner at/behind the eye: be conservative
                break;
            }
            const float ndcX = clip[0] / clip[3];
            const float ndcY = clip[1] / clip[3];
            const float sx = (ndcX * 0.5f + 0.5f) * fw;
            const float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * fh;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
        }

        std::uint32_t tileMinX = 0;
        std::uint32_t tileMinY = 0;
        std::uint32_t tileMaxX = ClusterGrid::kDimX - 1;
        std::uint32_t tileMaxY = ClusterGrid::kDimY - 1;
        if (!fullScreen) {
            if (maxX < 0.0f || minX > fw || maxY < 0.0f || minY > fh) {
                continue; // sphere projects fully off-screen
            }
            grid.TileForPixel(std::max(minX, 0.0f), std::max(minY, 0.0f), tileMinX, tileMinY);
            grid.TileForPixel(std::min(maxX, fw - 1.0f), std::min(maxY, fh - 1.0f), tileMaxX, tileMaxY);
        }

        for (std::uint32_t slice = sliceMin; slice <= sliceMax; ++slice) {
            for (std::uint32_t ty = tileMinY; ty <= tileMaxY; ++ty) {
                for (std::uint32_t tx = tileMinX; tx <= tileMaxX; ++tx) {
                    std::vector<std::uint32_t>& bucket =
                        m_buckets[ClusterGrid::Index(tx, ty, slice)];
                    if (bucket.size() < ClusterGrid::kMaxLightsPerCluster) {
                        bucket.push_back(packedIndex);
                    } else {
                        ++m_cappedAssignments;
                    }
                }
            }
        }
    }

    // Flatten buckets into the contiguous index list + per-cluster ranges.
    std::uint32_t offset = 0;
    for (std::size_t c = 0; c < clusterCount; ++c) {
        const std::vector<std::uint32_t>& bucket = m_buckets[c];
        m_ranges[c].offset = offset;
        m_ranges[c].count = static_cast<std::uint32_t>(bucket.size());
        m_indices.insert(m_indices.end(), bucket.begin(), bucket.end());
        offset += static_cast<std::uint32_t>(bucket.size());
    }
}

} // namespace Concord
