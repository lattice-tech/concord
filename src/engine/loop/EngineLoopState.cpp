#include "engine/loop/EngineLoopImpl.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>

namespace Concord {

EngineLoop::UpdateId EngineLoop::Impl::OnUpdate(std::function<void(float)> callback)
{
    return m_updateDispatcher.Add(std::move(callback));
}

EngineLoop::UpdateId EngineLoop::Impl::OnSceneUpdate(std::function<void(float)> callback)
{
    return m_updateDispatcher.Add(std::move(callback), UpdateDispatcher::Phase::Scene);
}

void EngineLoop::Impl::RemoveUpdate(UpdateId id)
{
    m_updateDispatcher.Remove(id);
}

float EngineLoop::Impl::DeltaTime() const noexcept
{
    return m_frameClock.DeltaTime();
}

std::uint64_t EngineLoop::Impl::FrameCount() const noexcept
{
    return m_frameClock.FrameCount();
}

float EngineLoop::Impl::Fps() const noexcept
{
    return m_frameClock.Fps();
}

FrameStats EngineLoop::Impl::Stats() const noexcept
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

void EngineLoop::Impl::PublishWorldSnapshot(WindowId window, WorldSnapshot snapshot)
{
    std::lock_guard<std::mutex> openLock(m_openMutex);
    if (m_openWindows.count(window) == 0) {
        return;
    }
    snapshot.RebindBonePalettes();
    auto published = std::make_shared<WorldSnapshot>(std::move(snapshot));
    published->RebindBonePalettes();
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_worldSnapshots[window].published = std::move(published);
}

std::uint64_t EngineLoop::Impl::SimulationGeneration() const noexcept
{
    return m_simulationGeneration.load(std::memory_order_acquire);
}

std::shared_ptr<const WorldSnapshot> EngineLoop::Impl::AcquireWorldSnapshot(WindowId window)
{
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    const auto it = m_worldSnapshots.find(window);
    if (it == m_worldSnapshots.end()) {
        return {};
    }
    if (it->second.published) {
        it->second.rendering = std::move(it->second.published);
    }
    return it->second.rendering;
}

TaskGraphStats EngineLoop::Impl::RunTaskGraph(TaskGraph graph)
{
    return graph.Run(m_jobs);
}

EngineLoop::Impl::RequestDrainStats EngineLoop::Impl::DrainMeshRequests()
{
    using Clock = std::chrono::steady_clock;
    RequestDrainStats stats;
    const auto processingStart = Clock::now();
    std::queue<MeshRequest> local;
    {
        std::lock_guard<std::mutex> lock(m_meshMutex);
        local.swap(m_meshRequests);
    }
    while (!local.empty()) {
        MeshRequest req = std::move(local.front());
        local.pop();
        stats.queueLatencyMs = std::max(
            stats.queueLatencyMs,
            std::chrono::duration<float, std::milli>(Clock::now() - req.enqueuedAt).count());

        {
            std::lock_guard<std::mutex> lock(req.completion->mutex);
            if (req.completion->cancelled || req.completion->completed) {
                continue;
            }
            req.completion->processing = true;
        }

        MeshHandle result;
        try {
            if (!req.destroy) {
                if (m_backendReady && m_backendPtr != nullptr) {
                    result = m_backendPtr->CreateMesh(req.data);
                }
            } else if (m_backendReady && m_backendPtr != nullptr) {
                m_backendPtr->DestroyMesh(req.handle);
            }
        } catch (...) {
            result = MeshHandle::Invalid();
        }

        std::lock_guard<std::mutex> lock(req.completion->mutex);
        if (req.completion->cancelled && result.IsValid() && m_backendReady && m_backendPtr != nullptr) {
            try {
                m_backendPtr->DestroyMesh(result);
            } catch (...) {
            }
            result = MeshHandle::Invalid();
        }
        if (!req.completion->completed) {
            req.completion->processing = false;
            req.completion->completed = true;
            req.completion->result = result;
            req.completion->done.set_value(result);
        }
    }
    stats.processingMs = std::chrono::duration<float, std::milli>(Clock::now() - processingStart).count();
    return stats;
}

MeshHandle EngineLoop::Impl::CreateMesh(MeshData data)
{
    if (IsLoopThread()) {
        return (m_backendReady && m_backendPtr != nullptr)
            ? m_backendPtr->CreateMesh(data)
            : MeshHandle::Invalid();
    }
    MeshRequest req;
    req.data = std::move(data);
    const std::shared_ptr<MeshRequest::Completion> completion = req.completion;
    std::future<MeshHandle> future = completion->done.get_future();
    req.enqueuedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_meshMutex);
        if (!m_running.load()) {
            completion->completed = true;
            completion->done.set_value(MeshHandle::Invalid());
            return MeshHandle::Invalid();
        }
        m_meshRequests.push(std::move(req));
    }
    Wake();
    if (future.wait_for(kRequestTimeout) == std::future_status::ready) {
        return future.get();
    }

    std::lock_guard<std::mutex> lock(completion->mutex);
    if (completion->completed) {
        return completion->result;
    }
    completion->cancelled = true;
    if (!completion->processing) {
        completion->completed = true;
        completion->done.set_value(MeshHandle::Invalid());
    }
    return MeshHandle::Invalid();
}

