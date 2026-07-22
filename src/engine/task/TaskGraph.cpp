#include "engine/task/TaskGraph.h"

#include <chrono>
#include <future>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Concord {

TaskGraph::NodeId TaskGraph::Add(std::string name, std::function<void()> task)
{
    if (!task || m_nodes.size() >= kInvalidNode) {
        return kInvalidNode;
    }
    const NodeId id = static_cast<NodeId>(m_nodes.size());
    m_nodes.push_back(Node{std::move(name), std::move(task), {}});
    return id;
}

bool TaskGraph::DependsOn(NodeId dependent, NodeId dependency)
{
    if (dependent >= m_nodes.size() || dependency >= m_nodes.size() || dependent == dependency) {
        return false;
    }
    m_nodes[dependent].dependencies.push_back(dependency);
    return true;
}

TaskGraphStats TaskGraph::Run(JobSystem& jobs) const
{
    using Clock = std::chrono::steady_clock;
    const auto graphStart = Clock::now();
    TaskGraphStats stats;
    stats.nodes.resize(m_nodes.size());
    std::vector<std::shared_future<void>> completions(m_nodes.size());

    std::exception_ptr failure;
    try {
        for (NodeId id = 0; id < m_nodes.size(); ++id) {
            const Node& node = m_nodes[id];
            std::vector<std::shared_future<void>> dependencies;
            dependencies.reserve(node.dependencies.size());
            for (NodeId dependency : node.dependencies) {
                if (dependency >= id || !completions[dependency].valid()) {
                    throw std::logic_error("task graph dependencies must reference earlier nodes");
                }
                dependencies.push_back(completions[dependency]);
            }
            completions[id] = jobs.Submit([&, id, dependencies = std::move(dependencies)] {
                for (const std::shared_future<void>& dependency : dependencies) {
                    dependency.get();
                }
                const auto start = Clock::now();
                m_nodes[id].task();
                const auto end = Clock::now();
                stats.nodes[id].name = m_nodes[id].name;
                stats.nodes[id].cpuMs = std::chrono::duration<float, std::milli>(end - start).count();
            }).share();
        }
    } catch (...) {
        failure = std::current_exception();
    }
    for (const std::shared_future<void>& completion : completions) {
        if (!completion.valid()) {
            continue;
        }
        try {
            completion.get();
        } catch (...) {
            if (!failure) {
                failure = std::current_exception();
            }
        }
    }
    stats.wallMs = std::chrono::duration<float, std::milli>(Clock::now() - graphStart).count();
    if (failure) {
        std::rethrow_exception(failure);
    }
    return stats;
}

} // namespace Concord
