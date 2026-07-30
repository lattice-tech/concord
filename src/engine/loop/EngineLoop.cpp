#include "engine/loop/EngineLoop.h"

#include "engine/loop/EngineLoopImpl.h"
#include "engine/render/backend/BgfxRenderBackend.h"
#include "engine/window/Window.h"
#include "engine/window/WindowDesc.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace Concord {

class EngineLoopLifecycle {
public:
    EngineLoopLifecycle() = default;

    EngineLoopLifecycle(const EngineLoopLifecycle&) = delete;
    EngineLoopLifecycle& operator=(const EngineLoopLifecycle&) = delete;

    std::shared_ptr<EngineLoop> Acquire(RenderBackendType preferredBackend)
    {
        std::thread retiredWorker;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (true) {
                if (std::shared_ptr<EngineLoop> existing = m_instance.lock()) {
                    return existing;
                }
                if (m_state == State::Idle) {
                    retiredWorker = std::move(m_worker);
                    m_state = State::Active;
                    break;
                }
                m_idle.wait(lock);
            }
        }
        if (retiredWorker.joinable()) {
            retiredWorker.join();
        }

        try {
            std::shared_ptr<EngineLoop> loop(new EngineLoop(), [](EngineLoop* retiredLoop) {
                Instance().Retire(retiredLoop, retiredLoop->IsInternalThread());
            });
            loop->m_impl->Start(preferredBackend);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_instance = loop;
            }
            m_idle.notify_all();
            return loop;
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_state == State::Active) {
                    m_state = State::Idle;
                }
            }
            m_idle.notify_all();
            throw;
        }
    }

    static EngineLoopLifecycle& Instance()
    {
        // CEngine is a DLL. Destroying a function-local manager while Windows
        // holds the loader lock deadlocks if its retirement worker needs to run
        // before join can finish. Normal Game ownership still tears every loop
        // down; the process reclaims this small manager itself at termination.
        static EngineLoopLifecycle* lifecycle = new EngineLoopLifecycle();
        return *lifecycle;
    }

private:
    enum class State {
        Idle,
        Active,
        Deleting,
        Retiring,
    };

    void Retire(EngineLoop* loop, bool fromLoopThread)
    {
        if (fromLoopThread) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_state = State::Retiring;
                m_worker = std::thread([this, loop] {
                    delete loop;
                    {
                        std::lock_guard<std::mutex> workerLock(m_mutex);
                        m_state = State::Idle;
                    }
                    m_idle.notify_all();
                });
            }
            m_idle.notify_all();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = State::Deleting;
        }
        m_idle.notify_all();
        delete loop;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_state = State::Idle;
        }
        m_idle.notify_all();
    }

    std::mutex m_mutex;
    std::condition_variable m_idle;
    std::weak_ptr<EngineLoop> m_instance;
    State m_state = State::Idle;
    std::thread m_worker;
};

EngineLoop::Impl::~Impl()
{
    Stop();
}

void EngineLoop::Impl::Start(RenderBackendType type)
{
    if (m_running.exchange(true)) {
        return;
    }
    m_type = type;
    m_eventGeneration.store(EventDetail::EventBusCore::Activate([this] {
        Wake();
        RequestSimulation();
    }));
    try {
        m_simulationThread = std::thread(&Impl::SimulationMain, this);
        m_thread = std::thread(&Impl::Run, this);
    } catch (...) {
        const std::uint64_t eventGeneration = m_eventGeneration.exchange(0);
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_running.store(false);
        }
        m_queueCv.notify_all();
        m_openCv.notify_all();
        {
            std::lock_guard<std::mutex> lock(m_simulationMutex);
            m_simulationStopping = true;
        }
        m_simulationReady.notify_all();
        RejectPendingRequests();
        RejectMeshRequests();
        EventDetail::EventBusCore::Shutdown(eventGeneration);
        if (m_simulationThread.joinable()) {
            m_simulationThread.join();
        }
        throw;
    }
}

void EngineLoop::Impl::Stop()
{
    const std::uint64_t eventGeneration = m_eventGeneration.exchange(0);
    bool wasRunning = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        wasRunning = m_running.exchange(false);
    }
    if (wasRunning) {
        m_queueCv.notify_all();
        m_openCv.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(m_simulationMutex);
        m_simulationStopping = true;
    }
    m_simulationReady.notify_all();
    RejectPendingRequests();
    RejectMeshRequests();
    EventDetail::EventBusCore::Shutdown(eventGeneration);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_simulationThread.joinable()) {
        m_simulationThread.join();
    }
}

EngineLoop::EngineLoop()
    : m_impl(std::make_unique<Impl>())
{
}

