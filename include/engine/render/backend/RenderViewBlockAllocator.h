#ifndef CONCORD_RENDERVIEWBLOCKALLOCATOR_H
#define CONCORD_RENDERVIEWBLOCKALLOCATOR_H

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Concord {

/** Allocates and recycles fixed-size contiguous bgfx view-id blocks. */
class RenderViewBlockAllocator {
public:
    /** Sets the fixed block contract and clears previous allocation state. */
    void Configure(std::uint32_t blockSize, std::uint32_t capacity, std::uint32_t invalid)
    {
        Reset();
        m_blockSize = blockSize;
        m_capacity = capacity;
        m_invalid = invalid;
    }

    /** Returns the first id in a block, or the configured invalid sentinel. */
    std::uint32_t Acquire()
    {
        if (!m_free.empty()) {
            const std::uint32_t first = m_free.back();
            m_free.pop_back();
            m_live.insert(first);
            return first;
        }
        if (m_blockSize == 0 || m_next > m_capacity || m_blockSize > m_capacity - m_next
            || m_next >= m_invalid || m_blockSize > m_invalid - m_next) {
            return m_invalid;
        }
        const std::uint32_t first = m_next;
        m_next += m_blockSize;
        m_live.insert(first);
        return first;
    }

    /** Returns a live block for reuse; rejects arbitrary and duplicate releases. */
    bool Release(std::uint32_t first)
    {
        if (m_live.erase(first) == 0) {
            return false;
        }
        m_free.push_back(first);
        return true;
    }

    /** Drops all allocation state after the backend shuts down. */
    void Reset()
    {
        m_next = 0;
        m_free.clear();
        m_live.clear();
    }

private:
    std::uint32_t m_next = 0;
    std::uint32_t m_blockSize = 0;
    std::uint32_t m_capacity = 0;
    std::uint32_t m_invalid = 0;
    std::vector<std::uint32_t> m_free;
    std::unordered_set<std::uint32_t> m_live;
};

} // namespace Concord

#endif // CONCORD_RENDERVIEWBLOCKALLOCATOR_H
