#ifndef CONCORD_FRAMESTATS_H
#define CONCORD_FRAMESTATS_H

#include <cstdint>
#include <array>

namespace Concord {

/**
 * @brief Measured CPU time for one EngineLoop frame, in milliseconds.
 *
 * Filled on the render thread after each frame; readable from any thread via
 * EngineLoop / Game accessors. Used for Milestone 6 measured optimization —
 * do not invent "faster" rewrites without consulting these numbers.
 */
struct FrameStats {
    /** Wall time for the whole frame (input through present). */
    float totalMs = 0.0f;
    /** SDL pump, InputState, window close/resize normalization. */
    float inputMs = 0.0f;
    /** InputActions sample + EventBus dispatch. */
    float eventsMs = 0.0f;
    /** EventBus dispatch measured on the simulation coordinator. */
    float eventDispatchMs = 0.0f;
    /** Game OnUpdate / SubscribeUpdate + Scene tick. */
    float updateMs = 0.0f;
    /** Per-window mesh submit + RenderView (CPU side). */
    float submitMs = 0.0f;
    /** backend->Frame() (CPU submit / present handoff). */
    float presentMs = 0.0f;
    /** Game/system callbacks executed by the simulation task graph. */
    float systemMs = 0.0f;
    /** Scene simulation and extraction on workers. */
    float sceneMs = 0.0f;
    /** Scene-phase callbacks measured on the simulation coordinator. */
    float sceneUpdateMs = 0.0f;
    /** Wall time of the latest completed simulation graphs. */
    float taskGraphMs = 0.0f;
    /** Latest GPU frame duration reported by bgfx, if gpuStatsValid. */
    float gpuFrameMs = 0.0f;
    /** Oldest-to-newest request queue delay observed in this frame. */
    float requestLatencyMs = 0.0f;
    /** CPU time spent executing control and resource requests this frame. */
    float requestProcessingMs = 0.0f;
    /** Longest queue delay among requests executed this frame. */
    float requestQueueLatencyMs = 0.0f;
    /** Number of measured task-graph nodes. */
    std::uint32_t taskNodeCount = 0;
    /** Number of update systems/callbacks executed. */
    std::uint32_t systemCount = 0;
    /** Number of Scene systems executed. */
    std::uint32_t sceneCount = 0;
    /** Draw instances in the newest consumed world snapshot. */
    std::uint32_t renderInstanceCount = 0;
    /** True when gpuFrameMs represents a valid backend sample. */
    bool gpuStatsValid = false;
    /** Number of colliders evaluated by the latest world snapshot. */
    std::uint32_t colliderCount = 0;
    /** Authored mesh instances before frustum cull. */
    std::uint32_t visibilityAuthored = 0;
    /** Mesh instances culled by the camera frustum. */
    std::uint32_t visibilityCulled = 0;
    /** Mesh instances submitted after cull. */
    std::uint32_t visibilitySubmitted = 0;
    /** Spatial tree nodes visited during visibility cull. */
    std::uint32_t visibilityNodesVisited = 0;
    /** Off-screen draws kept for the shadow pass (subset of visibilityCulled). */
    std::uint32_t visibilityShadowCasters = 0;
    /** CPU ms spent on frustum visibility cull. */
    float visibilityMs = 0.0f;
    /** Number of GPU views/passes measured by the backend. */
    std::uint32_t gpuPassCount = 0;
    /** Slowest measured GPU pass duration. */
    float slowestGpuPassMs = 0.0f;
    /** Allocation-free backend pass label. */
    std::array<char, 64> slowestGpuPass{};
    /** Nodes visited by the newest completed simulation snapshot. */
    std::uint32_t nodeCount = 0;
    /** Serial compatibility callback time for Scene nodes. */
    float nodeCallbackMs = 0.0f;
    /** Render/light extraction task time. */
    float extractionMs = 0.0f;
    /** Render collection/extraction time from the consumed world snapshot. */
    float renderCollectMs = 0.0f;
    /** Collision broadphase task time. */
    float collisionMs = 0.0f;
    /** Slowest CPU task-graph node. */
    float slowestTaskMs = 0.0f;
    std::array<char, 64> slowestTask{};
    /** Slowest game or scene callback in the latest simulation run. */
    float slowestCallbackMs = 0.0f;
    /** Per-callback frame budget used for overrun classification. */
    float callbackBudgetMs = 16.6667f;
    /** Number of callbacks exceeding callbackBudgetMs. */
    std::uint32_t callbackBudgetOverruns = 0;
    /** Simulation generation consumed by rendering. */
    std::uint64_t simulationFrame = 0;
    /** World snapshot generation consumed by rendering. */
    std::uint64_t simulationGeneration = 0;
    /** bgfx draw submissions in the latest backend sample. */
    std::uint32_t drawCalls = 0;
    /** bgfx compute submissions in the latest backend sample. */
    std::uint32_t computeCalls = 0;
    /** bgfx GPU frame number represented by the latest backend sample. */
    std::uint32_t gpuFrame = 0;
    /** Number of textures currently tracked by bgfx. */
    std::uint32_t textureCount = 0;
    std::uint64_t textureMemoryBytes = 0;
    std::uint64_t renderTargetMemoryBytes = 0;
    std::uint32_t transientVertexBytes = 0;
    std::uint32_t transientIndexBytes = 0;
    /** Frames completed when this sample was taken. */
    std::uint64_t frameCount = 0;
};

} // namespace Concord

#endif // CONCORD_FRAMESTATS_H
