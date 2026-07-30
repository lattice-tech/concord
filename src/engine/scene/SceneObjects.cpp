#include "engine/scene/Scene.h"

#include "engine/object/Camera.h"
#include "engine/object/Collider.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Concord {

Object::ObjectHandle Scene::AllocateHandleLocked(Object::Node* node)
{
    std::uint32_t index = 0;
    if (!m_freeObjectSlots.empty()) {
        index = m_freeObjectSlots.back();
        m_freeObjectSlots.pop_back();
    } else {
        if (m_objectSlots.size() >= std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("scene object handle capacity exhausted");
        }
        index = static_cast<std::uint32_t>(m_objectSlots.size());
        m_objectSlots.emplace_back();
    }
    Detail::SceneObjectSlot& slot = m_objectSlots[index];
    slot.node = node;
    slot.state = Detail::SceneObjectState::Live;
    return Object::ObjectHandle{m_sceneIdentity, index, slot.generation};
}

void Scene::AddNode(std::unique_ptr<Object::Node> node)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    const Object::ObjectId nextEntityId =
        m_nextEntityId.load(std::memory_order_relaxed);
    if (nextEntityId == 0
        || nextEntityId == std::numeric_limits<Object::ObjectId>::max()) {
        throw std::overflow_error("scene runtime object IDs exhausted");
    }
    if (m_nextPersistentId == 0
        || m_nextPersistentId == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("scene persistent object IDs exhausted");
    }
    if (m_freeObjectSlots.empty()
        && m_objectSlots.size() >= std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("scene object handle capacity exhausted");
    }
    if (m_nodes.size() == m_nodes.max_size()) {
        throw std::length_error("scene node storage capacity exhausted");
    }
    m_nodes.reserve(m_nodes.size() + 1u);
    if (m_freeObjectSlots.empty()) {
        m_objectSlots.reserve(m_objectSlots.size() + 1u);
    }

    Object::Node* raw = node.get();
    raw->m_scene = this;
    raw->m_graphState = m_graphState;
    raw->m_id = nextEntityId;
    raw->m_persistentId = Object::PersistentObjectId{m_nextPersistentId};
    raw->m_handle = AllocateHandleLocked(raw);
    m_nodes.push_back(std::move(node));
    m_nextEntityId.store(nextEntityId + 1u, std::memory_order_relaxed);
    ++m_nextPersistentId;
    if (raw->IsCamera() && !m_activeCamera.IsValid()) {
        m_activeCamera = raw->m_handle;
    }
}

Object::Node* Scene::ResolveLiveLocked(Object::ObjectHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.scene != m_sceneIdentity
        || handle.slot >= m_objectSlots.size()) {
        return nullptr;
    }
    const Detail::SceneObjectSlot& slot = m_objectSlots[handle.slot];
    return slot.state == Detail::SceneObjectState::Live
        && slot.generation == handle.generation ? slot.node : nullptr;
}

Object::Node* Scene::Find(Object::ObjectHandle handle) noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return ResolveLiveLocked(handle);
}

const Object::Node* Scene::Find(Object::ObjectHandle handle) const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return ResolveLiveLocked(handle);
}

Object::Node* Scene::Find(Object::PersistentObjectId id) noexcept
{
    return const_cast<Object::Node*>(
        static_cast<const Scene&>(*this).Find(id));
}

const Object::Node* Scene::Find(Object::PersistentObjectId id) const noexcept
{
    if (!id.IsValid()) {
        return nullptr;
    }
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        if (ResolveLiveLocked(node->m_handle) == node.get()
            && node->m_persistentId == id) {
            return node.get();
        }
    }
    return nullptr;
}

bool Scene::IsAlive(Object::ObjectHandle handle) const noexcept
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    return ResolveLiveLocked(handle) != nullptr;
}

