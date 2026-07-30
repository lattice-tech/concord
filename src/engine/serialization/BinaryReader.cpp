#include "engine/serialization/BinaryReader.h"

#include <bit>

namespace Concord::Serialization {

BinaryReader::BinaryReader(const std::uint8_t* data, std::size_t size) noexcept
    : m_data(data), m_size(size)
{
}

std::size_t BinaryReader::Remaining() const noexcept
{
    return (!m_error && m_pos <= m_size) ? m_size - m_pos : 0;
}

bool BinaryReader::CanRead(std::size_t count) const noexcept
{
    return !m_error && m_pos <= m_size && count <= m_size - m_pos;
}

std::uint8_t BinaryReader::GetU8()
{
    if (!CanRead(1)) { Fail(); return 0; }
    return m_data[m_pos++];
}

std::uint16_t BinaryReader::GetU16()
{
    if (!CanRead(2)) { Fail(); return 0; }
    std::uint16_t value = 0;
    for (unsigned byte = 0; byte < 2; ++byte) {
        value |= static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(m_data[m_pos + byte]) << (byte * 8));
    }
    m_pos += 2;
    return value;
}

std::uint32_t BinaryReader::GetU32()
{
    if (!CanRead(4)) { Fail(); return 0; }
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(m_data[m_pos + byte]) << (byte * 8);
    }
    m_pos += 4;
    return value;
}

std::uint64_t BinaryReader::GetU64()
{
    if (!CanRead(8)) { Fail(); return 0; }
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(m_data[m_pos + byte]) << (byte * 8);
    }
    m_pos += 8;
    return value;
}

std::int32_t BinaryReader::GetI32() { return std::bit_cast<std::int32_t>(GetU32()); }
float BinaryReader::GetF32() { return std::bit_cast<float>(GetU32()); }

void BinaryReader::GetBytes(std::string& out, std::size_t maxBytes)
{
    out.clear();
    const std::uint32_t size = GetU32();
    if (size > maxBytes || !CanRead(size)) {
        Fail();
        return;
    }
    out.assign(reinterpret_cast<const char*>(m_data + m_pos), size);
    m_pos += size;
}

std::string BinaryReader::GetString(std::size_t maxBytes)
{
    std::string value;
    GetBytes(value, maxBytes);
    return value;
}

BinaryReader BinaryReader::GetFrame(std::size_t size)
{
    if (!CanRead(size)) {
        Fail();
        BinaryReader frame(nullptr, 0);
        frame.Fail();
        return frame;
    }
    BinaryReader frame(m_data + m_pos, size);
    m_pos += size;
    return frame;
}

} // namespace Concord::Serialization
