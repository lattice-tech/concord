#ifndef CONCORD_ECSWORLD_H
#define CONCORD_ECSWORLD_H

#include "engine/ecs/Entity.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Concord::Ecs {

/**
 * @brief Generation-safe sparse-set ECS component world.
 *
 * Structural mutation is serialized by the world lock. Systems may run in
 * parallel when their declared component access does not conflict. Each()
 * snapshots entity handles and component ownership before invoking user code,
 * so callbacks never execute while a world or storage lock is held.
 */
class World {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    /** Allocates an entity, reusing a retired index with a new generation. */
    Entity Create()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::uint32_t index = 0;
        if (m_free.empty()) {
            index = static_cast<std::uint32_t>(m_generations.size());
            m_generations.push_back(1);
            m_alive.push_back(true);
        } else {
            index = m_free.back();
            m_free.pop_back();
            m_alive[index] = true;
        }
        return Entity{index, m_generations[index]};
    }

    /** Destroys an entity and removes every attached component. */
    bool Destroy(Entity entity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!IsAliveLocked(entity)) {
            return false;
        }
        for (auto& [type, storage] : m_storages) {
            std::lock_guard<std::mutex> storageLock(storage->mutex);
            storage->Erase(entity.index);
        }
        m_alive[entity.index] = false;
        ++m_generations[entity.index];
        m_free.push_back(entity.index);
        return true;
    }

    /** True when the index and generation still name a live entity. */
    bool IsAlive(Entity entity) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return IsAliveLocked(entity);
    }

    /** Adds or replaces component T on entity. */
    template <typename T, typename... Args>
    T& Emplace(Entity entity, Args&&... args)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!IsAliveLocked(entity)) {
            throw std::invalid_argument("cannot attach a component to a stale entity");
        }
        Storage<T>& storage = StorageFor<T>();
        std::lock_guard<std::mutex> storageLock(storage.mutex);
        return storage.Emplace(entity.index, std::forward<Args>(args)...);
    }

    /** Returns component T or null when absent/stale. Valid until structural mutation. */
    template <typename T>
    T* Get(Entity entity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!IsAliveLocked(entity)) {
            return nullptr;
        }
        const auto it = m_storages.find(std::type_index(typeid(T)));
        if (it == m_storages.end()) {
            return nullptr;
        }
        Storage<T>* storage = static_cast<Storage<T>*>(it->second.get());
        std::lock_guard<std::mutex> storageLock(storage->mutex);
        return storage->Get(entity.index);
    }

    template <typename T>
    const T* Get(Entity entity) const
    {
        return const_cast<World*>(this)->Get<T>(entity);
    }

    /** Removes component T when present. */
    template <typename T>
    bool Remove(Entity entity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!IsAliveLocked(entity)) {
            return false;
        }
        const auto it = m_storages.find(std::type_index(typeid(T)));
        if (it == m_storages.end()) {
            return false;
        }
        std::lock_guard<std::mutex> storageLock(it->second->mutex);
        return it->second->Erase(entity.index);
    }

    /**
     * Visits a snapshot of all entities containing T in dense storage order.
     *
     * Snapshot membership and generations are captured atomically with respect
     * to structural mutation, then all engine locks are released before the
     * callback runs. A callback may therefore call Create, Destroy, Emplace,
     * Get, Remove, or Each without lock inversion. Components removed or
     * replaced after capture remain alive for this pass and may still be
     * visited with an Entity that is now stale; newly attached components are
     * visible on the next pass. The component reference is valid only for the
     * duration of the callback and must not be retained.
     */
    template <typename T, typename Function>
    void Each(Function&& function)
    {
        struct SnapshotEntry {
            Entity entity;
            std::shared_ptr<T> component;
        };

        std::vector<SnapshotEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_storages.find(std::type_index(typeid(T)));
            if (it == m_storages.end()) {
                return;
            }
            Storage<T>* storage = static_cast<Storage<T>*>(it->second.get());
            std::lock_guard<std::mutex> storageLock(storage->mutex);
            snapshot.reserve(storage->values.size());
            for (std::size_t dense = 0; dense < storage->values.size(); ++dense) {
                const std::uint32_t index = storage->entities[dense];
                snapshot.push_back(SnapshotEntry{
                    Entity{index, m_generations[index]},
                    storage->values[dense],
                });
            }
        }

        for (SnapshotEntry& entry : snapshot) {
            function(entry.entity, *entry.component);
        }
    }

private:
    struct IStorage {
        virtual ~IStorage() = default;
        virtual bool Erase(std::uint32_t entity) = 0;
        std::mutex mutex;
    };

    template <typename T>
    struct Storage final : IStorage {
        template <typename... Args>
        T& Emplace(std::uint32_t entity, Args&&... args)
        {
            auto component = std::make_shared<T>(std::forward<Args>(args)...);
            const auto found = sparse.find(entity);
            if (found != sparse.end()) {
                values[found->second] = std::move(component);
                return *values[found->second];
            }
            const std::size_t dense = values.size();
            values.push_back(std::move(component));
            try {
                entities.push_back(entity);
                try {
                    sparse.emplace(entity, dense);
                } catch (...) {
                    entities.pop_back();
                    throw;
                }
            } catch (...) {
                values.pop_back();
                throw;
            }
            return *values.back();
        }

        T* Get(std::uint32_t entity)
        {
            const auto it = sparse.find(entity);
            return it == sparse.end() ? nullptr : values[it->second].get();
        }

        bool Erase(std::uint32_t entity) override
        {
            const auto it = sparse.find(entity);
            if (it == sparse.end()) {
                return false;
            }
            const std::size_t dense = it->second;
            const std::size_t last = values.size() - 1;
            if (dense != last) {
                values[dense] = std::move(values[last]);
                entities[dense] = entities[last];
                sparse[entities[dense]] = dense;
            }
            values.pop_back();
            entities.pop_back();
            sparse.erase(it);
            return true;
        }

        std::vector<std::shared_ptr<T>> values;
        std::vector<std::uint32_t> entities;
        std::unordered_map<std::uint32_t, std::size_t> sparse;
    };

    template <typename T>
    Storage<T>& StorageFor()
    {
        const std::type_index type(typeid(T));
        auto [it, inserted] = m_storages.try_emplace(type);
        if (inserted) {
            it->second = std::make_unique<Storage<T>>();
        }
        return *static_cast<Storage<T>*>(it->second.get());
    }

    bool IsAliveLocked(Entity entity) const noexcept
    {
        return entity.index < m_generations.size() && m_alive[entity.index]
            && m_generations[entity.index] == entity.generation;
    }

    mutable std::mutex m_mutex;
    std::vector<std::uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::vector<std::uint32_t> m_free;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> m_storages;
};

} // namespace Concord::Ecs

#endif // CONCORD_ECSWORLD_H
