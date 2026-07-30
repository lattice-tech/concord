#include "engine/scene/Scene.h"

#include "engine/scene/io/SceneLoadBatch.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Concord {
namespace {

constexpr std::size_t kNoParent = std::numeric_limits<std::size_t>::max();

struct LoadPlan {
    std::vector<std::size_t> parents;
    std::vector<std::size_t> childCounts;
    std::unordered_map<std::uint64_t, std::size_t> nodeById;
    std::size_t activeCamera = kNoParent;
    std::uint64_t highestPersistentId = 0;
};

LoadPlan ValidateDetachedBatch(const Detail::SceneLoadBatch& batch)
{
    const std::size_t count = batch.nodes.size();
    if (batch.identities.size() != count) {
        throw std::invalid_argument("loaded scene identity count does not match node count");
    }

    LoadPlan plan;
    plan.parents.assign(count, kNoParent);
    plan.childCounts.assign(count, 0u);
    plan.nodeById.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        const std::uint64_t id = batch.identities[index].id.value;
        if (id == 0) {
            throw std::invalid_argument("loaded scene contains an invalid persistent ID");
        }
        if (!plan.nodeById.emplace(id, index).second) {
            throw std::invalid_argument("loaded scene contains duplicate persistent IDs");
        }
        plan.highestPersistentId = std::max(plan.highestPersistentId, id);
    }

    for (std::size_t index = 0; index < count; ++index) {
        const Object::PersistentObjectId parentId = batch.identities[index].parentId;
        if (!parentId.IsValid()) {
            continue;
        }
        const auto parent = plan.nodeById.find(parentId.value);
        if (parent == plan.nodeById.end()) {
            throw std::invalid_argument("loaded scene parent is outside the loaded batch");
        }
        plan.parents[index] = parent->second;
        ++plan.childCounts[parent->second];
    }

    std::vector<std::uint8_t> states(count, 0u);
    for (std::size_t start = 0; start < count; ++start) {
        std::size_t current = start;
        while (current != kNoParent && states[current] == 0u) {
            states[current] = 1u;
            current = plan.parents[current];
        }
        if (current != kNoParent && states[current] == 1u) {
            throw std::invalid_argument("loaded scene hierarchy contains a cycle");
        }
        current = start;
        while (current != kNoParent && states[current] == 1u) {
            states[current] = 2u;
            current = plan.parents[current];
        }
    }

    if (batch.hasActiveCamera) {
        const auto active = plan.nodeById.find(batch.activeCameraId.value);
        if (active == plan.nodeById.end()) {
            throw std::invalid_argument("loaded scene active camera is invalid");
        }
        plan.activeCamera = active->second;
    }
    return plan;
}

} // namespace

