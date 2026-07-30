#include "engine/serialization/BinaryWriter.h"

#include <bit>
#include <limits>
#include <stdexcept>

namespace Concord::Serialization {

void BinaryWriter::PutU8(std::uint8_t value)
{
    m_bytes.push_back(value);
}

void BinaryWriter::PutU16(std::uint16_t value)
{
    for (unsigned shift = 0; shift < 16; shift += 8) {
        m_bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void BinaryWriter::PutU32(std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        m_bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void BinaryWriter::PutU64(std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        m_bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void BinaryWriter::PutI32(std::int32_t value)
{
    PutU32(std::bit_cast<std::uint32_t>(value));
}

void BinaryWriter::PutF32(float value)
{
    PutU32(std::bit_cast<std::uint32_t>(value));
}

void BinaryWriter::PutBytes(const void* data, std::size_t size)
{
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("binary block exceeds the u32 length field");
    }
    PutU32(static_cast<std::uint32_t>(size));
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    m_bytes.insert(m_bytes.end(), bytes, bytes + size);
}

void BinaryWriter::PutString(std::string_view value)
{
    PutBytes(value.data(), value.size());
}

void BinaryWriter::PatchU32(std::size_t offset, std::uint32_t value)
{
    if (offset > m_bytes.size() || sizeof(value) > m_bytes.size() - offset) {
        throw std::out_of_range("binary patch is outside the buffer");
    }
    for (unsigned byte = 0; byte < 4; ++byte) {
        m_bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

} // namespace Concord::Serialization
