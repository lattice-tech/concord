#ifndef CONCORD_SCENEIOCODEC_H
#define CONCORD_SCENEIOCODEC_H

#include "engine/collision/CollisionShape.h"
#include "engine/object/Transform.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Detail::SceneIo {

inline constexpr std::uint32_t kMagic = 0x4E435343u;
inline constexpr std::uint32_t kVersion6 = 6;
inline constexpr std::uint32_t kVersion7 = 7;
inline constexpr std::size_t kMaxSceneFileBytes = 64u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxSceneNodes = 65'536u;
inline constexpr std::uint32_t kMaxStringBytes = 1024u * 1024u;
inline constexpr std::uint32_t kMaxParticleForceFields = 1024u;
inline constexpr std::uint32_t kMaxParticleBursts = 16'384u;
inline constexpr std::uint32_t kMaxParticleCapacity = 65'536u;
inline constexpr std::uint32_t kMaxTotalParticleCapacity = 262'144u;

enum class NodeKind : std::uint8_t {
    Box = 0,
    Light = 1,
    Camera = 2,
    Model = 3,
    Collider = 4,
    ParticleEmitter = 5,
    SunLight = 6,
    /** A plain Object::Node used as a transform pivot; carries no payload but its own transform. */
    Pivot = 7,
};

class Writer {
public:
    void PutU8(std::uint8_t value);
    void PutU32(std::uint32_t value);
    void PutU64(std::uint64_t value);
    void PutI32(std::int32_t value);
    void PutF32(float value);
    void PutVec3(const Vector3& value);
    void PutQuat(const Quaternion& value);
    void PutString(const std::string& value);
    void PutCollisionShape(const Collision::CollisionShape& shape);
    void PatchU32(std::size_t offset, std::uint32_t value);
    void EnforceFileBudget() const;

    std::vector<std::uint8_t> bytes;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size);

    bool Ok() const noexcept;
    bool AtEnd() const noexcept;
    std::size_t Remaining() const noexcept;
    void Fail() noexcept;
    std::uint8_t GetU8();
    std::uint32_t GetU32();
    std::uint64_t GetU64();
    std::int32_t GetI32();
    float GetF32();
    Vector3 GetVec3();
    Quaternion GetQuat();
    std::string GetString();
    Collision::CollisionShape GetCollisionShape();
    Reader GetFrame(std::uint32_t size);

private:
    bool CanRead(std::size_t count) const noexcept;

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
    bool m_error = false;
};

void WriteTransform(Writer& writer, const Transform& transform);
Transform ReadTransform(Reader& reader);
void WriteSkyEnvironment(Writer& writer, const SkyEnvironment& environment);
SkyEnvironment ReadSkyEnvironment(Reader& reader);
bool ValidateCount(Reader& reader, std::uint32_t count,
                   std::uint32_t maximum, std::size_t minimumElementBytes);

} // namespace Concord::Detail::SceneIo

#endif // CONCORD_SCENEIOCODEC_H
