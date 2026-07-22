#include "engine/ui/UiSurface.h"

#include <mutex>
#include <unordered_map>

namespace Concord::UI {

namespace {

std::mutex g_mutex;
std::unordered_map<WindowId, DrawList> g_windows;
DrawList g_fallback;

} // namespace

void Submit(WindowId window, const DrawList& drawList)
{
    if (window == kInvalidWindowId) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_windows[window] = drawList;
}

void Submit(const DrawList& drawList)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fallback = drawList;
}

void ClearSurface(WindowId window)
{
    if (window == kInvalidWindowId) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_windows.erase(window);
}

void ClearSurface()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_fallback.Clear();
}

namespace Detail {

void SnapshotDrawList(WindowId window, DrawList& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_windows.find(window);
    out = it != g_windows.end() ? it->second : g_fallback;
}

void SnapshotDrawList(DrawList& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    out = g_fallback;
}

} // namespace Detail

} // namespace Concord::UI
