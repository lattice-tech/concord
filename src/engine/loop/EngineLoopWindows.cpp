#include "engine/loop/EngineLoopImpl.h"

#include "engine/debug/Logger.h"
#include "engine/render/mesh/Primitives.h"
#include "engine/ui/UiSurface.h"

#include <SDL3/SDL.h>

#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Concord {

void EngineLoop::Impl::MarkWindowOpen(WindowId id, bool mouseCaptured)
{
    {
        std::lock_guard<std::mutex> lock(m_openMutex);
        m_openWindows.insert(id);
        m_mouseCaptured[id] = mouseCaptured;
        m_windowMinimized[id] = false;
        m_windowMaximized[id] = false;
    }
    m_openCv.notify_all();
}

void EngineLoop::Impl::MarkWindowClosed(WindowId id)
{
    {
        std::lock_guard<std::mutex> lock(m_openMutex);
        m_openWindows.erase(id);
        m_mouseCaptured.erase(id);
        m_windowMinimized.erase(id);
        m_windowMaximized.erase(id);
        m_windowPixelSize.erase(id);
    }
    // State publishers hold m_openMutex through their per-map write. Once the
    // membership erase above completes, no publisher can reintroduce this id.
    {
        std::lock_guard<std::mutex> stateLock(m_snapshotMutex);
        m_worldSnapshots.erase(id);
    }
    UI::ClearSurface(id);
    m_openCv.notify_all();
}

void EngineLoop::Impl::SetMouseCaptured(WindowId id, bool mouseCaptured)
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    if (m_openWindows.count(id) != 0) {
        m_mouseCaptured[id] = mouseCaptured;
    }
}

void EngineLoop::Impl::SetWindowMinimized(WindowId id, bool minimized)
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    if (m_openWindows.count(id) != 0) {
        m_windowMinimized[id] = minimized;
    }
}

void EngineLoop::Impl::SetWindowMaximized(WindowId id, bool maximized)
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    if (m_openWindows.count(id) != 0) {
        m_windowMaximized[id] = maximized;
    }
}

bool EngineLoop::Impl::IsMouseCaptured(WindowId id) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    const auto it = m_mouseCaptured.find(id);
    return it != m_mouseCaptured.end() && it->second;
}

bool EngineLoop::Impl::IsWindowMinimized(WindowId id) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    const auto it = m_windowMinimized.find(id);
    return it != m_windowMinimized.end() && it->second;
}

bool EngineLoop::Impl::IsWindowMaximized(WindowId id) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    const auto it = m_windowMaximized.find(id);
    return it != m_windowMaximized.end() && it->second;
}

bool EngineLoop::Impl::IsWindowOpen(WindowId id) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    return m_openWindows.count(id) != 0;
}

void EngineLoop::Impl::SetWindowPixelSize(WindowId id, int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_openMutex);
    m_windowPixelSize[id] = {width, height};
}

bool EngineLoop::Impl::WindowPixelSize(WindowId id, int& outWidth, int& outHeight) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    if (m_openWindows.count(id) == 0) {
        return false;
    }
    const auto it = m_windowPixelSize.find(id);
    if (it == m_windowPixelSize.end()) {
        return false;
    }
    outWidth = it->second.first;
    outHeight = it->second.second;
    return true;
}

void EngineLoop::Impl::WaitForWindowClose(WindowId id)
{
    std::unique_lock<std::mutex> lock(m_openMutex);
    m_openCv.wait(lock, [&] { return m_openWindows.count(id) == 0 || !m_running.load(); });
}

void EngineLoop::Impl::ApplyCursorVisibility(bool visible)
{
    if (visible) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
}

