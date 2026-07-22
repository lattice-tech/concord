#ifndef CONCORD_NODE_H
#define CONCORD_NODE_H

#include "Concord/CExport.h"
#include "engine/object/Transform.h"
#include "engine/object/ObjectId.h"
#include "engine/object/ReflectionMode.h"
#include "engine/render/frame/CameraView.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/RenderSmokeVolume.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Concord {

class Scene;
struct SceneGraphState;

namespace Object {

class Collider;

/**
 * Base class for everything a Scene owns: a transform node in the scene graph
 * that can also carry game logic.
 *
 * Every node has a **local transform** (relative to its parent) and, through
 * SetParent/AddChild, a place in a parent/child hierarchy. Its **world
 * transform** is the local transform composed with the parent's world
 * transform, all the way to the root. World matrices are cached and lazily
 * recomputed: changing a node's transform marks it and its whole subtree
 * dirty, and WorldMatrix() rebuilds only what is stale — so moving a parent
 * cheaply carries its children along.
 *
 * A node also carries two optional callbacks run on the simulation coordinator while
 * its scene is active: OnStart (once, the first frame it is ticked) and
 * OnUpdate (every frame). Renderable nodes (Box) and the viewpoint (Camera)
 * derive from Node and override the private hooks the Scene collects each frame.
 */
class CENGINE_API Node {
public:
    Node() = default;
    virtual ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    /** Stable ECS entity identity for this node's lifetime. */
    ObjectId Id() const noexcept { return m_id; }

    /**
     * Selects the reflection source for this node's renderable geometry.
     *
     * `RealtimeScene` works with built-in primitives, arbitrary imported
     * Model meshes and SkinnedModel meshes. Geometry keeps its normal depth,
     * normal and material rendering; the live HDR capture only replaces the
     * environment contribution. Non-renderable nodes retain the setting but
     * do not submit a draw.
     */
    void SetReflectionMode(ReflectionMode mode) noexcept;

    /** Returns the reflection source currently selected for this node. */
    ReflectionMode GetReflectionMode() const noexcept;

    /** True when this node requests the live HDR scene-reflection path. */
    bool UsesRealtimeReflection() const noexcept;

    /**
     * Legacy compatibility alias for `SetReflectionMode`.
     * @param enabled Maps true to RealtimeScene and false to Standard.
     */
    void SetRayTraced(bool enabled) noexcept
    {
        SetReflectionMode(enabled ? ReflectionMode::RealtimeScene
                                  : ReflectionMode::Standard);
    }

    /** Legacy compatibility alias for `UsesRealtimeReflection`. */
    bool IsRayTraced() const noexcept { return UsesRealtimeReflection(); }

    /**
     * Sets an object-wide multiplier on material reflection strength.
     *
     * This is useful for imported models because it preserves every authored
     * sub-mesh material and texture while scaling their real-time/planar
     * reflection contribution together. Values are clamped to [0, 1].
     */
    void SetReflectivity(float reflectivity) noexcept;

    /** Returns the object-wide reflection multiplier in [0, 1]. */
    float Reflectivity() const noexcept;

    /** Sets the callback run once, the first frame this node's scene ticks it. */
    void OnStart(std::function<void()> callback);

    /** Sets the callback run every frame while this node's scene is active. */
    void OnUpdate(std::function<void(float deltaTime)> callback);

    /**
     * Reparents this node under `parent` (pass nullptr to detach to the scene
     * root). The world transform is preserved conceptually only in the sense
     * that the local transform is kept as-is; the resulting world position
     * changes to follow the new parent, matching typical "attach" semantics.
     */
    /**
     * Reparents this node when the relationship is valid.
     * @return false when `parent` would create a hierarchy cycle or belongs to
     *         a different Scene; the existing parent is unchanged on failure.
     */
    bool SetParent(Node* parent);

    /** Convenience for `child.SetParent(this)`. */
    /** @return The result of `child.SetParent(this)`. */
    bool AddChild(Node& child);

    /** This node's parent, or nullptr if it is at the scene root. */
    Node* Parent() const;

    /** The local transform (relative to the parent). */
    Transform LocalTransform() const;

    void SetLocalTransform(const Transform& transform);
    void SetPosition(Vector3 position);
    void SetRotation(Quaternion rotation);
    void SetScale(Vector3 scale);

    /** Adds `delta` to the local position. */
    void Translate(Vector3 delta);

    /** Composes `delta` onto the local rotation. */
    void Rotate(Quaternion delta);

    /** Column-major 4x4 world matrix, recomputed lazily from the hierarchy. */
    const float* WorldMatrix() const;

    /** World-space position (translation of the world matrix). */
    Vector3 WorldPosition() const;

protected:
    /**
     * The Scene that owns this node (set by Scene::AddNode), or nullptr before
     * the node is added / after the scene drops it. Lets renderable nodes reach
     * the engine loop for queued resource upload during CollectRender, which
     * runs on a CPU worker. Worker hooks must never call SDL or bgfx directly.
     */
    Scene* OwningScene() const noexcept { return m_scene; }

    /** Scene-wide graph lock used by derived types to synchronize their state. */
    std::recursive_mutex& GraphMutex() const;

    /**
     * Stable plain-data key shared by this node's sub-mesh draws for one frame.
     * The render backend compares it only for equality and never dereferences it.
     */
    std::uintptr_t ReflectionOwnerKey() const noexcept
    {
        return reinterpret_cast<std::uintptr_t>(this);
    }

private:
    friend class Concord::Scene;

    /** True for camera nodes, so the scene can pick a default active camera. */
    virtual bool IsCamera() const noexcept { return false; }

    /** Fills `out` with this node's view/projection and returns true (cameras only). */
    virtual bool GetCameraView(CameraView& out) const
    {
        (void)out;
        return false;
    }

    /** Appends this node's draw(s) for the frame (renderable nodes only). */
    virtual void CollectRender(std::vector<RenderInstance>& out) const { (void)out; }

    /** Appends this node's light(s) for the frame (light nodes only). */
    virtual void CollectLights(std::vector<RenderLight>& out) const { (void)out; }

    /** Appends this node's smoke volume(s) for the frame (SmokeVolume nodes only). */
    virtual void CollectSmokeVolumes(std::vector<RenderSmokeVolume>& out) const { (void)out; }

    /** Appends this node to the frame's collider list (collider nodes only). */
    virtual void CollectColliders(std::vector<Collider*>& out) { (void)out; }

    /** Marks this node and its whole subtree as needing a world-matrix rebuild. */
    void MarkWorldDirty() const;

    /** Removes this node from its current parent's child list, if any. */
    void DetachFromParent();

    Transform m_local{};
    Node* m_parent = nullptr;
    Scene* m_scene = nullptr;
    std::shared_ptr<SceneGraphState> m_graphState;
    std::vector<Node*> m_children;

    mutable bool m_worldDirty = true;
    mutable float m_world[16]{};

    /** Reflection source applied to renderable draws produced by this node. */
    ReflectionMode m_reflectionMode = ReflectionMode::Standard;

    /** Multiplier applied after each renderable node resolves its material. */
    float m_reflectivity = 1.0f;

    std::function<void()> m_onStart;
    std::function<void(float)> m_onUpdate;
    bool m_started = false;
    ObjectId m_id = kInvalidObjectId;
};

} // namespace Object
} // namespace Concord

#endif // CONCORD_NODE_H
