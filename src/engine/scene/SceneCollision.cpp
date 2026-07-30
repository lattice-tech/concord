#include "engine/scene/Scene.h"

#include "engine/collision/AabbOps.h"
#include "engine/collision/SweepAndPrune.h"
#include "engine/object/Collider.h"
#include "engine/render/frame/WorldSnapshot.h"
#include "engine/spatial/SpatialProxy.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Concord {
namespace {

bool TickCurrent(const std::shared_ptr<SceneGraphState>& state,
                 std::uint64_t generation)
{
    return state->alive.load(std::memory_order_acquire)
        && state->active.load(std::memory_order_acquire)
        && state->activationGeneration.load(std::memory_order_acquire) == generation;
}

/** Packs a dense frame index into SpatialProxy::userData for pair recovery. */
constexpr std::uint64_t PackIndex(std::size_t index) noexcept
{
    return static_cast<std::uint64_t>(index);
}

/**
 * Syncs the persistent tree with this frame's colliders under the graph lock.
 * Inserts new proxies, Moves existing ones (fat-box early-out inside the tree),
 * and Removes handles that left the set. userData is overwritten with the dense
 * frame index so QueryOverlap can emit OverlapPair indices.
 */
void SyncCollisionTreeLocked(
    Spatial::DynamicAabbTree& tree,
    std::unordered_map<Object::ObjectHandle, Spatial::SpatialId>& proxies,
    const std::vector<Object::ObjectHandle>& colliderHandles,
    const std::vector<Collision::Aabb>& bounds,
    const std::vector<std::uint32_t>& layers)
{
    std::unordered_set<Object::ObjectHandle> live;
    live.reserve(colliderHandles.size());

    for (std::size_t i = 0; i < colliderHandles.size(); ++i) {
        const Object::ObjectHandle handle = colliderHandles[i];
        live.insert(handle);
        if (!Collision::IsValidAabb(bounds[i])) {
            const auto found = proxies.find(handle);
            if (found != proxies.end()) {
                tree.Remove(found->second);
                proxies.erase(found);
            }
            continue;
        }

        Spatial::SpatialProxy proxy;
        proxy.bounds = bounds[i];
        proxy.userData = PackIndex(i);
        proxy.layer = layers[i] == 0 ? 1u : layers[i];

        const auto found = proxies.find(handle);
        if (found == proxies.end()) {
            const Spatial::SpatialId id = tree.Insert(proxy);
            if (id.IsValid()) {
                proxies.emplace(handle, id);
            }
            continue;
        }

        if (!tree.IsAlive(found->second)) {
            const Spatial::SpatialId id = tree.Insert(proxy);
            if (id.IsValid()) {
                found->second = id;
            } else {
                proxies.erase(found);
            }
            continue;
        }

        // Move updates the fat AABB (no-op topology when still contained).
        tree.Move(found->second, bounds[i]);
        // Dense frame index / layer can change without bounds motion.
        if (!tree.SetProxyMeta(found->second, PackIndex(i), proxy.layer)) {
            proxies.erase(found);
        }
    }

    for (auto it = proxies.begin(); it != proxies.end();) {
        if (live.contains(it->first)) {
            ++it;
            continue;
        }
        tree.Remove(it->second);
        it = proxies.erase(it);
    }
}

std::vector<Collision::OverlapPair> QueryTreePairs(
    const Spatial::DynamicAabbTree& tree,
    const std::vector<Object::ObjectHandle>& colliderHandles,
    const std::vector<Collision::Aabb>& bounds,
    const std::unordered_map<Object::ObjectHandle, Spatial::SpatialId>& proxies)
{
    std::vector<Collision::OverlapPair> overlaps;
    std::unordered_set<std::uint64_t> seen;
    for (std::size_t i = 0; i < colliderHandles.size(); ++i) {
        const auto found = proxies.find(colliderHandles[i]);
        if (found == proxies.end() || !found->second.IsValid()) {
            continue;
        }
        if (!Collision::IsValidAabb(bounds[i])) {
            continue;
        }
        tree.QueryOverlap(bounds[i], {}, [&](Spatial::SpatialId id,
                                             const Spatial::SpatialProxy& proxy) {
            if (id == found->second) {
                return;
            }
            const std::size_t j = static_cast<std::size_t>(proxy.userData);
            if (j <= i || j >= colliderHandles.size()) {
                return;
            }
            const std::uint64_t key =
                (static_cast<std::uint64_t>(i) << 32) | static_cast<std::uint64_t>(j);
            if (!seen.insert(key).second) {
                return;
            }
            overlaps.emplace_back(i, j);
        });
    }
    std::sort(overlaps.begin(), overlaps.end());
    return overlaps;
}

} // namespace

