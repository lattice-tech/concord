#ifndef CONCORD_EVENTSUBSCRIPTION_H
#define CONCORD_EVENTSUBSCRIPTION_H

#include "Concord/CExport.h"

#include <cstdint>

namespace Concord {

namespace EventDetail {
struct EventTypeDescriptor;
class EventBusCore;
}

/**
 * @brief Move-only ownership handle for a typed event handler.
 *
 * Reset and destruction cancel callbacks that have not started and wait for an
 * in-flight callback on another thread. A handle from a retired EngineLoop
 * generation can never affect a later generation.
 */
class CENGINE_API EventSubscription {
public:
    /** @brief Constructs an empty subscription. */
    EventSubscription() noexcept = default;

    /** @brief Cancels the owned handler. */
    ~EventSubscription();

    EventSubscription(const EventSubscription&) = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;

    /** @brief Transfers ownership from `other`. */
    EventSubscription(EventSubscription&& other) noexcept;

    /** @brief Replaces this subscription with the one owned by `other`. */
    EventSubscription& operator=(EventSubscription&& other) noexcept;

    /** @brief Cancels the handler immediately; a no-op when already empty. */
    void Reset();

    /** @brief Returns true while this token names an active handler. */
    explicit operator bool() const noexcept;

private:
    friend class EventDetail::EventBusCore;

    EventSubscription(std::uint64_t generation, std::uint64_t id) noexcept;

    std::uint64_t m_generation = 0;
    std::uint64_t m_id = 0;
};

} // namespace Concord

#endif // CONCORD_EVENTSUBSCRIPTION_H
