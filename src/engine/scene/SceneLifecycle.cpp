#include "engine/scene/Scene.h"

#include "engine/app/Game.h"
#include "engine/object/Collider.h"

#include <atomic>
#include <exception>

namespace Concord {
namespace {

std::atomic<std::uint64_t> g_nextSceneIdentity{1};
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
    : m_graphState(std::make_shared<SceneGraphState>()),
      m_sceneIdentity(g_nextSceneIdentity.fetch_add(1, std::memory_order_relaxed))
{
    if (m_sceneIdentity == 0) {
        std::terminate();
    }
}

Scene::~Scene()
{
    EnforceCoordinatorTeardown();
    m_graphState->alive.store(false, std::memory_order_release);
    Deactivate();
    if (m_game != nullptr) {
        m_game->ForgetScene(this);
        m_game = nullptr;
    }
}

bool Scene::Bind(Game* game, std::shared_ptr<EngineLoop> loop,
                 EngineLoop::WindowId window)
{
    EnforceCoordinatorTeardown();
    if (game == nullptr || !loop) {
        return false;
    }
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    if (!m_graphState->alive.load(std::memory_order_acquire)
        || (m_game != nullptr && m_game != game)) {
        return false;
    }
    if (m_game == game && m_graphState->active.load(std::memory_order_acquire)) {
        return true;
    }

    const std::uint64_t generation =
        m_graphState->activationGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    m_game = game;
    m_loop = loop;
    m_window = window;
    m_updateId = loop->OnSceneUpdate(
        [this, state = m_graphState, generation](float deltaTime) {
            if (IsTickCurrent(state, generation)) {
                Tick(deltaTime, generation);
            }
        });
    if (m_updateId != EngineLoop::kInvalidUpdateId) {
        return true;
    }

    m_graphState->active.store(false, std::memory_order_release);
    m_graphState->activationGeneration.fetch_add(1, std::memory_order_acq_rel);
    m_game = nullptr;
    m_loop.reset();
    m_window = EngineLoop::kInvalidWindowId;
    return false;
}

void Scene::ActivateBinding()
{
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
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
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    m_game = nullptr;
}

void Scene::Deactivate()
{
    EnforceCoordinatorTeardown();
    std::shared_ptr<EngineLoop> loop;
    EngineLoop::UpdateId updateId = EngineLoop::kInvalidUpdateId;
    EngineLoop::WindowId window = EngineLoop::kInvalidWindowId;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
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
    CommitDespawns();
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

} // namespace Concord
