#ifndef CONCORD_RENDERFLUID_H
#define CONCORD_RENDERFLUID_H

#include <cstdint>

namespace Concord {

/** Maximum number of DFSPH fluid bodies the fluid pass draws for one view. */
inline constexpr std::uint32_t kMaxRenderFluids = 4;

/**
 * @brief The render thread's flat, resolved form of one DFSPH fluid body.
 *
 * Object::FluidWater owns the authored description and the consumption of the
 * frame delta; this is the backend-agnostic POD the Scene gathers each frame.
 * Like RenderInstance it carries no ownership — the `fluidKey` is only an
 * opaque identity the backend uses to find the persistent GPU state of the
 * body across frames (same convention as RenderParticleEmitter::emitterKey).
 *
 * All simulation-space values are **local**: the GPU solver works in the
 * node's local frame (tank AABB centred on the origin) and the generated
 * surface mesh is emitted in local space, then transformed by `world` at
 * draw time. `worldInverse` maps world rays back into simulation space for
 * the refraction march.
 */
struct RenderFluid {
    /** Opaque identity of the authoring node; never dereferenced. */
    std::uintptr_t fluidKey = 0;

    /** Column-major local→world matrix of the tank centre. */
    float world[16]{1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};

    /** Column-major world→local matrix (inverse of `world`). */
    float worldInverse[16]{1.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 1.0f};

    /** Inner tank extents (local) and particle/solver constants. */
    float tankSize[3]{1.2f, 0.9f, 0.9f};
    float spacing = 0.03f;
    float kernelRadius = 0.06f;
    float restDensity = 1000.0f;

    /** World-space gravity for this frame, and the consumed frame delta. */
    float gravity[3]{0.0f, -9.81f, 0.0f};
    float deltaTime = 0.0f;

    std::uint32_t particleCount = 0;
    std::uint32_t boundaryCount = 0;
    std::uint32_t substeps = 2;
    std::uint32_t densityIterations = 6;
    std::uint32_t divergenceIterations = 4;
    float viscosity = 0.08f;

    /** Neighbor-search grid (local space, cell = kernelRadius). */
    std::uint32_t gridDims[3]{1, 1, 1};
    float gridOrigin[3]{0.0f, 0.0f, 0.0f};

    /** Scalar field for surface reconstruction and the refraction march. */
    std::uint32_t fieldDims[3]{1, 1, 1};
    float fieldOrigin[3]{0.0f, 0.0f, 0.0f};
    float fieldCell = 0.04f;
    float fieldSmoothing = 0.55f;
    float isoLevel = 0.5f;

    /** Optics: dual-interface Snell refraction, absorption, glint. */
    float ior = 1.33f;
    float absorption = 0.9f;
    std::uint32_t waterColor = 0x2E6F8Effu;
    float glintStrength = 1.0f;
    float roughness = 0.05f;

    /** Fill lattice counts and local fill-box min corner, for (re)seeding. */
    std::uint32_t fluidLattice[3]{1, 1, 1};
    float fillOrigin[3]{0.0f, 0.0f, 0.0f};

    /**
     * Optional static inner obstacle (local AABB) sampled with boundary
     * particles; `obstacleBoundaryCount` is the tail segment of
     * `boundaryCount` that belongs to the obstacle shell (0 = disabled).
     */
    float obstacleMin[3]{0.0f, 0.0f, 0.0f};
    float obstacleMax[3]{0.0f, 0.0f, 0.0f};
    std::uint32_t obstacleBoundaryCount = 0;

    /** One-frame pulse: reseed particles to the initial fill this frame. */
    bool reset = false;

    /** Paused bodies render but do not advance. */
    bool paused = false;
};

} // namespace Concord

#endif // CONCORD_RENDERFLUID_H
