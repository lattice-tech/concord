#include "engine/events/EventSubscriptionGroup.h"

#include <utility>

namespace Concord {

EventSubscriptionGroup::~EventSubscriptionGroup()
{
    Clear();
}

EventSubscriptionGroup::EventSubscriptionGroup(EventSubscriptionGroup&& other) noexcept
    : m_subscriptions(std::move(other.m_subscriptions))
{
}

EventSubscriptionGroup& EventSubscriptionGroup::operator=(EventSubscriptionGroup&& other) noexcept
{
    if (this != &other) {
        Clear();
        m_subscriptions = std::move(other.m_subscriptions);
    }
    return *this;
}

bool EventSubscriptionGroup::Add(EventSubscription subscription)
{
    if (!subscription) {
        return false;
    }
    m_subscriptions.push_back(std::move(subscription));
    return true;
}

void EventSubscriptionGroup::Clear()
{
    while (!m_subscriptions.empty()) {
        m_subscriptions.back().Reset();
        m_subscriptions.pop_back();
    }
}

std::size_t EventSubscriptionGroup::Size() const noexcept
{
    return m_subscriptions.size();
}

bool EventSubscriptionGroup::Empty() const noexcept
{
    return m_subscriptions.empty();
}

} // namespace Concord
