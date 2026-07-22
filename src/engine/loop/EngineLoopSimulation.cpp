#include "engine/loop/EngineLoopImpl.h"

#include "engine/debug/Logger.h"
#include "engine/input/InputState.h"
#include "engine/input/action/InputActions.h"

#include <exception>
#include <algorithm>
#include <chrono>
#include <utility>

namespace Concord {

void EngineLoop::Impl::RequestSimulation()
{
    QueueSimulation(0.0f);
}

void EngineLoop::Impl::QueueSimulation(float deltaTime)
{
    {
        std::lock_guard<std::mutex> lock(m_simulationMutex);
        m_pendingDeltaTime += deltaTime;
        m_simulationPending = true;
    }
    m_simulationReady.notify_one();
}

void EngineLoop::Impl::SimulationMain()
{
    {
        std::lock_guard<std::mutex> lock(m_loopThreadMutex);
        m_simulationThreadId = std::this_thread::get_id();
    }
    while (true) {
        float deltaTime = 0.0f;
        {
            std::unique_lock<std::mutex> lock(m_simulationMutex);
            m_simulationReady.wait(lock, [this] {
                return m_simulationStopping || m_simulationPending;
            });
            if (m_simulationStopping) {
                std::lock_guard<std::mutex> threadLock(m_loopThreadMutex);
                m_simulationThreadId = {};
                return;
            }
            deltaTime = std::min(m_pendingDeltaTime, 0.25f);
            m_pendingDeltaTime = 0.0f;
            m_simulationPending = false;
        }

        try {
            m_simulationGeneration.fetch_add(1, std::memory_order_acq_rel);
            InputState::Instance().BeginSimulationFrame();
            InputActions::UpdateFromInputState();
            const auto eventStart = std::chrono::steady_clock::now();
            EventDetail::EventBusCore::Dispatch(m_eventGeneration.load());
            const float eventDispatchMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - eventStart).count();
            const UpdateDispatcher::RunStats stats = m_updateDispatcher.RunAll(deltaTime);
            std::lock_guard<std::mutex> lock(m_simulationMutex);
            m_updateStats = stats;
            m_eventDispatchMs = eventDispatchMs;
        } catch (const std::exception& exception) {
            Debug::Logger::Error("Simulation", "frame task graph failed: %s", exception.what());
        } catch (...) {
            Debug::Logger::Error("Simulation", "frame task graph failed with an unknown exception");
        }
    }
}

} // namespace Concord
