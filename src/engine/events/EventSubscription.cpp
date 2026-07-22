#include "engine/events/EventSubscription.h"

#include "engine/events/EventBusCore.h"

#include <utility>

namespace Concord {

EventSubscription::EventSubscription(std::uint64_t generation, std::uint64_t id) noexcept
    : m_generation(generation)
    , m_id(id)
{
}

EventSubscription::~EventSubscription()
{
    Reset();
}

EventSubscription::EventSubscription(EventSubscription&& other) noexcept
    : m_generation(std::exchange(other.m_generation, 0))
    , m_id(std::exchange(other.m_id, 0))
{
}

EventSubscription& EventSubscription::operator=(EventSubscription&& other) noexcept
{
    if (this != &other) {
        Reset();
        m_generation = std::exchange(other.m_generation, 0);
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

void EventSubscription::Reset()
{
    const std::uint64_t generation = std::exchange(m_generation, 0);
    const std::uint64_t id = std::exchange(m_id, 0);
    if (generation != 0 && id != 0) {
        EventDetail::EventBusCore::Unsubscribe(generation, id);
    }
}

EventSubscription::operator bool() const noexcept
{
    return m_generation != 0 && m_id != 0
        && EventDetail::EventBusCore::IsSubscribed(m_generation, m_id);
}

} // namespace Concord
