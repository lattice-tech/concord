#include "engine/object/Node.h"

#include "engine/scene/Scene.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Concord::Object {

namespace {

std::recursive_mutex g_detachedGraphMutex;

/**
 * Multiplies matrices in the intuitive column-vector order: out = lhs * rhs, so
 * a point transforms as (lhs * rhs) * p — rhs applied first, then lhs.
 *
 * bx::mtxMul(out, a, b) actually yields b * a (bx stores matrices row-major),
 * which is the opposite of what column-vector math expects. Routing every
 * multiply through this wrapper keeps that gotcha in one place.
 */
void Multiply(float* out, const float* lhs, const float* rhs) noexcept
{
    bx::mtxMul(out, rhs, lhs);
}

/** Builds a column-major local matrix (T * R * S) from a Transform. */
void BuildLocalMatrix(float* out, const Transform& transform) noexcept
{
    float scaleMtx[16];
    float rotMtx[16];
    float transMtx[16];
    float rotScale[16];
    bx::mtxScale(scaleMtx, transform.scale.x, transform.scale.y, transform.scale.z);
    bx::mtxFromQuaternion(
        rotMtx,
        bx::Quaternion(transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w));
    Multiply(rotScale, rotMtx, scaleMtx);  // R * S
    bx::mtxTranslate(transMtx, transform.position.x, transform.position.y, transform.position.z);
    Multiply(out, transMtx, rotScale);     // T * (R * S)
}

} // namespace

Node::~Node()
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    DetachFromParent();
    // Orphan children so their dangling parent pointer is cleared, regardless
    // of the order in which the scene destroys its nodes.
    for (Node* child : m_children) {
        child->m_parent = nullptr;
        child->MarkWorldDirty();
    }
}

void Node::SetReflectionMode(ReflectionMode mode) noexcept
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_reflectionMode = mode;
}

ReflectionMode Node::GetReflectionMode() const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_reflectionMode;
}

bool Node::UsesRealtimeReflection() const noexcept
{
    return GetReflectionMode() == ReflectionMode::RealtimeScene;
}

void Node::SetReflectivity(float reflectivity) noexcept
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_reflectivity = std::isfinite(reflectivity)
        ? std::clamp(reflectivity, 0.0f, 1.0f) : 1.0f;
}

float Node::Reflectivity() const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_reflectivity;
}

void Node::OnStart(std::function<void()> callback)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_onStart = std::move(callback);
}

void Node::OnUpdate(std::function<void(float)> callback)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_onUpdate = std::move(callback);
}

bool Node::SetParent(Node* parent)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    if (m_scene != nullptr && !m_scene->IsAlive(m_handle)) {
        return false;
    }
    if (parent == m_parent) {
        return true;
    }
    if (parent == this) {
        return false;
    }
    if (parent != nullptr) {
        if (m_scene != parent->m_scene) {
            return false;
        }
        if (m_scene != nullptr && !m_scene->IsAlive(parent->m_handle)) {
            return false;
        }
        for (const Node* ancestor = parent; ancestor != nullptr; ancestor = ancestor->m_parent) {
            if (ancestor == this) {
                return false;
            }
        }
    }
    DetachFromParent();
    m_parent = parent;
    if (m_parent != nullptr) {
        m_parent->m_children.push_back(this);
    }
    MarkWorldDirty();
    return true;
}

bool Node::AddChild(Node& child)
{
    return child.SetParent(this);
}

Node* Node::Parent() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_parent;
}

Transform Node::LocalTransform() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    return m_local;
}

void Node::SetLocalTransform(const Transform& transform)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_local = transform;
    MarkWorldDirty();
}

void Node::SetPosition(Vector3 position)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_local.position = position;
    MarkWorldDirty();
}

void Node::SetRotation(Quaternion rotation)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_local.rotation = rotation;
    MarkWorldDirty();
}

void Node::SetScale(Vector3 scale)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_local.scale = scale;
    MarkWorldDirty();
}

void Node::Translate(Vector3 delta)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_local.position = m_local.position + delta;
    MarkWorldDirty();
}

void Node::Rotate(Quaternion delta)
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    m_local.rotation = m_local.rotation * delta;
    MarkWorldDirty();
}

const float* Node::WorldMatrix() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    if (!m_worldDirty) {
        return m_world;
    }

    float local[16];
    BuildLocalMatrix(local, m_local);
    if (m_parent != nullptr) {
        Multiply(m_world, m_parent->WorldMatrix(), local); // world = parentWorld * local
    } else {
        std::copy(local, local + 16, m_world);
    }
    m_worldDirty = false;
    return m_world;
}

Vector3 Node::WorldPosition() const
{
    std::lock_guard<std::recursive_mutex> lock(GraphMutex());
    const float* world = WorldMatrix();
    return Vector3{world[12], world[13], world[14]};
}

void Node::MarkWorldDirty() const
{
    m_worldDirty = true;
    for (const Node* child : m_children) {
        child->MarkWorldDirty();
    }
}

void Node::DetachFromParent()
{
    if (m_parent == nullptr) {
        return;
    }
    std::vector<Node*>& siblings = m_parent->m_children;
    const auto it = std::find(siblings.begin(), siblings.end(), this);
    if (it != siblings.end()) {
        *it = siblings.back();
        siblings.pop_back();
    }
    m_parent = nullptr;
}

std::recursive_mutex& Node::GraphMutex() const
{
    return m_graphState ? m_graphState->mutex : g_detachedGraphMutex;
}

} // namespace Concord::Object
