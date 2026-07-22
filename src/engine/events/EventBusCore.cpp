#include "engine/events/EventBusCore.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Concord::EventDetail {

namespace {

constexpr std::size_t kEventQueueCapacity = 4096;
constexpr std::size_t kMaxQueueEntriesPerDispatch = 512;
constexpr std::chrono::seconds kQueueFullLogInterval{1};

struct EventTypeIdHash {
    std::size_t operator()(EventTypeId id) const noexcept
    {
        return static_cast<std::size_t>(id.low ^ (id.high + 0x9e3779b97f4a7c15ULL
            + (id.low << 6U) + (id.low >> 2U)));
    }
};

struct HandlerEntry {
    EventTypeId type;
    EventBusCore::ErasedHandler handler;
    std::mutex mutex;
    std::condition_variable idle;
    bool active = true;
    std::uint32_t executing = 0;
};

struct QueuedItem {
    enum class Kind : std::uint8_t {
        Event,
        Fence,
    };

    Kind kind = Kind::Event;
    EventTypeId type;
    std::shared_ptr<const void> payload;
    std::shared_ptr<std::atomic<EventFenceResult>> fence;
};

struct BusState {
    std::mutex mutex;
    std::condition_variable wakeIdle;
    EventBusStatus status = EventBusStatus::Active;
    std::uint64_t generation = 0;
    std::uint64_t nextHandlerId = 1;
    std::function<void()> wake;
    std::uint32_t executingWakes = 0;
    std::deque<QueuedItem> queue;
    std::unordered_map<std::uint64_t, std::shared_ptr<HandlerEntry>> handlers;
    std::unordered_map<EventTypeId,
                       std::vector<std::pair<std::uint64_t, std::shared_ptr<HandlerEntry>>>,
                       EventTypeIdHash> handlersByType;
    std::unordered_map<EventTypeId, std::string, EventTypeIdHash> typeNames;
    std::chrono::steady_clock::time_point lastQueueFullLog{};
    std::uint64_t suppressedQueueFullLogs = 0;
    std::uint64_t highWatermark = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t dispatched = 0;
    std::uint64_t delivered = 0;
    std::uint64_t handlerFailures = 0;
};

struct BusManager {
    std::mutex mutex;
    std::shared_ptr<BusState> active;
    std::atomic<std::uint64_t> nextGeneration{1};
    EventBusStats retiredStats{.capacity = kEventQueueCapacity};
};

thread_local const HandlerEntry* g_executingHandler = nullptr;

BusManager& Manager()
{
    static BusManager* manager = new BusManager();
    return *manager;
}

std::shared_ptr<BusState> ActiveState()
{
    BusManager& manager = Manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    return manager.active;
}

bool RegisterType(BusState& state, const EventTypeDescriptor& descriptor)
{
    const auto existing = state.typeNames.find(descriptor.id);
    if (existing == state.typeNames.end()) {
        state.typeNames.emplace(
            descriptor.id, std::string(descriptor.name, descriptor.nameLength));
        return true;
    }

    if (existing->second != std::string_view(descriptor.name, descriptor.nameLength)) {
        Debug::Logger::Error("Events", "event TypeId collision between '%s' and '%.*s'",
                             existing->second.c_str(), static_cast<int>(descriptor.nameLength),
                             descriptor.name);
        return false;
    }
    return true;
}

void FinishExecution(const std::shared_ptr<HandlerEntry>& entry, bool disable)
{
    {
        std::lock_guard<std::mutex> lock(entry->mutex);
        if (disable) {
            entry->active = false;
        }
        --entry->executing;
    }
    entry->idle.notify_all();
}

void FinishWake(const std::shared_ptr<BusState>& state)
{
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        --state->executingWakes;
    }
    state->wakeIdle.notify_all();
}