std::future<MeshHandle> EngineLoop::Impl::CreateMeshAsync(MeshData data)
{
    MeshRequest req;
    req.data = std::move(data);
    std::future<MeshHandle> future = req.completion->done.get_future();
    if (IsLoopThread()) {
        const MeshHandle result = (m_backendReady && m_backendPtr != nullptr)
            ? m_backendPtr->CreateMesh(req.data)
            : MeshHandle::Invalid();
        req.completion->completed = true;
        req.completion->result = result;
        req.completion->done.set_value(result);
        return future;
    }
    req.enqueuedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_meshMutex);
        if (!m_running.load()) {
            req.completion->completed = true;
            req.completion->done.set_value(MeshHandle::Invalid());
            return future;
        }
        m_meshRequests.push(std::move(req));
    }
    Wake();
    return future;
}

void EngineLoop::Impl::DestroyMesh(MeshHandle mesh)
{
    if (!mesh.IsValid()) {
        return;
    }
    if (IsLoopThread()) {
        if (m_backendReady && m_backendPtr != nullptr) {
            m_backendPtr->DestroyMesh(mesh);
        }
        return;
    }
    MeshRequest req;
    req.destroy = true;
    req.handle = mesh;
    req.enqueuedAt = std::chrono::steady_clock::now();
    std::future<MeshHandle> future = req.completion->done.get_future();
    {
        std::lock_guard<std::mutex> lock(m_meshMutex);
        if (!m_running.load()) {
            req.completion->completed = true;
            req.completion->done.set_value(MeshHandle::Invalid());
            return;
        }
        m_meshRequests.push(std::move(req));
    }
    Wake();
    // Wait briefly so the handle is reclaimed before the caller assumes it;
    // a timeout still leaves the request queued (processed next frame).
    future.wait_for(kRequestTimeout);
}

void EngineLoop::Impl::RejectMeshRequests() noexcept
{
    while (true) {
        std::shared_ptr<MeshRequest::Completion> completion;
        try {
            std::lock_guard<std::mutex> lock(m_meshMutex);
            if (m_meshRequests.empty()) {
                return;
            }
            completion = m_meshRequests.front().completion;
            m_meshRequests.pop();
        } catch (...) {
            return;
        }
        try {
            std::lock_guard<std::mutex> lock(completion->mutex);
            if (!completion->completed) {
                completion->cancelled = true;
                completion->completed = true;
                completion->done.set_value(MeshHandle::Invalid());
            }
        } catch (...) {
        }
    }
}

} // namespace Concord
