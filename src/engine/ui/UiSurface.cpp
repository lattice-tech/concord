#include "engine/ui/UiSurface.h"

#include <mutex>

namespace Concord::UI {

namespace {

std::mutex g_mutex;
DrawList g_current;

} // namespace

void Submit(const DrawList& drawList)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current = drawList;
}

void ClearSurface()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_current.Clear();
}

namespace Detail {

void SnapshotDrawList(DrawList& out)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    out = g_current;
}

} // namespace Detail

} // namespace Concord::UI