void InvokeWake(const std::shared_ptr<BusState>& state) noexcept
{
    try {
        state->wake();
    } catch (const std::exception& exception) {
        Debug::Logger::Error("Events", "event wake callback threw: %s", exception.what());
    } catch (...) {
        Debug::Logger::Error("Events", "event wake callback threw an unknown exception");
    }
    FinishWake(state);
}

void CompleteFence(const std::shared_ptr<std::atomic<EventFenceResult>>& fence,
                   EventFenceResult result) noexcept
{
    if (!fence) {
        return;
    }
    EventFenceResult expected = EventFenceResult::Pending;
    fence->compare_exchange_strong(expected, result,
                                   std::memory_order_release,
                                   std::memory_order_relaxed);
}

EventBusStats SnapshotStats(const BusState& state) noexcept
{
    return {
        .status = state.status,
        .generation = state.generation,
        .queued = static_cast<std::uint64_t>(state.queue.size()),
        .capacity = kEventQueueCapacity,
        .highWatermark = state.highWatermark,
        .accepted = state.accepted,
        .rejected = state.rejected,
        .dispatched = state.dispatched,
        .delivered = state.delivered,
        .handlerFailures = state.handlerFailures,
    };
}

} // namespace

EventPublishResult EventBusCore::Publish(const EventTypeDescriptor& type,
                                         std::shared_ptr<const void> payload)
{
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state) {
        return EventPublishResult::Inactive;
    }

    bool shouldWake = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->status == EventBusStatus::ShuttingDown) {
            ++state->rejected;
            return EventPublishResult::ShuttingDown;
        }
        if (state->status != EventBusStatus::Active) {
            ++state->rejected;
            return EventPublishResult::Inactive;
        }
        if (!RegisterType(*state, type)) {
            ++state->rejected;
            return EventPublishResult::Inactive;
        }
        if (state->queue.size() >= kEventQueueCapacity) {
            ++state->rejected;
            const auto now = std::chrono::steady_clock::now();
            ++state->suppressedQueueFullLogs;
            if (state->lastQueueFullLog.time_since_epoch().count() == 0
                || now - state->lastQueueFullLog >= kQueueFullLogInterval) {
                Debug::Logger::Warn("Events", "event queue full; rejected %llu publication(s)",
                                    static_cast<unsigned long long>(state->suppressedQueueFullLogs));
                state->lastQueueFullLog = now;
                state->suppressedQueueFullLogs = 0;
            }
            return EventPublishResult::QueueFull;
        }

        shouldWake = state->queue.empty() && static_cast<bool>(state->wake);
        state->queue.push_back(QueuedItem{
            .kind = QueuedItem::Kind::Event,
            .type = type.id,
            .payload = std::move(payload),
        });
        ++state->accepted;
        state->highWatermark = std::max(
            state->highWatermark, static_cast<std::uint64_t>(state->queue.size()));
        if (shouldWake) {
            ++state->executingWakes;
        }
    }

    if (shouldWake) {
        InvokeWake(state);
    }
    return EventPublishResult::Published;
}

EventFence EventBusCore::Fence()
{
    auto completion = std::make_shared<std::atomic<EventFenceResult>>(EventFenceResult::Pending);
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state) {
        completion->store(EventFenceResult::Inactive, std::memory_order_release);
        return EventFence(std::move(completion));
    }

    bool shouldWake = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->status != EventBusStatus::Active) {
            completion->store(state->status == EventBusStatus::ShuttingDown
                                  ? EventFenceResult::Retired
                                  : EventFenceResult::Inactive,
                              std::memory_order_release);
            return EventFence(std::move(completion));
        }
        if (state->queue.size() >= kEventQueueCapacity) {
            completion->store(EventFenceResult::QueueFull, std::memory_order_release);
            return EventFence(std::move(completion));
        }

        shouldWake = state->queue.empty() && static_cast<bool>(state->wake);
        state->queue.push_back(QueuedItem{
            .kind = QueuedItem::Kind::Fence,
            .fence = completion,
        });
        state->highWatermark = std::max(
            state->highWatermark, static_cast<std::uint64_t>(state->queue.size()));
        if (shouldWake) {
            ++state->executingWakes;
        }
    }

    if (shouldWake) {
        InvokeWake(state);
    }
    return EventFence(std::move(completion));
}

