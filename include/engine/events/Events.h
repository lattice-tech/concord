#ifndef CONCORD_EVENTS_H
#define CONCORD_EVENTS_H

#include "engine/events/EventBusCore.h"

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Concord {

/** @brief Process-wide typed notification API dispatched by the simulation coordinator. */
class Events {
public:
    /**
     * @brief Moves or copies one event into the bounded process queue.
     * @return Published on acceptance, otherwise the observable rejection reason.
     */
    template <typename T>
        requires std::constructible_from<std::remove_cvref_t<T>, T>
    static EventPublishResult Publish(T&& event)
    {
        using Event = std::remove_cvref_t<T>;
        auto payload = std::make_shared<Event>(std::forward<T>(event));
        return EventDetail::EventBusCore::Publish(
            EventDetail::TypeDescriptor<Event>(), std::move(payload));
    }

    /**
     * @brief Enqueues a non-blocking marker after previously accepted events.
     *
     * Dispatched means every earlier accepted event completed its dispatch.
     * Publications from handlers or other threads after this call are not part
     * of the marker. Shutdown completes an accepted pending marker as Retired.
     */
    static EventFence Fence()
    {
        return EventDetail::EventBusCore::Fence();
    }

    /** @brief Returns a thread-safe snapshot of the current or most recently retired generation. */
    static EventBusStats Stats() noexcept
    {
        return EventDetail::EventBusCore::Stats();
    }

    /**
     * @brief Registers a handler for exactly event type `T`.
     *
     * Handlers run in registration order on the simulation coordinator. The
     * callback receives a const reference, so one move-only payload can be
     * observed by every matching handler without copies.
     */
    template <typename T, typename Handler>
        requires std::invocable<Handler&, const T&>
              && std::copy_constructible<std::remove_cvref_t<Handler>>
    static EventSubscription Subscribe(Handler&& handler)
    {
        using Event = std::remove_cvref_t<T>;
        using StoredHandler = std::remove_cvref_t<Handler>;
        return EventDetail::EventBusCore::Subscribe(
            EventDetail::TypeDescriptor<Event>(),
            [callback = StoredHandler(std::forward<Handler>(handler))](const void* payload) mutable {
                std::invoke(callback, *static_cast<const Event*>(payload));
            });
    }
};

} // namespace Concord

#endif // CONCORD_EVENTS_H
