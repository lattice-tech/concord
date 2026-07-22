#ifndef CONCORD_CLUSTEREDLIGHTCULLER_H
#define CONCORD_CLUSTEREDLIGHTCULLER_H

#include "engine/render/frame/RenderLight.h"
#include "engine/render/lighting/ClusterGrid.h"
#include "engine/render/lighting/GpuLight.h"

#include <cstdint>
#include <vector>

namespace Concord {

/**
 * CPU light culler for Forward+ clustered shading.
 *
 * Each frame `Assign` packs the view's lights into a GPU-ready light list and
 * builds, for every froxel cluster, the set of **local** (point/spot) lights
 * whose bounding volume touches that cluster. Directional lights have no bound
 * and are packed first (0..DirectionalCount) so the shader can always apply
 * them without a cluster lookup; local lights follow and are referenced by the
 * per-cluster index ranges.
 *
 * This class is pure and backend-free (no bgfx): the render backend calls
 * `Assign` then uploads `Lights()`, `Ranges()` and `Indices()` into GPU
 * buffers/textures. Keeping the assignment here makes it unit-testable without
 * a graphics device (AGENTS.md §5) and gives the later GPU-compute culler a
 * reference to match.
 */
class ClusteredLightCuller {
public:
    /** Per-cluster window into the flat light-index list. */
    struct ClusterRange {
        std::uint32_t offset = 0;
        std::uint32_t count = 0;
    };

    /**
     * Rebuilds the packed light list and per-cluster local-light assignment for
     * this frame. `view` and `viewProj` are column-major (bx convention, as used
     * by the backend). `grid.nearPlane/farPlane/screenWidth/screenHeight` must
     * be set to the current view.
     */
    void Assign(const RenderLight* lights, std::uint32_t count,
                const float view[16], const float viewProj[16], const ClusterGrid& grid);

    /**
     * Packs the light list only (directional first, then local), skipping the
     * O(lights x clusters) CPU assignment. Used ahead of the GPU compute
     * culler, which performs that assignment on-device from the packed list.
     */
    void PackOnly(const RenderLight* lights, std::uint32_t count);

    /** Packed lights: directional first (0..DirectionalCount), then local. */
    const std::vector<GpuLight>& Lights() const noexcept { return m_lights; }

    /** Number of leading directional lights the shader applies to every fragment. */
    std::uint32_t DirectionalCount() const noexcept { return m_directionalCount; }

    /** Per-cluster (offset,count) into Indices(); size == ClusterGrid::kClusterCount. */
    const std::vector<ClusterRange>& Ranges() const noexcept { return m_ranges; }

    /** Flat local-light index list referenced by Ranges(); indices into Lights(). */
    const std::vector<std::uint32_t>& Indices() const noexcept { return m_indices; }

    /** Light-cluster assignments dropped this frame because a cluster hit capacity. */
    std::uint32_t CappedAssignments() const noexcept { return m_cappedAssignments; }

private:
    std::vector<GpuLight> m_lights;
    std::vector<ClusterRange> m_ranges;
    std::vector<std::uint32_t> m_indices;
    std::uint32_t m_directionalCount = 0;
    std::uint32_t m_cappedAssignments = 0;

    /** Scratch per-cluster bucket reused across frames to avoid reallocation. */
    std::vector<std::vector<std::uint32_t>> m_buckets;
};

} // namespace Concord

#endif // CONCORD_CLUSTEREDLIGHTCULLER_H
