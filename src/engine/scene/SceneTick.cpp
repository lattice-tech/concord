#include "engine/scene/Scene.h"

#include "engine/environment/RenderEnvironment.h"
#include "engine/object/Camera.h"
#include "engine/render/frame/ShadowCasterLight.h"
#include "engine/render/frame/WorldSnapshot.h"
#include "engine/render/shadow/ShadowConfig.h"
#include "engine/spatial/Frustum.h"
#include "engine/spatial/InstanceCuller.h"
#include "engine/spatial/ShadowCasterCuller.h"
#include "engine/debug/Logger.h"

#include <chrono>
#include <cstring>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Concord {
namespace {

void LogSlowSceneSection(const char* name,
                         std::chrono::steady_clock::time_point start) noexcept
{
    const float elapsedMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    if (elapsedMs > 100.0f) {
        Debug::Logger::Warn("Scene", "%s took %.2f ms", name, elapsedMs);
    }
}

bool TickCurrent(const std::shared_ptr<SceneGraphState>& state,
                 std::uint64_t generation)
{
    return state->alive.load(std::memory_order_acquire)
        && state->active.load(std::memory_order_acquire)
        && state->activationGeneration.load(std::memory_order_acquire) == generation;
}

class TraversalScope {
public:
    explicit TraversalScope(std::shared_ptr<SceneGraphState> state)
        : m_state(std::move(state))
    {
        m_state->traversalDepth.fetch_add(1, std::memory_order_acq_rel);
    }

