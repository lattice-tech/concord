#include "time/FrameCounter.h"

namespace Concord {

float FrameCounter::Tick() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    ++m_frameCount;

    if (!m_started) {
        // First tick has no previous frame to measure against.
        m_started = true;
        m_lastTick = now;
        return 0.0f;
    }

    m_deltaTime = std::chrono::duration<float>(now - m_lastTick).count();
    m_lastTick = now;

    if (m_deltaTime > 0.0f) {
        const double instantaneous = 1.0 / static_cast<double>(m_deltaTime);
        // Exponential moving average so the reading stays readable frame to frame.
        m_fps = m_fps > 0.0 ? m_fps * 0.9 + instantaneous * 0.1 : instantaneous;
    }
    return m_deltaTime;
}

std::uint64_t FrameCounter::FrameCount() const noexcept
{
    return m_frameCount;
}

float FrameCounter::DeltaTime() const noexcept
{
    return m_deltaTime;
}

double FrameCounter::Fps() const noexcept
{
    return m_fps;
}

void FrameCounter::Reset() noexcept
{
    m_lastTick = std::chrono::steady_clock::now();
    m_frameCount = 0;
    m_deltaTime = 0.0f;
    m_fps = 0.0;
    m_started = false;
}

} // namespace Concord
