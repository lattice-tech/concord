#ifndef CONCORD_SCENE_H
#define CONCORD_SCENE_H

#include "Concord/CExport.h"
#include "engine/collision/query/Ray.h"
#include "engine/collision/query/RaycastFilter.h"
#include "engine/collision/query/RaycastHit.h"
#include "engine/environment/EnvironmentSettings.h"
#include "engine/loop/EngineLoop.h"
#include "engine/object/Node.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/ecs/CommandBuffer.h"
#include "engine/ecs/SystemGraph.h"

#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
#include <string>

namespace Concord {

class Game;
class SceneIO;

/** Shared synchronization and callback-lifetime state for one scene graph. */
struct SceneGraphState {
    std::recursive_mutex mutex;
    std::atomic<bool> alive{true};
    std::atomic<bool> active{false};
    std::atomic<std::uint64_t> activationGeneration{0};
};

namespace Object {
class Camera;
}

struct MeshData;

/**
 * A container of nodes the engine renders and ticks together.
 *
 * A Scene owns its nodes: create them with Spawn, which heap-allocates the
 * node and returns a reference, and the Scene destroys them when it itself is
 * destroyed. Objects are never added to a Game directly; a Game holds at most
 * one active scene, set with Game::LoadScene. Loading a scene activates it
 * (its nodes render, their OnStart fires the first frame, their OnUpdate ticks
 * every frame); loading a different scene, or destroying the Game, deactivates
 * it. Spawning into an already-active scene brings the new node to life at once.
 */
class CENGINE_API Scene {
public:
    Scene();

    /**
     * Deactivates the scene and waits for its coordinator update to retire.
     * Destruction from an ECS system worker is a contract violation and
     * terminates immediately because the current scheduler has no deferred
     * coordinator-teardown queue.
     */
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /**
     * Creates a node of type T (which must derive from Object::Node),
     * forwarding `args` to its constructor, takes ownership of it, and returns
     * a reference. If the scene is already active, the node begins rendering
     * and ticking immediately.
     */
    template <typename T, typename... Args>
    T& Spawn(Args&&... args)
    {
        static_assert(std::is_base_of_v<Object::Node, T>,
                      "Scene nodes must derive from Concord::Object::Node");
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *node;
        AddNode(std::move(node));
        return ref;
    }

    /**
     * Selects a camera owned by this scene.
     * The first camera added becomes active when no camera has been selected.
     * @return false without changing the active camera when `camera` belongs to
     *         another scene.
     */
    bool SetActiveCamera(Object::Camera& camera);

    /** Replaces the scene-level visible sky and indirect-light settings. */
    void SetSkyEnvironment(const SkyEnvironment& environment);

    /** Returns a thread-safe copy of the current scene-level sky settings. */
    SkyEnvironment GetSkyEnvironment() const;

    /** Replaces the scene-level sky, cloud, fog, and animation settings. */
    void SetEnvironmentSettings(const EnvironmentSettings& settings);

    /** Returns a thread-safe copy of the complete environment authoring state. */
    EnvironmentSettings GetEnvironmentSettings() const;

    /**
     * Uploads `data` to GPU memory through the engine loop and returns a handle
     * naming it, for renderable nodes that own custom geometry (e.g. an
     * imported model). CollectRender runs on a CPU worker, so uploads are queued
     * to the render thread. Returns an invalid handle when no loop is bound yet.
     */
    MeshHandle AcquireMesh(const MeshData& data);

    /** Releases a handle previously returned by AcquireMesh; a no-op if invalid. */
    void ReleaseMesh(MeshHandle mesh);

    /**
     * A snapshot of every node the scene currently owns (non-owning pointers,
     * in spawn order). Used by scene serialization to walk the objects; safe to
     * call at any time.
     */
    std::vector<Object::Node*> Nodes() const { return SnapshotNodes(); }

    /** Returns the generation-safe ECS component world. */
    Ecs::World& EcsWorld() noexcept { return m_ecsWorld; }

    /** Returns the structural queue committed before the next system graph. */
    Ecs::CommandBuffer& EcsCommands() noexcept { return m_ecsCommands; }

