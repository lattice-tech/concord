#include "time/Time.h"

#include <chrono>
#include <thread>

namespace Concord {

namespace {

/** Process-wide zero point, fixed on first use so only differences matter. */
std::chrono::steady_clock::time_point StartPoint()
{
    static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    return start;
}

} // namespace

void Sleep(std::uint32_t milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

std::uint64_t Ticks()
{
    const auto elapsed = std::chrono::steady_clock::now() - StartPoint();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

double Seconds()
{
    const auto elapsed = std::chrono::steady_clock::now() - StartPoint();
    return std::chrono::duration<double>(elapsed).count();
}

} // namespace Concord
