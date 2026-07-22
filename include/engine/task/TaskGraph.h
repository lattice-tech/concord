#ifndef CONCORD_TASKGRAPH_H
#define CONCORD_TASKGRAPH_H

#include "engine/task/JobSystem.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Concord {

/** One completed task-graph node measurement. */
struct TaskNodeStats {
    std::string name;
    float cpuMs = 0.0f;
};

/** Aggregate result of one graph execution. */
struct TaskGraphStats {
    std::vector<TaskNodeStats> nodes;
    float wallMs = 0.0f;
};

/**
 * @brief A per-frame directed acyclic graph of CPU jobs.
 *
 * Dependencies are explicit and ready nodes execute concurrently on JobSystem.
 */
class TaskGraph {
public:
    using NodeId = std::uint32_t;
    static constexpr NodeId kInvalidNode = UINT32_MAX;

    /** Adds a named node and returns its stable graph-local identifier. */
    NodeId Add(std::string name, std::function<void()> task);

    /** Adds an edge to an earlier-added dependency; forward references are rejected by Run. */
    bool DependsOn(NodeId dependent, NodeId dependency);

    /** Executes all nodes, blocks the caller, and returns node timings. */
    TaskGraphStats Run(JobSystem& jobs) const;

    std::size_t Size() const noexcept { return m_nodes.size(); }

private:
    struct Node {
        std::string name;
        std::function<void()> task;
        std::vector<NodeId> dependencies;
    };

    std::vector<Node> m_nodes;
};

} // namespace Concord

#endif // CONCORD_TASKGRAPH_H
