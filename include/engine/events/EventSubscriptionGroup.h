#ifndef CONCORD_EVENTSUBSCRIPTIONGROUP_H
#define CONCORD_EVENTSUBSCRIPTIONGROUP_H

#include "Concord/CExport.h"
#include "engine/events/EventSubscription.h"

#include <cstddef>
#include <vector>

namespace Concord {

/** @brief Move-only RAII owner that cancels a group of event subscriptions together. */
class CENGINE_API EventSubscriptionGroup {
public:
    /** @brief Constructs an empty group. */
    EventSubscriptionGroup() = default;

    /** @brief Cancels every subscription still owned by the group. */
    ~EventSubscriptionGroup();

    EventSubscriptionGroup(const EventSubscriptionGroup&) = delete;
    EventSubscriptionGroup& operator=(const EventSubscriptionGroup&) = delete;

    /** @brief Transfers all subscriptions from `other`. */
    EventSubscriptionGroup(EventSubscriptionGroup&& other) noexcept;

    /** @brief Cancels current subscriptions and takes those owned by `other`. */
    EventSubscriptionGroup& operator=(EventSubscriptionGroup&& other) noexcept;

    /**
     * @brief Takes ownership of `subscription` when it is active.
     * @return True when a live subscription was added.
     */
    bool Add(EventSubscription subscription);

    /** @brief Cancels and releases every owned subscription. */
    void Clear();

    /** @brief Returns the number of subscription handles currently stored. */
    std::size_t Size() const noexcept;

    /** @brief Returns true when the group owns no subscription handles. */
    bool Empty() const noexcept;

private:
    std::vector<EventSubscription> m_subscriptions;
};

} // namespace Concord

#endif // CONCORD_EVENTSUBSCRIPTIONGROUP_H
