#include "engine/loop/EngineLoopImpl.h"

#include "engine/debug/Logger.h"
#include "engine/input/InputState.h"
#include "engine/render/backend/BgfxRenderBackend.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using SteadyClock = std::chrono::steady_clock;

float ElapsedMs(SteadyClock::time_point start, SteadyClock::time_point end)
{
    return std::chrono::duration<float, std::milli>(end - start).count();
}

} // namespace

namespace Concord {

void EngineLoop::Impl::Run()
{
    SetLoopThreadId(std::this_thread::get_id());
    bool sdlInitialized = false;
    std::unique_ptr<IRenderBackend> backend;
    std::unordered_map<WindowId, WindowSlot> windows;

    try {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            Debug::Logger::Error("EngineLoop", "SDL video initialization failed: %s", SDL_GetError());
            m_running.store(false);
        } else {
            sdlInitialized = true;
            backend = std::make_unique<BgfxRenderBackend>();
            m_backendPtr = backend.get();
        }

        auto drainControlRequests = [&]() {
            RequestDrainStats stats;
            const auto processingStart = SteadyClock::now();
            std::vector<Request> pending;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                while (!m_requests.empty()) {
                    pending.push_back(std::move(m_requests.front()));
                    m_requests.pop();
                }
            }

            for (Request& request : pending) {
                try {
                    stats.queueLatencyMs = std::max(
                        stats.queueLatencyMs, ElapsedMs(request.enqueuedAt, SteadyClock::now()));
                    if (request.kind == Request::Kind::Attach) {
                        HandleAttach(request, backend, m_backendReady, windows);
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(request.completion->mutex);
                        if (request.completion->cancelled || request.completion->completed) {
                            continue;
                        }
                        request.completion->processing = true;
                    }

                    bool ok = true;
                    if (request.kind == Request::Kind::Update) {
                        ok = HandleUpdate(request, backend, m_backendReady, windows);
                    } else {
                        const auto it = windows.find(request.id);
                        if (it != windows.end()) {
                            m_eventRouter.UnregisterWindow(request.id);
                            CloseWindow(it->second, *backend, m_backendReady);
                            windows.erase(it);
                        }
                        MarkWindowClosed(request.id);
                    }
                    CompleteRequest(request.completion, ok);
                } catch (...) {
                    for (Request& uncompleted : pending) {
                        CompleteRequest(uncompleted.completion, false);
                    }
                    throw;
                }
            }
            stats.processingMs = ElapsedMs(processingStart, SteadyClock::now());
            return stats;
        };

        while (sdlInitialized && m_running.load()) {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (m_requests.empty() && m_running.load() && windows.empty()) {
                    const std::uint64_t wakeGeneration = m_wakeGeneration;
                    m_queueCv.wait_for(lock, kIdleWait, [this, wakeGeneration] {
                        return !m_running.load() || !m_requests.empty()
                            || m_wakeGeneration != wakeGeneration;
                    });
                }
            }

            RequestDrainStats requestStats = drainControlRequests();
            const RequestDrainStats meshStats = DrainMeshRequests();
            requestStats.processingMs += meshStats.processingMs;
            requestStats.queueLatencyMs = std::max(requestStats.queueLatencyMs, meshStats.queueLatencyMs);

