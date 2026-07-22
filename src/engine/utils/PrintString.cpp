#include "engine/utils/PrintString.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <vector>

namespace Concord {

namespace {

struct Line {
    std::string text;
    std::chrono::steady_clock::time_point expireAt;
    std::uint32_t color = 0xffffffffu;
};

std::mutex g_mutex;
std::vector<Line> g_lines;
constexpr std::size_t kMaxLines = 12;
constexpr int kMaxCols = 96;

void PushLine(std::string text, float durationSeconds, std::uint32_t color)
{
    if (text.empty()) {
        return;
    }
    if (text.size() > static_cast<std::size_t>(kMaxCols)) {
        text.resize(static_cast<std::size_t>(kMaxCols));
    }
    const float dur = std::clamp(durationSeconds, 0.25f, 30.0f);
    Line line;
    line.text = std::move(text);
    line.expireAt = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<float>(dur));
    line.color = color | 0x000000ffu;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.push_back(std::move(line));
    while (g_lines.size() > kMaxLines) {
        g_lines.erase(g_lines.begin());
    }
}

} // namespace

void PrintString(const char* text, const PrintStringOptions& options)
{
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    PushLine(text, options.durationSeconds, options.color);
}

void PrintString(const std::string& text, const PrintStringOptions& options)
{
    if (text.empty()) {
        return;
    }
    PushLine(text, options.durationSeconds, options.color);
}

void PrintString(const char* text, float durationSeconds)
{
    PrintStringOptions opts;
    opts.durationSeconds = durationSeconds;
    PrintString(text, opts);
}

namespace Detail {

void SnapshotPrintStrings(std::vector<PrintStringLine>& out)
{
    out.clear();
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.erase(std::remove_if(g_lines.begin(), g_lines.end(),
                                 [&](const Line& line) { return line.expireAt <= now; }),
                  g_lines.end());
    out.reserve(g_lines.size());
    for (const Line& line : g_lines) {
        out.push_back(PrintStringLine{line.text, line.color});
    }
}

} // namespace Detail

} // namespace Concord
