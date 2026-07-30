#include "engine/asset/cook/CookPlanner.h"

namespace Concord::Asset::CookPlanner {

std::optional<CookPlan> Plan(const AssetDependencyGraph& graph,
                             const CookManifest& manifest,
                             std::uint32_t cookerVersion)
{
    // Topological order doubles as the cycle check: a cyclic graph has no order
    // and therefore no well-defined cook sequence.
    const std::optional<std::vector<AssetId>> order = graph.TopologicalOrder();
    if (!order) {
        return std::nullopt;
    }

    CookPlan plan;
    for (const AssetId& id : *order) {
        // ResolvedHash folds the asset's own source hash with its transitive
        // dependencies, so an upstream change flips NeedsCook here automatically.
        const std::optional<AssetContentHash> resolved = graph.ResolvedHash(id);
        if (!resolved) {
            return std::nullopt; // unreachable once TopologicalOrder succeeded
        }
        if (manifest.NeedsCook(id, *resolved, cookerVersion)) {
            plan.entries.push_back(CookPlanEntry{id, *resolved});
        }
    }
    return plan;
}

bool RecordCooked(CookManifest& manifest, const CookPlanEntry& entry,
                  AssetContentHash outputHash, std::uint32_t cookerVersion)
{
    return manifest.Put(CookRecord{entry.id, entry.resolvedHash, outputHash,
                                   cookerVersion});
}

} // namespace Concord::Asset::CookPlanner
