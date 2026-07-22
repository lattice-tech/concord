#include "engine/scene/Scene.h"

#include "engine/app/Game.h"
#include "engine/collision/SweepAndPrune.h"
#include "engine/collision/query/RayIntersection.h"
#include "engine/environment/RenderEnvironment.h"
#include "engine/object/Camera.h"
#include "engine/object/Collider.h"
#include "engine/render/frame/CameraView.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/WorldSnapshot.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/render/mesh/MeshHandle.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Concord {

namespace {

thread_local std::uint32_t g_sceneSystemDepth = 0;

class SceneSystemScope {
public:
    SceneSystemScope() { ++g_sceneSystemDepth; }
    ~SceneSystemScope() { --g_sceneSystemDepth; }
};

void EnforceCoordinatorTeardown()
{
    if (g_sceneSystemDepth != 0) {
        std::terminate();
    }
}

bool IsTickCurrent(const std::shared_ptr<SceneGraphState>& graphState,
                   std::uint64_t activationGeneration)
{
    return graphState->alive.load(std::memory_order_acquire)
        && graphState->active.load(std::memory_order_acquire)
        && graphState->activationGeneration.load(std::memory_order_acquire)
            == activationGeneration;
}

} // namespace

Scene::Scene()
    : m_graphState(std::make_shared<SceneGraphState>())
{
}

Scene::~Scene()
{
    EnforceCoordinatorTeardown();
    const std::shared_ptr<SceneGraphState> graphState = m_graphState;
    graphState->alive.store(false, std::memory_order_release);
    Deactivate();
    if (m_game != nullptr) {
        // Tell the Game we are gone so it never dereferences a dangling scene,
        // regardless of which of the two is destroyed first.
        m_game->ForgetScene(this);
        m_game = nullptr;
    }
}

bool Scene::Bind(Game* game, std::shared_ptr<EngineLoop> loop, EngineLoop::WindowId window)
{
    EnforceCoordinatorTeardown();
    if (game == nullptr || !loop) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> graphLock(m_graphState->mutex);
    if (!m_graphState->alive.load(std::memory_order_acquire)
        || (m_game != nullptr && m_game != game)) {
        return false;
    }
    if (m_game == game && m_graphState->active.load(std::memory_order_acquire)) {
        return true;
    }

    const std::uint64_t activationGeneration =
        m_graphState->activationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_game = game;
    m_loop = loop;
    m_window = window;

    // One tick registration drives the whole scene: it ticks nodes, gathers
    // the frame's draw list, and pushes the active camera's view.
    m_updateId = loop->OnSceneUpdate(
        [this, graphState = m_graphState, activationGeneration](float deltaTime) {
            if (IsTickCurrent(graphState, activationGeneration)) {
                Tick(deltaTime, activationGeneration);
            }
        });
    if (m_updateId == EngineLoop::kInvalidUpdateId) {
        m_graphState->active.store(false, std::memory_order_release);
        m_graphState->activationGeneration.fetch_add(1, std::memory_order_acq_rel);
        m_game = nullptr;
        m_loop.reset();
        m_window = EngineLoop::kInvalidWindowId;
        return false;
    }
    return true;
}

void Scene::ActivateBinding()
{
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> graphLock(m_graphState->mutex);
        if (m_updateId != EngineLoop::kInvalidUpdateId
            && m_graphState->alive.load(std::memory_order_acquire)) {
            m_graphState->active.store(true, std::memory_order_release);
            loop = m_loop.lock();
        }
    }
    if (loop) {
        loop->RequestSimulation();
    }
}

void Scene::RebindWindow(EngineLoop::WindowId window)
{
    EnforceCoordinatorTeardown();
    std::lock_guard<std::recursive_mutex> graphLock(m_graphState->mutex);
    if (m_window == window) {
        return;
    }

    if (const std::shared_ptr<EngineLoop> loop = m_loop.lock();
        loop && m_window != EngineLoop::kInvalidWindowId) {
        loop->PublishWorldSnapshot(m_window, {});
    }
    m_window = window;
}

void Scene::Unbind()
{
    EnforceCoordinatorTeardown();
    Deactivate();
    std::lock_guard<std::recursive_mutex> graphLock(m_graphState->mutex);
    m_game = nullptr;
}