            const auto frameStart = SteadyClock::now();
            InputState::Instance().BeginFrame();
            m_eventRouter.BeginFrame();

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                m_eventRouter.Route(event);
                if (event.type == SDL_EVENT_QUIT) {
                    for (auto& [id, slot] : windows) {
                        m_eventRouter.NotifyCloseRequested(id);
                        slot.window->RequestClose();
                    }
                    continue;
                }
                for (auto& [id, slot] : windows) {
                    slot.window->HandleEvent(event);
                }
            }
            for (const auto& [id, slot] : windows) {
                slot.window->RefreshRelativeMouseMode();
                SetMouseCaptured(id, slot.window->IsRelativeMouseMode());
            }
            for (auto it = windows.begin(); it != windows.end();) {
                if (!it->second.window->CloseRequested()) {
                    ++it;
                    continue;
                }
                const WindowId closedId = it->first;
                m_eventRouter.NotifyCloseRequested(closedId);
                m_eventRouter.UnregisterWindow(closedId);
                CloseWindow(it->second, *backend, m_backendReady);
                it = windows.erase(it);
                MarkWindowClosed(closedId);
            }
            for (auto& [id, slot] : windows) {
                int newWidth = 0;
                int newHeight = 0;
                if (!slot.window->TakeResize(newWidth, newHeight) || newWidth <= 0 || newHeight <= 0) {
                    continue;
                }
                if (m_backendReady && slot.view != kInvalidRenderView) {
                    RenderViewInit viewInit;
                    viewInit.nativeWindowHandle = slot.window->NativeHandle();
                    viewInit.width = static_cast<std::uint32_t>(newWidth);
                    viewInit.height = static_cast<std::uint32_t>(newHeight);
                    viewInit.msaa = slot.msaa;
                    viewInit.aa = slot.aa;
                    viewInit.vsync = slot.vsync;
                    if (!backend->RecreateView(slot.view, viewInit)) {
                        Debug::Logger::Error("EngineLoop", "failed to resize render view for window %llu",
                                             static_cast<unsigned long long>(id));
                        continue;
                    }
                }
                slot.width = newWidth;
                slot.height = newHeight;
                m_eventRouter.NotifyResized(id, newWidth, newHeight);
            }
            m_eventRouter.FlushCoalesced();
            const auto inputEnd = SteadyClock::now();

            RequestDrainStats phaseRequests = drainControlRequests();
            requestStats.processingMs += phaseRequests.processingMs;
            requestStats.queueLatencyMs = std::max(requestStats.queueLatencyMs, phaseRequests.queueLatencyMs);

            const float deltaTime = m_frameClock.Tick();
            QueueSimulation(deltaTime);
            const auto updateEnd = SteadyClock::now();

            phaseRequests = drainControlRequests();
            requestStats.processingMs += phaseRequests.processingMs;
            requestStats.queueLatencyMs = std::max(requestStats.queueLatencyMs, phaseRequests.queueLatencyMs);

            std::uint32_t renderInstanceCount = 0;
            std::uint32_t colliderCount = 0;
            WorldSnapshot telemetry;
            bool hasTelemetry = false;
            for (auto& [id, slot] : windows) {
                if (!m_backendReady || slot.view == kInvalidRenderView) {
                    continue;
                }
                const std::shared_ptr<const WorldSnapshot> snapshot = AcquireWorldSnapshot(id);
                if (!snapshot) {
                    backend->RenderView(slot.view, nullptr, nullptr, 0, nullptr, nullptr, 0);
                    continue;
                }
                renderInstanceCount += static_cast<std::uint32_t>(snapshot->instances.size());
                colliderCount += snapshot->colliderCount;
                if (!hasTelemetry || snapshot->simulationFrame > telemetry.simulationFrame
                    || (snapshot->simulationFrame == telemetry.simulationFrame
                        && snapshot->generation > telemetry.generation)) {
                    telemetry.generation = snapshot->generation;
                    telemetry.simulationFrame = snapshot->simulationFrame;
                    telemetry.nodeCount = snapshot->nodeCount;
                    telemetry.nodeCallbackMs = snapshot->nodeCallbackMs;
                    telemetry.extractionMs = snapshot->extractionMs;
                    telemetry.collisionMs = snapshot->collisionMs;
                    telemetry.taskGraphMs = snapshot->taskGraphMs;
                    telemetry.taskNodeCount = snapshot->taskNodeCount;
                    telemetry.slowestTaskMs = snapshot->slowestTaskMs;
                    telemetry.slowestTask = snapshot->slowestTask;
                    hasTelemetry = true;
                }
                for (const RenderInstance& instance : snapshot->instances) {
                    const MeshHandle mesh = instance.mesh.IsValid()
                        ? instance.mesh : EnsurePrimitiveMesh(*backend, instance.shape);
                    if (!mesh.IsValid()) {
                        continue;
                    }
                    MeshDrawCommand command{};
                    command.mesh = mesh;
                    std::memcpy(command.worldMatrix, instance.world, sizeof(command.worldMatrix));
                    command.material = instance.material;
                    command.effect = instance.effect;
                    command.rayTraced = instance.rayTraced;
                    command.reflectionOwner = instance.reflectionOwner;
                    command.bonePalette = instance.bonePalette;
                    command.boneCount = instance.boneCount;
                    backend->SubmitMesh(slot.view, command);
                }
                backend->RenderView(slot.view, snapshot->hasCamera ? &snapshot->camera : nullptr,
                                    snapshot->lights.data(),
                                    static_cast<std::uint32_t>(snapshot->lights.size()),
                                    snapshot->hasSky ? &snapshot->sky : nullptr,
                                    snapshot->smokeVolumes.data(),
                                    static_cast<std::uint32_t>(snapshot->smokeVolumes.size()));
            }
            const auto submitEnd = SteadyClock::now();

            phaseRequests = drainControlRequests();
            requestStats.processingMs += phaseRequests.processingMs;
            requestStats.queueLatencyMs = std::max(requestStats.queueLatencyMs, phaseRequests.queueLatencyMs);
            if (m_backendReady) {
                backend->Frame();
            }
            const BackendFrameStats backendStats = m_backendReady ? backend->Stats() : BackendFrameStats{};
            const auto presentEnd = SteadyClock::now();

            FrameStats stats;
            stats.inputMs = ElapsedMs(frameStart, inputEnd);
            stats.updateMs = ElapsedMs(inputEnd, updateEnd);
            stats.submitMs = ElapsedMs(updateEnd, submitEnd);
            stats.presentMs = ElapsedMs(submitEnd, presentEnd);
            stats.totalMs = ElapsedMs(frameStart, presentEnd);
            {
                std::lock_guard<std::mutex> simulationLock(m_simulationMutex);
                stats.eventDispatchMs = m_eventDispatchMs;
                stats.eventsMs = m_eventDispatchMs;
                stats.systemMs = m_updateStats.updateMs;
                stats.sceneMs = m_updateStats.sceneMs;
                stats.sceneUpdateMs = m_updateStats.sceneMs;
                stats.systemCount = m_updateStats.updateCallbacks;
                stats.sceneCount = m_updateStats.sceneCallbacks;
                stats.slowestCallbackMs = m_updateStats.slowestCallbackMs;
                stats.callbackBudgetOverruns = m_updateStats.callbackBudgetOverruns;
                if (!hasTelemetry) {
                    stats.simulationGeneration = m_simulationGeneration;
                }
            }
            stats.taskGraphMs = telemetry.taskGraphMs;
            stats.taskNodeCount = telemetry.taskNodeCount;
            stats.renderInstanceCount = renderInstanceCount;
            stats.colliderCount = colliderCount;
            stats.nodeCount = telemetry.nodeCount;
            stats.nodeCallbackMs = telemetry.nodeCallbackMs;
            stats.extractionMs = telemetry.extractionMs;
            stats.renderCollectMs = telemetry.extractionMs;
            stats.collisionMs = telemetry.collisionMs;
            stats.slowestTaskMs = telemetry.slowestTaskMs;
            stats.slowestTask = telemetry.slowestTask;
            stats.simulationFrame = telemetry.simulationFrame;
            if (hasTelemetry) {
                stats.simulationGeneration = telemetry.generation;
            }
            stats.gpuFrameMs = backendStats.gpuFrameMs;
            stats.gpuStatsValid = backendStats.valid;
            stats.gpuPassCount = backendStats.passCount;
            stats.slowestGpuPassMs = backendStats.slowestPassMs;
            stats.slowestGpuPass = backendStats.slowestPass;
            stats.drawCalls = backendStats.drawCalls;
            stats.computeCalls = backendStats.computeCalls;
            stats.gpuFrame = backendStats.gpuFrame;
            stats.textureCount = backendStats.textureCount;
            stats.textureMemoryBytes = backendStats.textureMemoryBytes;
            stats.renderTargetMemoryBytes = backendStats.renderTargetMemoryBytes;
            stats.transientVertexBytes = backendStats.transientVertexBytes;
            stats.transientIndexBytes = backendStats.transientIndexBytes;
            stats.requestProcessingMs = requestStats.processingMs;
            stats.requestQueueLatencyMs = requestStats.queueLatencyMs;
            stats.requestLatencyMs = requestStats.queueLatencyMs;
            stats.frameCount = m_frameClock.FrameCount();
            std::lock_guard<std::mutex> lock(m_statsMutex);
            m_stats = stats;
        }
    } catch (const std::exception& exception) {
        Debug::Logger::Error("EngineLoop", "render loop terminated after exception: %s", exception.what());
    } catch (...) {
        Debug::Logger::Error("EngineLoop", "render loop terminated after an unknown exception");
    }

    m_running.store(false);
    EventDetail::EventBusCore::Shutdown(m_eventGeneration.exchange(0));
    for (auto& [id, slot] : windows) {
        try {
            m_eventRouter.UnregisterWindow(id);
            if (backend) {
                CloseWindow(slot, *backend, m_backendReady);
            } else {
                slot.window->Close();
            }
        } catch (...) {
            Debug::Logger::Error("EngineLoop", "window cleanup failed for window %llu",
                                 static_cast<unsigned long long>(id));
        }
        MarkWindowClosed(id);
    }
    windows.clear();
    m_eventRouter.Reset();
    if (backend) {
        try {
            backend->Shutdown();
        } catch (...) {
            Debug::Logger::Error("EngineLoop", "render backend cleanup failed");
        }
    }
    m_backendPtr = nullptr;
    m_backendReady = false;
    RejectPendingRequests();
    RejectMeshRequests();
    if (sdlInitialized) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    m_queueCv.notify_all();
    m_openCv.notify_all();
    SetLoopThreadId({});
}

} // namespace Concord
