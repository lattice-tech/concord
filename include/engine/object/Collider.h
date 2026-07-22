#ifndef CONCORD_COLLIDER_H
#define CONCORD_COLLIDER_H

#include "Concord/CExport.h"
#include "engine/collision/Aabb.h"
#include "engine/collision/CollisionShape.h"
#include "engine/object/ColliderDesc.h"
#include "engine/object/Node.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Concord::Object {

/**
 * A collision volume that can be attached to any object — Concord's equivalent
 * of a Godot Area/CollisionShape node.
 *
 * Created through Scene::Spawn<Collider>(desc) and usually parented to the
 * object it guards (`collider.SetParent(&box)`), so it follows that object's
 * world transform: move or scale the object and the collision box moves and
 * scales with it. Each frame the active Scene reduces every collider to a
 * world-space AABB and tests them with a deterministic broad phase; when two
 * colliders start touching it fires their OnEnter callback, and when they
 * separate, OnExit — the
 * signal-style overlap notifications Godot's areas provide.
 *
 * The shape is fully customizable (box half-extents or sphere radius, plus a
 * local offset; see CollisionShape) and can be swapped at runtime with SetShape.
 */
class CENGINE_API Collider : public Node {
public:
    explicit Collider(ColliderDesc desc = {});

    /** Returns a thread-safe copy of the shape this collider currently tests with. */
    Collision::CollisionShape Shape() const;

    /** Replaces the collision shape (takes effect next frame). */
    void SetShape(Collision::CollisionShape shape);

    /**
     * This collider's world-space bounding box, derived from its shape and the
     * node's current world transform (recomputed on demand).
     */
    Collision::Aabb WorldAabb() const;

    /** True if this collider's world box overlaps `other`'s this instant. */
    bool Overlaps(const Collider& other) const;

    /** The set of collision layers this collider occupies (Godot `collision_layer`). */
    std::uint32_t Layer() const;

    /** Replaces the occupied layers (takes effect next frame). */
    void SetLayer(std::uint32_t layer);

    /** The set of layers this collider scans for overlaps (Godot `collision_mask`). */
    std::uint32_t Mask() const;

    /** Replaces the scanned layers (takes effect next frame). */
    void SetMask(std::uint32_t mask);

    /**
     * Godot-style layer/mask gate: two colliders notify each other only when
     * either one scans a layer the other occupies. With the default layer 1 /
     * mask 1 this is always true, so everything collides unless narrowed.
     */
    bool CanInteractWith(const Collider& other) const noexcept;

    /**
     * Sets the callback fired when another collider begins overlapping this
     * one, receiving that other collider. Runs on the simulation coordinator
     * after worker collision detection, like Node::OnUpdate.
     */
    void OnEnter(std::function<void(Collider& other)> callback);

    /** Sets the callback fired when a previously overlapping collider separates. */
    void OnExit(std::function<void(Collider& other)> callback);

private:
    friend class Concord::Scene;

    void CollectColliders(std::vector<Collider*>& out) override;

    Collision::CollisionShape m_shape;
    std::uint32_t m_layer = 1;
    std::uint32_t m_mask = 1;
    std::function<void(Collider&)> m_onEnter;
    std::function<void(Collider&)> m_onExit;

    /** Colliders overlapping this one as of the previous resolved frame (Scene-owned state). */
    std::vector<Collider*> m_overlapping;
};

} // namespace Concord::Object

#endif // CONCORD_COLLIDER_H
