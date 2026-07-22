#ifndef CONCORD_THREEDS_CHUNKREADER_H
#define CONCORD_THREEDS_CHUNKREADER_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Concord::Asset::ThreeDs {

/**
 * One chunk header in a 3DS binary stream.
 *
 * The 3DS format is a hierarchy of typed chunks: each begins with a 2-byte id,
 * a 4-byte length (covering the header itself), then the chunk's data. A chunk
 * may contain nested sub-chunks or raw data; the reader uses `end` to skip
 * unknown chunks and to know when a parent's sub-chunks are exhausted.
 */
struct Chunk {
    /** The 2-byte chunk type identifier (e.g. 0x4D4D for MAIN3DS). */
    std::uint16_t id = 0;

    /** Absolute byte offset just past this chunk's header (where data begins). */
    std::size_t dataStart = 0;

    /** Absolute byte offset of the first byte after this chunk (start + length). */
    std::size_t end = 0;
};

/**
 * Sequential reader over a 3DS binary buffer.
 *
 * Wraps a raw byte array and offers typed reads (u16, u32, float, null-
 * terminated string) plus chunk-header parsing. The caller drives the descent:
 * read a chunk header, decide which sub-chunks to parse, and skip to the
 * chunk's `end` to ignore anything unrecognized. All multi-byte values are
 * little-endian, matching the 3DS specification.
 */
class ChunkReader {
public:
    ChunkReader(const std::uint8_t* data, std::size_t size) noexcept
        : m_data(data), m_size(size) {}

    /** Current read position. */
    std::size_t Position() const noexcept { return m_pos; }

    /** Moves the cursor to `pos` (clamped to the buffer end). */
    void Seek(std::size_t pos) noexcept { m_pos = pos > m_size ? m_size : pos; }

    /** True when the cursor has not reached `chunk.end`. */
    bool HasMore(const Chunk& chunk) const noexcept { return m_pos < chunk.end && m_pos < m_size; }

    /** Reads a chunk header at the cursor and advances past the 6-byte header. */
    Chunk ReadHeader();

    /** Skips the cursor to `chunk.end`, ignoring the chunk's contents. */
    void Skip(const Chunk& chunk) noexcept { Seek(chunk.end); }

    std::uint8_t ReadU8();
    std::uint16_t ReadU16();
    std::uint32_t ReadU32();
    float ReadFloat();

    /** Reads a null-terminated ASCII string (the null is consumed, not returned). */
    std::string ReadCString();

private:
    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
};

} // namespace Concord::Asset::ThreeDs

#endif // CONCORD_THREEDS_CHUNKREADER_H