void Scene::Deactivate()
{
    EnforceCoordinatorTeardown();
    std::shared_ptr<EngineLoop> loop;
    EngineLoop::UpdateId updateId = EngineLoop::kInvalidUpdateId;
    EngineLoop::WindowId window = EngineLoop::kInvalidWindowId;
    {
        std::lock_guard<std::recursive_mutex> graphLock(m_graphState->mutex);
        if (!m_graphState->active.load(std::memory_order_acquire)
            && m_updateId == EngineLoop::kInvalidUpdateId) {
            return;
        }
        loop = m_loop.lock();
        updateId = m_updateId;
        window = m_window;
        m_updateId = EngineLoop::kInvalidUpdateId;
        m_window = EngineLoop::kInvalidWindowId;
        m_graphState->active.store(false, std::memory_order_release);
        m_graphState->activationGeneration.fetch_add(1, std::memory_order_acq_rel);
        m_loop.reset();
        for (const std::unique_ptr<Object::Node>& node : m_nodes) {
            node->m_started = false;
            std::vector<Object::Collider*> colliders;
            node->CollectColliders(colliders);
            for (Object::Collider* collider : colliders) {
                collider->m_overlapping.clear();
            }
        }
    }
    if (loop && updateId != EngineLoop::kInvalidUpdateId) {
        loop->RemoveUpdate(updateId);
    }
    if (loop && window != EngineLoop::kInvalidWindowId) {
        loop->PublishWorldSnapshot(window, {});
    }
}

void Scene::AddNode(std::unique_ptr<Object::Node> node)
{
    Object::Node* raw = node.get();
    raw->m_scene = this;
    raw->m_graphState = m_graphState;
    raw->m_id = m_nextEntityId.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        m_nodes.push_back(std::move(node));
        // Publish the node and its default-camera role atomically to Tick.
        if (raw->IsCamera() && m_activeCamera == nullptr) {
            m_activeCamera = dynamic_cast<Object::Camera*>(raw);
        }
    }
    // No explicit attach needed: Tick discovers new nodes each frame.
}

void Scene::CommitLoadedNodes(const SkyEnvironment& environment,
                              std::vector<std::unique_ptr<Object::Node>> nodes)
{
    const SkyEnvironment sanitizedEnvironment = SanitizeSkyEnvironment(environment);
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);

    if (nodes.size() > m_nodes.max_size() - m_nodes.size()) {
        throw std::length_error("loaded scene exceeds node storage capacity");
    }
    m_nodes.reserve(m_nodes.size() + nodes.size());

    Object::Camera* firstCamera = nullptr;
    for (std::unique_ptr<Object::Node>& node : nodes) {
        Object::Node* raw = node.get();
        raw->m_scene = this;
        raw->m_graphState = m_graphState;
        raw->m_id = m_nextEntityId.fetch_add(1, std::memory_order_relaxed);
        if (firstCamera == nullptr && raw->IsCamera()) {
            firstCamera = dynamic_cast<Object::Camera*>(raw);
        }
        m_nodes.push_back(std::move(node));
    }

    m_environmentSettings.sky = sanitizedEnvironment;
    if (m_activeCamera == nullptr) {
        m_activeCamera = firstCamera;
    }
}

bool Scene::SetActiveCamera(Object::Camera& camera)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    if (camera.m_scene != this) {
        return false;
    }
    m_activeCamera = &camera;
    return true;
}

void Scene::SetSkyEnvironment(const SkyEnvironment& environment)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    m_environmentSettings.sky = SanitizeSkyEnvironment(environment);
}

SkyEnvironment Scene::GetSkyEnvironment() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return m_environmentSettings.sky;
}

void Scene::SetEnvironmentSettings(const EnvironmentSettings& settings)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    m_environmentSettings = SanitizeEnvironmentSettings(settings);
}

EnvironmentSettings Scene::GetEnvironmentSettings() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return m_environmentSettings;
}

