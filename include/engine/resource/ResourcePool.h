#ifndef CONCORD_RESOURCEPOOL_H
#define CONCORD_RESOURCEPOOL_H

#include "engine/resource/ResourceHandle.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Concord {

/**
 * A slot map that owns a set of T resources addressed by generation-checked
 * handles (see ResourceHandle).
 *
 * Freed slots are recycled through a free list, so the backing storage does
 * not grow without bound as resources come and go, and every recycle bumps
 * the slot's generation so any handle to the previous occupant stops
 * validating. Add/Get/Remove are all O(1) and never invalidate a handle to a
 * different slot.
 *
 * This container is deliberately engine-agnostic: the render backend uses it
 * to track GPU meshes, but nothing here is mesh-specific, so any other
 * subsystem can reuse it for its own resource kind by pairing it with a fresh
 * `Tag`. It performs no locking of its own; a pool shared across threads is
 * synchronized by its owner (the render backend only ever touches its pools
 * from the render thread).
 */
template <typename T, typename Tag>
class ResourcePool {
public:
    using Handle = ResourceHandle<Tag>;

    /** Stores `value` in a recycled or freshly grown slot and returns its handle. */
    Handle Add(T value)
    {
        std::uint32_t index;
        if (!m_freeList.empty()) {
            index = m_freeList.back();
            m_freeList.pop_back();
            m_slots[index].value = std::move(value);
            m_slots[index].occupied = true;
        } else {
            index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.push_back(Slot{std::move(value), 1, true});
        }
        return Handle{index, m_slots[index].generation};
    }

    /** Pointer to `handle`'s resource, or nullptr when the handle is stale or invalid. */
    T* Get(Handle handle) noexcept
    {
        Slot* slot = ResolveSlot(handle);
        return slot ? &slot->value : nullptr;
    }

    /** Const overload of Get. */
    const T* Get(Handle handle) const noexcept
    {
        const Slot* slot = ResolveSlot(handle);
        return slot ? &slot->value : nullptr;
    }

    /**
     * Frees `handle`'s slot for reuse and bumps its generation so every
     * outstanding handle to it becomes stale.
     * @param outValue When non-null, receives the removed resource so the
     *        caller can run whatever teardown it needs (e.g. release a GPU
     *        buffer) before it is dropped.
     * @return false, doing nothing, when `handle` is already stale or invalid.
     */
    bool Remove(Handle handle, T* outValue = nullptr)
    {
        Slot* slot = ResolveSlot(handle);
        if (!slot) {
            return false;
        }
        if (outValue) {
            *outValue = std::move(slot->value);
        }
        slot->value = T{};
        slot->occupied = false;
        ++slot->generation;
        m_freeList.push_back(handle.index);
        return true;
    }

    /** Invokes `fn(Handle, T&)` for every live resource, in slot order. */
    template <typename Fn>
    void ForEach(Fn&& fn)
    {
        for (std::uint32_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].occupied) {
                fn(Handle{i, m_slots[i].generation}, m_slots[i].value);
            }
        }
    }

    /** Drops every slot; all outstanding handles are stale afterwards. */
    void Clear() noexcept
    {
        m_slots.clear();
        m_freeList.clear();
    }

private:
    struct Slot {
        T value{};
        std::uint32_t generation = 1; // fresh slots start at 1; 0 is the invalid handle
        bool occupied = false;
    };

    Slot* ResolveSlot(Handle handle) noexcept
    {
        if (!handle.IsValid() || handle.index >= m_slots.size()) {
            return nullptr;
        }
        Slot& slot = m_slots[handle.index];
        if (!slot.occupied || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    const Slot* ResolveSlot(Handle handle) const noexcept
    {
        if (!handle.IsValid() || handle.index >= m_slots.size()) {
            return nullptr;
        }
        const Slot& slot = m_slots[handle.index];
        if (!slot.occupied || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeList;
};

} // namespace Concord

#endif // CONCORD_RESOURCEPOOL_H
