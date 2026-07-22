#ifndef CONCORD_SYSTEMGRAPH_H
#define CONCORD_SYSTEMGRAPH_H

#include "engine/ecs/World.h"
#include "engine/task/TaskGraph.h"

#include <cstdint>
#include <algorithm>
#include <functional>
#include <string>
#include <typeindex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Concord::Ecs {

/** Declared component access used to derive safe system dependencies. */
struct SystemAccess {
    std::unordered_set<std::type_index> reads;
    std::unordered_set<std::type_index> writes;

    template <typename T>
    SystemAccess& Read() { reads.emplace(typeid(T)); return *this; }

    template <typename T>
    SystemAccess& Write() { writes.emplace(typeid(T)); return *this; }
};

/** Builds a conflict-aware task graph from registered ECS systems. */
class SystemGraph {
public:
    using SystemId = std::uint64_t;

    SystemId Add(std::string name, SystemAccess access,
                 std::function<void(World&, float)> system)
    {
        if (!system) {
            return 0;
        }
        const SystemId id = m_nextId++;
        m_systems.push_back(System{id, std::move(name), std::move(access), std::move(system)});
        return id;
    }

    bool Remove(SystemId id)
    {
        const auto it = std::find_if(m_systems.begin(), m_systems.end(),
                                     [id](const System& system) { return system.id == id; });
        if (it == m_systems.end()) {
            return false;
        }
        m_systems.erase(it);
        return true;
    }

    TaskGraph Build(World& world, float deltaTime) const
    {
        TaskGraph graph;
        std::vector<TaskGraph::NodeId> nodes;
        nodes.reserve(m_systems.size());
        for (const System& system : m_systems) {
            nodes.push_back(graph.Add(system.name, [&world, deltaTime, callback = system.callback] {
                callback(world, deltaTime);
            }));
        }
        for (std::size_t dependent = 0; dependent < m_systems.size(); ++dependent) {
            for (std::size_t dependency = 0; dependency < dependent; ++dependency) {
                if (Conflicts(m_systems[dependent].access, m_systems[dependency].access)) {
                    graph.DependsOn(nodes[dependent], nodes[dependency]);
                }
            }
        }
        return graph;
    }

    std::size_t Size() const noexcept { return m_systems.size(); }

private:
    struct System {
        SystemId id;
        std::string name;
        SystemAccess access;
        std::function<void(World&, float)> callback;
    };

    static bool Intersects(const std::unordered_set<std::type_index>& left,
                           const std::unordered_set<std::type_index>& right)
    {
        for (const std::type_index& type : left) {
            if (right.contains(type)) {
                return true;
            }
        }
        return false;
    }

    static bool Conflicts(const SystemAccess& left, const SystemAccess& right)
    {
        return Intersects(left.writes, right.writes)
            || Intersects(left.writes, right.reads)
            || Intersects(left.reads, right.writes);
    }

    std::vector<System> m_systems;
    SystemId m_nextId = 1;
};

} // namespace Concord::Ecs

#endif // CONCORD_SYSTEMGRAPH_H
