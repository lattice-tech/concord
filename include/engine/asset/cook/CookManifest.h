#ifndef CONCORD_COOKMANIFEST_H
#define CONCORD_COOKMANIFEST_H

#include "engine/asset/id/AssetContentHash.h"
#include "engine/asset/id/AssetId.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset {

/**
 * @brief One asset's cook record: what was cooked, from which inputs.
 *
 * `resolvedHash` folds the asset's own source hash with its transitive
 * dependencies (see AssetDependencyGraph::ResolvedHash), so a record is stale
 * whenever any source in its dependency closure or the cooker version changes.
 * `outputHash` fingerprints the produced cooked bytes and lets a package verify
 * that what is on disk matches what the manifest promises.
 */
struct CookRecord {
    AssetId id;
    AssetContentHash resolvedHash;
    AssetContentHash outputHash;
    std::uint32_t cookerVersion = 0;

    friend bool operator==(const CookRecord&, const CookRecord&) = default;
};

/**
 * @brief The set of cook records describing a cooked package.
 *
 * The manifest is pure data shared by the offline cooker and runtime loading.
 * It serializes to a deterministic, line-oriented text form so two byte-equal
 * cooks produce byte-equal manifests, and it re-derives every AssetId through
 * AssetId::FromKey on load so a corrupt manifest can never inject a malformed
 * identity.
 */
class CookManifest {
public:
    /**
     * Inserts or replaces the record for `record.id`.
     * @return false when the record's id is invalid or its hashes are the zero
     *         hash (an unhashed record can never drive a correct rebuild).
     */
    bool Put(const CookRecord& record);

    /** The record for `id`, or nullptr when absent. */
    const CookRecord* Find(const AssetId& id) const;

    bool Contains(const AssetId& id) const;
    std::size_t Size() const noexcept { return m_records.size(); }

    /** Records in ascending identity-key order, for deterministic iteration. */
    std::vector<CookRecord> Records() const;

    /**
     * True when `id` must be re-cooked: it is absent, its resolved input hash
     * changed, or it was cooked by a different cooker version. A present record
     * whose resolved hash and cooker version both match is up to date.
     */
    bool NeedsCook(const AssetId& id, AssetContentHash resolvedHash,
                   std::uint32_t cookerVersion) const;

    /** Serializes to the deterministic text form (records sorted by key). */
    std::string Serialize() const;

    /**
     * Parses the text form. Returns nullopt on any malformed line, unknown
     * version, duplicate id, malformed key, zero hash, control characters, or
     * when the payload exceeds the hard input budgets, leaving the caller's
     * manifest untouched.
     */
    static std::optional<CookManifest> Deserialize(std::string_view text);

    /** Hard ceilings applied while deserializing untrusted manifest text. */
    struct Limits {
        std::size_t maxBytes = 16ull * 1024ull * 1024ull;
        std::size_t maxRecords = 100'000u;
        std::size_t maxKeyBytes = 4096u;
    };

private:
    std::unordered_map<std::string, CookRecord> m_records;
};

} // namespace Concord::Asset

#endif // CONCORD_COOKMANIFEST_H
