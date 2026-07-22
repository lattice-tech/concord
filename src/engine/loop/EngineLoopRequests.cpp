#include "engine/loop/EngineLoopImpl.h"

#include "engine/debug/Logger.h"
#include "engine/window/Window.h"
#include "engine/window/WindowDesc.h"
#include "engine/window/WindowId.h"

#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace Concord {

EngineLoop::WindowId EngineLoop::Impl::AttachWindow(const Window& window)
{
    const WindowId id = AllocateWindowId();

    Request request;
    request.kind = Request::Kind::Attach;
    request.id = id;
    request.title = window.Title();
    request.width = window.Width();
    request.height = window.Height();
    request.mode = window.Mode();
    request.resizable = window.Resizable();
    request.visible = window.Visible();
    request.vsync = window.Vsync();
    request.showCursor = window.ShowCursor();
    request.captureMouse = window.CaptureMouse();
    request.clickToRecapture = window.ClickToRecapture();
    request.aa = window.Antialiasing();
    request.msaa = ToMsaaLevel(request.aa);

    if (IsLoopThread()) {
        Debug::Logger::Error("EngineLoop",
                             "AttachWindow cannot synchronously create a window from the render thread");
        return kInvalidWindowId;
    }

    std::future<bool> future = request.completion->done.get_future();
    const std::shared_ptr<RequestCompletion> completion = request.completion;
    if (!Enqueue(std::move(request))) {
        CompleteRequest(completion, false);
    }
    return WaitForRequest(completion, future, kWindowAttachTimeout) ? id : kInvalidWindowId;
}

void EngineLoop::Impl::DetachWindow(WindowId id)
{
    if (id == kInvalidWindowId) {
        return;
    }

    Request request;
    request.kind = Request::Kind::Detach;
    request.id = id;
    if (IsLoopThread()) {
        Enqueue(std::move(request));
        return;
    }

    std::future<bool> future = request.completion->done.get_future();
    const std::shared_ptr<RequestCompletion> completion = request.completion;
    if (!Enqueue(std::move(request))) {
        CompleteRequest(completion, false);
    }
    WaitForRequest(completion, future);
}

void EngineLoop::Impl::SetAntiAliasing(AntiAliasing aa)
{
    m_antiAliasing.store(aa, std::memory_order_relaxed);
}

void EngineLoop::Impl::UpdateWindow(WindowId id, const WindowDesc& desc)
{
    if (id == kInvalidWindowId) {
        return;
    }

    Request request;
    request.kind = Request::Kind::Update;
    request.id = id;
    request.title = desc.title;
    request.width = desc.resolution.width;
    request.height = desc.resolution.height;
    request.mode = desc.mode;
    request.resizable = desc.resizable;
    request.visible = desc.visible;
    request.vsync = desc.vsync;
    request.showCursor = desc.showCursor;
    request.captureMouse = desc.captureMouse;
    request.clickToRecapture = desc.clickToRecapture;
    request.aa = desc.antialiasing;
    request.msaa = ToMsaaLevel(desc.antialiasing);
    if (IsLoopThread()) {
        Enqueue(std::move(request));
        return;
    }

    std::future<bool> future = request.completion->done.get_future();
    const std::shared_ptr<RequestCompletion> completion = request.completion;
    if (!Enqueue(std::move(request))) {
        CompleteRequest(completion, false);
    }
    WaitForRequest(completion, future);
}

void EngineLoop::Impl::CompleteRequest(const std::shared_ptr<RequestCompletion>& completion, bool result)
{
    std::lock_guard<std::mutex> lock(completion->mutex);
    if (completion->completed) {
        return;
    }
    completion->completed = true;
    completion->result = result;
    completion->done.set_value(result);
}

bool EngineLoop::Impl::WaitForRequest(const std::shared_ptr<RequestCompletion>& completion,
                                      std::future<bool>& future,
                                      std::chrono::milliseconds timeout)
{
    if (future.wait_for(timeout) == std::future_status::ready) {
        return future.get();
    }

    std::lock_guard<std::mutex> lock(completion->mutex);
    if (!completion->completed) {
        completion->cancelled = true;
        completion->completed = true;
        completion->result = false;
        completion->done.set_value(false);
    }
    return completion->completed && completion->result;
}

bool EngineLoop::Impl::IsLoopThread() const
{
    std::lock_guard<std::mutex> lock(m_loopThreadMutex);
    return m_loopThreadId == std::this_thread::get_id();
}

bool EngineLoop::Impl::IsInternalThread() const
{
    std::lock_guard<std::mutex> lock(m_loopThreadMutex);
    const std::thread::id caller = std::this_thread::get_id();
    return m_loopThreadId == caller || m_simulationThreadId == caller;
}

void EngineLoop::Impl::SetLoopThreadId(std::thread::id id)
{
    std::lock_guard<std::mutex> lock(m_loopThreadMutex);
    m_loopThreadId = id;
}

bool EngineLoop::Impl::Enqueue(Request&& request)
{
    request.enqueuedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (!m_running.load()) {
            return false;
        }
        m_requests.push(std::move(request));
    }
    m_queueCv.notify_all();
    return true;
}

void EngineLoop::Impl::RejectPendingRequests()
{
    std::queue<Request> pending;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pending.swap(m_requests);
    }
    while (!pending.empty()) {
        CompleteRequest(pending.front().completion, false);
        pending.pop();
    }
}

} // namespace Concord
