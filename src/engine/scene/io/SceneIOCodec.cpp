#include "engine/scene/io/SceneIOCodec.h"

#include <bit>
#include <limits>
#include <stdexcept>

namespace Concord::Detail::SceneIo {

void Writer::PutU8(std::uint8_t value) { bytes.push_back(value); }

void Writer::PutU32(std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Writer::PutU64(std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Writer::PutI32(std::int32_t value) { PutU32(std::bit_cast<std::uint32_t>(value)); }
void Writer::PutF32(float value) { PutU32(std::bit_cast<std::uint32_t>(value)); }
void Writer::PutVec3(const Vector3& value)
{
    PutF32(value.x); PutF32(value.y); PutF32(value.z);
}
void Writer::PutQuat(const Quaternion& value)
{
    PutF32(value.x); PutF32(value.y); PutF32(value.z); PutF32(value.w);
}

void Writer::PutString(const std::string& value)
{
    if (value.size() > kMaxStringBytes) {
        throw std::length_error("scene string exceeds the CSCN budget");
    }
    PutU32(static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
    EnforceFileBudget();
}

void Writer::PutCollisionShape(const Collision::CollisionShape& shape)
{
    PutU8(static_cast<std::uint8_t>(shape.type));
    PutVec3(shape.halfExtents);
    PutF32(shape.radius);
    PutVec3(shape.offset);
}

void Writer::PatchU32(std::size_t offset, std::uint32_t value)
{
    if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
        throw std::out_of_range("scene output patch is outside the buffer");
    }
    for (unsigned byte = 0; byte < 4; ++byte) {
        bytes[offset + byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

void Writer::EnforceFileBudget() const
{
    if (bytes.size() > kMaxSceneFileBytes) {
        throw std::length_error("scene output exceeds the file budget");
    }
}

Reader::Reader(const std::uint8_t* data, std::size_t size)
    : m_data(data), m_size(size)
{
}

bool Reader::Ok() const noexcept { return !m_error; }
bool Reader::AtEnd() const noexcept { return !m_error && m_pos == m_size; }
std::size_t Reader::Remaining() const noexcept
{
    return m_pos <= m_size ? m_size - m_pos : 0;
}
void Reader::Fail() noexcept { m_error = true; }
bool Reader::CanRead(std::size_t count) const noexcept
{
    return !m_error && m_pos <= m_size && count <= m_size - m_pos;
}

std::uint8_t Reader::GetU8()
{
    if (!CanRead(1)) { Fail(); return 0; }
    return m_data[m_pos++];
}

std::uint32_t Reader::GetU32()
{
    if (!CanRead(4)) { Fail(); return 0; }
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(m_data[m_pos + byte]) << (byte * 8);
    }
    m_pos += 4;
    return value;
}

std::uint64_t Reader::GetU64()
{
    if (!CanRead(8)) { Fail(); return 0; }
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(m_data[m_pos + byte]) << (byte * 8);
    }
    m_pos += 8;
    return value;
}

std::int32_t Reader::GetI32() { return std::bit_cast<std::int32_t>(GetU32()); }
float Reader::GetF32() { return std::bit_cast<float>(GetU32()); }
Vector3 Reader::GetVec3() { return {GetF32(), GetF32(), GetF32()}; }
Quaternion Reader::GetQuat() { return {GetF32(), GetF32(), GetF32(), GetF32()}; }

std::string Reader::GetString()
{
    const std::uint32_t size = GetU32();
    if (size > kMaxStringBytes || !CanRead(size)) { Fail(); return {}; }
    std::string value(reinterpret_cast<const char*>(m_data + m_pos), size);
    m_pos += size;
    return value;
}

Collision::CollisionShape Reader::GetCollisionShape()
{
    Collision::CollisionShape shape;
    shape.type = static_cast<Collision::ShapeType>(GetU8());
    shape.halfExtents = GetVec3();
    shape.radius = GetF32();
    shape.offset = GetVec3();
    return shape;
}

Reader Reader::GetFrame(std::uint32_t size)
{
    if (!CanRead(size)) { Fail(); return Reader(nullptr, 0); }
    Reader frame(m_data + m_pos, size);
    m_pos += size;
    return frame;
}

void WriteTransform(Writer& writer, const Transform& transform)
{
    writer.PutVec3(transform.position);
    writer.PutQuat(transform.rotation);
    writer.PutVec3(transform.scale);
}

Transform ReadTransform(Reader& reader)
{
    Transform transform;
    transform.position = reader.GetVec3();
    transform.rotation = reader.GetQuat();
    transform.scale = reader.GetVec3();
    return transform;
}

void WriteSkyEnvironment(Writer& writer, const SkyEnvironment& environment)
{
    writer.PutU8(static_cast<std::uint8_t>(environment.mode));
    writer.PutU32(environment.solidColor); writer.PutU32(environment.zenithColor);
    writer.PutU32(environment.horizonColor); writer.PutU32(environment.groundColor);
    writer.PutU32(environment.ambientColor); writer.PutF32(environment.intensity);
    writer.PutF32(environment.ambientIntensity); writer.PutF32(environment.nightAmbientIntensity);
    writer.PutF32(environment.horizonFalloff); writer.PutF32(environment.sunDiskIntensity);
    writer.PutU8(environment.sunDisk ? 1u : 0u);
}

SkyEnvironment ReadSkyEnvironment(Reader& reader)
{
    SkyEnvironment value;
    value.mode = static_cast<SkyMode>(reader.GetU8());
    value.solidColor = reader.GetU32(); value.zenithColor = reader.GetU32();
    value.horizonColor = reader.GetU32(); value.groundColor = reader.GetU32();
    value.ambientColor = reader.GetU32(); value.intensity = reader.GetF32();
    value.ambientIntensity = reader.GetF32(); value.nightAmbientIntensity = reader.GetF32();
    value.horizonFalloff = reader.GetF32(); value.sunDiskIntensity = reader.GetF32();
    value.sunDisk = reader.GetU8() != 0;
    return value;
}

bool ValidateCount(Reader& reader, std::uint32_t count,
                   std::uint32_t maximum, std::size_t minimumElementBytes)
{
    if (!reader.Ok() || count > maximum
        || (minimumElementBytes != 0 && count > reader.Remaining() / minimumElementBytes)) {
        reader.Fail();
        return false;
    }
    return true;
}

} // namespace Concord::Detail::SceneIo
