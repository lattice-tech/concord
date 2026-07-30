#ifndef CONCORD_COOKEDMATH_H
#define CONCORD_COOKEDMATH_H

#include "engine/object/Transform.h"
#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"
#include "math/Matrix4.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

namespace Concord::Asset {

/**
 * @brief Inline math-value codecs shared by cooked skeleton/animation formats.
 *
 * These wrap the module-agnostic Concord::Serialization primitives so every
 * cooked format encodes a Vector3, Quaternion, Transform, or Matrix4 with the
 * exact same deterministic byte layout, avoiding per-format drift.
 */
inline void PutVec3(Serialization::BinaryWriter& writer, const Vector3& value)
{
    writer.PutF32(value.x);
    writer.PutF32(value.y);
    writer.PutF32(value.z);
}

inline Vector3 GetVec3(Serialization::BinaryReader& reader)
{
    Vector3 value;
    value.x = reader.GetF32();
    value.y = reader.GetF32();
    value.z = reader.GetF32();
    return value;
}

inline void PutQuat(Serialization::BinaryWriter& writer, const Quaternion& value)
{
    writer.PutF32(value.x);
    writer.PutF32(value.y);
    writer.PutF32(value.z);
    writer.PutF32(value.w);
}

inline Quaternion GetQuat(Serialization::BinaryReader& reader)
{
    Quaternion value;
    value.x = reader.GetF32();
    value.y = reader.GetF32();
    value.z = reader.GetF32();
    value.w = reader.GetF32();
    return value;
}

inline void PutTransform(Serialization::BinaryWriter& writer, const Transform& value)
{
    PutVec3(writer, value.position);
    PutQuat(writer, value.rotation);
    PutVec3(writer, value.scale);
}

inline Transform GetTransform(Serialization::BinaryReader& reader)
{
    Transform value;
    value.position = GetVec3(reader);
    value.rotation = GetQuat(reader);
    value.scale = GetVec3(reader);
    return value;
}

inline void PutMatrix4(Serialization::BinaryWriter& writer, const Matrix4& value)
{
    for (const float element : value.m) {
        writer.PutF32(element);
    }
}

inline Matrix4 GetMatrix4(Serialization::BinaryReader& reader)
{
    Matrix4 value;
    for (float& element : value.m) {
        element = reader.GetF32();
    }
    return value;
}

} // namespace Concord::Asset

#endif // CONCORD_COOKEDMATH_H
