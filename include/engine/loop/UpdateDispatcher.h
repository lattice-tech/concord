#ifndef CONCORD_UPDATEDISPATCHER_H
#define CONCORD_UPDATEDISPATCHER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <map>
#include <vector>

namespace Concord {

/**
 * A thread-safe registry of per-frame callbacks, invoked once per frame by
 * whichever thread owns the frame loop (see EngineLoop). Add()/Remove() may
 * be called from any thread; RunAll() is meant to be called only by the
 * frame loop's own thread.
 */
class UpdateDispatcher {
public:
    using UpdateId = std::uint64_t;
    static constexpr UpdateId kInvalidId = 0;

    /** Execution phase within a frame; scene collection runs after game logic. */
    enum class Phase : std::uint8_t {
        Update,
        Scene,
    };

    /** Timing and callback count for one dispatcher run. */
    struct RunStats {
        float updateMs = 0.0f;
        float sceneMs = 0.0f;
        float slowestCallbackMs = 0.0f;
        std::uint32_t updateCallbacks = 0;
        std::uint32_t sceneCallbacks = 0;
        std::uint32_t callbackBudgetOverruns = 0;
    };

    /** Registers `callback`; returns kInvalidId if `callback` is empty. */
    UpdateId Add(std::function<void(float deltaTime)> callback, Phase phase = Phase::Update);

    /**
     * Unregisters a callback and prevents a snapshotted callback that has not
     * started from running. Waits for an execution already in progress, except
     * when a callback removes itself.
     */
    void Remove(UpdateId id);

    /**
     * Runs every registered callback with `deltaTime`. Only the shared_ptr
     * handles are copied out from under the internal lock (cheap refcount
     * bumps), so a slow or re-entrant callback (e.g. one that calls
     * Add()/Remove()) never holds up registration/removal on another
     * thread; each call still runs the one shared std::function instance,
     * so mutable state it captured (a running total, a counter, ...)
     * persists correctly from frame to frame.
     */
    RunStats RunAll(float deltaTime);

private:
    struct Entry {
        UpdateId id = kInvalidId;
        std::function<void(float)> callback;
        Phase phase = Phase::Update;
        std::mutex mutex;
        std::condition_variable idle;
        bool active = true;
        bool budgetWarningActive = false;
        std::uint32_t executing = 0;
    };

    struct PendingCallback {
        UpdateId id = kInvalidId;
        std::shared_ptr<Entry> entry;
    };

    void RebuildSnapshot();

    std::mutex m_mutex;
    std::map<UpdateId, std::shared_ptr<Entry>> m_callbacks;
    std::shared_ptr<const std::vector<PendingCallback>> m_snapshot =
        std::make_shared<const std::vector<PendingCallback>>();
    std::atomic<UpdateId> m_nextId{1};
};

} // namespace Concord

#endif // CONCORD_UPDATEDISPATCHER_H