void EngineLoop::Impl::HandleAttach(const Request& request,
                                    std::unique_ptr<IRenderBackend>& backend,
                                    bool& backendReady,
                                    std::unordered_map<WindowId, WindowSlot>& windows)
{
    {
        std::lock_guard<std::mutex> lock(request.completion->mutex);
        if (request.completion->cancelled || request.completion->completed) {
            return;
        }
        request.completion->processing = true;
    }

    auto window = std::make_unique<SdlWindow>();
    if (!window->Open(request.title, request.width, request.height, request.visible, request.resizable)) {
        CompleteRequest(request.completion, false);
        return;
    }
    window->SetMode(request.mode);
    // Relative mouse mode (capture) hides + locks the cursor itself, so only
    // apply plain cursor visibility when not capturing.
    window->SetClickToRecapture(request.clickToRecapture);
    window->SetRelativeMouseMode(request.captureMouse);
    window->RefreshRelativeMouseMode();
    if (!request.captureMouse) {
        ApplyCursorVisibility(request.showCursor);
    }

    if (!backendReady) {
        RenderInit init;
        init.type = m_type;
        bool initialized = false;
        try {
            initialized = backend->Prepare(init) && backend->Init(init);
        } catch (...) {
            backend->Shutdown();
            window->Close();
            throw;
        }
        if (!initialized) {
            backend->Shutdown();
            window->Close();
            CompleteRequest(request.completion, false);
            return;
        }
        backendReady = true;
    }

    RenderViewHandle view = kInvalidRenderView;
    int pixelWidth = request.width;
    int pixelHeight = request.height;
    window->PixelSize(pixelWidth, pixelHeight);
    if (m_pendingWindow) {
        Debug::Logger::Error("EngineLoop", "cannot attach a window while another attach is pending cleanup");
        window->Close();
        CompleteRequest(request.completion, false);
        return;
    }
    m_pendingWindow = std::move(window);
    if (backendReady) {
        RenderViewInit viewInit;
        viewInit.window = request.id;
        viewInit.nativeWindowHandle = m_pendingWindow->NativeHandle();
        viewInit.width = static_cast<std::uint32_t>(pixelWidth);
        viewInit.height = static_cast<std::uint32_t>(pixelHeight);
        viewInit.msaa = request.msaa;
        viewInit.aa = request.aa;
        viewInit.vsync = request.vsync;
        view = backend->CreateView(viewInit);
        if (view == kInvalidRenderView) {
            RetireNativeWindow(m_pendingWindow, backend->NativeWindowRetirementFrames());
            DrainRetiredNativeWindows(false);
            CompleteRequest(request.completion, false);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(request.completion->mutex);
    if (request.completion->cancelled || request.completion->completed) {
        if (backendReady && view != kInvalidRenderView) {
            RetireNativeWindow(m_pendingWindow, backend->NativeWindowRetirementFrames());
            backend->DestroyView(view);
            DrainRetiredNativeWindows(false);
        } else {
            m_pendingWindow->Close();
            m_pendingWindow.reset();
        }
        return;
    }

    auto [slotIt, inserted] = windows.try_emplace(request.id);
    if (!inserted) {
        RetireNativeWindow(m_pendingWindow, backend->NativeWindowRetirementFrames());
        backend->DestroyView(view);
        DrainRetiredNativeWindows(false);
        request.completion->completed = true;
        request.completion->result = false;
        request.completion->done.set_value(false);
        return;
    }
    WindowSlot& slot = slotIt->second;
    slot.window = std::move(m_pendingWindow);
    slot.view = view;
    slot.width = pixelWidth;
    slot.height = pixelHeight;
    slot.requestedWidth = request.width;
    slot.requestedHeight = request.height;
    slot.mode = request.mode;
    slot.vsync = request.vsync;
    slot.msaa = request.msaa;
    slot.aa = request.aa;
    m_eventRouter.RegisterWindow(request.id, slot.window->Handle());
    SetWindowPixelSize(request.id, pixelWidth, pixelHeight);
    MarkWindowOpen(request.id, slot.window->IsRelativeMouseMode());
    SetWindowMinimized(request.id, slot.window->IsMinimized());
    SetWindowMaximized(request.id, slot.window->IsMaximized());
    request.completion->completed = true;
    request.completion->result = true;
    request.completion->done.set_value(true);
}

MeshHandle EngineLoop::Impl::EnsurePrimitiveMesh(IRenderBackend& backend, Object::PrimitiveShape shape)
{
    const auto it = m_primitiveMeshes.find(shape);
    if (it != m_primitiveMeshes.end()) {
        return it->second;
    }

    MeshData data;
    switch (shape) {
        case Object::PrimitiveShape::Sphere:   data = Primitives::Sphere();   break;
        case Object::PrimitiveShape::Cylinder: data = Primitives::Cylinder(); break;
        case Object::PrimitiveShape::Cone:     data = Primitives::Cone();     break;
        case Object::PrimitiveShape::Quad:     data = Primitives::UnitQuad(); break;
        case Object::PrimitiveShape::Capsule:  data = Primitives::Capsule();  break;
        case Object::PrimitiveShape::Torus:    data = Primitives::Torus();    break;
        case Object::PrimitiveShape::Cube:
        default:                               data = Primitives::UnitCube(); break;
    }
    const MeshHandle mesh = backend.CreateMesh(data);
    m_primitiveMeshes.emplace(shape, mesh);
    return mesh;
}

void EngineLoop::Impl::RetireNativeWindow(std::unique_ptr<SdlWindow>& window,
                                          std::uint32_t retirementFrames)
{
    if (!window) {
        return;
    }
    m_retiredWindows.emplace_back();
    RetiredWindow& retired = m_retiredWindows.back();
    const std::uint64_t remaining = std::numeric_limits<std::uint64_t>::max()
        - m_backendFrameGeneration;
    retired.closeAfterFrame = retirementFrames > remaining
        ? std::numeric_limits<std::uint64_t>::max()
        : m_backendFrameGeneration + retirementFrames;
    retired.window = std::move(window);
    retired.window->SetRelativeMouseMode(false);
    retired.window->SetVisible(false);
}

void EngineLoop::Impl::DrainRetiredNativeWindows(bool force)
{
    for (auto it = m_retiredWindows.begin(); it != m_retiredWindows.end();) {
        if (!force && m_backendFrameGeneration < it->closeAfterFrame) {
            ++it;
            continue;
        }
        if (it->window) {
            it->window->Close();
        }
        it = m_retiredWindows.erase(it);
    }
}

void EngineLoop::Impl::CloseWindow(WindowSlot& slot, IRenderBackend& backend, bool backendReady)
{
    if (backendReady && slot.view != kInvalidRenderView) {
        RetireNativeWindow(slot.window, backend.NativeWindowRetirementFrames());
        backend.DestroyView(slot.view);
        slot.view = kInvalidRenderView;
        DrainRetiredNativeWindows(false);
        return;
    }
    if (slot.window) {
        slot.window->Close();
    }
}

bool EngineLoop::Impl::HandleUpdate(const Request& request,
                                    std::unique_ptr<IRenderBackend>& backend,
                                    bool backendReady,
                                    std::unordered_map<WindowId, WindowSlot>& windows)
{
    const auto it = windows.find(request.id);
    if (it == windows.end() || !it->second.window->IsOpen()) {
        return false;
    }
    WindowSlot& slot = it->second;
    const bool requestedSizeChanged = request.width != slot.requestedWidth
        || request.height != slot.requestedHeight;
    const bool modeChanged = request.mode != slot.mode;
    slot.window->SetTitle(request.title);
    if (requestedSizeChanged) {
        slot.window->SetSize(request.width, request.height);
    }
    if (modeChanged) {
        slot.window->SetMode(request.mode);
    }
    slot.window->SetResizable(request.resizable);
    slot.window->SetVisible(request.visible);
    slot.window->SetClickToRecapture(request.clickToRecapture);
    slot.window->SetRelativeMouseMode(request.captureMouse);
    slot.window->RefreshRelativeMouseMode();
    SetMouseCaptured(request.id, slot.window->IsRelativeMouseMode());
    if (!request.captureMouse) {
        ApplyCursorVisibility(request.showCursor);
    }

    int pixelWidth = slot.width;
    int pixelHeight = slot.height;
    slot.window->PixelSize(pixelWidth, pixelHeight);
    const bool sizeChanged = pixelWidth != slot.width || pixelHeight != slot.height;
    const bool msaaChanged = request.msaa != slot.msaa;
    const bool aaChanged = request.aa != slot.aa;
    const bool vsyncChanged = request.vsync != slot.vsync;

    if (backendReady && slot.view != kInvalidRenderView && (sizeChanged || msaaChanged || aaChanged || vsyncChanged)) {
        RenderViewInit viewInit;
        viewInit.window = request.id;
        viewInit.nativeWindowHandle = slot.window->NativeHandle();
        viewInit.width = static_cast<std::uint32_t>(pixelWidth);
        viewInit.height = static_cast<std::uint32_t>(pixelHeight);
        viewInit.msaa = request.msaa;
        viewInit.aa = request.aa;
        viewInit.vsync = request.vsync;
        if (!backend->RecreateView(slot.view, viewInit)) {
            Debug::Logger::Error("EngineLoop", "failed to recreate render view for window %llu",
                                 static_cast<unsigned long long>(request.id));
            return false;
        }
        // MSAA and vsync are process-wide bgfx::reset state, so a change
        // through one window becomes the value every window now shares.
        if (msaaChanged || vsyncChanged) {
            for (auto& [windowId, otherSlot] : windows) {
                otherSlot.msaa = request.msaa;
                otherSlot.vsync = request.vsync;
            }
        }
    }

    slot.width = pixelWidth;
    slot.height = pixelHeight;
    slot.requestedWidth = request.width;
    slot.requestedHeight = request.height;
    slot.mode = request.mode;
    slot.vsync = request.vsync;
    slot.msaa = request.msaa;
    slot.aa = request.aa;
    SetWindowPixelSize(request.id, pixelWidth, pixelHeight);
    SetWindowMinimized(request.id, slot.window->IsMinimized());
    SetWindowMaximized(request.id, slot.window->IsMaximized());
    return true;
}

bool EngineLoop::Impl::HandleControl(const Request& request,
                                     std::unordered_map<WindowId, WindowSlot>& windows)
{
    const auto it = windows.find(request.id);
    if (it == windows.end() || !it->second.window->IsOpen()) {
        return false;
    }

    SdlWindow& window = *it->second.window;
    switch (request.control) {
        case Request::Control::Minimize:
            window.Minimize();
            break;
        case Request::Control::Maximize:
            window.Maximize();
            break;
        case Request::Control::Restore:
            window.Restore();
            break;
        case Request::Control::BeginDrag:
            window.BeginDrag(request.pointerX, request.pointerY);
            break;
        case Request::Control::BeginResize:
            window.BeginResize(request.resizeEdge);
            break;
        case Request::Control::SetChrome:
            window.SetChrome(request.chrome);
            break;
    }

    SetWindowMinimized(request.id, window.IsMinimized());
    SetWindowMaximized(request.id, window.IsMaximized());
    return true;
}

} // namespace Concord
