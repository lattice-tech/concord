#ifndef CONCORD_COMMANDBUFFER_H
#define CONCORD_COMMANDBUFFER_H

#include "engine/ecs/World.h"

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Concord::Ecs {

/** Thread-safe deferred structural mutation queue, committed at a frame boundary. */
class CommandBuffer {
public:
    /** Queues a structural operation to run before the next system graph. */
    void Defer(std::function<void(World&)> command)
    {
        if (!command) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_commands.push_back(std::move(command));
    }

    /** Applies all commands currently queued; commands added during commit wait one frame. */
    std::size_t Commit(World& world)
    {
        std::vector<std::function<void(World&)>> commands;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            commands.swap(m_commands);
        }
        for (auto& command : commands) {
            command(world);
        }
        return commands.size();
    }

private:
    std::mutex m_mutex;
    std::vector<std::function<void(World&)>> m_commands;
};

} // namespace Concord::Ecs

#endif // CONCORD_COMMANDBUFFER_H
