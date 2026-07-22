#ifndef CONCORD_RAY_H
#define CONCORD_RAY_H

#include "math/Vector3.h"

namespace Concord::Collision {

/**
 * @brief World-space half-line used by gameplay collision queries.
 *
 * Scene queries accept any finite, non-zero direction and normalize it once so
 * returned distances are expressed in world units. Keeping this type as plain
 * data also makes it suitable for command queues and deterministic tests.
 */
struct Ray {
    /** World-space origin. */
    Vector3 origin{};

    /** Direction away from the origin; need not already be normalized. */
    Vector3 direction{0.0f, 0.0f, 1.0f};
};

} // namespace Concord::Collision

#endif // CONCORD_RAY_H
