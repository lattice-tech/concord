#include "engine/asset/cook/CookCatalog.h"

#include "engine/asset/cook/AssetDependencyGraph.h"

#include <algorithm>
#include <utility>

namespace Concord::Asset {
namespace {

bool HasControlOrNul(std::string_view text) noexcept
{
    for (const unsigned char c : text) {
        if (c < 0x20u || c == 0x7fu) {
            return true;
        }
    }
    return false;
}

} // namespace

CookCatalog::CookCatalog(CookCatalogLimits limits)
    : m_limits(limits)
{
}

bool CookCatalog::Put(CookCatalogEntry entry)
{
    if (!entry.id.IsValid()) {
        return false;
    }
    if (entry.sourceBytes.size() > m_limits.maxSourceBytes) {
        return false;
    }
    if (entry.sourcePath.size() > m_limits.maxSourcePathBytes
        || HasControlOrNul(entry.sourcePath)) {
        return false;
    }
    if (entry.dependencies.size() > m_limits.maxDependenciesPerAsset) {
        return false;
    }
    for (const AssetId& dependency : entry.dependencies) {
        if (!dependency.IsValid() || dependency == entry.id) {
            return false;
        }
    }

    AssetContentHash sourceHash = entry.sourceHash;
    if (!entry.sourceBytes.empty()) {
        sourceHash = HashBytes(entry.sourceBytes.data(), entry.sourceBytes.size());
    }
    if (!sourceHash.IsValid()) {
        return false;
    }
    entry.sourceHash = sourceHash;

    for (CookCatalogEntry& existing : m_entries) {
        if (existing.id == entry.id) {
            existing = std::move(entry);
            return true;
        }
    }
    if (m_entries.size() >= m_limits.maxAssets) {
        return false;
    }
    m_entries.push_back(std::move(entry));
    return true;
}

bool CookCatalog::Erase(const AssetId& id)
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [&](const CookCatalogEntry& entry) {
                                     return entry.id == id;
                                 });
    if (it == m_entries.end()) {
        return false;
    }
    m_entries.erase(it);
    return true;
}

const CookCatalogEntry* CookCatalog::Find(const AssetId& id) const
{
    for (const CookCatalogEntry& entry : m_entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

bool CookCatalog::Contains(const AssetId& id) const
{
    return Find(id) != nullptr;
}

std::size_t CookCatalog::Size() const noexcept
{
    return m_entries.size();
}

std::vector<CookCatalogEntry> CookCatalog::Entries() const
{
    std::vector<CookCatalogEntry> ordered = m_entries;
    std::sort(ordered.begin(), ordered.end(),
              [](const CookCatalogEntry& lhs, const CookCatalogEntry& rhs) {
                  return lhs.id.Key() < rhs.id.Key();
              });
    return ordered;
}

std::optional<AssetDependencyGraph> CookCatalog::BuildGraph() const
{
    AssetDependencyGraph graph;
    const std::vector<CookCatalogEntry> ordered = Entries();
    for (const CookCatalogEntry& entry : ordered) {
        if (!graph.AddAsset(entry.id, entry.sourceHash)) {
            return std::nullopt;
        }
    }
    for (const CookCatalogEntry& entry : ordered) {
        for (const AssetId& dependency : entry.dependencies) {
            if (!graph.AddDependency(entry.id, dependency)) {
                return std::nullopt;
            }
        }
    }
    return graph;
}

} // namespace Concord::Asset
