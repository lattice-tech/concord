#include "engine/debug/DebugOverlay.h"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace Concord::Debug {

namespace {

constexpr std::size_t kMaxCols = 96;

std::mutex g_mutex;
/** Persistent debug overlay lines, replaced wholesale by SetDebugOverlay. */
std::vector<std::string> g_lines;

} // namespace

void SetDebugOverlay(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    const auto flush = [&]() {
        if (current.size() > kMaxCols) {
            current.resize(kMaxCols);
        }
        lines.push_back(std::move(current));
        current.clear();
    };
    for (const char character : text) {
        if (character == '\n') {
            flush();
        } else {
            current.push_back(character);
        }
    }
    if (!current.empty()) {
        flush();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines = std::move(lines);
}

void ClearDebugOverlay()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.clear();
}

namespace Detail {

void SnapshotDebugOverlay(std::vector<Concord::Detail::PrintStringLine>& out)
{
    out.clear();
    std::lock_guard<std::mutex> lock(g_mutex);
    out.reserve(g_lines.size());
    for (const std::string& line : g_lines) {
        out.push_back(Concord::Detail::PrintStringLine{line, 0xd8f0ffffu});
    }
}

} // namespace Detail

} // namespace Concord::Debug
