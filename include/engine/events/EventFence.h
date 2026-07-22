#ifndef CONCORD_EVENTFENCE_H
#define CONCORD_EVENTFENCE_H

#include "Concord/CExport.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace Concord {

namespace EventDetail {
class EventBusCore;
}

/** @brief Observable non-blocking completion state for an event queue fence. */
enum class EventFenceResult : std::uint8_t {
    Pending,
    Dispatched,
    Retired,
    QueueFull,
    Inactive,
};

/**
 * @brief Asynchronous marker ordered after previously accepted event publications.
 *
 * Poll IsReady and Result from a later update or another thread. The type does
 * not expose a blocking wait, so it is safe to retain on the simulation
 * coordinator. Shutdown completes every queued marker with Retired.
 */
class CENGINE_API EventFence {
public:
    /** @brief Constructs an already-inactive fence. */
    EventFence() noexcept = default;

    /** @brief Returns true once this marker reached dispatch or was rejected/retired. */
    bool IsReady() const noexcept;

    /** @brief Returns the current non-blocking completion state. */
    EventFenceResult Result() const noexcept;

private:
    friend class EventDetail::EventBusCore;

    explicit EventFence(std::shared_ptr<std::atomic<EventFenceResult>> result) noexcept;

    std::shared_ptr<std::atomic<EventFenceResult>> m_result;
};

} // namespace Concord

#endif // CONCORD_EVENTFENCE_H
