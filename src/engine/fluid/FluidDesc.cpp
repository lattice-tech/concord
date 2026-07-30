#include "engine/fluid/FluidDesc.h"

#include <algorithm>
#include <cmath>

namespace {

float SanitizePositive(float value, float fallback) noexcept
{
    return (std::isfinite(value) && value > 0.0f) ? value : fallback;
}

float SanitizeNonNegative(float value, float fallback) noexcept
{
    return (std::isfinite(value) && value >= 0.0f) ? value : fallback;
}

float SanitizeFraction(float value, float fallback) noexcept
{
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

std::uint32_t SanitizeCount(std::uint32_t value, std::uint32_t fallback,
                            std::uint32_t lo, std::uint32_t hi) noexcept
{
    if (value == 0) {
        return fallback;
    }
    return std::clamp(value, lo, hi);
}

} // namespace

namespace Concord::Fluid {

FluidTankDesc NormalizeFluidDesc(const FluidTankDesc& desc)
{
    FluidTankDesc out = desc;
    out.tankSize.x = SanitizePositive(desc.tankSize.x, 1.2f);
    out.tankSize.y = SanitizePositive(desc.tankSize.y, 0.9f);
    out.tankSize.z = SanitizePositive(desc.tankSize.z, 0.9f);
    out.particleSpacing = std::clamp(SanitizePositive(desc.particleSpacing, 0.03f),
                                     0.005f, 1.0f);
    out.fillMin.x = SanitizeFraction(desc.fillMin.x, 0.05f);
    out.fillMin.y = SanitizeFraction(desc.fillMin.y, 0.05f);
    out.fillMin.z = SanitizeFraction(desc.fillMin.z, 0.05f);
    out.fillMax.x = SanitizeFraction(desc.fillMax.x, 0.5f);
    out.fillMax.y = SanitizeFraction(desc.fillMax.y, 0.85f);
    out.fillMax.z = SanitizeFraction(desc.fillMax.z, 0.95f);
    // An empty or inverted fill box would seed zero particles; force a span.
    const float kMinSpan = 0.02f;
    auto fixAxis = [kMinSpan](float& lo, float& hi) {
        if (hi - lo < kMinSpan) {
            hi = std::min(1.0f, lo + kMinSpan);
            lo = std::min(lo, hi - kMinSpan);
        }
    };
    fixAxis(out.fillMin.x, out.fillMax.x);
    fixAxis(out.fillMin.y, out.fillMax.y);
    fixAxis(out.fillMin.z, out.fillMax.z);

    out.restDensity = SanitizePositive(desc.restDensity, 1000.0f);
    out.substeps = SanitizeCount(desc.substeps, 2, 1, 8);
    out.densityIterations = SanitizeCount(desc.densityIterations, 6, 1, 32);
    out.divergenceIterations = SanitizeCount(desc.divergenceIterations, 4, 1, 32);
    out.viscosity = std::clamp(SanitizeNonNegative(desc.viscosity, 0.08f), 0.0f, 1.0f);
    out.gravityScale = SanitizeNonNegative(desc.gravityScale, 1.0f);
    if (!(desc.fieldCellSize > 0.0f) || !std::isfinite(desc.fieldCellSize)) {
        out.fieldCellSize = 0.0f; // auto: derived in ComputeFluidLayout
    } else {
        out.fieldCellSize = std::clamp(desc.fieldCellSize, 0.005f, 1.0f);
    }
    out.fieldSmoothing = std::clamp(SanitizeNonNegative(desc.fieldSmoothing, 0.55f),
                                    0.0f, 0.95f);
    out.isoLevel = std::clamp(SanitizePositive(desc.isoLevel, 0.5f), 0.05f, 0.95f);
    out.ior = std::clamp(SanitizePositive(desc.ior, 1.33f), 1.0f, 2.5f);
    out.absorption = std::clamp(SanitizeNonNegative(desc.absorption, 0.9f), 0.0f, 40.0f);
    out.glintStrength = std::clamp(SanitizeNonNegative(desc.glintStrength, 1.0f), 0.0f, 8.0f);
    out.roughness = std::clamp(SanitizePositive(desc.roughness, 0.05f), 0.01f, 1.0f);

    if (out.obstacleEnabled) {
        const float halves[3] = {out.tankSize.x * 0.5f, out.tankSize.y * 0.5f,
                                 out.tankSize.z * 0.5f};
        float lo[3] = {desc.obstacleMin.x, desc.obstacleMin.y, desc.obstacleMin.z};
        float hi[3] = {desc.obstacleMax.x, desc.obstacleMax.y, desc.obstacleMax.z};
        bool finite = true;
        for (int axis = 0; axis < 3; ++axis) {
            finite = finite && std::isfinite(lo[axis]) && std::isfinite(hi[axis]);
            if (lo[axis] > hi[axis]) {
                std::swap(lo[axis], hi[axis]);
            }
        }
        // Keep one spacing of clearance to the tank walls so the two boundary
        // shells never interpenetrate; a box that no longer fits is disabled.
        const float margin = out.particleSpacing;
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::clamp(lo[axis], -halves[axis] + margin, halves[axis] - margin);
            hi[axis] = std::clamp(hi[axis], -halves[axis] + margin, halves[axis] - margin);
            if (hi[axis] - lo[axis] < margin) {
                out.obstacleEnabled = false;
            }
        }
        if (!finite) {
            out.obstacleEnabled = false;
        }
        out.obstacleMin = {lo[0], lo[1], lo[2]};
        out.obstacleMax = {hi[0], hi[1], hi[2]};
    }
    return out;
}

FluidLayout ComputeFluidLayout(const FluidTankDesc& authored)
{
    const FluidTankDesc desc = NormalizeFluidDesc(authored);
    FluidLayout layout;

    const float sizes[3] = {desc.tankSize.x, desc.tankSize.y, desc.tankSize.z};
    const float fillLo[3] = {desc.fillMin.x, desc.fillMin.y, desc.fillMin.z};
    const float fillHi[3] = {desc.fillMax.x, desc.fillMax.y, desc.fillMax.z};

    // Fill lattice: one particle per spacing cell inside the fill box. When
    // the raw count exceeds the cap, or when the neighbor-search grid would
    // exceed the single-workgroup scan limit the compute shaders rely on, the
    // spacing is widened uniformly so the authored volume stays filled rather
    // than silently truncated.
    float spacing = desc.particleSpacing;
    for (;;) {
        std::uint64_t total = 1;
        std::uint32_t counts[3];
        std::uint64_t cells = 1;
        for (int axis = 0; axis < 3; ++axis) {
            const float span = (fillHi[axis] - fillLo[axis]) * sizes[axis];
            counts[axis] = std::max<std::uint32_t>(
                1, static_cast<std::uint32_t>(std::floor(span / spacing)));
            total *= counts[axis];

            const float h = 2.0f * spacing;
            const std::uint32_t gridAxis = std::max<std::uint32_t>(
                1, static_cast<std::uint32_t>(std::ceil(sizes[axis] / h)) + 2);
            cells *= gridAxis;
        }
        if ((total <= kMaxFluidParticles && cells <= kMaxFluidGridCells) || spacing >= 1.0f) {
            layout.particleCount = static_cast<std::uint32_t>(total);
            layout.fluidLattice[0] = counts[0];
            layout.fluidLattice[1] = counts[1];
            layout.fluidLattice[2] = counts[2];
            break;
        }
        spacing *= 1.25f;
    }
    layout.effectiveSpacing = spacing;
    layout.kernelRadius = 2.0f * spacing;

    // Boundary particles: two layers on every wall (on the wall plane and one
    // spacing inward), so the kernel support of a fluid particle touching a
    // wall is fully covered (Akinci et al. 2012).
    std::uint32_t wall[3];
    std::uint64_t boundary = 0;
    for (int axis = 0; axis < 3; ++axis) {
        wall[axis] = std::max<std::uint32_t>(
            2, static_cast<std::uint32_t>(std::floor(sizes[axis] / spacing)));
    }
    boundary = 2ull * 2ull * (static_cast<std::uint64_t>(wall[0]) * wall[1]
                              + static_cast<std::uint64_t>(wall[0]) * wall[2]
                              + static_cast<std::uint64_t>(wall[1]) * wall[2]);

    // Optional obstacle shell: same two-layer sampling on the obstacle box,
    // layered outward (the fluid surrounds the box), appended after the walls.
    std::uint64_t obstacle = 0;
    if (desc.obstacleEnabled) {
        const float omin[3] = {desc.obstacleMin.x, desc.obstacleMin.y, desc.obstacleMin.z};
        const float omax[3] = {desc.obstacleMax.x, desc.obstacleMax.y, desc.obstacleMax.z};
        std::uint32_t owall[3];
        for (int axis = 0; axis < 3; ++axis) {
            owall[axis] = std::max<std::uint32_t>(
                2, static_cast<std::uint32_t>(std::floor((omax[axis] - omin[axis]) / spacing)));
        }
        obstacle = 2ull * 2ull * (static_cast<std::uint64_t>(owall[0]) * owall[1]
                                  + static_cast<std::uint64_t>(owall[0]) * owall[2]
                                  + static_cast<std::uint64_t>(owall[1]) * owall[2]);
    }
    layout.obstacleBoundaryCount = static_cast<std::uint32_t>(obstacle);
    boundary += obstacle;
    while (layout.particleCount + boundary > kMaxFluidPoints && boundary > 0) {
        // Over the point budget: drop to a single wall layer.
        boundary /= 2;
        layout.obstacleBoundaryCount /= 2;
    }
    layout.boundaryCount = static_cast<std::uint32_t>(boundary);

    // Neighbor grid: cell = kernel radius, one-cell margin outside the tank so
    // wall particles sit inside the grid too.
    const float h = layout.kernelRadius;
    std::uint32_t cells = 1;
    for (int axis = 0; axis < 3; ++axis) {
        const std::uint32_t n = std::max<std::uint32_t>(
            1, static_cast<std::uint32_t>(std::ceil(sizes[axis] / h)) + 2);
        layout.gridDims[axis] = n;
        layout.gridOrigin[axis] = -0.5f * sizes[axis] - h;
        cells *= n;
    }
    // The spacing-widening loop above should already satisfy the shader contract,
    // but keep a hard safety net that *really* converges rather than clamping
    // each axis independently (which can still leave the product above the cap).
    while (cells > kMaxFluidGridCells) {
        layout.effectiveSpacing *= 1.25f;
        layout.kernelRadius = 2.0f * layout.effectiveSpacing;
        const float widenedH = layout.kernelRadius;
        cells = 1;
        for (int axis = 0; axis < 3; ++axis) {
            const std::uint32_t n = std::max<std::uint32_t>(
                1, static_cast<std::uint32_t>(std::ceil(sizes[axis] / widenedH)) + 2);
            layout.gridDims[axis] = n;
            layout.gridOrigin[axis] = -0.5f * sizes[axis] - widenedH;
            cells *= n;
        }
    }

    const float fieldCell = desc.fieldCellSize > 0.0f
        ? desc.fieldCellSize : 1.25f * spacing;
    layout.fieldCell = fieldCell;
    for (int axis = 0; axis < 3; ++axis) {
        const std::uint32_t n = std::clamp<std::uint32_t>(
            static_cast<std::uint32_t>(std::ceil(sizes[axis] / fieldCell)) + 4,
            4, kMaxFluidFieldCells);
        layout.fieldDims[axis] = n;
        layout.fieldOrigin[axis] = -0.5f * sizes[axis] - 2.0f * fieldCell;
    }
    return layout;
}

} // namespace Concord::Fluid
