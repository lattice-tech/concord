#ifndef CONCORD_COOKPLANNER_H
#define CONCORD_COOKPLANNER_H

#include "engine/asset/cook/AssetDependencyGraph.h"
#include "engine/asset/cook/CookManifest.h"
#include "engine/asset/id/AssetContentHash.h"
#include "engine/asset/id/AssetId.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/**
 * @brief One asset the planner decided must be (re)cooked, with the resolved
 * input hash the cooker should record for it afterwards.
 *
 * `resolvedHash` is the dependency-folded fingerprint from the graph; the caller
 * cooks the asset, fingerprints the produced bytes, then stores a CookRecord
 * carrying this exact `resolvedHash` so a later plan sees the asset as current.
 */
struct CookPlanEntry {
    AssetId id;
    AssetContentHash resolvedHash;

    friend bool operator==(const CookPlanEntry&, const CookPlanEntry&) = default;
};

/**
 * @brief An ordered incremental cook plan: the assets to (re)cook, dependencies
 * before dependants, so a cooker can process the list front to back.
 */
struct CookPlan {
    std::vector<CookPlanEntry> entries;

    bool Empty() const noexcept { return entries.empty(); }
    std::size_t Size() const noexcept { return entries.size(); }
};

/**
 * @brief Pure incremental-cook decision engine over a dependency graph and the
 * prior cook manifest.
 *
 * The planner is the brain of the offline cooker but performs no file I/O: it
 * consumes an AssetDependencyGraph (source hashes + edges) and the CookManifest
 * from the previous cook, and produces the deterministic, dependency-ordered
 * set of assets whose cooked output is missing or stale. Because a graph's
 * resolved hash folds every transitive dependency's source hash, an asset is
 * automatically replanned whenever anything in its dependency closure changes —
 * changing one leaf source rebuilds exactly it and its transitive dependants,
 * nothing more.
 */
namespace CookPlanner {

/**
 * Computes the incremental cook plan for `graph` against the prior `manifest`
 * and the current `cookerVersion`. Assets appear in topological order
 * (dependencies first). An asset is included when the manifest reports it as
 * missing, its resolved input hash changed, or it was cooked by a different
 * cooker version. Returns nullopt when the graph contains a cycle.
 */
std::optional<CookPlan> Plan(const AssetDependencyGraph& graph,
                             const CookManifest& manifest,
                             std::uint32_t cookerVersion);

/**
 * Records a completed cook into `manifest`: stores a CookRecord for `entry.id`
 * with its planned `resolvedHash`, the produced `outputHash`, and
 * `cookerVersion`. Returns false when the record is unusable (invalid id or a
 * zero hash), leaving the manifest unchanged.
 */
bool RecordCooked(CookManifest& manifest, const CookPlanEntry& entry,
                  AssetContentHash outputHash, std::uint32_t cookerVersion);

} // namespace CookPlanner

} // namespace Concord::Asset

#endif // CONCORD_COOKPLANNER_H
