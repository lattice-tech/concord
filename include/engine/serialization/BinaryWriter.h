#ifndef CONCORD_BINARYWRITER_H
#define CONCORD_BINARYWRITER_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Concord::Serialization {

/**
 * @brief Little-endian byte builder shared by every binary format.
 *
 * Reusable and module-agnostic (see AGENTS.md §5): integers are emitted in an
 * explicit little-endian byte order so output is identical across compilers and
 * architectures, and floats go through their IEEE-754 bit pattern. The writer
 * appends to an owned buffer the caller drains with Bytes(); it performs no I/O
 * so it is trivially testable and safe to build entirely in memory.
 */
class BinaryWriter {
public:
    void PutU8(std::uint8_t value);
    void PutU16(std::uint16_t value);
    void PutU32(std::uint32_t value);
    void PutU64(std::uint64_t value);
    void PutI32(std::int32_t value);
    void PutF32(float value);

    /** Length-prefixed (u32) raw byte block; safe for arbitrary binary payloads. */
    void PutBytes(const void* data, std::size_t size);

    /** Length-prefixed (u32) UTF-8/opaque string. */
    void PutString(std::string_view value);

    /** Overwrites four bytes at a previously reserved offset (e.g. a patched size). */
    void PatchU32(std::size_t offset, std::uint32_t value);

    std::size_t Size() const noexcept { return m_bytes.size(); }
    const std::vector<std::uint8_t>& Bytes() const noexcept { return m_bytes; }
    std::vector<std::uint8_t> Take() noexcept { return std::move(m_bytes); }

private:
    std::vector<std::uint8_t> m_bytes;
};

} // namespace Concord::Serialization

#endif // CONCORD_BINARYWRITER_H