Ecs::SystemGraph::SystemId Scene::AddSystem(
    std::string name, Ecs::SystemAccess access,
    std::function<void(Ecs::World&, float)> system)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    if (!system) {
        return 0;
    }
    return m_ecsSystems.Add(
        std::move(name), std::move(access),
        [system = std::move(system)](Ecs::World& world, float deltaTime) {
            SceneSystemScope scope;
            system(world, deltaTime);
        });
}

bool Scene::RemoveSystem(Ecs::SystemGraph::SystemId id)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return m_ecsSystems.Remove(id);
}

bool Scene::RaycastClosest(const Collision::Ray& ray,
                           const Collision::RaycastFilter& filter,
                           Collision::RaycastHit& outHit) const
{
    Collision::RaycastHit closest;
    if (!RaycastInternal(ray, filter, &closest)) {
        return false;
    }
    outHit = closest;
    return true;
}

bool Scene::RaycastAny(const Collision::Ray& ray,
                       const Collision::RaycastFilter& filter) const
{
    return RaycastInternal(ray, filter, nullptr);
}

bool Scene::RaycastInternal(const Collision::Ray& ray,
                            const Collision::RaycastFilter& filter,
                            Collision::RaycastHit* outClosest) const
{
    Collision::Ray normalizedRay;
    if (filter.layerMask == 0
        || !Collision::NormalizeRay(ray, normalizedRay)
        || !Collision::IsValidRaycastRange(filter.minDistance, filter.maxDistance)) {
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    std::vector<Object::Collider*> colliders;
    colliders.reserve(m_nodes.size());
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        node->CollectColliders(colliders);
    }

    bool found = false;
    Collision::RaycastHit closest;
    for (Object::Collider* collider : colliders) {
        const Object::ObjectId colliderId = collider->Id();
        if (colliderId == filter.ignoreColliderId
            || (collider->Layer() & filter.layerMask) == 0) {
            continue;
        }

        Collision::RaycastHit candidate;
        if (!Collision::IntersectRayShape(
                normalizedRay, collider->Shape(), collider->WorldMatrix(),
                filter.minDistance, filter.maxDistance, candidate)) {
            continue;
        }
        candidate.colliderId = colliderId;
        const Object::Node* parent = collider->Parent();
        candidate.objectId = parent != nullptr ? parent->Id() : colliderId;
        if (outClosest == nullptr) {
            return true;
        }

        if (!found || candidate.distance < closest.distance
            || (candidate.distance == closest.distance
                && candidate.colliderId < closest.colliderId)) {
            closest = candidate;
            found = true;
        }
    }
    if (found) {
        *outClosest = closest;
    }
    return found;
}

MeshHandle Scene::AcquireMesh(const MeshData& data)
{
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        loop = m_loop.lock();
    }
    if (loop) {
        return loop->CreateMesh(data);
    }
    return MeshHandle::Invalid();
}

void Scene::ReleaseMesh(MeshHandle mesh)
{
    if (!mesh.IsValid()) {
        return;
    }
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        loop = m_loop.lock();
    }
    if (loop) {
        loop->DestroyMesh(mesh);
    }
}