EventBusStats EventBusCore::Stats() noexcept
{
    try {
        BusManager& manager = Manager();
        std::shared_ptr<BusState> state;
        EventBusStats retired;
        {
            std::lock_guard<std::mutex> lock(manager.mutex);
            state = manager.active;
            retired = manager.retiredStats;
        }
        if (!state) {
            return retired;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        return SnapshotStats(*state);
    } catch (...) {
        return EventBusStats{.capacity = kEventQueueCapacity};
    }
}

EventSubscription EventBusCore::Subscribe(const EventTypeDescriptor& type, ErasedHandler handler)
{
    if (!handler) {
        return {};
    }
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state) {
        return {};
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->status != EventBusStatus::Active || !RegisterType(*state, type)) {
        return {};
    }
    const std::uint64_t id = state->nextHandlerId++;
    auto entry = std::make_shared<HandlerEntry>();
    entry->type = type.id;
    entry->handler = std::move(handler);
    state->handlers.emplace(id, entry);
    state->handlersByType[type.id].emplace_back(id, std::move(entry));
    return EventSubscription(state->generation, id);
}

void EventBusCore::Unsubscribe(std::uint64_t generation, std::uint64_t id)
{
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state || state->generation != generation || id == 0) {
        return;
    }

    std::shared_ptr<HandlerEntry> entry;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto it = state->handlers.find(id);
        if (it == state->handlers.end()) {
            return;
        }
        entry = it->second;
    }
    {
        std::unique_lock<std::mutex> lock(entry->mutex);
        entry->active = false;
        if (g_executingHandler != entry.get()) {
            entry->idle.wait(lock, [&entry] { return entry->executing == 0; });
        }
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    const auto it = state->handlers.find(id);
    if (it != state->handlers.end() && it->second == entry) {
        state->handlers.erase(it);
        const auto typeIt = state->handlersByType.find(entry->type);
        if (typeIt != state->handlersByType.end()) {
            auto& typedHandlers = typeIt->second;
            typedHandlers.erase(
                std::remove_if(typedHandlers.begin(), typedHandlers.end(),
                               [id](const auto& handler) { return handler.first == id; }),
                typedHandlers.end());
            if (typedHandlers.empty()) {
                state->handlersByType.erase(typeIt);
            }
        }
    }
}

