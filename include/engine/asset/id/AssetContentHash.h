#ifndef CONCORD_ASSETCONTENTHASH_H
#define CONCORD_ASSETCONTENTHASH_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Concord::Asset {

/**
 * @brief Deterministic 128-bit content fingerprint.
 *
 * Used to identify cooked-asset content and to detect when a source, importer
 * version, cook settings, or dependency changed. The hash is a fixed FNV-1a
 * variant so a given byte sequence always produces the same value across runs,
 * builds, and machines; it is a fingerprint, not a cryptographic MAC.
 *
 * The zero value is reserved as "no hash" and never equals a hashed empty
 * input, because Hasher seeds both lanes with distinct nonzero offsets.
 */
struct AssetContentHash {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    bool IsValid() const noexcept { return high != 0 || low != 0; }

    /** Lowercase 32-character hexadecimal form, stable for manifests and logs. */
    std::string ToHex() const;

    friend bool operator==(const AssetContentHash&, const AssetContentHash&) = default;
};

/**
 * @brief Incremental FNV-1a builder feeding two independently seeded lanes.
 *
 * Combining a big-endian byte-length prefix with each chunk makes the stream
 * length-delimited, so concatenating two logically distinct fields cannot
 * collide with a single field carrying their catenation.
 */
class AssetContentHasher {
public:
    AssetContentHasher() noexcept = default;

    void Mix(const void* data, std::size_t size) noexcept;
    void Mix(std::string_view text) noexcept;
    void MixU64(std::uint64_t value) noexcept;

    AssetContentHash Finish() const noexcept;

private:
    std::uint64_t m_high = 0xcbf29ce484222325ull;
    std::uint64_t m_low = 0x84222325cbf29ce4ull;
};

/** Convenience one-shot hash of a byte range. */
AssetContentHash HashBytes(const void* data, std::size_t size) noexcept;

} // namespace Concord::Asset

#endif // CONCORD_ASSETCONTENTHASH_H
