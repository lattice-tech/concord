#ifndef CONCORD_FLUIDDESC_H
#define CONCORD_FLUIDDESC_H

#include "engine/object/Transform.h"
#include "math/Vector3.h"

#include <cstdint>

namespace Concord::Fluid {

/**
 * Hard caps for one fluid body's GPU resources. The particle cap keeps every
 * per-particle buffer under a few MB; the grid cap keeps the single-workgroup
 * prefix-sum in the neighbor search valid (see cs_fluid_grid_scan.sc).
 */
inline constexpr std::uint32_t kMaxFluidParticles = 65536;
inline constexpr std::uint32_t kMaxFluidPoints = 131072;
inline constexpr std::uint32_t kMaxFluidGridCells = 262144;
inline constexpr std::uint32_t kMaxFluidFieldCells = 96;

/**
 * @brief Authored description of one DFSPH fluid body (a tank of water).
 *
 * The tank is an axis-aligned box in the node's local space, centred on the
 * node origin, with inner extents `tankSize`. Boundary walls are sampled with
 * static particles (Akinci et al. 2012), so the free surface stays volume
 * conserving right up to the wall instead of dipping from kernel truncation.
 *
 * The solver is DFSPH (Bender & Koschier): every substep runs a divergence
 * -free velocity pass *and* a constant-density pass — never one without the
 * other — which is what keeps the fluid incompressible (no compression,
 * no expansion, volume conserved) where plain WCSPH would drift.
 *
 * The initial fill is an axis-aligned sub-box of the tank given as fractions
 * of the inner extents, so a "dam break" column is `fillMax = {0.5, 0.9, 0.9}`
 * against the left wall. Fractions are ordered per axis (min < max).
 */
struct FluidTankDesc {
    /** Placement of the tank's centre; the tank box is axis-aligned locally. */
    Transform transform{};

    /** Inner extents of the tank box in world units, per axis (> 0). */
    Vector3 tankSize{1.2f, 0.9f, 0.9f};

    /** Particle spacing in world units; smaller is heavier and more detailed. */
    float particleSpacing = 0.03f;

    /** Initial fill box, per-axis fractions of `tankSize` (min corner). */
    Vector3 fillMin{0.05f, 0.05f, 0.05f};

    /** Initial fill box, per-axis fractions of `tankSize` (max corner). */
    Vector3 fillMax{0.5f, 0.85f, 0.95f};

    /**
     * Optional static inner obstacle (e.g. a land mass rising through the
     * water), an axis-aligned box in local space sampled with the same
     * two-layer static boundary particles as the tank walls (Akinci et al.
     * 2012). Fluid particles are pushed out of it every integrate, so the
     * visible body of water wraps the obstacle instead of intersecting it.
     * Fill-lattice cells that land inside the box are mirrored to the opposite
     * side of it at seed time, keeping the particle count exact.
     */
    bool obstacleEnabled = false;

    /** Obstacle box corners (local); must lie inside the tank when enabled. */
    Vector3 obstacleMin{-0.25f, -0.45f, -0.25f};
    Vector3 obstacleMax{0.25f, 0.45f, 0.25f};

    /** Reference density in kg/m^3; only ratios matter to the solver. */
    float restDensity = 1000.0f;

    /** Substeps per rendered frame; more is stiffer and slower. */
    std::uint32_t substeps = 2;

    /** Fixed DFSPH iteration counts (both solvers always run). */
    std::uint32_t densityIterations = 6;
    std::uint32_t divergenceIterations = 4;

    /** XSPH viscosity smoothing strength in [0, 1]. */
    float viscosity = 0.08f;

    /** Multiplier on the scene gravity applied to the fluid. */
    float gravityScale = 1.0f;

    /**
     * Surface reconstruction scalar-field cell size in world units; 0 picks
     * 1.25 * particleSpacing. Smaller cells sharpen the surface but cost
     * field/MC passes and shrink the refraction march step.
     */
    float fieldCellSize = 0.0f;

    /** Temporal smoothing blend for the density field, in [0, 1): the weight
     *  of the previous frame's field. Kills frame-to-frame surface jitter. */
    float fieldSmoothing = 0.55f;

    /** Iso level of the normalized density field that defines the surface. */
    float isoLevel = 0.5f;

    /** Index of refraction of the liquid; water is 1.33. */
    float ior = 1.33f;

    /** Beer-Lambert extinction per world unit of travel inside the liquid. */
    float absorption = 0.9f;

    /** Body colour tint, packed 0xRRGGBBAA (sRGB). */
    std::uint32_t waterColor = 0x2E6F8Effu;

    /** Sun glint intensity multiplier; 0 disables the specular sparkle. */
    float glintStrength = 1.0f;

    /** Microfacet roughness of the reconstructed surface. */
    float roughness = 0.05f;

    /** Whether the simulation advances; a paused body still renders. */
    bool paused = false;
};

/**
 * @brief Fully resolved, clamped and counted form of a FluidTankDesc.
 *
 * ComputeLayout derives every count both the CPU snapshot and the GPU pass
 * setup need, so the two never disagree about buffer sizes or lattice
 * placement. All values are finite and within the hard caps.
 */
struct FluidLayout {
    std::uint32_t particleCount = 0;   ///< Fluid particles actually simulated.
    std::uint32_t boundaryCount = 0;   ///< Static wall particles appended after.
    std::uint32_t obstacleBoundaryCount = 0;  ///< Of boundaryCount, the obstacle shell tail.
    std::uint32_t fluidLattice[3]{1, 1, 1};  ///< Fill lattice counts per axis.
    float effectiveSpacing = 0.03f;    ///< Spacing after the particle cap.
    float kernelRadius = 0.075f;       ///< SPH support radius h.
    std::uint32_t gridDims[3]{1, 1, 1};      ///< Neighbor-search grid cells.
    float gridOrigin[3]{0.0f, 0.0f, 0.0f};   ///< Local min corner of the grid.
    std::uint32_t fieldDims[3]{1, 1, 1};     ///< Scalar-field cells per axis.
    float fieldOrigin[3]{0.0f, 0.0f, 0.0f};  ///< Local min corner of field.
    float fieldCell = 0.04f;                 ///< Field cell size used.
};

/**
 * Clamps `desc` to finite, in-range values and derives its FluidLayout.
 * Invalid sizes fall back to the defaults rather than producing a zero or
 * overflowing particle count; a fill box that would exceed the particle cap
 * is preserved by widening the effective spacing instead of truncating.
 */
FluidLayout ComputeFluidLayout(const FluidTankDesc& desc);

/** Returns a copy of `desc` with every field clamped to a usable value. */
FluidTankDesc NormalizeFluidDesc(const FluidTankDesc& desc);

} // namespace Concord::Fluid

#endif // CONCORD_FLUIDDESC_H
