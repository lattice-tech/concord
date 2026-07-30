#ifndef CONCORD_ASSETID_H
#define CONCORD_ASSETID_H

#include "engine/asset/id/AssetType.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Concord::Asset {

/**
 * @brief Stable identity of a cooked asset, independent of disk layout.
 *
 * An AssetId is derived from a normalized, project-relative virtual path plus
 * an AssetType. Normalization lower-cases, converts separators to '/', and
 * collapses '.'/'..' segments so that the same logical asset always maps to the
 * same identity regardless of how a reference was spelled. The identity never
 * embeds an absolute filesystem path, so cooked packages and manifests stay
 * portable across machines.
 *
 * The default-constructed id is invalid (empty path, Unknown type) and never
 * names real content.
 */
class AssetId {
public:
    AssetId() = default;

    /**
     * Builds an id from an untrusted reference and a type. Returns nullopt when
     * the reference is empty, absolute/rooted, escapes the project root with
     * '..', or contains a NUL or drive/scheme prefix.
     */
    static std::optional<AssetId> FromVirtualPath(std::string_view reference,
                                                  AssetType type);

    /**
     * Builds an id from an authored 128-bit GUID and a type. The GUID form is
     * used for content that has no stable authored path (procedural or
     * editor-generated). A zero GUID is rejected.
     */
    static std::optional<AssetId> FromGuid(std::uint64_t high, std::uint64_t low,
                                           AssetType type);

    /**
     * Reconstructs an id from a serialized identity key (the exact value of
     * Key()). Every field is re-validated through the same rules as the
     * builders, so a corrupt or malformed key yields nullopt rather than an
     * ill-formed identity. Used by manifest deserialization.
     */
    static std::optional<AssetId> FromKey(std::string_view key);

    bool IsValid() const noexcept
    {
        return m_type != AssetType::Unknown && !m_key.empty();
    }

    AssetType Type() const noexcept { return m_type; }

    /**
     * The canonical identity key: `type ':' normalizedPath` for path ids, or
     * `type ":guid:" hex` for GUID ids. Stable across runs and machines.
     */
    const std::string& Key() const noexcept { return m_key; }

    friend bool operator==(const AssetId& lhs, const AssetId& rhs) noexcept
    {
        return lhs.m_type == rhs.m_type && lhs.m_key == rhs.m_key;
    }

    friend bool operator!=(const AssetId& lhs, const AssetId& rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    AssetId(AssetType type, std::string key)
        : m_type(type), m_key(std::move(key)) {}

    AssetType m_type = AssetType::Unknown;
    std::string m_key;
};

/**
 * Normalizes an untrusted virtual path to the canonical form used by AssetId,
 * or returns nullopt when the reference is empty, rooted, escapes the root, or
 * carries a NUL/scheme/drive prefix. Exposed for reference resolution and tests.
 */
std::optional<std::string> NormalizeVirtualPath(std::string_view reference);

} // namespace Concord::Asset

template <>
struct std::hash<Concord::Asset::AssetId> {
    std::size_t operator()(const Concord::Asset::AssetId& id) const noexcept
    {
        return std::hash<std::string>{}(id.Key());
    }
};

#endif // CONCORD_ASSETID_H
