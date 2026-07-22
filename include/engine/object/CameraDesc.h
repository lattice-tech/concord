#ifndef CONCORD_CAMERADESC_H
#define CONCORD_CAMERADESC_H

#include "engine/render/frame/Projection.h"
#include "math/Vector3.h"

namespace Concord::Object {

/**
 * Every field a Camera can be constructed or Set() from.
 *
 * A plain aggregate, like the other *Desc types, so a caller names only the
 * fields it wants. The default reproduces the engine's classic framing —
 * pulled back and up, looking at the origin, 60-degree perspective — so a
 * scene with a default camera looks the same as one relying on the built-in
 * fallback.
 *
 * The camera uses a look-at model: it is placed at `position` and aimed at
 * `target`, with `up` fixing the roll. Free-look/FPS orientation is layered on
 * top via Camera helpers (e.g. SetTarget from a direction).
 */
struct CameraDesc {
    /** Eye position in world space. */
    Vector3 position{0.0f, 5.0f, -10.0f};

    /** World-space point the camera looks at. */
    Vector3 target{0.0f, 0.0f, 0.0f};

    /** World up direction, fixing camera roll. */
    Vector3 up{0.0f, 1.0f, 0.0f};

    /** Perspective or orthographic. */
    Projection projection = Projection::Perspective;

    /** Vertical field of view in degrees (perspective only). */
    float fovYDegrees = 60.0f;

    /** Vertical world extent covered by the viewport (orthographic only). */
    float orthoHeight = 10.0f;

    /** Near clip plane distance. */
    float nearPlane = 0.1f;

    /** Far clip plane distance. */
    float farPlane = 100.0f;
};

} // namespace Concord::Object

#endif // CONCORD_CAMERADESC_H
