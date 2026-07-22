#ifndef CONCORD_JOBSYSTEM_H
#define CONCORD_JOBSYSTEM_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Concord {

/**
 * @brief Process-local fixed worker pool for CPU-only engine work.
 *
 * Jobs must not call SDL, bgfx, or touch render-thread-owned resources.
 */
class JobSystem {
public:
    explicit JobSystem(std::size_t workerCount = DefaultWorkerCount());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    /** Returns a conservative worker count that leaves one core for rendering. */
    static std::size_t DefaultWorkerCount() noexcept;

    /** Enqueues a callable and returns a future for its result. */
    template <typename Function>
    auto Submit(Function&& function)
        -> std::future<std::invoke_result_t<std::decay_t<Function>>>
    {
        using Result = std::invoke_result_t<std::decay_t<Function>>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            std::forward<Function>(function));
        std::future<Result> result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) {
                throw std::runtime_error("cannot submit a job after shutdown");
            }
            m_jobs.emplace([task] { (*task)(); });
        }
        m_ready.notify_one();
        return result;
    }

    /** Number of persistent worker threads. */
    std::size_t WorkerCount() const noexcept { return m_workers.size(); }

private:
    void WorkerMain();

    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::queue<std::function<void()>> m_jobs;
    std::vector<std::thread> m_workers;
    bool m_stopping = false;
};

} // namespace Concord

#endif // CONCORD_JOBSYSTEM_H
