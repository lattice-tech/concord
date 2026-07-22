#ifndef CONCORD_EVENTPUBLISHRESULT_H
#define CONCORD_EVENTPUBLISHRESULT_H

#include <cstdint>

namespace Concord {

/** @brief Observable result of attempting to enqueue an event. */
enum class EventPublishResult : std::uint8_t {
    Published,
    QueueFull,
    Inactive,
    ShuttingDown,
};

} // namespace Concord

#endif // CONCORD_EVENTPUBLISHRESULT_H
