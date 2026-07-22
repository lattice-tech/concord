#include "engine/window/WindowId.h"

#include <atomic>

namespace Concord {

WindowId AllocateWindowId()
{
    static std::atomic<WindowId> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace Concord
