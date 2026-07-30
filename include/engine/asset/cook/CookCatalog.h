#ifndef CONCORD_COOKCATALOG_H
#define CONCORD_COOKCATALOG_H

#include "engine/asset/cook/AssetDependencyGraph.h"
#include "engine/asset/id/AssetContentHash.h"
#include "engine/asset/id/AssetId.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Concord::Asset {

/**
 * @brief One source asset the cooker should track for incremental rebuild.
 *
 * Pure authoring data: identity, the content bytes that become the source hash
 * (or a precomputed hash), an optional project-relative source path for prune
 * diagnostics, and direct dependency identities already registered in the same
 * catalog. No filesystem access happens here.
 */
struct CookCatalogEntry {
    AssetId id;
    /** When non-empty, HashBytes(sourceBytes) becomes the graph source hash. */
    std::string sourceBytes;
    /** Used when sourceBytes is empty; must be a non-zero hash. */
    AssetContentHash sourceHash{};
    /** Project-relative source path for logs; may be empty for procedural content. */
    std::string sourcePath;
    std::vector<AssetId> dependencies;

    friend bool operator==(const CookCatalogEntry&, const CookCatalogEntry&) = default;
};

/**
 * @brief Deterministic, budgeted collection of assets to cook.
 *
 * Entries are stored by identity key and re-emitted in ascending key order so a
 * given set of assets always builds the same graph regardless of registration
 * order. Building an AssetDependencyGraph rejects cycles, unknown dependency
 * endpoints, and entries that exceed the configured size budgets.
 */
struct CookCatalogLimits {
    std::uint32_t maxAssets = 100'000u;
    std::uint32_t maxSourceBytes = 64u * 1024u * 1024u;
    std::uint32_t maxSourcePathBytes = 4096u;
    std::uint32_t maxDependenciesPerAsset = 4096u;
};

class CookCatalog {
public:
    explicit CookCatalog(CookCatalogLimits limits = {});

    /**
     * Inserts or replaces the entry for `entry.id`.
     * @return false when the id is invalid, budgets are exceeded, the source is
     *         unusable (empty bytes and zero hash), or a source path contains a
     *         control character / NUL.
     */
    bool Put(CookCatalogEntry entry);

    bool Erase(const AssetId& id);
    const CookCatalogEntry* Find(const AssetId& id) const;
    bool Contains(const AssetId& id) const;
    std::size_t Size() const noexcept;

    /** Entries sorted by identity key for deterministic scan order. */
    std::vector<CookCatalogEntry> Entries() const;

    /**
     * Builds the pure dependency graph used by CookPlanner. Returns nullopt when
     * a dependency endpoint is missing from the catalog.
     */
    std::optional<AssetDependencyGraph> BuildGraph() const;

    const CookCatalogLimits& Limits() const noexcept { return m_limits; }

private:
    CookCatalogLimits m_limits;
    std::vector<CookCatalogEntry> m_entries;
};

} // namespace Concord::Asset

#endif // CONCORD_COOKCATALOG_H
