#ifndef CONCORD_VECTOR3_H
#define CONCORD_VECTOR3_H

namespace Concord {

/**
 * A 3D position/offset/scale, in whatever space the caller is using it for
 * (world units, local offsets, per-axis scale factors, ...).
 *
 * A plain aggregate, like Resolution, so callers can build one inline with
 * a designated initializer, e.g. `Vector3{.x = 1.0f, .z = -2.0f}`. The
 * arithmetic operators below cover the handful of operations Move()-style
 * APIs need (adding an offset, scaling); anything more elaborate belongs
 * in application code rather than this small, dependency-free type.
 */
struct Vector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vector3 operator+(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return Vector3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

inline Vector3 operator-(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

inline Vector3 operator*(const Vector3& v, float scalar) noexcept
{
    return Vector3{v.x * scalar, v.y * scalar, v.z * scalar};
}

inline Vector3 operator*(float scalar, const Vector3& v) noexcept
{
    return v * scalar;
}

} // namespace Concord

#endif // CONCORD_VECTOR3_H
