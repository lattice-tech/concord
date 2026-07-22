#ifndef CONCORD_EULERANGLES_H
#define CONCORD_EULERANGLES_H

namespace Concord {

/**
 * A rotation expressed as three separate angles, in degrees.
 *
 * A plain aggregate so a caller can write a rotation without any trig or
 * matrix math, e.g. `EulerAngles{.yaw = 45.0f}`. This is purely an
 * ergonomic *input/output* format: Quaternion::FromEuler/ToEuler convert
 * to and from it, but Transform stores rotation as a Quaternion so
 * repeatedly composing rotations (see Object::Box::Rotate) never
 * accumulates gimbal-lock error.
 *
 * Angles are applied pitch, then yaw, then roll (X, then Y, then Z),
 * matching Quaternion::FromEuler's implementation.
 */
struct EulerAngles {
    /** Rotation around the X axis, in degrees. */
    float pitch = 0.0f;

    /** Rotation around the Y axis, in degrees. */
    float yaw = 0.0f;

    /** Rotation around the Z axis, in degrees. */
    float roll = 0.0f;
};

} // namespace Concord

#endif // CONCORD_EULERANGLES_H
