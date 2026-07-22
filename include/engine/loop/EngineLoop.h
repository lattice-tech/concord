#ifndef CONCORD_ENGINELOOP_H
#define CONCORD_ENGINELOOP_H

#include "engine/loop/FrameStats.h"
#include "engine/loop/UpdateDispatcher.h"
#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/render/frame/CameraView.h"
#include "engine/render/backend/RenderBackendType.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/frame/WorldSnapshot.h"
#include "engine/render/mesh/MeshHandle.h"
#include "engine/task/TaskGraph.h"
#include "engine/window/WindowId.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace Concord {

class Window;
struct WindowDesc;
struct MeshData;

/**
 * The engine's shared render thread for the whole process.
 *
 * One process-wide instance is lazily created by the first Game (see
 * Acquire) and kept alive through shared ownership for as long as any Game
 * still references it; the last owner to let go stops the thread and tears
 * down every window still open. This is what lets multiple Game objects
 * coexist without colliding: bgfx's globally-scoped state and every
 * SdlWindow are only ever touched by this one thread, exactly once per
 * process, no matter how many Game objects are alive.
 *
 * It lives on its own thread on purpose: while application logic runs (or
 * sleeps, see Concord::Sleep) on other threads, this thread keeps every
 * open window pumping events and rendering, so none of them ever freeze.
 *
 * A window is requested with AttachWindow and dropped with DetachWindow;
 * both only enqueue a request for the loop thread, which is the single
 * thread that actually creates, pumps and destroys OS windows and render
 * views (window message queues must be pumped by the thread that created
 * them, and bgfx's API must be called from a single, consistent thread).
 */
class EngineLoop {
public:
    /** @brief Process-unique window handle; see Concord::WindowId. */
    using WindowId = Concord::WindowId;
    static constexpr WindowId kInvalidWindowId = Concord::kInvalidWindowId;

    using UpdateId = UpdateDispatcher::UpdateId;
    static constexpr UpdateId kInvalidUpdateId = UpdateDispatcher::kInvalidId;

    ~EngineLoop();

    EngineLoop(const EngineLoop&) = delete;
    EngineLoop& operator=(const EngineLoop&) = delete;

    /**
     * Returns the process-wide loop, starting it on the first call.
     * Later calls reuse that same instance and ignore `preferredBackend`,
     * since the graphics device it already started with is committed for
     * the rest of the process's lifetime.
     */
    static std::shared_ptr<EngineLoop> Acquire(RenderBackendType preferredBackend);

    /**
     * Requests the loop to open a window described by `window`. Calls from
     * other threads block for a bounded interval; a timeout cancels the
     * request, including an in-progress native window that has not committed.
     * Calls from the loop thread are rejected because synchronous creation
     * cannot complete until the current update callback returns.
     * @return A live, manageable handle, or kInvalidWindowId on failure/timeout.
     */
    WindowId AttachWindow(const Window& window);

    /**
     * Requests the loop to close `id`'s window. Loop-thread calls enqueue and
     * return immediately. Other calls wait for a bounded interval; a timeout
     * cancels work that has not started, while already-started closure finishes
     * asynchronously and still completes its internal request state.
     */
    void DetachWindow(WindowId id);

    /**
     * Sets the anti-aliasing technique applied to windows attached from now on.
     * Process-wide (the shared loop backs every window), so the most recent
     * caller wins — the same "last write" model as MSAA/vsync. Set it before
     * AttachWindow; the Game does this from the config.
     */
    void SetAntiAliasing(AntiAliasing aa);

    /**
     * Requests the loop to apply `desc`'s title/resolution/cursor to `id`'s
     * already-open window. Loop-thread calls enqueue and return immediately.
     * Other calls wait for a bounded interval; a timeout cancels work that has
     * not started, while an already-started update may finish asynchronously.
     * A no-op if `id` no longer names an open window (e.g. detached).
     */
    void UpdateWindow(WindowId id, const WindowDesc& desc);

    /**
     * Registers `callback` once per turn on the simulation coordinator,
     * receiving the time elapsed since the previous frame (seconds). Runs
     * for as long as it stays registered, whether or not its caller has a
     * window attached. Fires from the frame after this call onward — it
     * does not block waiting for the simulation coordinator, since there is nothing
     * for the caller to observe as "applied" the way a window request has.
     * @return A handle to pass to RemoveUpdate, or kInvalidUpdateId if `callback` is empty.
     */
    UpdateId OnUpdate(std::function<void(float deltaTime)> callback);

    /** Unregisters a callback previously returned by OnUpdate; a no-op if `id` is already gone. */
    void RemoveUpdate(UpdateId id);

    /** Time elapsed between the two most recently completed frames, in seconds. */
    float DeltaTime() const noexcept;

    /** Total number of frames the loop has completed since it started. */
    std::uint64_t FrameCount() const noexcept;

    /** Current simulation-coordinator turn generation. */
    std::uint64_t SimulationGeneration() const noexcept;

    /** Smoothed frames per second the loop is currently running at. */
    float Fps() const noexcept;

    /** Latest measured CPU phase times for the completed frame (any thread). */
    FrameStats Stats() const noexcept;

    /** Atomically publishes one complete simulation/render snapshot. */
    void PublishWorldSnapshot(WindowId window, WorldSnapshot snapshot);

    /**
     * Uploads `data` into GPU buffers and returns a handle naming them.
     *
     * Render-thread calls upload inline. Scene extraction runs on a CPU worker,
     * so its uploads use the request queue. From any other thread the request is queued and the
     * caller blocks for a bounded interval, mirroring AttachWindow. Returns an
     * invalid handle if the backend is not ready or `data` is empty.
     */
    MeshHandle CreateMesh(const MeshData& data);

    /**
     * Queues a mesh upload without imposing a timeout on the caller.
     * The future becomes ready with an invalid handle if the loop stops or the
     * backend is unavailable. Render-thread calls return an already-ready future.
     */
    std::future<MeshHandle> CreateMeshAsync(MeshData data);

    /**
     * Releases the GPU buffers named by `mesh`. Safe to call from any thread;
     * on the render thread it frees inline, otherwise it queues the teardown.
     * A no-op for an invalid or already-freed handle.
     */
    void DestroyMesh(MeshHandle mesh);

    /** True while `id` still names a window the loop has open. Thread-safe. */
    bool IsWindowOpen(WindowId id) const;

    /** True when SDL relative mouse mode is physically active for `id`. Thread-safe. */
    bool IsMouseCaptured(WindowId id) const;

    /**
     * Blocks the calling thread until `id`'s window closes — whether the user
     * closes it (the title-bar ✕) or DetachWindow does. Returns immediately if
     * the window is already gone or never opened, or once the loop stops. This
     * is how an application keeps running until its window is dismissed rather
     * than for a fixed time.
     */
    void WaitForWindowClose(WindowId id);

private:
    friend class Scene;
    friend class EngineLoopLifecycle;

    EngineLoop();

    /** Returns whether the caller is the thread currently executing the loop. */
    bool IsLoopThread() const;
    /** Returns whether the caller is an engine-owned render/simulation thread. */
    bool IsInternalThread() const;

    /** Registers scene ticking after ordinary Game update callbacks. */
    UpdateId OnSceneUpdate(std::function<void(float deltaTime)> callback);

    /** Executes a CPU-only Scene task graph on the shared worker pool. */
    TaskGraphStats RunTaskGraph(TaskGraph graph);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Concord

#endif // CONCORD_ENGINELOOP_H