    /**
     * Registers a system whose component access determines task dependencies.
     * Systems run on ECS workers and must restrict mutation to their declared
     * World components. They must not mutate Nodes or bind, unbind, or destroy
     * a Scene/Game; scene teardown must be requested from a coordinator callback
     * or an application-owned thread after the system returns.
     */
    Ecs::SystemGraph::SystemId AddSystem(
        std::string name, Ecs::SystemAccess access,
        std::function<void(Ecs::World&, float)> system);

    /** Removes a registered ECS system before the next graph build. */
    bool RemoveSystem(Ecs::SystemGraph::SystemId id);

    /**
     * @brief Finds the closest collider intersected by a world-space ray.
     *
     * The query takes the scene graph lock for its complete traversal, so the
     * tested transforms, shapes, layers, and parent IDs belong to one coherent
     * scene state. The returned value contains IDs and geometry only; it does
     * not extend any scene-owned object's lifetime.
     *
     * @param ray World-space ray; its finite, non-zero direction is normalized
     *            internally so hit distance is measured in world units.
     * @param filter Layer, distance, and ignored-collider constraints.
     * @param outHit Receives the closest hit and is unchanged when none exists.
     * @return true when at least one collider passes the filter and intersects.
     */
    bool RaycastClosest(const Collision::Ray& ray,
                        const Collision::RaycastFilter& filter,
                        Collision::RaycastHit& outHit) const;

    /**
     * @brief Tests whether any collider intersects a filtered world-space ray.
     *
     * Uses the same graph-lock snapshot and shape semantics as RaycastClosest,
     * but returns on the first accepted hit and does not construct an identity
     * result for the caller.
     */
    bool RaycastAny(const Collision::Ray& ray,
                    const Collision::RaycastFilter& filter = {}) const;

private:
    friend class Game;
    friend class SceneIO;
    friend class Object::Node;

    /**
     * Called by Game::LoadScene to claim and activate this scene.
     * @return false when another Game already owns the scene or registration fails.
     */
    bool Bind(Game* game, std::shared_ptr<EngineLoop> loop, EngineLoop::WindowId window);

    /** Makes a successfully bound scene visible to its registered update callback. */
    void ActivateBinding();

    /** Updates the render target when the owning Game attaches, replaces, or detaches its window. */
    void RebindWindow(EngineLoop::WindowId window);

    /** Called by Game when this scene stops being the active one; reverses Bind and drops the back-link. */
    void Unbind();

    /** Removes the simulation tick and detaches every node's renderable; safe to call when inactive. */
    void Deactivate();

    void AddNode(std::unique_ptr<Object::Node> node);

    /** Commits a fully constructed scene-file batch without exposing partial state. */
    void CommitLoadedNodes(const SkyEnvironment& environment,
                           std::vector<std::unique_ptr<Object::Node>> nodes);

    void Tick(float deltaTime, std::uint64_t activationGeneration);
    std::vector<Object::Node*> SnapshotNodes() const;
    bool RaycastInternal(const Collision::Ray& ray,
                         const Collision::RaycastFilter& filter,
                         Collision::RaycastHit* outClosest) const;

    std::shared_ptr<SceneGraphState> m_graphState;
    std::vector<std::unique_ptr<Object::Node>> m_nodes;

    Game* m_game = nullptr;
    std::weak_ptr<EngineLoop> m_loop;
    EngineLoop::WindowId m_window = EngineLoop::kInvalidWindowId;
    EngineLoop::UpdateId m_updateId = EngineLoop::kInvalidUpdateId;
    Object::Camera* m_activeCamera = nullptr; // non-owning; points into m_nodes
    EnvironmentSettings m_environmentSettings{};
    std::uint64_t m_snapshotGeneration = 0;
    std::atomic<Object::ObjectId> m_nextEntityId{1};
    Ecs::World m_ecsWorld;
    Ecs::CommandBuffer m_ecsCommands;
    Ecs::SystemGraph m_ecsSystems;
};

} // namespace Concord

#endif // CONCORD_SCENE_H
