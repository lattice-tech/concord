#ifndef CONCORD_BINARYREADER_H
#define CONCORD_BINARYREADER_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Concord::Serialization {

/**
 * @brief Bounds-checked little-endian reader shared by every binary format.
 *
 * Reusable and module-agnostic (see AGENTS.md §5). Every read is bounds-checked
 * against the backing buffer: an out-of-range read sets a sticky error flag,
 * returns a zero/default value, and makes every later read fail too, so callers
 * can parse optimistically and check Ok() once at the end. The reader never
 * owns or copies the buffer; the caller keeps it alive for the reader's life.
 */
class BinaryReader {
public:
    BinaryReader(const std::uint8_t* data, std::size_t size) noexcept;

    /** False once any read has run past the end of the buffer. */
    bool Ok() const noexcept { return !m_error; }

    /** True when no error occurred and every byte has been consumed exactly. */
    bool AtEnd() const noexcept { return !m_error && m_pos == m_size; }

    /** Bytes not yet consumed (0 once failed). */
    std::size_t Remaining() const noexcept;

    /** Forces the sticky error state (e.g. on a failed semantic check). */
    void Fail() noexcept { m_error = true; }

    std::uint8_t GetU8();
    std::uint16_t GetU16();
    std::uint32_t GetU32();
    std::uint64_t GetU64();
    std::int32_t GetI32();
    float GetF32();

    /**
     * Reads a length-prefixed (u32) byte block into `out`, capped at `maxBytes`.
     * On overflow or truncation it fails and leaves `out` empty.
     */
    void GetBytes(std::string& out, std::size_t maxBytes);

    /** Reads a length-prefixed (u32) string, capped at `maxBytes`. */
    std::string GetString(std::size_t maxBytes);

    /**
     * Returns a bounded reader over the next `size` bytes and advances this
     * reader past them. An oversized frame fails this reader and returns an
     * empty failed reader; callers must check both readers after parsing.
     */
    BinaryReader GetFrame(std::size_t size);

private:
    bool CanRead(std::size_t count) const noexcept;

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
    bool m_error = false;
};

} // namespace Concord::Serialization

#endif // CONCORD_BINARYREADER_H
