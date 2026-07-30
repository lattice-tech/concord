#include "engine/asset/cook/AssetDependencyGraph.h"

#include <algorithm>

namespace Concord::Asset {

namespace {

enum class Mark : std::uint8_t { Unvisited, Active, Done };

} // namespace

bool AssetDependencyGraph::AddAsset(const AssetId& asset, AssetContentHash sourceHash)
{
    if (!asset.IsValid() || !sourceHash.IsValid()) {
        return false;
    }
    const auto existing = m_index.find(asset);
    if (existing != m_index.end()) {
        m_nodes[existing->second].sourceHash = sourceHash;
        return true;
    }
    const std::size_t index = m_nodes.size();
    m_nodes.push_back(Node{asset, sourceHash, {}});
    m_index.emplace(asset, index);
    return true;
}

const AssetDependencyGraph::Node* AssetDependencyGraph::Find(const AssetId& asset) const
{
    const auto found = m_index.find(asset);
    return found == m_index.end() ? nullptr : &m_nodes[found->second];
}

bool AssetDependencyGraph::AddDependency(const AssetId& asset, const AssetId& dependency)
{
    if (asset == dependency) {
        return false;
    }
    const auto assetIt = m_index.find(asset);
    const auto dependencyIt = m_index.find(dependency);
    if (assetIt == m_index.end() || dependencyIt == m_index.end()) {
        return false;
    }
    std::vector<std::size_t>& edges = m_nodes[assetIt->second].dependencies;
    if (std::find(edges.begin(), edges.end(), dependencyIt->second) != edges.end()) {
        return true;
    }
    edges.push_back(dependencyIt->second);
    return true;
}

bool AssetDependencyGraph::Contains(const AssetId& asset) const
{
    return m_index.contains(asset);
}

std::vector<AssetId> AssetDependencyGraph::DirectDependencies(const AssetId& asset) const
{
    std::vector<AssetId> result;
    const Node* node = Find(asset);
    if (node == nullptr) {
        return result;
    }
    result.reserve(node->dependencies.size());
    for (const std::size_t dependency : node->dependencies) {
        result.push_back(m_nodes[dependency].id);
    }
    return result;
}

std::optional<std::vector<AssetId>> AssetDependencyGraph::TopologicalOrder() const
{
    std::vector<Mark> marks(m_nodes.size(), Mark::Unvisited);
    std::vector<AssetId> order;
    order.reserve(m_nodes.size());

    // Iterative post-order DFS in insertion order keeps the result deterministic.
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    for (std::size_t root = 0; root < m_nodes.size(); ++root) {
        if (marks[root] != Mark::Unvisited) {
            continue;
        }
        stack.push_back({root, 0});
        marks[root] = Mark::Active;
        while (!stack.empty()) {
            auto& [node, cursor] = stack.back();
            if (cursor < m_nodes[node].dependencies.size()) {
                const std::size_t next = m_nodes[node].dependencies[cursor];
                ++cursor;
                if (marks[next] == Mark::Active) {
                    return std::nullopt;
                }
                if (marks[next] == Mark::Unvisited) {
                    marks[next] = Mark::Active;
                    stack.push_back({next, 0});
                }
            } else {
                marks[node] = Mark::Done;
                order.push_back(m_nodes[node].id);
                stack.pop_back();
            }
        }
    }
    return order;
}

std::optional<AssetContentHash> AssetDependencyGraph::ResolvedHash(const AssetId& asset) const
{
    const auto rootIt = m_index.find(asset);
    if (rootIt == m_index.end()) {
        return std::nullopt;
    }

    std::vector<Mark> marks(m_nodes.size(), Mark::Unvisited);
    std::vector<AssetContentHash> resolved(m_nodes.size());
    std::vector<std::pair<std::size_t, std::size_t>> stack;
    stack.push_back({rootIt->second, 0});
    marks[rootIt->second] = Mark::Active;
    while (!stack.empty()) {
        auto& [node, cursor] = stack.back();
        if (cursor < m_nodes[node].dependencies.size()) {
            const std::size_t next = m_nodes[node].dependencies[cursor];
            ++cursor;
            if (marks[next] == Mark::Active) {
                return std::nullopt;
            }
            if (marks[next] == Mark::Unvisited) {
                marks[next] = Mark::Active;
                stack.push_back({next, 0});
            }
        } else {
            AssetContentHasher hasher;
            hasher.Mix(m_nodes[node].id.Key());
            hasher.MixU64(m_nodes[node].sourceHash.high);
            hasher.MixU64(m_nodes[node].sourceHash.low);
            for (const std::size_t dependency : m_nodes[node].dependencies) {
                hasher.Mix(m_nodes[dependency].id.Key());
                hasher.MixU64(resolved[dependency].high);
                hasher.MixU64(resolved[dependency].low);
            }
            resolved[node] = hasher.Finish();
            marks[node] = Mark::Done;
            stack.pop_back();
        }
    }
    return resolved[rootIt->second];
}

} // namespace Concord::Asset