    ~TraversalScope()
    {
        if (m_active) {
            m_state->traversalDepth.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void Release()
    {
        m_state->traversalDepth.fetch_sub(1, std::memory_order_acq_rel);
        m_active = false;
    }

private:
    std::shared_ptr<SceneGraphState> m_state;
    bool m_active = true;
};

void FillEnvironmentSnapshot(const EnvironmentSettings& settings,
                             WorldSnapshot& snapshot)
{
    snapshot.environment = ResolveRenderEnvironment(settings);
    snapshot.hasEnvironment = true;
    snapshot.sky = snapshot.environment.Sky();
    const VolumetricCloudSettings& clouds = snapshot.environment.Clouds();
    const RenderCloudAnimation& animation = snapshot.environment.CloudAnimation();
    snapshot.sky.clouds = clouds.enabled;
    snapshot.sky.cloudCoverage = clouds.coverage;
    snapshot.sky.cloudDensity = clouds.density;
    snapshot.sky.cloudBaseHeight = clouds.baseAltitudeKm * 1000.0f;
    snapshot.sky.cloudThickness = clouds.thicknessKm * 1000.0f;
    snapshot.sky.cloudOffsetEast = animation.offsetEastKm * 1000.0f;
    snapshot.sky.cloudOffsetNorth = animation.offsetNorthKm * 1000.0f;
    snapshot.sky.cloudScale = clouds.shapeScaleKm * 1000.0f;
    snapshot.sky.cloudErosion = clouds.erosion;
    snapshot.sky.cloudDetail = clouds.detail;
    snapshot.sky.cloudSilverLining = clouds.silverLining;
    snapshot.sky.cloudLitColor = clouds.litColor;
    snapshot.sky.cloudShadowColor = clouds.shadowColor;
    snapshot.sky.cloudFireColor = clouds.fireColor;
    snapshot.sky.cloudFireEmission = clouds.colorMode == CloudColorMode::Fire
        ? clouds.fireEmission : 0.0f;
    const HeightFogSettings& fog = snapshot.environment.HeightFog();
    snapshot.sky.volumetricFog = fog.enabled;
    snapshot.sky.fogDensity = fog.density;
    snapshot.sky.fogBaseHeight = fog.baseHeight;
    snapshot.sky.fogHeightFalloff = fog.heightFalloff;
    snapshot.sky.fogColor = fog.inscatteringColor;
}

/**
 * Live framebuffer aspect for @p window, or 16:9 until the size is known.
 *
 * Culling with the real aspect keeps the extraction frustum equal to the one the
 * render thread projects with; the 16:9 fallback stays slightly conservative on
 * wider views so nothing pops in on the first frames of a window.
 */
float WindowAspect(const EngineLoop& loop, EngineLoop::WindowId window) noexcept
{
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (loop.WindowPixelSize(window, pixelWidth, pixelHeight) && pixelWidth > 0
        && pixelHeight > 0) {
        return static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight);
    }
    return 16.0f / 9.0f;
}

/**
 * Collects the off-screen draws whose shadow still lands inside @p frustum.
 *
 * The extrusion matches ShadowConfig::casterExtrusionWorld, the distance the
 * shadow cascades extrude their caster range: geometry farther up-light than
 * that is outside the shadow map's depth range anyway.
 */
std::uint32_t SelectSnapshotShadowCasters(const std::vector<RenderInstance>& authored,
                                          const std::vector<std::size_t>& culledIndices,
                                          const Spatial::Frustum& frustum,
                                          const std::vector<RenderLight>& lights,
                                          std::vector<RenderInstance>& out)
{
    out.clear();
    const int caster = FindShadowCastingLight(
        lights.data(), static_cast<std::uint32_t>(lights.size()));
    if (caster < 0 || culledIndices.empty()) {
        return 0;
    }
    return Spatial::SelectShadowCasters(authored, culledIndices, frustum,
                                        lights[static_cast<std::size_t>(caster)].direction,
                                        ShadowConfig{}.casterExtrusionWorld, out);
}

void RecordTaskStats(const TaskGraphStats& stats, WorldSnapshot& snapshot)
{
    snapshot.taskNodeCount += static_cast<std::uint32_t>(stats.nodes.size());
    snapshot.taskGraphMs += stats.wallMs;
    for (const TaskNodeStats& node : stats.nodes) {
        if (node.cpuMs <= snapshot.slowestTaskMs) {
            continue;
        }
        snapshot.slowestTaskMs = node.cpuMs;
        snapshot.slowestTask.fill('\0');
        std::strncpy(snapshot.slowestTask.data(), node.name.c_str(),
                     snapshot.slowestTask.size() - 1);
    }
}

} // namespace

void Scene::Tick(float deltaTime, std::uint64_t activationGeneration)
{
    using Clock = std::chrono::steady_clock;
    const std::shared_ptr<SceneGraphState> state = m_graphState;
    if (!TickCurrent(state, activationGeneration)) {
        return;
    }
    TraversalScope traversal(state);
    const std::vector<Object::ObjectHandle> callbackHandles = SnapshotHandles();

    const auto callbacksStart = Clock::now();
    for (Object::ObjectHandle handle : callbackHandles) {
        {
            std::lock_guard<std::recursive_mutex> lock(state->mutex);
            if (!TickCurrent(state, activationGeneration)) {
                return;
            }
            Object::Node* node = ResolveLiveLocked(handle);
            if (node == nullptr) {
                continue;
            }
                if (!node->m_started) {
                    node->m_started = true;
                    const std::function<void()> onStart = node->m_onStart;
                    if (onStart) {
                        const auto nodeStart = Clock::now();
                        onStart();
                        const float nodeStartMs = std::chrono::duration<float, std::milli>(
                            Clock::now() - nodeStart).count();
                        if (nodeStartMs > 20.0f) {
                            Debug::Logger::Warn(
                                "Scene", "OnStart %s took %.2f ms",
                                typeid(*node).name(), nodeStartMs);
                        }
                    }
                }
            if (!TickCurrent(state, activationGeneration)) {
                return;
            }
            node = ResolveLiveLocked(handle);
            if (node == nullptr) {
                continue;
            }
            const std::function<void(float)> onUpdate = node->m_onUpdate;
            if (onUpdate) {
                const auto nodeUpdateStart = Clock::now();
                onUpdate(deltaTime);
                const float nodeUpdateMs = std::chrono::duration<float, std::milli>(
                    Clock::now() - nodeUpdateStart).count();
                if (nodeUpdateMs > 20.0f) {
                    Debug::Logger::Warn(
                        "Scene", "OnUpdate %s took %.2f ms",
                        typeid(*node).name(), nodeUpdateMs);
                }
            }
            if (!TickCurrent(state, activationGeneration)) {
                return;
            }
        }
    }
    LogSlowSceneSection("SceneTick.Callbacks", callbacksStart);
    const float callbackMs = std::chrono::duration<float, std::milli>(
        Clock::now() - callbacksStart).count();

    std::shared_ptr<EngineLoop> loop;
    EngineLoop::WindowId window = EngineLoop::kInvalidWindowId;
    EnvironmentSettings environment;
    Object::ObjectHandle cameraHandle;
    {
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        if (!TickCurrent(state, activationGeneration)) {
            return;
        }
        loop = m_loop.lock();
        window = m_window;
        m_environmentSettings.time = AdvanceEnvironmentTime(m_environmentSettings.time,
                                                             deltaTime);
        environment = m_environmentSettings;
        cameraHandle = m_activeCamera;
    }
    if (!loop) {
        traversal.Release();
        CommitDespawns();
        return;
    }

    const bool hasWindow = window != EngineLoop::kInvalidWindowId
        && loop->IsWindowOpen(window);
    if (!hasWindow && window != EngineLoop::kInvalidWindowId) {
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        if (m_window == window) {
            m_window = EngineLoop::kInvalidWindowId;
        }
    }

    const std::vector<Object::ObjectHandle> handles = SnapshotHandles();
    WorldSnapshot snapshot;
    snapshot.generation = ++m_snapshotGeneration;
    snapshot.simulationFrame = loop->SimulationGeneration();
    snapshot.nodeCount = static_cast<std::uint32_t>(handles.size());
    snapshot.nodeCallbackMs = callbackMs;
    FillEnvironmentSnapshot(environment, snapshot);
    snapshot.hasSky = hasWindow;

    m_ecsCommands.Commit(m_ecsWorld);
    TaskGraph ecsGraph;
    const auto ecsStart = Clock::now();
    {
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        ecsGraph = m_ecsSystems.Build(m_ecsWorld, deltaTime);
    }
    RecordTaskStats(loop->RunTaskGraph(std::move(ecsGraph)), snapshot);
    LogSlowSceneSection("SceneTick.Ecs", ecsStart);
    if (!TickCurrent(state, activationGeneration)) {
        return;
    }

    const auto cameraStart = Clock::now();
    {
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        if (auto* camera = dynamic_cast<Object::Camera*>(ResolveLiveLocked(cameraHandle))) {
            camera->AdvanceEffects(deltaTime);
            if (hasWindow) {
                snapshot.hasCamera = camera->GetCameraView(snapshot.camera);
            }
        }
    }
    LogSlowSceneSection("SceneTick.Camera", cameraStart);

    const auto extractionStart = Clock::now();
    if (hasWindow) {
        std::vector<RenderInstance> authored;
        float collectRenderMs = 0.0f;
        float collectEmittersMs = 0.0f;
        float collectLightsMs = 0.0f;
        float collectSmokeMs = 0.0f;
        float collectWaterMs = 0.0f;
        for (Object::ObjectHandle handle : handles) {
            std::lock_guard<std::recursive_mutex> lock(state->mutex);
            Object::Node* node = ResolveLiveLocked(handle);
            if (node == nullptr) {
                continue;
            }
            auto section = Clock::now();
            node->CollectRender(authored);
            collectRenderMs += std::chrono::duration<float, std::milli>(
                Clock::now() - section).count();
            section = Clock::now();
            node->CollectParticleEmitters(snapshot.particleEmitters);
            collectEmittersMs += std::chrono::duration<float, std::milli>(
                Clock::now() - section).count();
            section = Clock::now();
            node->CollectLights(snapshot.lights);
            collectLightsMs += std::chrono::duration<float, std::milli>(
                Clock::now() - section).count();
            section = Clock::now();
            node->CollectSmokeVolumes(snapshot.smokeVolumes);
            collectSmokeMs += std::chrono::duration<float, std::milli>(
                Clock::now() - section).count();
            section = Clock::now();
            node->CollectWaterSurfaces(snapshot.waterSurfaces);
            node->CollectFluids(snapshot.fluids);
            collectWaterMs += std::chrono::duration<float, std::milli>(
                Clock::now() - section).count();
        }
        if (collectRenderMs > 100.0f || collectEmittersMs > 100.0f
            || collectLightsMs > 100.0f || collectSmokeMs > 100.0f
            || collectWaterMs > 100.0f) {
            Debug::Logger::Warn(
                "Scene",
                "Extraction breakdown render=%.2f emitters=%.2f lights=%.2f smoke=%.2f water=%.2f ms",
                collectRenderMs, collectEmittersMs, collectLightsMs,
                collectSmokeMs, collectWaterMs);
        }
        snapshot.visibilityAuthored = static_cast<std::uint32_t>(authored.size());

        const auto visibilityStart = Clock::now();
        if (snapshot.hasCamera && !authored.empty()) {
            const Spatial::Frustum frustum = Spatial::BuildFrustum(
                snapshot.camera, WindowAspect(*loop, window));
            std::vector<RenderInstance> submitted;
            std::vector<std::size_t> culledIndices;
            Spatial::VisibilityStats visibility;
            if (authored.size() >= Spatial::kInstanceCullParallelThreshold) {
                TaskGraphStats cullStats;
                visibility = Spatial::CullInstancesParallel(
                    authored, frustum, Spatial::kInstanceCullChunkSize,
                    [&loop](TaskGraph graph) {
                        return loop->RunTaskGraph(std::move(graph));
                    },
                    submitted, cullStats, &culledIndices);
                RecordTaskStats(cullStats, snapshot);
            } else {
                visibility = Spatial::CullInstances(authored, frustum, submitted,
                                                    &culledIndices);
            }
            snapshot.visibilityNodesVisited = visibility.nodesVisited;
            snapshot.visibilitySubmitted = visibility.submitted;
            snapshot.visibilityCulled = visibility.culled;
            // Shadow casters are deliberately not limited to the visible set: a
            // prop behind or above the camera still darkens the ground in front
            // of it, so off-screen draws whose shadow reaches the view are kept
            // for the depth-only shadow pass.
            snapshot.visibilityShadowCasters = SelectSnapshotShadowCasters(
                authored, culledIndices, frustum, snapshot.lights, snapshot.shadowCasters);
            snapshot.instances = std::move(submitted);
        } else {
            snapshot.visibilitySubmitted = snapshot.visibilityAuthored;
            snapshot.visibilityCulled = 0;
            snapshot.visibilityNodesVisited = 0;
            snapshot.instances = std::move(authored);
        }
        snapshot.visibilityMs = std::chrono::duration<float, std::milli>(
            Clock::now() - visibilityStart).count();

        // Order matters: RebindBonePalettes walks instances then shadowCasters
        // against one running offset into boneMatrices.
        const auto copyPalettes = [&snapshot](const std::vector<RenderInstance>& list) {
            for (const RenderInstance& instance : list) {
                if (instance.bonePalette == nullptr || instance.boneCount == 0) {
                    continue;
                }
                const std::size_t count = static_cast<std::size_t>(instance.boneCount) * 16u;
                snapshot.boneMatrices.insert(snapshot.boneMatrices.end(),
                                             instance.bonePalette,
                                             instance.bonePalette + count);
            }
        };
        copyPalettes(snapshot.instances);
        copyPalettes(snapshot.shadowCasters);
    }
    snapshot.extractionMs = std::chrono::duration<float, std::milli>(
        Clock::now() - extractionStart).count();
    LogSlowSceneSection("SceneTick.Extraction", extractionStart);
    const auto collisionStart = Clock::now();
    const TaskGraphStats collisionStats = ResolveCollisions(
        handles, snapshot, loop, activationGeneration);
    LogSlowSceneSection("SceneTick.Collision", collisionStart);
    if (!TickCurrent(state, activationGeneration)) {
        return;
    }
    RecordTaskStats(collisionStats, snapshot);
    snapshot.RebindBonePalettes();

    if (hasWindow) {
        const auto publishStart = Clock::now();
        std::lock_guard<std::recursive_mutex> lock(state->mutex);
        if (m_window == window && TickCurrent(state, activationGeneration)) {
            loop->PublishWorldSnapshot(window, std::move(snapshot));
        }
        LogSlowSceneSection("SceneTick.Publish", publishStart);
    }
    traversal.Release();
    CommitDespawns();
}

} // namespace Concord