EngineLoop::~EngineLoop() = default;

std::shared_ptr<EngineLoop> EngineLoop::Acquire(RenderBackendType preferredBackend)
{
    return EngineLoopLifecycle::Instance().Acquire(preferredBackend);
}

void EngineLoop::Impl::Wake()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        ++m_wakeGeneration;
    }
    m_queueCv.notify_all();
}

bool EngineLoop::IsLoopThread() const
{
    return m_impl->IsLoopThread();
}

bool EngineLoop::IsInternalThread() const
{
    return m_impl->IsInternalThread();
}

EngineLoop::WindowId EngineLoop::AttachWindow(const Window& window)
{
    return m_impl->AttachWindow(window);
}

void EngineLoop::DetachWindow(WindowId id)
{
    m_impl->DetachWindow(id);
}

void EngineLoop::SetAntiAliasing(AntiAliasing aa)
{
    m_impl->SetAntiAliasing(aa);
}

void EngineLoop::MinimizeWindow(WindowId id)
{
    m_impl->MinimizeWindow(id);
}

void EngineLoop::MaximizeWindow(WindowId id)
{
    m_impl->MaximizeWindow(id);
}

void EngineLoop::RestoreWindow(WindowId id)
{
    m_impl->RestoreWindow(id);
}

void EngineLoop::BeginWindowDrag(WindowId id, float pointerX, float pointerY)
{
    m_impl->BeginWindowDrag(id, pointerX, pointerY);
}

void EngineLoop::BeginWindowResize(WindowId id, WindowResizeEdge edge)
{
    m_impl->BeginWindowResize(id, edge);
}

void EngineLoop::SetWindowChrome(WindowId id, const WindowChromeConfig& config)
{
    m_impl->SetWindowChrome(id, config);
}

void EngineLoop::UpdateWindow(WindowId id, const WindowDesc& desc)
{
    m_impl->UpdateWindow(id, desc);
}

EngineLoop::UpdateId EngineLoop::OnUpdate(std::function<void(float)> callback)
{
    return m_impl->OnUpdate(std::move(callback));
}

EngineLoop::UpdateId EngineLoop::OnSceneUpdate(std::function<void(float)> callback)
{
    return m_impl->OnSceneUpdate(std::move(callback));
}

void EngineLoop::RequestSimulation()
{
    m_impl->RequestSimulation();
}

void EngineLoop::RemoveUpdate(UpdateId id)
{
    m_impl->RemoveUpdate(id);
}

float EngineLoop::DeltaTime() const noexcept
{
    return m_impl->DeltaTime();
}

std::uint64_t EngineLoop::FrameCount() const noexcept
{
    return m_impl->FrameCount();
}

float EngineLoop::Fps() const noexcept
{
    return m_impl->Fps();
}

FrameStats EngineLoop::Stats() const noexcept
{
    return m_impl->Stats();
}

void EngineLoop::PublishWorldSnapshot(WindowId window, WorldSnapshot snapshot)
{
    m_impl->PublishWorldSnapshot(window, std::move(snapshot));
}

std::uint64_t EngineLoop::SimulationGeneration() const noexcept
{
    return m_impl->SimulationGeneration();
}

TaskGraphStats EngineLoop::RunTaskGraph(TaskGraph graph)
{
    return m_impl->RunTaskGraph(std::move(graph));
}

MeshHandle EngineLoop::CreateMesh(const MeshData& data)
{
    return m_impl->CreateMesh(data);
}

void EngineLoop::DestroyMesh(MeshHandle mesh)
{
    m_impl->DestroyMesh(mesh);
}

bool EngineLoop::IsWindowOpen(WindowId id) const
{
    return m_impl->IsWindowOpen(id);
}

bool EngineLoop::WindowPixelSize(WindowId id, int& outWidth, int& outHeight) const
{
    return m_impl->WindowPixelSize(id, outWidth, outHeight);
}

std::future<MeshHandle> EngineLoop::CreateMeshAsync(MeshData data)
{
    return m_impl->CreateMeshAsync(std::move(data));
}

bool EngineLoop::IsMouseCaptured(WindowId id) const
{
    return m_impl->IsMouseCaptured(id);
}

bool EngineLoop::IsWindowMinimized(WindowId id) const
{
    return m_impl->IsWindowMinimized(id);
}

bool EngineLoop::IsWindowMaximized(WindowId id) const
{
    return m_impl->IsWindowMaximized(id);
}

void EngineLoop::WaitForWindowClose(WindowId id)
{
    m_impl->WaitForWindowClose(id);
}

} // namespace Concord
