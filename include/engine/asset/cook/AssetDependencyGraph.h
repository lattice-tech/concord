#ifndef CONCORD_ASSETDEPENDENCYGRAPH_H
#define CONCORD_ASSETDEPENDENCYGRAPH_H

#include "engine/asset/id/AssetContentHash.h"
#include "engine/asset/id/AssetId.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Concord::Asset {

/**
 * @brief Source-to-cooked dependency graph over AssetIds.
 *
 * Each registered asset carries its own source content hash and a set of assets
 * it directly depends on (a model on its materials, a material on its textures,
 * a skinned model on its skeleton and clips, a scene on referenced prefabs, and
 * so on). The graph is the input a deterministic cooker uses to decide what to
 * rebuild: changing one source invalidates exactly its transitive dependants.
 *
 * The structure is pure data with no filesystem access, so it is trivially
 * testable and reused by both the offline cooker and runtime manifest loading.
 */
class AssetDependencyGraph {
public:
    /**
     * Registers or updates `asset` with its direct source content hash.
     * @return false when `asset` is invalid or `sourceHash` is the zero hash.
     */
    bool AddAsset(const AssetId& asset, AssetContentHash sourceHash);

    /**
     * Records that `asset` directly depends on `dependency`. Both must already
     * be registered and differ.
     * @return false on unknown/equal endpoints; duplicate edges are ignored.
     */
    bool AddDependency(const AssetId& asset, const AssetId& dependency);

    bool Contains(const AssetId& asset) const;
    std::size_t Size() const noexcept { return m_nodes.size(); }

    /** Direct dependencies of `asset`, in insertion order; empty when unknown. */
    std::vector<AssetId> DirectDependencies(const AssetId& asset) const;

    /**
     * Composite hash folding an asset's own source hash with the resolved
     * identity hashes of its transitive dependencies, so any upstream change
     * alters the result. Returns nullopt when `asset` is unknown or the graph
     * contains a cycle reachable from it.
     */
    std::optional<AssetContentHash> ResolvedHash(const AssetId& asset) const;

    /**
     * Deterministic topological order (dependencies before dependants).
     * Returns nullopt when the graph contains a cycle.
     */
    std::optional<std::vector<AssetId>> TopologicalOrder() const;

    /** True when no directed cycle exists. */
    bool IsAcyclic() const { return TopologicalOrder().has_value(); }

private:
    struct Node {
        AssetId id;
        AssetContentHash sourceHash;
        std::vector<std::size_t> dependencies;
    };

    const Node* Find(const AssetId& asset) const;

    std::vector<Node> m_nodes;
    std::unordered_map<AssetId, std::size_t> m_index;
};

} // namespace Concord::Asset

#endif // CONCORD_ASSETDEPENDENCYGRAPH_H
