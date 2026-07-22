#ifndef CONCORD_ENGINELOOPIMPL_H
#define CONCORD_ENGINELOOPIMPL_H

#include "engine/loop/EngineLoop.h"

#include "engine/loop/FrameClock.h"
#include "engine/loop/FrameStats.h"
#include "engine/loop/SdlEventRouter.h"
#include "engine/loop/UpdateDispatcher.h"
#include "engine/events/EventBusCore.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/CameraView.h"
#include "engine/render/frame/RenderInstance.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/frame/WorldSnapshot.h"
#include "engine/task/JobSystem.h"
#include "engine/task/TaskGraph.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/render/mesh/MeshHandle.h"
#include "engine/window/MsaaLevel.h"
#include "engine/window/SdlWindow.h"
#include "engine/window/WindowDesc.h"
#include "engine/window/WindowMode.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Concord {

/**
 * Idle wait used when the loop has no windows.
 */
inline constexpr std::chrono::milliseconds kIdleWait{50};

/** How long ordinary synchronous requests wait for the loop thread to react. */
inline constexpr std::chrono::milliseconds kRequestTimeout{3000};

/** Allows first-window Vulkan device initialization without cancelling a valid attach. */
inline constexpr std::chrono::milliseconds kWindowAttachTimeout{15000};

/**
 * @brief Owns EngineLoop coordination and render-thread state.
 *
 * SDL windows and the render backend are accessed only by the loop thread.
 */
class EngineLoop::Impl {
public:
    ~Impl();

    void Start(RenderBackendType type);

    /** Returns whether the caller is the thread currently executing Run(). */
    bool IsLoopThread() const;
    /** Returns whether the caller is either engine-owned coordinator thread. */
    bool IsInternalThread() const;

    WindowId AttachWindow(const Window& window);
    void DetachWindow(WindowId id);
    void SetAntiAliasing(AntiAliasing aa);
    void UpdateWindow(WindowId id, const WindowDesc& desc);

    UpdateId OnUpdate(std::function<void(float)> callback);
    UpdateId OnSceneUpdate(std::function<void(float)> callback);
    void RemoveUpdate(UpdateId id);

    float DeltaTime() const noexcept;
    std::uint64_t FrameCount() const noexcept;
    std::uint64_t SimulationGeneration() const noexcept;
    float Fps() const noexcept;
    FrameStats Stats() const noexcept;

    void PublishWorldSnapshot(WindowId window, WorldSnapshot snapshot);
    std::shared_ptr<const WorldSnapshot> AcquireWorldSnapshot(WindowId window);
    TaskGraphStats RunTaskGraph(TaskGraph graph);

    bool IsWindowOpen(WindowId id) const;
    bool IsMouseCaptured(WindowId id) const;
    void WaitForWindowClose(WindowId id);

    /**
     * Applies process-wide SDL cursor visibility using the most recently
     * requested value.
     */
    void ApplyCursorVisibility(bool visible);

    /** Timing collected while draining mesh requests. */
    struct RequestDrainStats {
        float processingMs = 0.0f;
        float queueLatencyMs = 0.0f;
    };

    /**
     * Processes pending mesh requests on the render thread.
     *
     * Create requests complete with an invalid handle when the backend is not
     * ready. Destroy requests are ignored until a backend is ready.
     */
    RequestDrainStats DrainMeshRequests();

    /**
     * Uploads `data` inline on the render thread and queues other callers.
     *
     * Returns an invalid handle when the backend is not ready or the data is
     * empty.
     */
    MeshHandle CreateMesh(MeshData data);
    std::future<MeshHandle> CreateMeshAsync(MeshData data);

    /** Releases `mesh`: inline on the render thread, queued otherwise. */
    void DestroyMesh(MeshHandle mesh);

    void Run();

private:
    struct RequestCompletion {
        std::mutex mutex;
        bool cancelled = false;
        bool processing = false;
        bool completed = false;
        bool result = false;
        std::promise<bool> done;
    };

    struct Request {
        enum class Kind { Attach, Detach, Update };
        Kind kind = Kind::Attach;
        WindowId id = kInvalidWindowId;
        std::string title;
        int width = 0;
        int height = 0;
        WindowMode mode = WindowMode::Windowed;
        bool resizable = true;
        bool visible = true;
        bool vsync = true;
        bool showCursor = true;
        bool captureMouse = false;
        bool clickToRecapture = true;
        MsaaLevel msaa = MsaaLevel::X4;
        AntiAliasing aa = AntiAliasing::Off;
        std::shared_ptr<RequestCompletion> completion = std::make_shared<RequestCompletion>();
        std::chrono::steady_clock::time_point enqueuedAt{};
    };

    struct WindowSlot {
        std::unique_ptr<SdlWindow> window;
        RenderViewHandle view = kInvalidRenderView;
        int width = 0;
        int height = 0;
        int requestedWidth = 0;
        int requestedHeight = 0;
        WindowMode mode = WindowMode::Windowed;
        bool vsync = true;
        MsaaLevel msaa = MsaaLevel::X4;
        AntiAliasing aa = AntiAliasing::Off;
    };