bool EventBusCore::IsSubscribed(std::uint64_t generation, std::uint64_t id) noexcept
{
    try {
        const std::shared_ptr<BusState> state = ActiveState();
        if (!state || state->generation != generation || id == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto it = state->handlers.find(id);
        if (state->status != EventBusStatus::Active || it == state->handlers.end()) {
            return false;
        }
        std::lock_guard<std::mutex> entryLock(it->second->mutex);
        return it->second->active;
    } catch (...) {
        return false;
    }
}

std::uint64_t EventBusCore::Activate(std::function<void()> wake)
{
    BusManager& manager = Manager();
    auto state = std::make_shared<BusState>();
    state->generation = manager.nextGeneration.fetch_add(1, std::memory_order_relaxed);
    state->wake = std::move(wake);
    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.active = state;
    return state->generation;
}

void EventBusCore::Shutdown(std::uint64_t generation) noexcept
{
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state || state->generation != generation) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->status != EventBusStatus::Active) {
            return;
        }
        state->status = EventBusStatus::ShuttingDown;
    }

    while (true) {
        QueuedItem item;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->queue.empty()) {
                break;
            }
            item = std::move(state->queue.front());
            state->queue.pop_front();
        }
        if (item.kind == QueuedItem::Kind::Fence) {
            CompleteFence(item.fence, EventFenceResult::Retired);
        }
    }

    {
        std::function<void()> retiredWake;
        {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->wakeIdle.wait(lock, [&state] { return state->executingWakes == 0; });
            retiredWake.swap(state->wake);
            state->typeNames.clear();
        }
    }

    while (true) {
        std::shared_ptr<HandlerEntry> indexedEntry;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->handlersByType.empty()) {
                break;
            }
            const auto typeHandlers = state->handlersByType.begin();
            if (typeHandlers->second.empty()) {
                state->handlersByType.erase(typeHandlers);
                continue;
            }
            indexedEntry = std::move(typeHandlers->second.back().second);
            typeHandlers->second.pop_back();
        }
    }

    while (true) {
        std::shared_ptr<HandlerEntry> entry;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->handlers.empty()) {
                break;
            }
            const auto handler = state->handlers.begin();
            entry = handler->second;
            state->handlers.erase(handler);
        }
        std::unique_lock<std::mutex> lock(entry->mutex);
        entry->active = false;
        if (g_executingHandler != entry.get()) {
            entry->idle.wait(lock, [&entry] { return entry->executing == 0; });
        }
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->status = EventBusStatus::Inactive;
    }
    BusManager& manager = Manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    if (manager.active == state) {
        std::lock_guard<std::mutex> stateLock(state->mutex);
        manager.retiredStats = SnapshotStats(*state);
        manager.active.reset();
    }
}

void EventBusCore::Dispatch(std::uint64_t generation)
{
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state || state->generation != generation) {
        return;
    }

    std::size_t turnEntries = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->status != EventBusStatus::Active) {
            return;
        }
        turnEntries = std::min(state->queue.size(), kMaxQueueEntriesPerDispatch);
    }

    for (std::size_t entryIndex = 0; entryIndex < turnEntries; ++entryIndex) {
        QueuedItem item;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->status != EventBusStatus::Active || state->queue.empty()) {
                break;
            }
            item = std::move(state->queue.front());
            state->queue.pop_front();
        }

        if (item.kind == QueuedItem::Kind::Fence) {
            CompleteFence(item.fence, EventFenceResult::Dispatched);
            continue;
        }

        std::vector<std::pair<std::uint64_t, std::shared_ptr<HandlerEntry>>> handlers;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->status != EventBusStatus::Active) {
                break;
            }
            const auto typedHandlers = state->handlersByType.find(item.type);
            if (typedHandlers != state->handlersByType.end()) {
                handlers = typedHandlers->second;
            }
        }

        std::uint64_t delivered = 0;
        std::uint64_t handlerFailures = 0;
        for (const auto& [id, entry] : handlers) {
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                if (!entry->active) {
                    continue;
                }
                ++entry->executing;
            }
            const HandlerEntry* previous = g_executingHandler;
            g_executingHandler = entry.get();
            bool disable = false;
            try {
                entry->handler(item.payload.get());
            } catch (const std::exception& exception) {
                disable = true;
                Debug::Logger::Error("Events", "event handler %llu threw: %s",
                                     static_cast<unsigned long long>(id), exception.what());
            } catch (...) {
                disable = true;
                Debug::Logger::Error("Events", "event handler %llu threw an unknown exception",
                                     static_cast<unsigned long long>(id));
            }
            g_executingHandler = previous;
            FinishExecution(entry, disable);
            ++delivered;
            if (disable) {
                ++handlerFailures;
            }
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            ++state->dispatched;
            state->delivered += delivered;
            state->handlerFailures += handlerFailures;
        }
    }

    bool shouldWake = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        shouldWake = state->status == EventBusStatus::Active
            && !state->queue.empty() && static_cast<bool>(state->wake);
        if (shouldWake) {
            ++state->executingWakes;
        }
    }
    if (shouldWake) {
        InvokeWake(state);
    }
}

} // namespace Concord::EventDetail