void Scene::Tick(float deltaTime, std::uint64_t activationGeneration)
{
    using Clock = std::chrono::steady_clock;
    const std::shared_ptr<SceneGraphState> graphState = m_graphState;
    if (!IsTickCurrent(graphState, activationGeneration)) {
        return;
    }
    const std::vector<Object::Node*> nodes = SnapshotNodes();

    const auto callbacksStart = Clock::now();
    for (Object::Node* node : nodes) {
        std::function<void()> onStart;
        std::function<void(float)> onUpdate;
        {
            std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
            if (!IsTickCurrent(graphState, activationGeneration)) {
                return;
            }
            if (!node->m_started) {
                node->m_started = true;
                onStart = node->m_onStart;
            }
            onUpdate = node->m_onUpdate;
        }
        if (onStart) {
            onStart();
            if (!IsTickCurrent(graphState, activationGeneration)) {
                return;
            }
        }
        if (onUpdate) {
            onUpdate(deltaTime);
            if (!IsTickCurrent(graphState, activationGeneration)) {
                return;
            }
        }
    }
    const float callbackMs = std::chrono::duration<float, std::milli>(
        Clock::now() - callbacksStart).count();

    std::shared_ptr<EngineLoop> loop;
    EngineLoop::WindowId window = EngineLoop::kInvalidWindowId;
    EnvironmentSettings environmentSettings;
    Object::Camera* activeCamera = nullptr;
    {
        std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
        if (!IsTickCurrent(graphState, activationGeneration)) {
            return;
        }
        loop = m_loop.lock();
        window = m_window;
        m_environmentSettings.time = AdvanceEnvironmentTime(m_environmentSettings.time, deltaTime);
        environmentSettings = m_environmentSettings;
        activeCamera = m_activeCamera;
    }
    if (!loop) {
        return;
    }

    const bool hasOpenWindow = window != EngineLoop::kInvalidWindowId
        && loop->IsWindowOpen(window);
    if (!hasOpenWindow && window != EngineLoop::kInvalidWindowId) {
        std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
        if (m_window == window) {
            m_window = EngineLoop::kInvalidWindowId;
        }
    }

    WorldSnapshot snapshot;
    snapshot.generation = ++m_snapshotGeneration;
    snapshot.simulationFrame = loop->SimulationGeneration();
    snapshot.nodeCount = static_cast<std::uint32_t>(nodes.size());
    snapshot.nodeCallbackMs = callbackMs;
    snapshot.environment = ResolveRenderEnvironment(environmentSettings);
    snapshot.hasEnvironment = true;
    snapshot.sky = snapshot.environment.Sky();
    const VolumetricCloudSettings& clouds = snapshot.environment.Clouds();
    const RenderCloudAnimation& cloudAnimation = snapshot.environment.CloudAnimation();
    snapshot.sky.clouds = clouds.enabled;
    snapshot.sky.cloudCoverage = clouds.coverage;
    snapshot.sky.cloudDensity = clouds.density;
    snapshot.sky.cloudBaseHeight = clouds.baseAltitudeKm * 1000.0f;
    snapshot.sky.cloudThickness = clouds.thicknessKm * 1000.0f;
    snapshot.sky.cloudOffsetEast = cloudAnimation.offsetEastKm * 1000.0f;
    snapshot.sky.cloudOffsetNorth = cloudAnimation.offsetNorthKm * 1000.0f;
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
    snapshot.hasSky = hasOpenWindow;
    m_ecsCommands.Commit(m_ecsWorld);
    TaskGraph ecsGraph;
    {
        std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
        ecsGraph = m_ecsSystems.Build(m_ecsWorld, deltaTime);
    }
    const TaskGraphStats ecsStats = loop->RunTaskGraph(std::move(ecsGraph));
    if (!IsTickCurrent(graphState, activationGeneration)) {
        return;
    }
    if (activeCamera != nullptr) {
        activeCamera->AdvanceEffects(deltaTime);
        if (hasOpenWindow) {
            snapshot.hasCamera = activeCamera->GetCameraView(snapshot.camera);
        }
    }

    std::vector<Object::Collider*> colliders;
    for (Object::Node* node : nodes) {
        node->CollectColliders(colliders);
    }
    snapshot.colliderCount = static_cast<std::uint32_t>(colliders.size());
    const std::size_t count = colliders.size();
    std::vector<Collision::Aabb> bounds;
    bounds.reserve(count);
    for (Object::Collider* collider : colliders) {
        bounds.push_back(collider->WorldAabb());
    }
    std::vector<Collision::OverlapPair> overlapPairs;
    const auto extractionStart = Clock::now();
    if (hasOpenWindow) {
        for (Object::Node* node : nodes) {
            node->CollectRender(snapshot.instances);
            node->CollectParticleEmitters(snapshot.particleEmitters);
            node->CollectLights(snapshot.lights);
            node->CollectSmokeVolumes(snapshot.smokeVolumes);
        }
        for (RenderInstance& instance : snapshot.instances) {
            if (instance.bonePalette == nullptr || instance.boneCount == 0) {
                continue;
            }
            const std::size_t floatCount = static_cast<std::size_t>(instance.boneCount) * 16;
            snapshot.boneMatrices.insert(snapshot.boneMatrices.end(),
                                         instance.bonePalette,
                                         instance.bonePalette + floatCount);
        }
    }
    snapshot.extractionMs = std::chrono::duration<float, std::milli>(
        Clock::now() - extractionStart).count();

    TaskGraph graph;
    graph.Add("Scene.CollisionBroadphase", [&] {
        const auto start = Clock::now();
        overlapPairs = Collision::SweepAndPrune(bounds);
        snapshot.collisionMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
    });
    const TaskGraphStats graphStats = loop->RunTaskGraph(std::move(graph));
    if (!IsTickCurrent(graphState, activationGeneration)) {
        return;
    }
    snapshot.taskGraphMs = ecsStats.wallMs + graphStats.wallMs;
    snapshot.taskNodeCount = static_cast<std::uint32_t>(ecsStats.nodes.size() + graphStats.nodes.size());
    const auto recordSlowest = [&snapshot](const TaskGraphStats& stats) {
        for (const TaskNodeStats& node : stats.nodes) {
            if (node.cpuMs <= snapshot.slowestTaskMs) {
                continue;
            }
            snapshot.slowestTaskMs = node.cpuMs;
            snapshot.slowestTask.fill('\0');
            std::strncpy(snapshot.slowestTask.data(), node.name.c_str(), snapshot.slowestTask.size() - 1);
        }
    };
    recordSlowest(ecsStats);
    recordSlowest(graphStats);
    snapshot.RebindBonePalettes();

    std::vector<std::vector<std::size_t>> currentIndices(count);
    for (const Collision::OverlapPair& pair : overlapPairs) {
        // Geometry says these AABBs overlap; the Godot-style layer/mask gate
        // decides whether that counts as an interaction for these two colliders.
        if (!colliders[pair.first]->CanInteractWith(*colliders[pair.second])) {
            continue;
        }
        currentIndices[pair.first].push_back(pair.second);
        currentIndices[pair.second].push_back(pair.first);
    }
    std::vector<std::vector<Object::Collider*>> current(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::sort(currentIndices[index].begin(), currentIndices[index].end());
        current[index].reserve(currentIndices[index].size());
        for (const std::size_t otherIndex : currentIndices[index]) {
            current[index].push_back(colliders[otherIndex]);
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        Object::Collider* collider = colliders[i];
        const std::vector<Object::Collider*>& now = current[i];
        std::vector<Object::Collider*> before;
        {
            std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
            if (!IsTickCurrent(graphState, activationGeneration)) {
                return;
            }
            before = collider->m_overlapping;
        }

        for (Object::Collider* other : now) {
            if (std::find(before.begin(), before.end(), other) == before.end()) {
                std::function<void(Object::Collider&)> callback;
                {
                    std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
                    if (!IsTickCurrent(graphState, activationGeneration)) {
                        return;
                    }
                    callback = collider->m_onEnter;
                }
                if (callback) {
                    callback(*other);
                }
                if (!IsTickCurrent(graphState, activationGeneration)) {
                    return;
                }
            }
        }
        for (Object::Collider* other : before) {
            if (std::find(now.begin(), now.end(), other) == now.end()) {
                std::function<void(Object::Collider&)> callback;
                {
                    std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
                    if (!IsTickCurrent(graphState, activationGeneration)) {
                        return;
                    }
                    callback = collider->m_onExit;
                }
                if (callback) {
                    callback(*other);
                }
                if (!IsTickCurrent(graphState, activationGeneration)) {
                    return;
                }
            }
        }
    }
    {
        std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
        if (!IsTickCurrent(graphState, activationGeneration)) {
            return;
        }
        for (std::size_t i = 0; i < count; ++i) {
            colliders[i]->m_overlapping = std::move(current[i]);
        }
    }
    if (hasOpenWindow) {
        std::lock_guard<std::recursive_mutex> graphLock(graphState->mutex);
        if (m_window == window && IsTickCurrent(graphState, activationGeneration)) {
            loop->PublishWorldSnapshot(window, std::move(snapshot));
        }
    }
}

std::vector<Object::Node*> Scene::SnapshotNodes() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    std::vector<Object::Node*> nodes;
    nodes.reserve(m_nodes.size());
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        nodes.push_back(node.get());
    }
    return nodes;
}

} // namespace Concord
