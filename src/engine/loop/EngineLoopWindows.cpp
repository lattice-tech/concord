#include "engine/loop/EngineLoopImpl.h"

#include "engine/debug/Logger.h"
#include "engine/render/mesh/Primitives.h"

#include <SDL3/SDL.h>

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
    }
    m_openCv.notify_all();
}

void EngineLoop::Impl::MarkWindowClosed(WindowId id)
{
    {
        std::lock_guard<std::mutex> lock(m_openMutex);
        m_openWindows.erase(id);
        m_mouseCaptured.erase(id);
    }
    // State publishers hold m_openMutex through their per-map write. Once the
    // membership erase above completes, no publisher can reintroduce this id.
    std::lock_guard<std::mutex> stateLock(m_snapshotMutex);
    m_worldSnapshots.erase(id);
    m_openCv.notify_all();
}

void EngineLoop::Impl::SetMouseCaptured(WindowId id, bool mouseCaptured)
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    if (m_openWindows.count(id) != 0) {
        m_mouseCaptured[id] = mouseCaptured;
    }
}

bool EngineLoop::Impl::IsMouseCaptured(WindowId id) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    const auto it = m_mouseCaptured.find(id);
    return it != m_mouseCaptured.end() && it->second;
}

bool EngineLoop::Impl::IsWindowOpen(WindowId id) const
{
    std::lock_guard<std::mutex> lock(m_openMutex);
    return m_openWindows.count(id) != 0;
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
    if (backendReady) {
        RenderViewInit viewInit;
        viewInit.nativeWindowHandle = window->NativeHandle();
        viewInit.width = static_cast<std::uint32_t>(pixelWidth);
        viewInit.height = static_cast<std::uint32_t>(pixelHeight);
        viewInit.msaa = request.msaa;
        viewInit.aa = request.aa;
        viewInit.vsync = request.vsync;
        try {
            view = backend->CreateView(viewInit);
        } catch (...) {
            window->Close();
            throw;
        }
        if (view == kInvalidRenderView) {
            window->Close();
            CompleteRequest(request.completion, false);
            return;
        }
    }

    std::lock_guard<std::mutex> lock(request.completion->mutex);
    if (request.completion->cancelled || request.completion->completed) {
        if (backendReady && view != kInvalidRenderView) {
            backend->DestroyView(view);
        }
        window->Close();
        return;
    }

    m_eventRouter.RegisterWindow(request.id, window->Handle());
    windows.emplace(request.id, WindowSlot{
        std::move(window),
        view,
        pixelWidth,
        pixelHeight,
        request.width,
        request.height,
        request.mode,
        request.vsync,
        request.msaa,
        request.aa,
    });
    MarkWindowOpen(request.id, windows.at(request.id).window->IsRelativeMouseMode());
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

void EngineLoop::Impl::CloseWindow(WindowSlot& slot, IRenderBackend& backend, bool backendReady)
{
    if (slot.view != kInvalidRenderView) {
        backend.DestroyView(slot.view);
    }
    slot.window->Close();
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
    return true;
}

} // namespace Concord