void Scene::CommitLoadedNodes(Detail::SceneLoadBatch batch)
{
    std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
    if (batch.sourceVersion == 6u) {
        if (batch.nodes.size() > std::numeric_limits<std::uint64_t>::max()
                - m_nextPersistentId) {
            throw std::overflow_error("scene persistent object IDs exhausted");
        }
        for (std::size_t index = 0; index < batch.identities.size(); ++index) {
            batch.identities[index].id = Object::PersistentObjectId{
                m_nextPersistentId + index};
            batch.identities[index].parentId = {};
        }
        batch.activeCameraId = {};
        batch.hasActiveCamera = false;
    }
    LoadPlan plan = ValidateDetachedBatch(batch);
    const SkyEnvironment environment = SanitizeSkyEnvironment(batch.environment);
    const std::size_t count = batch.nodes.size();
    for (const std::unique_ptr<Object::Node>& node : batch.nodes) {
        if (node == nullptr || node->m_scene != nullptr || node->m_graphState
            || node->m_parent != nullptr || !node->m_children.empty()
            || node->m_id != Object::kInvalidObjectId || node->m_handle.IsValid()
            || node->m_persistentId.IsValid()) {
            throw std::invalid_argument("loaded scene contains a non-detached node");
        }
    }
    if (plan.activeCamera != kNoParent
        && !batch.nodes[plan.activeCamera]->IsCamera()) {
        throw std::invalid_argument("loaded scene active camera is not a camera");
    }
    for (const std::unique_ptr<Object::Node>& liveNode : m_nodes) {
        if (!liveNode->m_persistentId.IsValid()) {
            continue;
        }
        if (plan.nodeById.contains(liveNode->m_persistentId.value)) {
            throw std::invalid_argument(
                "loaded scene persistent ID conflicts with the live scene");
        }
    }
    if (batch.sourceVersion == 7u) {
        for (const Detail::LoadedNodeIdentity& identity : batch.identities) {
            if (identity.id.value < m_nextPersistentId) {
                throw std::invalid_argument(
                    "loaded scene persistent ID is below the allocation watermark");
            }
        }
    }

    const Object::ObjectId nextEntityId =
        m_nextEntityId.load(std::memory_order_relaxed);
    const Object::ObjectId maxEntityId =
        std::numeric_limits<Object::ObjectId>::max();
    if (nextEntityId == 0 || count > maxEntityId - nextEntityId) {
        throw std::overflow_error("scene runtime object IDs exhausted");
    }
    if (plan.highestPersistentId == std::numeric_limits<std::uint64_t>::max()
        || m_nextPersistentId == 0) {
        throw std::overflow_error("scene persistent object IDs exhausted");
    }
    if (count > m_nodes.max_size() - m_nodes.size()) {
        throw std::length_error("loaded scene exceeds node storage capacity");
    }
    const std::size_t newSlotCount = count > m_freeObjectSlots.size()
        ? count - m_freeObjectSlots.size() : 0u;
    if (newSlotCount > std::numeric_limits<std::uint32_t>::max()
            - m_objectSlots.size()
        || newSlotCount > m_objectSlots.max_size() - m_objectSlots.size()) {
        throw std::length_error("scene object handle capacity exhausted");
    }

    std::vector<Object::Node*> rawNodes;
    rawNodes.reserve(count);
    for (const std::unique_ptr<Object::Node>& node : batch.nodes) {
        rawNodes.push_back(node.get());
    }
    for (std::size_t index = 0; index < count; ++index) {
        batch.nodes[index]->m_children.reserve(plan.childCounts[index]);
    }

    std::vector<std::unique_ptr<Object::Node>> committedNodes;
    committedNodes.reserve(m_nodes.size() + count);
    std::vector<Detail::SceneObjectSlot> committedSlots = m_objectSlots;
    committedSlots.reserve(m_objectSlots.size() + newSlotCount);
    std::vector<std::uint32_t> committedFreeSlots = m_freeObjectSlots;

    for (std::size_t index = 0; index < count; ++index) {
        Object::Node* node = rawNodes[index];
        node->m_scene = this;
        node->m_graphState = m_graphState;
        node->m_id = nextEntityId + index;
        node->m_persistentId = batch.identities[index].id;
        std::uint32_t slotIndex = 0;
        if (!committedFreeSlots.empty()) {
            slotIndex = committedFreeSlots.back();
            committedFreeSlots.pop_back();
        } else {
            slotIndex = static_cast<std::uint32_t>(committedSlots.size());
            committedSlots.emplace_back();
        }
        Detail::SceneObjectSlot& slot = committedSlots[slotIndex];
        slot.node = node;
        slot.state = Detail::SceneObjectState::Live;
        node->m_handle = Object::ObjectHandle{
            m_sceneIdentity, slotIndex, slot.generation};
    }
    for (std::unique_ptr<Object::Node>& node : m_nodes) {
        committedNodes.push_back(std::move(node));
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (plan.parents[index] != kNoParent) {
            Object::Node* parent = rawNodes[plan.parents[index]];
            rawNodes[index]->m_parent = parent;
            parent->m_children.push_back(rawNodes[index]);
        }
        rawNodes[index]->m_worldDirty = true;
        committedNodes.push_back(std::move(batch.nodes[index]));
    }

    m_nodes.swap(committedNodes);
    m_objectSlots.swap(committedSlots);
    m_freeObjectSlots.swap(committedFreeSlots);
    m_nextEntityId.store(nextEntityId + count, std::memory_order_relaxed);
    m_nextPersistentId = std::max(m_nextPersistentId,
                                  plan.highestPersistentId + 1u);
    m_environmentSettings.sky = environment;
    if (plan.activeCamera != kNoParent) {
        m_activeCamera = rawNodes[plan.activeCamera]->m_handle;
    } else if (ResolveLiveLocked(m_activeCamera) == nullptr) {
        for (Object::Node* node : rawNodes) {
            if (node->IsCamera()) {
                m_activeCamera = node->m_handle;
                break;
            }
        }
    }
}

} // namespace Concord
