#ifndef CONCORD_WORLDSNAPSHOT_H
#define CONCORD_WORLDSNAPSHOT_H

#include "engine/render/frame/CameraView.h"
#include "engine/environment/RenderEnvironment.h"
#include "engine/render/frame/RenderFluid.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/RenderParticleEmitter.h"
#include "engine/render/frame/RenderSmokeVolume.h"
#include "engine/render/frame/RenderWaterSurface.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <cstdint>
#include <array>
#include <vector>

namespace Concord {

/**
 * @brief Immutable, self-contained render view of one simulated world frame.
 *
 * The render thread consumes a published snapshot without touching Scene or
 * Node storage. `boneMatrices` owns every skin palette referenced by instances.
 */
struct WorldSnapshot {
    std::uint64_t generation = 0;
    std::uint64_t simulationFrame = 0;
    std::vector<RenderInstance> instances;
    /**
     * Opaque draws the camera frustum rejected whose shadow can still fall
     * inside the view. Consumed only by the shadow depth pass
     * (IRenderBackend::SubmitShadowCaster), never drawn as visible geometry.
     */
    std::vector<RenderInstance> shadowCasters;
    std::vector<RenderParticleEmitter> particleEmitters;
    std::vector<RenderLight> lights;
    std::vector<RenderSmokeVolume> smokeVolumes;
    std::vector<RenderWaterSurface> waterSurfaces;
    std::vector<RenderFluid> fluids;
    std::vector<float> boneMatrices;
    CameraView camera{};
    SkyEnvironment sky{};
    /** Resolved sky, cloud, fog, and animation data for this frame. */
    RenderEnvironment environment{};
    bool hasCamera = false;
    bool hasSky = false;
    /** Whether `environment` was explicitly authored for this snapshot. */
    bool hasEnvironment = false;
    std::uint32_t nodeCount = 0;
    std::uint32_t colliderCount = 0;
    /** Mesh instances collected before frustum culling. */
    std::uint32_t visibilityAuthored = 0;
    /** Mesh instances rejected by the camera frustum. */
    std::uint32_t visibilityCulled = 0;
    /** Mesh instances surviving cull (submitted to the render thread). */
    std::uint32_t visibilitySubmitted = 0;
    /** BVH nodes visited during the visibility tree walk. */
    std::uint32_t visibilityNodesVisited = 0;
    /** Off-screen draws kept for the shadow pass (subset of visibilityCulled). */
    std::uint32_t visibilityShadowCasters = 0;
    float nodeCallbackMs = 0.0f;
    float extractionMs = 0.0f;
    float visibilityMs = 0.0f;
    float collisionMs = 0.0f;
    float taskGraphMs = 0.0f;
    std::uint32_t taskNodeCount = 0;
    float slowestTaskMs = 0.0f;
    std::array<char, 64> slowestTask{};

    /** Rebinds transient instance pointers after a snapshot move or buffer swap. */
    void RebindBonePalettes() noexcept;
};

} // namespace Concord

#endif // CONCORD_WORLDSNAPSHOT_H
