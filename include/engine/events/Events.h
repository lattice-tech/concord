#ifndef CONCORD_EVENTS_H
#define CONCORD_EVENTS_H

#include "engine/events/EventBusCore.h"

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace Concord {

/** @brief Process-wide typed notification API dispatched by the EngineLoop. */
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
     * @brief Registers a handler for exactly event type `T`.
     *
     * Handlers run in registration order on the EngineLoop thread. The callback
     * receives a const reference, so one move-only payload can be observed by
     * every matching handler without copies.
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
