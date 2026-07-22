#ifndef CONCORD_VECTOR2_H
#define CONCORD_VECTOR2_H

namespace Concord {

/**
 * A 2D coordinate/offset, in whatever space the caller is using it for
 * (texture UVs, screen offsets, ...).
 *
 * A plain aggregate, mirroring Vector3, so callers can build one inline with a
 * designated initializer, e.g. `Vector2{.x = 0.5f, .y = 1.0f}`. Kept small and
 * dependency-free; anything more elaborate belongs in application code.
 */
struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};

} // namespace Concord

#endif // CONCORD_VECTOR2_H