TaskGraphStats Scene::ResolveCollisions(
    const std::vector<Object::ObjectHandle>& handles,
    WorldSnapshot& snapshot, const std::shared_ptr<EngineLoop>& loop,
    std::uint64_t activationGeneration)
{
    using Clock = std::chrono::steady_clock;
    std::vector<Object::ObjectHandle> colliderHandles;
    std::vector<Collision::Aabb> bounds;
    std::vector<std::uint32_t> layers;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        for (Object::ObjectHandle handle : handles) {
            Object::Node* node = ResolveLiveLocked(handle);
            if (node == nullptr) {
                continue;
            }
            std::vector<Object::Collider*> colliders;
            node->CollectColliders(colliders);
            for (Object::Collider* collider : colliders) {
                colliderHandles.push_back(collider->Handle());
                bounds.push_back(collider->WorldAabb());
                layers.push_back(collider->Layer());
            }
        }
        // Tree sync must happen while handles/bounds are coherent with the graph.
        SyncCollisionTreeLocked(m_collisionTree, m_collisionProxies, colliderHandles,
                                bounds, layers);
    }
    snapshot.colliderCount = static_cast<std::uint32_t>(colliderHandles.size());

    // Snapshot of proxy ids for the worker query (tree is not mutated until next
    // ResolveCollisions; concurrent Tick is serialized by activationGeneration).
    std::unordered_map<Object::ObjectHandle, Spatial::SpatialId> proxySnapshot;
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        proxySnapshot = m_collisionProxies;
    }

    std::vector<Collision::OverlapPair> overlapPairs;
    TaskGraph graph;
    graph.Add("Scene.CollisionBroadphase", [&] {
        const auto start = Clock::now();
        overlapPairs = QueryTreePairs(m_collisionTree, colliderHandles, bounds,
                                      proxySnapshot);
        snapshot.collisionMs = std::chrono::duration<float, std::milli>(
            Clock::now() - start).count();
    });
    TaskGraphStats stats = loop->RunTaskGraph(std::move(graph));
    if (!TickCurrent(m_graphState, activationGeneration)) {
        return stats;
    }

    const std::size_t count = colliderHandles.size();
    std::vector<std::vector<Object::ObjectHandle>> current(count);
    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        for (const Collision::OverlapPair& pair : overlapPairs) {
            auto* first = dynamic_cast<Object::Collider*>(
                ResolveLiveLocked(colliderHandles[pair.first]));
            auto* second = dynamic_cast<Object::Collider*>(
                ResolveLiveLocked(colliderHandles[pair.second]));
            if (first == nullptr || second == nullptr
                || !first->CanInteractWith(*second)) {
                continue;
            }
            current[pair.first].push_back(second->Handle());
            current[pair.second].push_back(first->Handle());
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        std::vector<Object::ObjectHandle> before;
        {
            std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
            auto* collider = dynamic_cast<Object::Collider*>(
                ResolveLiveLocked(colliderHandles[i]));
            if (collider == nullptr) {
                continue;
            }
            before.reserve(collider->m_overlapping.size());
            for (Object::Collider* other : collider->m_overlapping) {
                if (ResolveLiveLocked(other->Handle()) == other) {
                    before.push_back(other->Handle());
                }
            }
        }

        for (Object::ObjectHandle otherHandle : current[i]) {
            if (std::find(before.begin(), before.end(), otherHandle) != before.end()) {
                continue;
            }
            {
                std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
                auto* collider = dynamic_cast<Object::Collider*>(
                    ResolveLiveLocked(colliderHandles[i]));
                auto* other = dynamic_cast<Object::Collider*>(
                    ResolveLiveLocked(otherHandle));
                if (collider != nullptr && other != nullptr) {
                    const std::function<void(Object::Collider&)> callback =
                        collider->m_onEnter;
                    if (callback) {
                        callback(*other);
                    }
                }
            }
            if (!TickCurrent(m_graphState, activationGeneration)) {
                return stats;
            }
        }

        for (Object::ObjectHandle otherHandle : before) {
            if (std::find(current[i].begin(), current[i].end(), otherHandle)
                != current[i].end()) {
                continue;
            }
            {
                std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
                auto* collider = dynamic_cast<Object::Collider*>(
                    ResolveLiveLocked(colliderHandles[i]));
                auto* other = dynamic_cast<Object::Collider*>(
                    ResolveLiveLocked(otherHandle));
                if (collider != nullptr && other != nullptr) {
                    const std::function<void(Object::Collider&)> callback =
                        collider->m_onExit;
                    if (callback) {
                        callback(*other);
                    }
                }
            }
            if (!TickCurrent(m_graphState, activationGeneration)) {
                return stats;
            }
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_graphState->mutex);
        for (std::size_t i = 0; i < count; ++i) {
            auto* collider = dynamic_cast<Object::Collider*>(
                ResolveLiveLocked(colliderHandles[i]));
            if (collider == nullptr) {
                continue;
            }
            collider->m_overlapping.clear();
            for (Object::ObjectHandle otherHandle : current[i]) {
                auto* other = dynamic_cast<Object::Collider*>(
                    ResolveLiveLocked(otherHandle));
                if (other != nullptr) {
                    collider->m_overlapping.push_back(other);
                }
            }
        }
    }
    return stats;
}

} // namespace Concord
