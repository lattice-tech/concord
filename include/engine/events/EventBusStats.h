#ifndef CONCORD_EVENTBUSSTATS_H
#define CONCORD_EVENTBUSSTATS_H

#include <cstdint>

namespace Concord {

/** @brief Lifecycle state of the process-wide event bus generation. */
enum class EventBusStatus : std::uint8_t {
    Inactive,
    Active,
    ShuttingDown,
};

/** @brief Thread-safe snapshot of event delivery and queue pressure counters. */
struct EventBusStats {
    /** Current bus lifecycle state. */
    EventBusStatus status = EventBusStatus::Inactive;
    /** EngineLoop generation that owns these counters, or zero before first activation. */
    std::uint64_t generation = 0;
    /** Queue entries waiting for a later simulation turn, including fence markers. */
    std::uint64_t queued = 0;
    /** Maximum number of event and fence entries accepted by the queue. */
    std::uint64_t capacity = 0;
    /** Greatest observed queue depth during this generation. */
    std::uint64_t highWatermark = 0;
    /** Event publications accepted during this generation. */
    std::uint64_t accepted = 0;
    /** Event publications rejected while this generation was observable. */
    std::uint64_t rejected = 0;
    /** Accepted events whose dispatch turn completed. */
    std::uint64_t dispatched = 0;
    /** Handler invocations completed, including invocations that threw. */
    std::uint64_t delivered = 0;
    /** Handler invocations disabled after throwing an exception. */
    std::uint64_t handlerFailures = 0;
};

} // namespace Concord

#endif // CONCORD_EVENTBUSSTATS_H
