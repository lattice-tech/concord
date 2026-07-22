#include "engine/object/Collider.h"

#include <algorithm>
#include <utility>

namespace Concord::Object {

namespace {

/**
 * Transforms local point `p` by the node's world matrix (row-major, row-vector
 * convention with translation at indices 12/13/14; see Node).
 */
Vector3 TransformPoint(const float* m, const Vector3& p) noexcept
{
    return {
        p.x * m[0] + p.y * m[4] + p.z * m[8]  + m[12],
        p.x * m[1] + p.y * m[5] + p.z * m[9]  + m[13],
        p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14],
    };
}

} // namespace

Collider::Collider(ColliderDesc desc)
    : m_shape(desc.shape)
    , m_layer(desc.layer)
    , m_mask(desc.mask)
{
    SetLocalTransform(desc.transform);
}

void Collider::SetShape(Collision::CollisionShape shape)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_shape = shape;
}

Collision::CollisionShape Collider::Shape() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_shape;
}

Collision::Aabb Collider::WorldAabb() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    // A Box uses its half-extents; a Sphere is bounded by a cube of its radius.
    // Both are transformed by the node's world matrix and reduced to a world
    // AABB by taking the min/max of the eight transformed corners — correct for
    // any rotation and scale, at the cost of being a touch conservative for a
    // rotated box (which is exactly the "collision box" model we want here).
    const Vector3 half = m_shape.type == Collision::ShapeType::Sphere
        ? Vector3{m_shape.radius, m_shape.radius, m_shape.radius}
        : m_shape.halfExtents;
    const Vector3 c = m_shape.offset;

    const float* world = WorldMatrix();
    Collision::Aabb box;
    bool first = true;
    for (int i = 0; i < 8; ++i) {
        const Vector3 corner{
            c.x + ((i & 1) ? half.x : -half.x),
            c.y + ((i & 2) ? half.y : -half.y),
            c.z + ((i & 4) ? half.z : -half.z),
        };
        const Vector3 p = TransformPoint(world, corner);
        if (first) {
            box.min = p;
            box.max = p;
            first = false;
        } else {
            box.min = {std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
            box.max = {std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
        }
    }
    return box;
}

bool Collider::Overlaps(const Collider& other) const
{
    return WorldAabb().Overlaps(other.WorldAabb());
}

std::uint32_t Collider::Layer() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_layer;
}

void Collider::SetLayer(std::uint32_t layer)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_layer = layer;
}

std::uint32_t Collider::Mask() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_mask;
}

void Collider::SetMask(std::uint32_t mask)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_mask = mask;
}

bool Collider::CanInteractWith(const Collider& other) const noexcept
{
    // Symmetric so a single overlap pair notifies both sides: interact when
    // either collider scans a layer the other occupies.
    return (m_mask & other.m_layer) != 0u || (other.m_mask & m_layer) != 0u;
}

void Collider::OnEnter(std::function<void(Collider&)> callback)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_onEnter = std::move(callback);
}

void Collider::OnExit(std::function<void(Collider&)> callback)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_onExit = std::move(callback);
}

void Collider::CollectColliders(std::vector<Collider*>& out)
{
    out.push_back(this);
}

} // namespace Concord::Object
