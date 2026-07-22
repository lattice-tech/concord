#include "engine/render/texture/TextureRegistry.h"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace Concord {

namespace {

/**
 * The intern table plus its lock. Kept function-local so its construction is
 * ordered on first use rather than at static-init time, and shared by every
 * caller across threads.
 */
struct Table {
    std::mutex mutex;
    std::unordered_map<std::string, TextureId> ids;
    std::vector<std::string> paths; // index (id - 1) -> path
};

Table& Instance()
{
    static Table table;
    return table;
}

} // namespace

TextureId TextureRegistry::Acquire(const std::string& path)
{
    if (path.empty()) {
        return TextureId::None;
    }

    Table& table = Instance();
    std::lock_guard<std::mutex> lock(table.mutex);

    const auto it = table.ids.find(path);
    if (it != table.ids.end()) {
        return it->second;
    }

    // Ids start at 1 so 0 stays the "no texture" sentinel (TextureId::None).
    const auto id = static_cast<TextureId>(table.paths.size() + 1);
    table.paths.push_back(path);
    table.ids.emplace(path, id);
    return id;
}

std::string TextureRegistry::Path(TextureId id)
{
    if (id == TextureId::None) {
        return {};
    }

    Table& table = Instance();
    std::lock_guard<std::mutex> lock(table.mutex);

    const auto index = static_cast<std::size_t>(id) - 1;
    if (index >= table.paths.size()) {
        return {};
    }
    return table.paths[index];
}

} // namespace Concord
