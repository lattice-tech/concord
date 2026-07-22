#ifndef CONCORD_PROJECTION_H
#define CONCORD_PROJECTION_H

namespace Concord {

/**
 * How a camera maps the 3D scene onto the 2D framebuffer.
 *
 * Perspective applies foreshortening (distant objects shrink) and is driven by
 * a vertical field of view; Orthographic keeps parallel lines parallel and is
 * driven by a fixed vertical world extent, which suits 2D, isometric or CAD-style
 * views. Both share the same near/far clip planes.
 */
enum class Projection {
    Perspective,
    Orthographic,
};

/** Canonical, human-readable name of a Projection (never null). */
inline const char* ToString(Projection projection)
{
    switch (projection) {
        case Projection::Perspective:  return "Perspective";
        case Projection::Orthographic: return "Orthographic";
    }
    return "Perspective";
}

} // namespace Concord

#endif // CONCORD_PROJECTION_H
