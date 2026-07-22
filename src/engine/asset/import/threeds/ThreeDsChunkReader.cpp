#include "engine/asset/import/threeds/ThreeDsChunkReader.h"

#include "engine/debug/Logger.h"

#include <cstring>

namespace Concord::Asset::ThreeDs {

Chunk ChunkReader::ReadHeader()
{
    Chunk chunk;
    if (m_pos + 6 > m_size) {
        chunk.end = m_pos;
        return chunk;
    }
    std::uint16_t id = 0;
    std::uint32_t len = 0;
    std::memcpy(&id, m_data + m_pos, 2);
    std::memcpy(&len, m_data + m_pos + 2, 4);
    chunk.id = id;
    chunk.dataStart = m_pos + 6;
    // The length covers the full chunk including its 6-byte header. Clamp to
    // the buffer so a truncated last chunk never sends the cursor past the end.
    chunk.end = m_pos + len;
    if (chunk.end > m_size || len < 6) {
        chunk.end = m_size;
    }
    m_pos = chunk.dataStart;
    return chunk;
}

std::uint8_t ChunkReader::ReadU8()
{
    if (m_pos >= m_size) {
        return 0;
    }
    return m_data[m_pos++];
}

std::uint16_t ChunkReader::ReadU16()
{
    if (m_pos + 2 > m_size) {
        m_pos = m_size;
        return 0;
    }
    std::uint16_t v = 0;
    std::memcpy(&v, m_data + m_pos, 2);
    m_pos += 2;
    return v;
}

std::uint32_t ChunkReader::ReadU32()
{
    if (m_pos + 4 > m_size) {
        m_pos = m_size;
        return 0;
    }
    std::uint32_t v = 0;
    std::memcpy(&v, m_data + m_pos, 4);
    m_pos += 4;
    return v;
}

float ChunkReader::ReadFloat()
{
    if (m_pos + 4 > m_size) {
        m_pos = m_size;
        return 0.0f;
    }
    float v = 0.0f;
    std::memcpy(&v, m_data + m_pos, 4);
    m_pos += 4;
    return v;
}

std::string ChunkReader::ReadCString()
{
    std::string out;
    while (m_pos < m_size) {
        const char c = static_cast<char>(m_data[m_pos++]);
        if (c == '\0') {
            break;
        }
        out.push_back(c);
    }
    return out;
}

} // namespace Concord::Asset::ThreeDs
