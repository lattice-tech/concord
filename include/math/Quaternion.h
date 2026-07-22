#ifndef CONCORD_QUATERNION_H
#define CONCORD_QUATERNION_H

#include "math/EulerAngles.h"

#include <cmath>

namespace Concord {

struct Quaternion;

/** Composes two rotations: the result applies `rhs` first, then `lhs`. */
inline Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept;

/**
 * A rotation, stored the way that composes cleanly and never drifts into
 * gimbal lock: repeatedly calling Object::Box::Rotate() (which multiplies
 * quaternions) stays well-behaved in a way repeatedly adding Euler angles
 * does not.
 *
 * A plain aggregate — like Resolution — defaulting to the identity
 * rotation, so `Quaternion{}` and direct field access (`.x`/`.y`/`.z`/`.w`)
 * both just work for advanced callers. Most callers should not need to
 * touch those fields directly, though: FromEuler() builds one from plain
 * degrees with no trig required, e.g. `Quaternion::FromEuler({.yaw = 45.0f})`.
 */
struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    /**
     * Builds the rotation that applies `euler`'s pitch, then yaw, then
     * roll (X, then Y, then Z), each in degrees.
     */
    static Quaternion FromEuler(EulerAngles euler) noexcept
    {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float halfPitch = euler.pitch * kDegToRad * 0.5f;
        const float halfYaw = euler.yaw * kDegToRad * 0.5f;
        const float halfRoll = euler.roll * kDegToRad * 0.5f;

        const Quaternion qPitch{std::sin(halfPitch), 0.0f, 0.0f, std::cos(halfPitch)};
        const Quaternion qYaw{0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
        const Quaternion qRoll{0.0f, 0.0f, std::sin(halfRoll), std::cos(halfRoll)};

        return qRoll * qYaw * qPitch;
    }

    /**
     * Recovers pitch/yaw/roll (degrees) from this rotation. Provided for
     * display/debugging convenience; like any quaternion-to-Euler
     * conversion it can hit gimbal lock (yaw pinned to +/-90 degrees), so
     * prefer keeping angles in EulerAngles form yourself if you need to
     * keep composing them, rather than round-tripping through here.
     */
    EulerAngles ToEuler() const noexcept
    {
        constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

        const float sinPitch = 2.0f * (w * x + y * z);
        const float cosPitch = 1.0f - 2.0f * (x * x + y * y);
        const float pitch = std::atan2(sinPitch, cosPitch);

        float sinYaw = 2.0f * (w * y - z * x);
        sinYaw = sinYaw < -1.0f ? -1.0f : (sinYaw > 1.0f ? 1.0f : sinYaw);
        const float yaw = std::asin(sinYaw);

        const float sinRoll = 2.0f * (w * z + x * y);
        const float cosRoll = 1.0f - 2.0f * (y * y + z * z);
        const float roll = std::atan2(sinRoll, cosRoll);

        return EulerAngles{pitch * kRadToDeg, yaw * kRadToDeg, roll * kRadToDeg};
    }
};

inline Quaternion operator*(const Quaternion& lhs, const Quaternion& rhs) noexcept
{
    return Quaternion{
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z,
    };
}

} // namespace Concord

#endif // CONCORD_QUATERNION_H
