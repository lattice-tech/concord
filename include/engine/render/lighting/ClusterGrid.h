#ifndef CONCORD_CLUSTERGRID_H
#define CONCORD_CLUSTERGRID_H

#include <cmath>
#include <cstdint>

namespace Concord {

/**
 * Froxel (clustered) grid parameters and math for Forward+ lighting.
 *
 * The camera frustum is divided into a 3D grid: `kDimX × kDimY` screen tiles
 * extruded across `kDimZ` depth slices distributed **logarithmically** between
 * the near and far planes, so slices pack densely near the camera where local
 * lights matter most. Each frame the light culler assigns local lights to the
 * clusters their volume touches; the mesh shader maps a fragment to its cluster
 * and shades only that cluster's lights.
 *
 * This header is the single source of truth for the grid constants shared by
 * the C++ culler and the shaders (mirrored into `clusters.sh`), and it is pure
 * and dependency-free so the mapping math can be unit-tested without a device.
 * (AGENTS.md §5: reusable, module-agnostic.)
 */
struct ClusterGrid {
    /** Screen tiles across X and Y, and depth slices along Z. */
    static constexpr std::uint32_t kDimX = 16;
    static constexpr std::uint32_t kDimY = 9;
    static constexpr std::uint32_t kDimZ = 24;

    /** Total clusters per view. */
    static constexpr std::uint32_t kClusterCount = kDimX * kDimY * kDimZ;

    /** Maximum local lights assigned to a single cluster (per-cluster capacity). */
    static constexpr std::uint32_t kMaxLightsPerCluster = 64;

    /** Camera near/far in view space; set per view before mapping. */
    float nearPlane = 0.1f;
    float farPlane = 200.0f;

    /**
     * tan(fovY/2) and aspect (width/height); only needed by the GPU compute
     * culler, which reconstructs each cluster's view-space frustum slice
     * on-device rather than receiving a CPU-built AABB. The CPU culler does not
     * use these (it works directly from the view-projection matrix).
     */
    float tanHalfFovY = 0.5773503f; // ~60 degree vertical FOV default
    float aspect = 16.0f / 9.0f;

    /** View resolution in pixels; tiles cover it uniformly. */
    std::uint32_t screenWidth = 1;
    std::uint32_t screenHeight = 1;

    /**
     * Depth slice index for a positive view-space depth (distance in front of
     * the camera), logarithmic between near and far, clamped to [0, kDimZ).
     */
    std::uint32_t SliceForViewDepth(float viewZ) const noexcept
    {
        const float z = viewZ < nearPlane ? nearPlane : viewZ;
        const float logFn = std::log(farPlane / nearPlane);
        if (logFn <= 0.0f) {
            return 0;
        }
        const float t = std::log(z / nearPlane) / logFn;
        int slice = static_cast<int>(t * static_cast<float>(kDimZ));
        if (slice < 0) {
            slice = 0;
        }
        if (slice >= static_cast<int>(kDimZ)) {
            slice = static_cast<int>(kDimZ) - 1;
        }
        return static_cast<std::uint32_t>(slice);
    }

    /** View-space depth at the near edge of a depth slice (0..kDimZ). */
    float SliceNearDepth(std::uint32_t slice) const noexcept
    {
        const float t = static_cast<float>(slice) / static_cast<float>(kDimZ);
        return nearPlane * std::pow(farPlane / nearPlane, t);
    }

    /** Screen tile index (x,y) for a pixel coordinate, clamped to the grid. */
    void TileForPixel(float px, float py, std::uint32_t& tileX, std::uint32_t& tileY) const noexcept
    {
        const float fx = px / (static_cast<float>(screenWidth) / static_cast<float>(kDimX));
        const float fy = py / (static_cast<float>(screenHeight) / static_cast<float>(kDimY));
        tileX = ClampTile(fx, kDimX);
        tileY = ClampTile(fy, kDimY);
    }

    /** Flat cluster index from tile (x,y) and depth slice. */
    static std::uint32_t Index(std::uint32_t tileX, std::uint32_t tileY, std::uint32_t slice) noexcept
    {
        return slice * (kDimX * kDimY) + tileY * kDimX + tileX;
    }

private:
    static std::uint32_t ClampTile(float f, std::uint32_t dim) noexcept
    {
        int i = static_cast<int>(f);
        if (i < 0) {
            i = 0;
        }
        if (i >= static_cast<int>(dim)) {
            i = static_cast<int>(dim) - 1;
        }
        return static_cast<std::uint32_t>(i);
    }
};

} // namespace Concord

#endif // CONCORD_CLUSTERGRID_H
