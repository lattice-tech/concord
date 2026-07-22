#ifndef CONCORD_EVENTBUSCORE_H
#define CONCORD_EVENTBUSCORE_H

#include "Concord/CExport.h"
#include "engine/events/EventPublishResult.h"
#include "engine/events/EventSubscription.h"
#include "engine/events/EventTypeId.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace Concord::EventDetail {

/** @brief Non-template CEngine core shared by all typed API instantiations. */
class CENGINE_API EventBusCore {
public:
    using ErasedHandler = std::function<void(const void*)>;

    static EventPublishResult Publish(const EventTypeDescriptor& type,
                                      std::shared_ptr<const void> payload);
    static EventSubscription Subscribe(const EventTypeDescriptor& type, ErasedHandler handler);
    static void Unsubscribe(std::uint64_t generation, std::uint64_t id);
    static bool IsSubscribed(std::uint64_t generation, std::uint64_t id) noexcept;

    static std::uint64_t Activate(std::function<void()> wake);
    static void Shutdown(std::uint64_t generation);
    static void Dispatch(std::uint64_t generation);
};

} // namespace Concord::EventDetail

#endif // CONCORD_EVENTBUSCORE_H