    /**
     * Mesh operations submitted by threads that cannot access the backend.
     * The loop thread drains this queue.
     */
    struct MeshRequest {
        struct Completion {
            std::mutex mutex;
            bool cancelled = false;
            bool processing = false;
            bool completed = false;
            MeshHandle result{};
            std::promise<MeshHandle> done;
        };

        bool destroy = false;
        MeshData data{};
        MeshHandle handle{};
        std::shared_ptr<Completion> completion = std::make_shared<Completion>();
        std::chrono::steady_clock::time_point enqueuedAt{};
    };

    static void CompleteRequest(const std::shared_ptr<RequestCompletion>& completion, bool result);
    static bool WaitForRequest(const std::shared_ptr<RequestCompletion>& completion,
                               std::future<bool>& future,
                               std::chrono::milliseconds timeout = kRequestTimeout);

    void SetLoopThreadId(std::thread::id id);
    bool Enqueue(Request&& request);
    void RejectPendingRequests();
    void RejectMeshRequests();
    void Stop();
    void Wake();
    void QueueSimulation(float deltaTime);
    void SimulationMain();

    /** Records a window as open and wakes any WaitForWindowClose waiters (render thread). */
    void MarkWindowOpen(WindowId id, bool mouseCaptured);
    /** Records a window as closed and wakes any WaitForWindowClose waiters (render thread). */
    void MarkWindowClosed(WindowId id);
    /** Mirrors render-thread SDL state for public thread-safe observation. */
    void SetMouseCaptured(WindowId id, bool mouseCaptured);

    /** Opens `request`'s window and registers it with the backend, if one is ready. */
    void HandleAttach(const Request& request,
                      std::unique_ptr<IRenderBackend>& backend,
                      bool& backendReady,
                      std::unordered_map<WindowId, WindowSlot>& windows);

    /**
     * Returns the shared mesh for `shape`, uploading it on first use. Called
     * from the draw loop on the render thread, so CreateMesh is safe here; the
     * mesh then lives for the whole backend lifetime (freed by Shutdown).
     */
    MeshHandle EnsurePrimitiveMesh(IRenderBackend& backend, Object::PrimitiveShape shape);

    void CloseWindow(WindowSlot& slot, IRenderBackend& backend, bool backendReady);

    /**
     * Applies a live title/size/mode/cursor/MSAA/vsync change to an already-open
     * window. Resizes the OS window and recreates the render framebuffer when the
     * backbuffer size or MSAA level changes (vsync-only changes just reset the device).
     */
    bool HandleUpdate(const Request& request,
                      std::unique_ptr<IRenderBackend>& backend,
                      bool backendReady,
                      std::unordered_map<WindowId, WindowSlot>& windows);

    std::thread m_thread;
    std::thread m_simulationThread;
    mutable std::mutex m_loopThreadMutex;
    std::thread::id m_loopThreadId;
    std::thread::id m_simulationThreadId;
    std::atomic<bool> m_running{false};
    RenderBackendType m_type = RenderBackendType::Auto;
    std::atomic<AntiAliasing> m_antiAliasing{AntiAliasing::Off};

    /**
     * Non-owning backend state for mesh operations invoked on the render
     * thread. `m_backendPtr` is valid only while Run() is executing.
     */
    IRenderBackend* m_backendPtr = nullptr;
    bool m_backendReady = false;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::queue<Request> m_requests;
    std::uint64_t m_wakeGeneration = 0;
    std::atomic<std::uint64_t> m_eventGeneration{0};

    FrameClock m_frameClock;
    UpdateDispatcher m_updateDispatcher;
    JobSystem m_jobs;
    SdlEventRouter m_eventRouter;
    mutable std::mutex m_statsMutex;
    FrameStats m_stats;
    std::unordered_map<Object::PrimitiveShape, MeshHandle> m_primitiveMeshes; // shared per-shape meshes

    std::mutex m_meshMutex;
    std::queue<MeshRequest> m_meshRequests;

    struct SnapshotBuffers {
        std::shared_ptr<const WorldSnapshot> published;
        std::shared_ptr<const WorldSnapshot> rendering;
    };
    std::mutex m_snapshotMutex;
    std::unordered_map<WindowId, SnapshotBuffers> m_worldSnapshots;

    std::mutex m_simulationMutex;
    std::condition_variable m_simulationReady;
    bool m_simulationStopping = false;
    bool m_simulationPending = false;
    float m_pendingDeltaTime = 0.0f;
    UpdateDispatcher::RunStats m_updateStats;
    float m_eventDispatchMs = 0.0f;
    std::atomic<std::uint64_t> m_simulationGeneration{0};

    mutable std::mutex m_openMutex;
    std::condition_variable m_openCv;
    std::unordered_set<WindowId> m_openWindows; // ids the loop currently has open
    std::unordered_map<WindowId, bool> m_mouseCaptured;
};

} // namespace Concord

#endif // CONCORD_ENGINELOOPIMPL_H