bool Scene::Despawn(Object::ObjectHandle handle)
{
    std::shared_ptr<EngineLoop> loop;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        Object::Node* root = ResolveLiveLocked(handle);
        if (root == nullptr) {
            return false;
        }

        std::vector<Object::Node*> subtree{root};
        for (std::size_t i = 0; i < subtree.size(); ++i) {
            subtree.insert(subtree.end(), subtree[i]->m_children.begin(),
                           subtree[i]->m_children.end());
        }
        m_pendingDespawns.reserve(m_pendingDespawns.size() + subtree.size());
        for (Object::Node* node : subtree) {
            Detail::SceneObjectSlot& slot = m_objectSlots[node->m_handle.slot];
            if (slot.state == Detail::SceneObjectState::Live) {
                slot.state = Detail::SceneObjectState::PendingDespawn;
                m_pendingDespawns.push_back(node->m_handle.slot);
            }
        }
        SelectFallbackCameraLocked();
        loop = m_loop.lock();
    }

    if (loop) {
        loop->RequestSimulation();
    }
    if (m_graphState->traversalDepth.load(std::memory_order_acquire) == 0) {
        CommitDespawns();
    }
    return true;
}

void Scene::SelectFallbackCameraLocked()
{
    if (ResolveLiveLocked(m_activeCamera) != nullptr) {
        return;
    }
    m_activeCamera = {};
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        if (ResolveLiveLocked(node->m_handle) != nullptr && node->IsCamera()) {
            m_activeCamera = node->m_handle;
            return;
        }
    }
}

void Scene::CommitDespawns()
{
    if (m_graphState->traversalDepth.load(std::memory_order_acquire) != 0) {
        return;
    }
    std::vector<std::unique_ptr<Object::Node>> retired;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        if (m_pendingDespawns.empty()
            || m_graphState->traversalDepth.load(std::memory_order_acquire) != 0) {
            return;
        }

        std::vector<Object::Collider*> retiringColliders;
        for (std::uint32_t index : m_pendingDespawns) {
            m_objectSlots[index].node->CollectColliders(retiringColliders);
        }
        for (const std::unique_ptr<Object::Node>& owner : m_nodes) {
            if (ResolveLiveLocked(owner->m_handle) == nullptr) {
                continue;
            }
            std::vector<Object::Collider*> colliders;
            owner->CollectColliders(colliders);
            for (Object::Collider* collider : colliders) {
                std::erase_if(collider->m_overlapping, [&](Object::Collider* other) {
                    return std::find(retiringColliders.begin(), retiringColliders.end(), other)
                        != retiringColliders.end();
                });
            }
        }

        for (std::uint32_t index : m_pendingDespawns) {
            Object::Node* node = m_objectSlots[index].node;
            node->DetachFromParent();
            for (Object::Node* child : node->m_children) {
                child->m_parent = nullptr;
                child->MarkWorldDirty();
            }
            node->m_children.clear();
        }
        std::erase_if(m_nodes, [&](std::unique_ptr<Object::Node>& owner) {
            const std::uint32_t index = owner->m_handle.slot;
            Detail::SceneObjectSlot& slot = m_objectSlots[index];
            if (slot.state != Detail::SceneObjectState::PendingDespawn) {
                return false;
            }
            retired.push_back(std::move(owner));
            slot.node = nullptr;
            if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
                slot.state = Detail::SceneObjectState::Exhausted;
            } else {
                ++slot.generation;
                slot.state = Detail::SceneObjectState::Free;
                m_freeObjectSlots.push_back(index);
            }
            return true;
        });
        m_pendingDespawns.clear();
        SelectFallbackCameraLocked();
    }
}

bool Scene::SetActiveCamera(Object::Camera& camera)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    if (ResolveLiveLocked(camera.m_handle) != &camera) {
        return false;
    }
    m_activeCamera = camera.m_handle;
    return true;
}

std::vector<Object::ObjectHandle> Scene::SnapshotHandles() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    std::vector<Object::ObjectHandle> handles;
    handles.reserve(m_nodes.size());
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        if (ResolveLiveLocked(node->m_handle) != nullptr) {
            handles.push_back(node->m_handle);
        }
    }
    return handles;
}

std::vector<Object::Node*> Scene::SnapshotNodes() const
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    std::vector<Object::Node*> nodes;
    nodes.reserve(m_nodes.size());
    for (const std::unique_ptr<Object::Node>& node : m_nodes) {
        if (ResolveLiveLocked(node->m_handle) != nullptr) {
            nodes.push_back(node.get());
        }
    }
    return nodes;
}

} // namespace Concord
