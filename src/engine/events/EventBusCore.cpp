#include "engine/events/EventBusCore.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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

struct QueuedEvent {
    EventTypeId type;
    std::shared_ptr<const void> payload;
};

enum class BusStatus {
    Active,
    ShuttingDown,
    Inactive,
};

struct BusState {
    std::mutex mutex;
    std::condition_variable wakeIdle;
    BusStatus status = BusStatus::Active;
    std::uint64_t generation = 0;
    std::uint64_t nextHandlerId = 1;
    std::function<void()> wake;
    std::uint32_t executingWakes = 0;
    std::vector<QueuedEvent> queue;
    std::unordered_map<std::uint64_t, std::shared_ptr<HandlerEntry>> handlers;
    std::unordered_map<EventTypeId,
                       std::vector<std::pair<std::uint64_t, std::shared_ptr<HandlerEntry>>>,
                       EventTypeIdHash> handlersByType;
    std::unordered_map<EventTypeId, std::string, EventTypeIdHash> typeNames;
    std::chrono::steady_clock::time_point lastQueueFullLog{};
    std::uint64_t suppressedQueueFullLogs = 0;
};

struct BusManager {
    std::mutex mutex;
    std::shared_ptr<BusState> active;
    std::atomic<std::uint64_t> nextGeneration{1};
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
        if (state->status == BusStatus::ShuttingDown) {
            return EventPublishResult::ShuttingDown;
        }
        if (state->status != BusStatus::Active) {
            return EventPublishResult::Inactive;
        }
        if (!RegisterType(*state, type)) {
            return EventPublishResult::Inactive;
        }
        if (state->queue.size() >= kEventQueueCapacity) {
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
        state->queue.push_back(QueuedEvent{type.id, std::move(payload)});
        if (shouldWake) {
            ++state->executingWakes;
        }
    }

    if (shouldWake) {
        try {
            state->wake();
        } catch (const std::exception& exception) {
            FinishWake(state);
            Debug::Logger::Error("Events", "event wake callback threw: %s", exception.what());
            return EventPublishResult::Published;
        } catch (...) {
            FinishWake(state);
            Debug::Logger::Error("Events", "event wake callback threw an unknown exception");
            return EventPublishResult::Published;
        }
        FinishWake(state);
    }
    return EventPublishResult::Published;
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
    if (state->status != BusStatus::Active || !RegisterType(*state, type)) {
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
        if (state->status != BusStatus::Active || it == state->handlers.end()) {
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
    state->queue.reserve(kEventQueueCapacity);

    std::lock_guard<std::mutex> lock(manager.mutex);
    manager.active = state;
    return state->generation;
}

void EventBusCore::Shutdown(std::uint64_t generation)
{
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state || state->generation != generation) {
        return;
    }

    std::vector<std::shared_ptr<HandlerEntry>> handlers;
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (state->status != BusStatus::Active) {
            return;
        }
        state->status = BusStatus::ShuttingDown;
        state->queue.clear();
        state->wakeIdle.wait(lock, [&state] { return state->executingWakes == 0; });
        handlers.reserve(state->handlers.size());
        for (const auto& [id, entry] : state->handlers) {
            handlers.push_back(entry);
        }
    }

    for (const auto& entry : handlers) {
        std::unique_lock<std::mutex> lock(entry->mutex);
        entry->active = false;
        if (g_executingHandler != entry.get()) {
            entry->idle.wait(lock, [&entry] { return entry->executing == 0; });
        }
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->handlers.clear();
        state->handlersByType.clear();
        state->typeNames.clear();
        state->wake = {};
        state->status = BusStatus::Inactive;
    }
    BusManager& manager = Manager();
    std::lock_guard<std::mutex> lock(manager.mutex);
    if (manager.active == state) {
        manager.active.reset();
    }
}

void EventBusCore::Dispatch(std::uint64_t generation)
{
    const std::shared_ptr<BusState> state = ActiveState();
    if (!state || state->generation != generation) {
        return;
    }

    std::vector<QueuedEvent> events;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->status != BusStatus::Active) {
            return;
        }
        events.swap(state->queue);
    }

    for (const QueuedEvent& event : events) {
        std::vector<std::pair<std::uint64_t, std::shared_ptr<HandlerEntry>>> handlers;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->status != BusStatus::Active) {
                return;
            }
            const auto typedHandlers = state->handlersByType.find(event.type);
            if (typedHandlers != state->handlersByType.end()) {
                handlers = typedHandlers->second;
            }
        }

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
                entry->handler(event.payload.get());
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
        }
    }
}

} // namespace Concord::EventDetail
