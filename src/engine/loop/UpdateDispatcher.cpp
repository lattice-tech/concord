#include "engine/loop/UpdateDispatcher.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>
#include <vector>

namespace Concord {

namespace {

thread_local const void* g_executingEntry = nullptr;
constexpr float kCallbackBudgetMs = 16.6667f;

} // namespace

UpdateDispatcher::UpdateId UpdateDispatcher::Add(std::function<void(float)> callback, Phase phase)
{
    if (!callback) {
        return kInvalidId;
    }
    const UpdateId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
    auto stored = std::make_shared<Entry>();
    stored->id = id;
    stored->callback = std::move(callback);
    stored->phase = phase;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.emplace(id, std::move(stored));
    RebuildSnapshot();
    return id;
}

void UpdateDispatcher::Remove(UpdateId id)
{
    if (id == kInvalidId) {
        return;
    }
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_callbacks.find(id);
        if (it == m_callbacks.end()) {
            return;
        }
        entry = it->second;
    }

    std::unique_lock<std::mutex> lock(entry->mutex);
    entry->active = false;
    if (g_executingEntry != entry.get()) {
        entry->idle.wait(lock, [&entry] { return entry->executing == 0; });
    }
    lock.unlock();
    std::lock_guard<std::mutex> registryLock(m_mutex);
    const auto it = m_callbacks.find(id);
    if (it != m_callbacks.end() && it->second == entry) {
        m_callbacks.erase(it);
        RebuildSnapshot();
    }
}

void UpdateDispatcher::RebuildSnapshot()
{
    auto snapshot = std::make_shared<std::vector<PendingCallback>>();
    snapshot->reserve(m_callbacks.size());
    for (const auto& [id, entry] : m_callbacks) {
        if (entry->phase == Phase::Update) {
            snapshot->push_back(PendingCallback{id, entry});
        }
    }
    for (const auto& [id, entry] : m_callbacks) {
        if (entry->phase == Phase::Scene) {
            snapshot->push_back(PendingCallback{id, entry});
        }
    }
    m_snapshot = std::move(snapshot);
}

UpdateDispatcher::RunStats UpdateDispatcher::RunAll(float deltaTime)
{
    using Clock = std::chrono::steady_clock;
    std::shared_ptr<const std::vector<PendingCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callbacks = m_snapshot;
    }
    RunStats stats;
    for (const PendingCallback& pending : *callbacks) {
        {
            std::lock_guard<std::mutex> lock(pending.entry->mutex);
            if (!pending.entry->active) {
                continue;
            }
            ++pending.entry->executing;
        }

        const void* previousEntry = g_executingEntry;
        g_executingEntry = pending.entry.get();
        const auto start = Clock::now();
        bool disable = false;
        try {
            pending.entry->callback(deltaTime);
        } catch (const std::exception& exception) {
            disable = true;
            Debug::Logger::Error("EngineLoop", "update callback %llu threw and was disabled: %s",
                                 static_cast<unsigned long long>(pending.id), exception.what());
        } catch (...) {
            disable = true;
            Debug::Logger::Error("EngineLoop",
                                 "update callback %llu threw an unknown exception and was disabled",
                                 static_cast<unsigned long long>(pending.id));
        }
        g_executingEntry = previousEntry;
        {
            std::lock_guard<std::mutex> lock(pending.entry->mutex);
            if (disable) {
                pending.entry->active = false;
            }
            --pending.entry->executing;
        }
        const float elapsedMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
        stats.slowestCallbackMs = std::max(stats.slowestCallbackMs, elapsedMs);
        if (elapsedMs > kCallbackBudgetMs) {
            ++stats.callbackBudgetOverruns;
            bool shouldWarn = false;
            {
                std::lock_guard<std::mutex> lock(pending.entry->mutex);
                shouldWarn = !pending.entry->budgetWarningActive;
                pending.entry->budgetWarningActive = true;
            }
            if (shouldWarn) {
                Debug::Logger::Warn("EngineLoop",
                                    "update callback %llu exceeded %.2f ms budget (%.2f ms)",
                                    static_cast<unsigned long long>(pending.id),
                                    kCallbackBudgetMs, elapsedMs);
            }
        } else {
            std::lock_guard<std::mutex> lock(pending.entry->mutex);
            pending.entry->budgetWarningActive = false;
        }
        if (pending.entry->phase == Phase::Scene) {
            stats.sceneMs += elapsedMs;
            ++stats.sceneCallbacks;
        } else {
            stats.updateMs += elapsedMs;
            ++stats.updateCallbacks;
        }
        pending.entry->idle.notify_all();
    }
    return stats;
}

} // namespace Concord
