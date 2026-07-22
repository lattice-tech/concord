#include "engine/events/EventFence.h"

#include <utility>

namespace Concord {

EventFence::EventFence(std::shared_ptr<std::atomic<EventFenceResult>> result) noexcept
    : m_result(std::move(result))
{
}

bool EventFence::IsReady() const noexcept
{
    return Result() != EventFenceResult::Pending;
}

EventFenceResult EventFence::Result() const noexcept
{
    return m_result
        ? m_result->load(std::memory_order_acquire)
        : EventFenceResult::Inactive;
}

} // namespace Concord
