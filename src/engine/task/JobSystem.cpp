#include "engine/task/JobSystem.h"

#include <algorithm>

namespace Concord {

JobSystem::JobSystem(std::size_t workerCount)
{
    workerCount = std::max<std::size_t>(workerCount, 1);
    m_workers.reserve(workerCount);
    try {
        for (std::size_t index = 0; index < workerCount; ++index) {
            m_workers.emplace_back([this] { WorkerMain(); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_ready.notify_all();
        for (std::thread& worker : m_workers) {
            worker.join();
        }
        throw;
    }
}

JobSystem::~JobSystem()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_ready.notify_all();
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t JobSystem::DefaultWorkerCount() noexcept
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return hardwareThreads > 3 ? hardwareThreads - 2 : 1;
}

void JobSystem::WorkerMain()
{
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_ready.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_stopping && m_jobs.empty()) {
                return;
            }
            job = std::move(m_jobs.front());
            m_jobs.pop();
        }
        job();
    }
}

} // namespace Concord
