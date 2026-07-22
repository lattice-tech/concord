#include "engine/loop/FrameClock.h"

namespace Concord {

float FrameClock::Tick() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    const float deltaTime = std::chrono::duration<float>(now - m_lastTick).count();
    m_lastTick = now;
    m_deltaTime.store(deltaTime, std::memory_order_relaxed);
    m_frameCount.fetch_add(1, std::memory_order_relaxed);

    if (deltaTime > 0.0f) {
        const float instantaneous = 1.0f / deltaTime;
        const float previous = m_fps.load(std::memory_order_relaxed);
        // Exponential moving average so readers see a stable rate, not per-frame jitter.
        const float smoothed = previous > 0.0f ? previous * 0.9f + instantaneous * 0.1f : instantaneous;
        m_fps.store(smoothed, std::memory_order_relaxed);
    }
    return deltaTime;
}

float FrameClock::DeltaTime() const noexcept
{
    return m_deltaTime.load(std::memory_order_relaxed);
}

std::uint64_t FrameClock::FrameCount() const noexcept
{
    return m_frameCount.load(std::memory_order_relaxed);
}

float FrameClock::Fps() const noexcept
{
    return m_fps.load(std::memory_order_relaxed);
}

} // namespace Concord
